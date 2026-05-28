#pragma once

#include "spp/base/expected.h"
#include "spp/base/types.h"
#include "spp/index/index_reader.h"
#include "spp/query/bm25_scorer.h"
#include "spp/query/query_ast.h"

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
};

struct SearchOptions {
    std::size_t size = 10;
    std::string default_field;  // analyzer to apply to bare terms in queries
    Bm25Params bm25;
};

class Searcher {
 public:
    explicit Searcher(std::shared_ptr<const spp::index::IndexReader> reader)
        : reader_(std::move(reader)) {}

    Expected<SearchResult> Search(const QueryAst& q, const SearchOptions& opts);

    const spp::index::IndexReader& reader() const noexcept {
        return *reader_;
    }

 private:
    std::shared_ptr<const spp::index::IndexReader> reader_;
};

}  // namespace spp::query
