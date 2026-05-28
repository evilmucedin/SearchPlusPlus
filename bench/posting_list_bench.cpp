#include "spp/base/types.h"
#include "spp/query/iterators.h"
#include "spp/store/varbyte.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

std::string MakePostingBytes(const std::vector<std::pair<spp::DocId, std::uint32_t>>& docs) {
    std::string out;
    spp::store::EncodeVarUint32(static_cast<std::uint32_t>(docs.size()), out);
    spp::DocId prev = 0;
    for (auto [id, tf] : docs) {
        spp::store::EncodeVarUint32(id - prev, out);
        spp::store::EncodeVarUint32(tf, out);
        prev = id;
    }
    return out;
}

std::vector<std::pair<spp::DocId, std::uint32_t>> Sample(std::size_t n,
                                                         std::uint32_t universe,
                                                         std::uint64_t seed) {
    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::uniform_int_distribution<std::uint32_t> tf_dist(1, 5);
    std::vector<std::uint32_t> ids;
    ids.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        ids.push_back(static_cast<std::uint32_t>(rng() % universe));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<std::pair<spp::DocId, std::uint32_t>> out;
    out.reserve(ids.size());
    for (auto id : ids)
        out.emplace_back(id, tf_dist(rng));
    return out;
}

}  // namespace

static void BM_TermIteratorWalk(benchmark::State& state) {
    const auto docs = Sample(static_cast<std::size_t>(state.range(0)), 1'000'000, 0x42);
    const auto bytes = MakePostingBytes(docs);
    for (auto _ : state) {
        spp::query::TermIterator it(bytes,
                                    static_cast<std::uint32_t>(docs.size()),
                                    static_cast<std::uint64_t>(docs.size() * 3));
        spp::DocId d;
        while ((d = it.Next()) != spp::kNoMoreDocs) {
            benchmark::DoNotOptimize(d);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(docs.size()));
}
BENCHMARK(BM_TermIteratorWalk)->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_ConjunctionIntersect(benchmark::State& state) {
    const auto a = Sample(static_cast<std::size_t>(state.range(0)), 1'000'000, 1);
    const auto b = Sample(static_cast<std::size_t>(state.range(0)), 1'000'000, 2);
    const auto ba = MakePostingBytes(a);
    const auto bb = MakePostingBytes(b);
    for (auto _ : state) {
        std::vector<std::unique_ptr<spp::query::DocIdSetIterator>> kids;
        kids.push_back(std::make_unique<spp::query::TermIterator>(
            ba, static_cast<std::uint32_t>(a.size()), a.size() * 3ULL));
        kids.push_back(std::make_unique<spp::query::TermIterator>(
            bb, static_cast<std::uint32_t>(b.size()), b.size() * 3ULL));
        spp::query::ConjunctionIterator conj(std::move(kids));
        spp::DocId d;
        std::size_t hits = 0;
        while ((d = conj.Next()) != spp::kNoMoreDocs) {
            benchmark::DoNotOptimize(d);
            ++hits;
        }
        benchmark::DoNotOptimize(hits);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(a.size() + b.size()));
}
BENCHMARK(BM_ConjunctionIntersect)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK_MAIN();
