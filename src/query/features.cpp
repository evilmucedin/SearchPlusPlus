#include "spp/query/features.h"

#include "spp/query/bm25_scorer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace spp::query {

const char* FeatureName(Feature f) noexcept {
    switch (f) {
        case Feature::kBm25Total:
            return "bm25_total";
        case Feature::kTfSum:
            return "tf_sum";
        case Feature::kIdfSum:
            return "idf_sum";
        case Feature::kNumMatchedTerms:
            return "num_matched_terms";
        case Feature::kNumQueryTerms:
            return "num_query_terms";
        case Feature::kMatchRatio:
            return "match_ratio";
        case Feature::kDocLengthAvg:
            return "doc_length_avg";
        case Feature::kDocLengthMin:
            return "doc_length_min";
        case Feature::kBm25Field0:
            return "bm25_field0";
        case Feature::kBm25Field1:
            return "bm25_field1";
        case Feature::kBm25Field2:
            return "bm25_field2";
        case Feature::kBm25Field3:
            return "bm25_field3";
        case Feature::kPositionDecaySum:
            return "position_decay_sum";
        case Feature::kTokenWeightSum:
            return "token_weight_sum";
        case Feature::kTokenWeightMax:
            return "token_weight_max";
        case Feature::kDocQuality:
            return "doc_quality";
        case Feature::kMinIdfMatched:
            return "min_idf_matched";
        case Feature::kMaxIdfMatched:
            return "max_idf_matched";
        case Feature::kFirstPosMinNorm:
            return "first_pos_min_norm";
    }
    return "unknown";
}

namespace {

constexpr std::size_t Idx(Feature f) noexcept {
    return static_cast<std::size_t>(f);
}

}  // namespace

FeatureVector ExtractFeatures(const CandidateContext& ctx, const Bm25Params& params) {
    FeatureVector v{};
    v.fill(0.0f);

    if (ctx.leaves.empty()) {
        v[Idx(Feature::kNumQueryTerms)] = static_cast<float>(ctx.num_query_leaves);
        if (ctx.segment != nullptr && ctx.segment->has_doc_quality()) {
            v[Idx(Feature::kDocQuality)] = ctx.segment->DocQuality(ctx.doc_id);
        }
        return v;
    }

    double bm25_total = 0.0;
    double tf_sum = 0.0;
    double idf_sum = 0.0;
    std::uint32_t matched = 0;
    double per_field_bm25[4] = {0.0, 0.0, 0.0, 0.0};
    double position_decay_sum = 0.0;
    double token_weight_sum = 0.0;
    float token_weight_max = 0.0f;
    bool any_weight = false;
    double min_idf = 0.0;
    double max_idf = 0.0;
    bool any_idf = false;
    double first_pos_min_norm = 0.0;
    bool any_pos = false;

    for (const auto& l : ctx.leaves) {
        if (!l.matched)
            continue;
        ++matched;
        const double sc = Bm25Score(l.idf, l.tf, l.field_len, l.avg_field_len, params) *
                          static_cast<double>(l.boost);
        bm25_total += sc;
        tf_sum += static_cast<double>(l.tf);
        idf_sum += l.idf;
        if (l.field_id < 4) {
            per_field_bm25[l.field_id] += sc;
        }
        if (!any_idf || l.idf < min_idf)
            min_idf = l.idf;
        if (!any_idf || l.idf > max_idf)
            max_idf = l.idf;
        any_idf = true;
        if (l.position_decay > 0.0f && l.field_len > 0.0) {
            const double pos = static_cast<double>(l.first_pos);
            position_decay_sum +=
                std::exp(-static_cast<double>(l.position_decay) * pos / l.field_len);
            // first_pos_min_norm gates on the same `position_decay > 0` field
            // opt-in so a slot value of 0 unambiguously means "no field
            // contributed", not "matched at position 0 in some unopted field".
            const double norm = pos / l.field_len;
            if (!any_pos || norm < first_pos_min_norm)
                first_pos_min_norm = norm;
            any_pos = true;
        }
        if (l.token_weight != 1.0f || l.boost != 1.0f) {
            // Tracking weights even when they default to 1.0 would dilute the
            // signal; we only count "real" weights when the field opted in.
        }
        token_weight_sum += static_cast<double>(l.token_weight);
        if (l.token_weight > token_weight_max)
            token_weight_max = l.token_weight;
        any_weight = true;
    }

    v[Idx(Feature::kBm25Total)] = static_cast<float>(bm25_total);
    v[Idx(Feature::kTfSum)] = static_cast<float>(tf_sum);
    v[Idx(Feature::kIdfSum)] = static_cast<float>(idf_sum);
    v[Idx(Feature::kNumMatchedTerms)] = static_cast<float>(matched);
    v[Idx(Feature::kNumQueryTerms)] = static_cast<float>(ctx.num_query_leaves);
    v[Idx(Feature::kMatchRatio)] =
        ctx.num_query_leaves == 0
            ? 0.0f
            : static_cast<float>(matched) / static_cast<float>(ctx.num_query_leaves);
    v[Idx(Feature::kBm25Field0)] = static_cast<float>(per_field_bm25[0]);
    v[Idx(Feature::kBm25Field1)] = static_cast<float>(per_field_bm25[1]);
    v[Idx(Feature::kBm25Field2)] = static_cast<float>(per_field_bm25[2]);
    v[Idx(Feature::kBm25Field3)] = static_cast<float>(per_field_bm25[3]);
    v[Idx(Feature::kPositionDecaySum)] = static_cast<float>(position_decay_sum);
    v[Idx(Feature::kTokenWeightSum)] = any_weight ? static_cast<float>(token_weight_sum) : 0.0f;
    v[Idx(Feature::kTokenWeightMax)] = token_weight_max;
    if (ctx.segment != nullptr && ctx.segment->has_doc_quality()) {
        v[Idx(Feature::kDocQuality)] = ctx.segment->DocQuality(ctx.doc_id);
    }
    if (any_idf) {
        v[Idx(Feature::kMinIdfMatched)] = static_cast<float>(min_idf);
        v[Idx(Feature::kMaxIdfMatched)] = static_cast<float>(max_idf);
    }
    if (any_pos)
        v[Idx(Feature::kFirstPosMinNorm)] = static_cast<float>(first_pos_min_norm);
    // kDocLengthAvg / kDocLengthMin stay 0 in v0.2 — see features.h.
    return v;
}

}  // namespace spp::query
