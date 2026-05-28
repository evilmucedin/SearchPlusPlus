#pragma once

#include <cstdint>
#include <string>

namespace spp::analyze {

struct Token {
    std::string text;
    std::uint32_t position = 0;
    std::uint32_t start_offset = 0;  // byte offset in the source string
    std::uint32_t end_offset = 0;    // exclusive
};

}  // namespace spp::analyze
