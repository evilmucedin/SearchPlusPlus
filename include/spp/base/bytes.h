#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace spp {

// An immutable view over bytes. v0.1 alias for string_view; may become a richer type later.
using Bytes = std::string_view;

inline Bytes AsBytes(const std::string& s) noexcept {
    return Bytes{s.data(), s.size()};
}

}  // namespace spp
