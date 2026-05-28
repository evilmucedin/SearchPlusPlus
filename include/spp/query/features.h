#pragma once

#include "spp/base/types.h"
#include "spp/index/schema.h"
#include "spp/index/segment_reader.h"
#include "spp/query/bm25_scorer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spp::query {

// Fixed-size LTR feature vector. Slot indices are stable: the CatBoost model
// trained against schema version V must be re-trained when this constant
// changes. Bumping kFeatureSchemaVersion forces a model retrain.
inline constexpr std::size_t kFeatureCount = 16;
inline constexpr std::uint32_t kFeatureSchemaVersion = 1;

using FeatureVector = std::array<float, kFeatureCount>;

// Stable feature indices. The names are persisted by spp_export_features so
// that retraining always lines up with the binary that emitted the pool.
enum class Feature : std::size_t {
    kBm25Total = 0,
    kTfSum = 1,
    kIdfSum = 2,
    kNumMatchedTerms = 3,
    kNumQueryTerms = 4,
    kMatchRatio = 5,
    // Slots 6 and 7 are reserved for per-doc field-length features. v0.2 does
    // not store per-doc field lengths, so these slots are populated with 0.
    // A future .dvl stripe will fill them.
    kDocLengthAvg = 6,
    kDocLengthMin = 7,
    kBm25Field0 = 8,
    kBm25Field1 = 9,
    kBm25Field2 = 10,
    kBm25Field3 = 11,
    kPositionDecaySum = 12,
    kTokenWeightSum = 13,
    kTokenWeightMax = 14,
    kDocQuality = 15,
};

const char* FeatureName(Feature f) noexcept;

// Per-leaf state at one candidate doc. `matched` is false in disjunctions when
// this leaf did not have the current doc.
struct LeafContextItem {
    FieldId field_id = kInvalidFieldId;
    double idf = 0.0;
    float boost = 1.0f;
    float position_decay = 0.0f;
    bool matched = false;
    std::uint32_t tf = 0;
    std::uint16_t first_pos = 0;  // valid only if position_decay > 0
    float token_weight = 1.0f;
    // Field length used for BM25. v0.2 uses the per-field average across the
    // index as a proxy for per-doc length.
    double field_len = 0.0;
    double avg_field_len = 0.0;
};

struct CandidateContext {
    const spp::index::SegmentReader* segment = nullptr;
    const spp::index::Schema* schema = nullptr;
    DocId doc_id = 0;
    std::size_t num_query_leaves = 0;  // denominator for match_ratio
    std::vector<LeafContextItem> leaves;
};

FeatureVector ExtractFeatures(const CandidateContext& ctx, const Bm25Params& params);

}  // namespace spp::query
