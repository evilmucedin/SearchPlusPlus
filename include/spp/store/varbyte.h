#pragma once

#include "spp/base/expected.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace spp::store {

// VarByte encoding (a.k.a. variable-length quantity): little-endian groups of 7 bits per byte,
// high bit set on continuation. Encodes any uint32_t in 1..5 bytes (uint64_t in 1..10).

void EncodeVarUint32(std::uint32_t v, std::string& out);
void EncodeVarUint64(std::uint64_t v, std::string& out);

// Decode the next VarByte uint32 from `[*cursor, end)`. Advances `*cursor` on success.
Expected<std::uint32_t> DecodeVarUint32(const char*& cursor, const char* end);
Expected<std::uint64_t> DecodeVarUint64(const char*& cursor, const char* end);

inline std::size_t VarUint32Length(std::uint32_t v) {
    std::size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

}  // namespace spp::store
