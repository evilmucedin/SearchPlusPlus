#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_bench_qlat_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

class PreparedIndex {
 public:
    PreparedIndex() {
        dir_ = TmpDir();
        auto v = spp::json::Parse(R"({"body":{"type":"text","stored":true}})").value();
        schema_ = spp::index::Schema::FromMappingsJson(v).value();
        spp::index::IndexOpenOptions opts;
        opts.initial_schema = &schema_;
        writer_ = spp::index::IndexWriter::Open(dir_, opts).value();

        std::mt19937 rng(0xdeadbeef);
        const char* corpus[] = {
            "search",
            "engine",
            "lucene",
            "elasticsearch",
            "fast",
            "tiny",
            "index",
            "segment",
            "posting",
            "document",
            "tokens",
            "score",
        };
        for (int i = 0; i < 10'000; ++i) {
            spp::index::Document d;
            d.id = std::to_string(i);
            std::string body;
            for (int j = 0; j < 40; ++j) {
                if (j != 0)
                    body.push_back(' ');
                body += corpus[rng() % 12];
            }
            d.fields["body"] = body;
            const std::string raw = R"({"_id":")" + d.id + R"(","body":")" + body + "\"}";
            (void)writer_->AddDocument(d, raw);
        }
        (void)writer_->Refresh();
    }
    ~PreparedIndex() {
        (void)writer_->Close();
        std::filesystem::remove_all(dir_);
    }

    spp::query::Searcher MakeSearcher() {
        return spp::query::Searcher(writer_->CurrentReader());
    }

 private:
    std::filesystem::path dir_;
    spp::index::Schema schema_;
    std::unique_ptr<spp::index::IndexWriter> writer_;
};

static PreparedIndex& GetIndex() {
    static PreparedIndex p;
    return p;
}

static void BM_QueryLatencyTop10(benchmark::State& state) {
    auto searcher = GetIndex().MakeSearcher();
    spp::query::SearchOptions opts;
    opts.size = 10;
    opts.default_field = "body";
    auto ast = spp::query::Parse("search engine", "body").value();
    for (auto _ : state) {
        auto res = searcher.Search(ast, opts).value();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_QueryLatencyTop10);

BENCHMARK_MAIN();
