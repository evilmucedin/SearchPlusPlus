#include "spp/store/varbyte.h"

namespace spp::store {

void EncodeVarUint32(std::uint32_t v, std::string& out) {
    while (v >= 0x80u) {
        out.push_back(static_cast<char>(0x80u | (v & 0x7Fu)));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

void EncodeVarUint64(std::uint64_t v, std::string& out) {
    while (v >= 0x80u) {
        out.push_back(static_cast<char>(0x80u | (v & 0x7Fu)));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

Expected<std::uint32_t> DecodeVarUint32(const char*& cursor, const char* end) {
    std::uint32_t result = 0;
    std::uint32_t shift = 0;
    while (cursor < end) {
        const auto byte = static_cast<std::uint8_t>(*cursor++);
        if (shift >= 32)
            return Status::Corruption("varuint32 overflow");
        result |= static_cast<std::uint32_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            return result;
        shift += 7;
    }
    return Status::Corruption("varuint32 truncated");
}

Expected<std::uint64_t> DecodeVarUint64(const char*& cursor, const char* end) {
    std::uint64_t result = 0;
    std::uint32_t shift = 0;
    while (cursor < end) {
        const auto byte = static_cast<std::uint8_t>(*cursor++);
        if (shift >= 64)
            return Status::Corruption("varuint64 overflow");
        result |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            return result;
        shift += 7;
    }
    return Status::Corruption("varuint64 truncated");
}

}  // namespace spp::store
