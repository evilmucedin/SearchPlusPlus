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

inline constexpr std::uint32_t kSegmentFormatMagic = 0x53505031u;  // 'SPP1'
inline constexpr std::uint32_t kSegmentFormatVersion = 1u;

}  // namespace spp::index
