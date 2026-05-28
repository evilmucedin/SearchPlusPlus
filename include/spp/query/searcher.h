#pragma once

#include "spp/base/expected.h"
#include "spp/base/types.h"
#include "spp/index/index_reader.h"
#include "spp/query/bm25_scorer.h"
#include "spp/query/features.h"
#include "spp/query/query_ast.h"
#include "spp/query/ranker.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spp::query {

struct Hit {
    double score = 0.0;
    std::string id;                  // _id of the document
    std::string stored_fields_json;  // raw JSON of stored fields (returned to client)
};

struct SearchResult {
    std::uint64_t total_hits = 0;
    std::vector<Hit> hits;

    // Populated only when SearchOptions::collect_features is true. Same order
    // and length as `hits` — the LTR training tool reads this to dump a
    // CatBoost pool.
    std::vector<FeatureVector> features;
};

struct SearchOptions {
    std::size_t size = 10;
    std::string default_field;  // analyzer to apply to bare terms in queries
    Bm25Params bm25;

    // Stage-1 candidate cutoff. The BM25 walk keeps the top `rerank_top_n` docs
    // (by BM25 only) before stage-2 reranking runs. Default sized so the
    // re-rank cost stays small even for slow models; bump for higher recall.
    std::size_t rerank_top_n = 1000;

    // Optional learned ranker. When non-null, Stage-2 re-scores the top-N
    // candidates with `ranker->Score(features)` before truncating to `size`.
    // When null, results come out in BM25 order (v0.1 behavior).
    const Ranker* ranker = nullptr;

    // When true, populate SearchResult::features with one FeatureVector per
    // returned hit (same order). Used by the LTR training tool to dump pools.
    bool collect_features = false;
};

class Searcher {
 public:
    explicit Searcher(std::shared_ptr<const spp::index::IndexReader> reader)
        : reader_(std::move(reader)) {}

    Expected<SearchResult> Search(const QueryAst& q, const SearchOptions& opts);

    // Convenience overload: parses `query_str` with `opts.default_field` as the
    // implicit field, then runs the resulting AST. Short-circuits with the
    // parser's Status if the query doesn't parse. Equivalent to:
    //     auto ast = Parse(query_str, opts.default_field);
    //     return ast.ok() ? Search(*ast, opts) : ast.status();
    Expected<SearchResult> Search(std::string_view query_str, const SearchOptions& opts);

    const spp::index::IndexReader& reader() const noexcept {
        return *reader_;
    }

 private:
    std::shared_ptr<const spp::index::IndexReader> reader_;
};

}  // namespace spp::query
