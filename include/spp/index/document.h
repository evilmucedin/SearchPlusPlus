#pragma once

#include <map>
#include <string>

namespace spp::index {

struct Document {
    std::string id;
    std::map<std::string, std::string> fields;  // field name → text
};

}  // namespace spp::index
