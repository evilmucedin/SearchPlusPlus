#pragma once

#include <cstdint>

namespace spp::query {

struct Bm25Params {
    double k1 = 1.2;
    double b = 0.75;
};

// Compute BM25 idf term: log((N - df + 0.5) / (df + 0.5) + 1).
double Bm25Idf(std::uint64_t total_docs, std::uint64_t df);

// Score a single (term, doc) given tf, field length, average field length, and idf.
double Bm25Score(double idf,
                 std::uint32_t tf,
                 double doc_len,
                 double avg_doc_len,
                 const Bm25Params& params = {});

}  // namespace spp::query
