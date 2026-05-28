#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

std::filesystem::path TmpDir(const char* prefix) {
    auto p = std::filesystem::temp_directory_path() /
             (std::string(prefix) + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

std::string MakeWord(std::mt19937& rng) {
    static const char* corpus[] = {
        "search", "engine",  "lucene",     "elasticsearch", "fast",   "tiny",
        "index",  "segment", "posting",    "document",      "tokens", "score",
        "term",   "field",   "query",      "analyzer",      "vector", "memory",
        "thread", "lock",    "concurrent", "atomic",        "graph",  "merge",
    };
    return corpus[rng() % (sizeof(corpus) / sizeof(corpus[0]))];
}

std::string MakeBody(std::mt19937& rng, int words) {
    std::string out;
    for (int i = 0; i < words; ++i) {
        if (i != 0)
            out.push_back(' ');
        out += MakeWord(rng);
    }
    return out;
}

}  // namespace

static void BM_IndexWriterAddDocument(benchmark::State& state) {
    const int words = static_cast<int>(state.range(0));
    auto dir = TmpDir("spp_bench_addd_");
    auto v = spp::json::Parse(R"({"body":{"type":"text"}})").value();
    auto schema = spp::index::Schema::FromMappingsJson(v).value();
    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &schema;
    auto writer = spp::index::IndexWriter::Open(dir, opts).value();
    std::mt19937 rng(0x42);
    std::uint64_t i = 0;
    for (auto _ : state) {
        spp::index::Document doc;
        doc.id = std::to_string(i++);
        doc.fields["body"] = MakeBody(rng, words);
        const auto raw = R"({"_id":")" + doc.id + R"(","body":")" + doc.fields["body"] + "\"}";
        (void)writer->AddDocument(doc, raw);
    }
    (void)writer->Refresh();
    (void)writer->Close();
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
    std::filesystem::remove_all(dir);
}
BENCHMARK(BM_IndexWriterAddDocument)->Arg(10)->Arg(100);

BENCHMARK_MAIN();
