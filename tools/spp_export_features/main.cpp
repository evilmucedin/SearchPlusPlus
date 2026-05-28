// spp_export_features — emit a CatBoost-compatible LTR training pool.
//
// Reads a judgments JSONL file with one object per line:
//   {"query": "...", "doc_id": "...", "label": <int>, "query_id": <int|opt>}
// For each judgment, runs the query against the index, finds the hit whose
// _id matches doc_id, and writes one TSV row per judgment to --out:
//   <label>\t<query_id>\t<f0>\t<f1>\t...\t<f15>
// Judged docs that don't appear in the top-N candidate pool are written with
// an all-zero feature vector so the model can learn to push them down.
//
// Usage:
//   spp_export_features --index <dir> --judgments <jsonl> --out <tsv>
//                       [--top-n N] [--default-field <name>]

#include "spp/base/expected.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/query/features.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

void Usage() {
    std::fputs(
        "Usage: spp_export_features --index <dir> --judgments <jsonl> --out <tsv>\n"
        "                           [--top-n N] [--default-field <name>]\n",
        stderr);
}

struct Args {
    std::string index_dir;
    std::string judgments_path;
    std::string out_path;
    std::size_t top_n = 1000;
    std::string default_field;
};

bool ParseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view k = argv[i];
        if (k == "--index" && i + 1 < argc) {
            a.index_dir = argv[++i];
        } else if (k == "--judgments" && i + 1 < argc) {
            a.judgments_path = argv[++i];
        } else if (k == "--out" && i + 1 < argc) {
            a.out_path = argv[++i];
        } else if (k == "--top-n" && i + 1 < argc) {
            a.top_n = static_cast<std::size_t>(std::atoll(argv[++i]));
        } else if (k == "--default-field" && i + 1 < argc) {
            a.default_field = argv[++i];
        } else if (k == "-h" || k == "--help") {
            Usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return false;
        }
    }
    return !a.index_dir.empty() && !a.judgments_path.empty() && !a.out_path.empty();
}

void WriteRow(std::ostream& out,
              int label,
              std::int64_t query_id,
              const spp::query::FeatureVector& fv) {
    out << label << '\t' << query_id;
    for (std::size_t i = 0; i < spp::query::kFeatureCount; ++i) {
        out << '\t' << fv[i];
    }
    out << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        Usage();
        return 2;
    }

    auto writer_e = spp::index::IndexWriter::Open(args.index_dir);
    if (!writer_e.ok()) {
        std::fprintf(stderr, "open index: %s\n", writer_e.status().message().c_str());
        return 2;
    }
    auto& writer = *writer_e;
    auto reader = writer->CurrentReader();
    if (!reader) {
        std::fprintf(stderr, "no reader (index has not been refreshed)\n");
        return 2;
    }

    std::string default_field = args.default_field;
    if (default_field.empty()) {
        for (const auto& f : reader->schema().fields()) {
            if (f.name != "_id" && f.type == spp::index::FieldType::kText) {
                default_field = f.name;
                break;
            }
        }
        if (default_field.empty())
            default_field = "_id";
    }

    std::ifstream judg(args.judgments_path);
    if (!judg) {
        std::fprintf(stderr, "open judgments: %s\n", args.judgments_path.c_str());
        return 2;
    }
    std::ofstream out(args.out_path);
    if (!out) {
        std::fprintf(stderr, "open out: %s\n", args.out_path.c_str());
        return 2;
    }

    // Cache search results so multiple judgments against the same query
    // don't re-run the candidate walk.
    struct QueryResults {
        std::int64_t query_id = 0;
        std::unordered_map<std::string, spp::query::FeatureVector> by_doc;
    };
    std::unordered_map<std::string, QueryResults> query_cache;
    std::int64_t auto_qid = 0;

    std::string line;
    std::uint64_t rows_written = 0;
    std::uint64_t rows_zero = 0;
    while (std::getline(judg, line)) {
        if (line.empty())
            continue;
        auto parsed = spp::json::Parse(line);
        if (!parsed.ok()) {
            std::fprintf(stderr, "skip line (json parse): %s\n", parsed.status().message().c_str());
            continue;
        }
        if (!parsed->is_object()) {
            std::fprintf(stderr, "skip line (not an object)\n");
            continue;
        }
        const auto* qv = parsed->find("query");
        const auto* dv = parsed->find("doc_id");
        const auto* lv = parsed->find("label");
        if (qv == nullptr || !qv->is_string() || dv == nullptr || !dv->is_string() ||
            lv == nullptr || !lv->is_number()) {
            std::fprintf(stderr, "skip line (missing query|doc_id|label)\n");
            continue;
        }
        const std::string& q_str = qv->as_string();
        const std::string& doc_id = dv->as_string();
        const int label = static_cast<int>(lv->as_double());

        std::int64_t query_id;
        if (const auto* qid = parsed->find("query_id"); qid != nullptr && qid->is_number()) {
            query_id = static_cast<std::int64_t>(qid->as_double());
        } else {
            // Auto-assign a stable id per unique query string.
            auto it = query_cache.find(q_str);
            if (it == query_cache.end()) {
                query_id = auto_qid++;
            } else {
                query_id = it->second.query_id;
            }
        }

        auto it = query_cache.find(q_str);
        if (it == query_cache.end()) {
            auto ast_e = spp::query::Parse(q_str, default_field);
            if (!ast_e.ok()) {
                std::fprintf(stderr, "skip query (parse): %s\n", ast_e.status().message().c_str());
                continue;
            }
            spp::query::Searcher s(reader);
            spp::query::SearchOptions sopts;
            sopts.size = args.top_n;
            sopts.rerank_top_n = args.top_n;
            sopts.default_field = default_field;
            sopts.collect_features = true;
            auto res_e = s.Search(*ast_e, sopts);
            if (!res_e.ok()) {
                std::fprintf(stderr, "skip query (search): %s\n", res_e.status().message().c_str());
                continue;
            }
            QueryResults qr;
            qr.query_id = query_id;
            for (std::size_t i = 0; i < res_e->hits.size(); ++i) {
                qr.by_doc[res_e->hits[i].id] = res_e->features[i];
            }
            it = query_cache.emplace(q_str, std::move(qr)).first;
        }

        const auto& by_doc = it->second.by_doc;
        if (auto hit_it = by_doc.find(doc_id); hit_it != by_doc.end()) {
            WriteRow(out, label, query_id, hit_it->second);
        } else {
            // Judged doc didn't make the top-N candidate pool.
            spp::query::FeatureVector zeros{};
            zeros.fill(0.0f);
            WriteRow(out, label, query_id, zeros);
            ++rows_zero;
        }
        ++rows_written;
    }
    std::fprintf(stderr,
                 "wrote %llu rows (%llu zero-feature) to %s\n",
                 static_cast<unsigned long long>(rows_written),
                 static_cast<unsigned long long>(rows_zero),
                 args.out_path.c_str());
    return writer->Close().ok() ? 0 : 1;
}
