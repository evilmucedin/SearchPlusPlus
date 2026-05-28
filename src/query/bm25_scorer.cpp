#include "spp/query/bm25_scorer.h"

#include <cmath>

namespace spp::query {

double Bm25Idf(std::uint64_t total_docs, std::uint64_t df) {
    if (total_docs == 0)
        return 0.0;
    const double n = static_cast<double>(total_docs);
    const double f = static_cast<double>(df);
    return std::log(((n - f + 0.5) / (f + 0.5)) + 1.0);
}

double Bm25Score(
    double idf, std::uint32_t tf, double doc_len, double avg_doc_len, const Bm25Params& params) {
    if (tf == 0)
        return 0.0;
    const double tf_d = static_cast<double>(tf);
    const double denom_factor =
        params.k1 * (1.0 - params.b + params.b * (avg_doc_len > 0 ? doc_len / avg_doc_len : 1.0));
    return idf * ((tf_d * (params.k1 + 1.0)) / (tf_d + denom_factor));
}

}  // namespace spp::query
