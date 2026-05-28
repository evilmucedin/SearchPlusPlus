#pragma once

#include "spp/base/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace spp::index {

// Per-field statistics persisted in `.si` alongside the dictionary offsets that
// `SegmentReader` needs to locate the field's portion of `.tim` and `.doc`.
struct FieldStats {
    std::string name;  // for debuggability; redundant with schema.json
    std::uint32_t doc_count = 0;
    std::uint64_t sum_field_length = 0;
    std::uint64_t term_count = 0;

    // Byte ranges in `.tim` and `.doc` for this field.
    std::uint64_t tim_offset = 0;
    std::uint64_t tim_size = 0;
    std::uint64_t doc_offset = 0;
    std::uint64_t doc_size = 0;

    // v0.2 LTR metadata. Defaults preserve v0.1 behavior; both flags decide
    // whether the per-(term, doc) posting payload in `.doc` carries extra bytes.
    float boost = 1.0f;
    float position_decay = 0.0f;
    bool has_positions = false;      // adds 2 bytes/entry to .doc
    bool has_token_weights = false;  // adds 1 byte/entry to .doc
};

struct SegmentInfo {
    std::uint32_t format_version = 1;
    std::string analyzer_fingerprint = "standard:v1+keyword:v1";
    DocId doc_count = 0;
    std::vector<FieldStats> fields;
    std::string stem;  // base name of the segment files
};

// On-disk file extensions, all relative to the segment stem.
inline constexpr const char* kSegInfoExt = ".si";
inline constexpr const char* kSegTermsExt = ".tim";
inline constexpr const char* kSegPostingsExt = ".doc";
inline constexpr const char* kSegStoredExt = ".fdt";
inline constexpr const char* kSegStoredIdxExt = ".fdx";
// v0.2: optional per-doc static quality stripe. Present only when the schema
// opted into store_doc_quality. Layout: [magic:u32][doc_count:u32][float32 × N].
inline constexpr const char* kSegDocQualityExt = ".dvq";

inline constexpr std::uint32_t kSegmentFormatMagic = 0x53505031u;  // 'SPP1'
inline constexpr std::uint32_t kSegDocQualityMagic = 0x44515031u;  // 'DQP1'
inline constexpr std::uint32_t kSegmentFormatVersion = 2u;

// Token-weight quantization. q8 ∈ [0, 255] maps linearly to [0.0, 8.0].
inline constexpr float kTokenWeightMaxStored = 8.0f;

inline std::uint8_t QuantizeTokenWeight(float w) noexcept {
    if (w <= 0.0f)
        return 0;
    if (w >= kTokenWeightMaxStored)
        return 255;
    return static_cast<std::uint8_t>(w * (255.0f / kTokenWeightMaxStored) + 0.5f);
}
inline float DequantizeTokenWeight(std::uint8_t q) noexcept {
    return static_cast<float>(q) * (kTokenWeightMaxStored / 255.0f);
}

// Sentinel returned by TermIterator::Position() when the field has no positions.
inline constexpr std::uint16_t kNoPosition = 0xFFFFu;

}  // namespace spp::index
