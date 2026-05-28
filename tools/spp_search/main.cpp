// spp_search — one-shot local query against a prepared index.
//
// Usage:
//   spp_search --index <dir> --query "<q>" [--size N] [--field <default_field>]

#include "spp/base/expected.h"
#include "spp/index/index_writer.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

void Usage() {
    std::fputs("Usage: spp_search --index <dir> --query \"<q>\" [--size N] [--field <name>]\n",
               stderr);
}

bool ParseArgs(int argc,
               char** argv,
               std::string& dir,
               std::string& query,
               std::size_t& size,
               std::string& field) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--index" && i + 1 < argc)
            dir = argv[++i];
        else if (a == "--query" && i + 1 < argc)
            query = argv[++i];
        else if (a == "--size" && i + 1 < argc)
            size = static_cast<std::size_t>(std::atoll(argv[++i]));
        else if (a == "--field" && i + 1 < argc)
            field = argv[++i];
        else if (a == "-h" || a == "--help") {
            Usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return false;
        }
    }
    return !dir.empty();
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, query, field;
    std::size_t size = 10;
    if (!ParseArgs(argc, argv, dir, query, size, field)) {
        Usage();
        return 2;
    }

    auto writer_e = spp::index::IndexWriter::Open(dir);
    if (!writer_e.ok()) {
        std::fprintf(stderr, "open: %s\n", writer_e.status().message().c_str());
        return 2;
    }
    auto& writer = *writer_e;
    auto reader = writer->CurrentReader();

    if (field.empty()) {
        for (const auto& f : reader->schema().fields()) {
            if (f.name != "_id" && f.type == spp::index::FieldType::kText) {
                field = f.name;
                break;
            }
        }
        if (field.empty())
            field = "_id";
    }

    auto ast_e = spp::query::Parse(query, field);
    if (!ast_e.ok()) {
        std::fprintf(stderr, "parse: %s\n", ast_e.status().message().c_str());
        return 2;
    }
    spp::query::Searcher s(reader);
    spp::query::SearchOptions opts;
    opts.size = size;
    opts.default_field = field;
    auto res_e = s.Search(*ast_e, opts);
    if (!res_e.ok()) {
        std::fprintf(stderr, "search: %s\n", res_e.status().message().c_str());
        return 2;
    }

    spp::json::JsonObject root;
    root["total"] = static_cast<std::int64_t>(res_e->total_hits);
    spp::json::JsonArray hits;
    for (const auto& h : res_e->hits) {
        spp::json::JsonObject one;
        one["_id"] = h.id;
        one["_score"] = h.score;
        hits.emplace_back(std::move(one));
    }
    root["hits"] = spp::json::JsonValue(std::move(hits));
    std::puts(spp::json::SerializePretty(spp::json::JsonValue(std::move(root)), 2).c_str());
    return writer->Close().ok() ? 0 : 1;
}
