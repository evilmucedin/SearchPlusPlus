// Measures the marginal cost of stage-2 reranking on the same index as
// query_latency_bench. We compare three configurations:
//   - BM25-only (no rerank)
//   - BM25 + LinearRanker over 1000 candidates
//   - BM25 + CatboostRanker (the stub model, so feature extraction dominates)
// so we can see what the rerank stage costs above the candidate walk.

#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/query/features.h"
#include "spp/query/query_parser.h"
#include "spp/query/ranker.h"
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
             ("spp_bench_rerank_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

class RerankBenchIndex {
 public:
    RerankBenchIndex() {
        dir_ = TmpDir();
        auto v = spp::json::Parse(R"({"body":{"type":"text","stored":true}})").value();
        schema_ = spp::index::Schema::FromMappingsJson(v).value();
        spp::index::IndexOpenOptions opts;
        opts.initial_schema = &schema_;
        writer_ = spp::index::IndexWriter::Open(dir_, opts).value();

        std::mt19937 rng(0xdeadbeef);
        const char* corpus[] = {"search",
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
                                "score"};
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
    ~RerankBenchIndex() {
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

RerankBenchIndex& Index() {
    static RerankBenchIndex idx;
    return idx;
}

}  // namespace

static void BM_RerankBm25Only(benchmark::State& state) {
    auto s = Index().MakeSearcher();
    spp::query::SearchOptions opts;
    opts.size = 10;
    opts.default_field = "body";
    auto ast = spp::query::Parse("search engine", "body").value();
    for (auto _ : state) {
        auto res = s.Search(ast, opts).value();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_RerankBm25Only);

static void BM_RerankLinearTopN(benchmark::State& state) {
    auto s = Index().MakeSearcher();
    std::array<float, spp::query::kFeatureCount> w{};
    w.fill(0.0f);
    w[0] = 1.0f;  // bm25_total
    spp::query::LinearRanker r(/*bias=*/0.0f, w);
    spp::query::SearchOptions opts;
    opts.size = 10;
    opts.rerank_top_n = static_cast<std::size_t>(state.range(0));
    opts.default_field = "body";
    opts.ranker = &r;
    auto ast = spp::query::Parse("search engine", "body").value();
    for (auto _ : state) {
        auto res = s.Search(ast, opts).value();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_RerankLinearTopN)->Arg(100)->Arg(1000)->Arg(5000);

static void BM_RerankCatboostStubTopN(benchmark::State& state) {
    auto s = Index().MakeSearcher();
    spp::query::CatboostRanker r;
    spp::query::SearchOptions opts;
    opts.size = 10;
    opts.rerank_top_n = static_cast<std::size_t>(state.range(0));
    opts.default_field = "body";
    opts.ranker = &r;
    auto ast = spp::query::Parse("search engine", "body").value();
    for (auto _ : state) {
        auto res = s.Search(ast, opts).value();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_RerankCatboostStubTopN)->Arg(100)->Arg(1000)->Arg(5000);

BENCHMARK_MAIN();
