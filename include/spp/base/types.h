#pragma once

#include <cstdint>
#include <limits>

namespace spp {

using DocId = std::uint32_t;
using TermId = std::uint32_t;
using FieldId = std::uint16_t;

inline constexpr DocId kNoMoreDocs = std::numeric_limits<DocId>::max();
inline constexpr FieldId kInvalidFieldId = std::numeric_limits<FieldId>::max();

using Generation = std::uint64_t;

}  // namespace spp
