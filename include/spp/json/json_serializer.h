#pragma once

#include "spp/json/json_value.h"

#include <string>
#include <string_view>

namespace spp::json {

// Compact serialization (no whitespace). UTF-8 string content is emitted as-is;
// bytes < 0x20, '"' and '\\' are escaped. Keys are sorted by std::map order (lexicographic).
std::string Serialize(const JsonValue& v);

// Pretty serialization with `indent` spaces per level. indent==0 ⇒ compact.
std::string SerializePretty(const JsonValue& v, int indent = 2);

// Escape a UTF-8 string per JSON rules (does not add surrounding quotes).
void EscapeStringInto(std::string_view in, std::string& out);

}  // namespace spp::json
