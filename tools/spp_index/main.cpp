// spp_index — offline JSONL → segment indexer.
//
// Usage:
//   spp_index --index <dir> --mapping <mapping.json>  < docs.jsonl

#include "spp/base/expected.h"
#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {

void Usage() {
    std::fputs("Usage: spp_index --index <dir> --mapping <mapping.json> [< docs.jsonl]\n", stderr);
}

bool ParseArgs(int argc, char** argv, std::string& index_dir, std::string& mapping) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--index" && i + 1 < argc) {
            index_dir = argv[++i];
        } else if (a == "--mapping" && i + 1 < argc) {
            mapping = argv[++i];
        } else if (a == "-h" || a == "--help") {
            Usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return false;
        }
    }
    return !index_dir.empty() && !mapping.empty();
}

std::string ReadFile(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        std::fprintf(stderr, "cannot open: %s\n", path.c_str());
        std::exit(2);
    }
    std::string buf((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    std::string index_dir, mapping_path;
    if (!ParseArgs(argc, argv, index_dir, mapping_path)) {
        Usage();
        return 2;
    }

    const auto mapping_text = ReadFile(mapping_path);
    auto mapping_json_e = spp::json::Parse(mapping_text);
    if (!mapping_json_e.ok()) {
        std::fprintf(stderr, "mapping parse: %s\n", mapping_json_e.status().message().c_str());
        return 2;
    }
    const auto* mappings_v = mapping_json_e->find("mappings");
    if (!mappings_v || !mappings_v->is_object()) {
        std::fprintf(stderr, "mapping file must have a top-level \"mappings\" object\n");
        return 2;
    }
    auto schema_e = spp::index::Schema::FromMappingsJson(*mappings_v);
    if (!schema_e.ok()) {
        std::fprintf(stderr, "schema: %s\n", schema_e.status().message().c_str());
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(index_dir, ec);
    if (ec) {
        std::fprintf(stderr, "create_directories: %s\n", ec.message().c_str());
        return 2;
    }

    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &*schema_e;
    auto writer_e = spp::index::IndexWriter::Open(index_dir, opts);
    if (!writer_e.ok()) {
        std::fprintf(stderr, "open: %s\n", writer_e.status().message().c_str());
        return 2;
    }
    auto& writer = *writer_e;

    std::string line;
    std::uint64_t added = 0;
    while (std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        auto v_e = spp::json::Parse(line);
        if (!v_e.ok()) {
            std::fprintf(stderr,
                         "skip malformed line %llu: %s\n",
                         static_cast<unsigned long long>(added + 1),
                         v_e.status().message().c_str());
            continue;
        }
        if (!v_e->is_object())
            continue;

        spp::index::Document doc;
        if (auto idv = v_e->find("_id"); idv && idv->is_string()) {
            doc.id = idv->as_string();
        }
        for (const auto& [k, v] : v_e->as_object()) {
            if (k == "_id")
                continue;
            if (v.is_string())
                doc.fields[k] = v.as_string();
            else if (v.is_number())
                doc.fields[k] = std::to_string(v.as_double());
            else if (v.is_bool())
                doc.fields[k] = v.as_bool() ? "true" : "false";
        }
        if (auto st = writer->AddDocument(doc, line); !st.ok()) {
            std::fprintf(stderr, "add doc: %s\n", st.message().c_str());
            return 3;
        }
        ++added;
    }

    if (auto gen_e = writer->Refresh(); !gen_e.ok()) {
        std::fprintf(stderr, "refresh: %s\n", gen_e.status().message().c_str());
        return 3;
    } else {
        std::fprintf(stderr,
                     "indexed %llu documents (generation=%llu)\n",
                     static_cast<unsigned long long>(added),
                     static_cast<unsigned long long>(*gen_e));
    }
    return writer->Close().ok() ? 0 : 3;
}
