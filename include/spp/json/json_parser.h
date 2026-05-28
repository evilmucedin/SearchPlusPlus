#pragma once

#include "spp/base/expected.h"
#include "spp/json/json_value.h"

#include <string_view>

namespace spp::json {

// Strict JSON parser:
//   - No comments, no trailing commas.
//   - Object keys must be double-quoted strings.
//   - Numbers: signed 64-bit ints if integral; otherwise IEEE-754 double.
//   - On error, the returned Status message includes the byte offset.
Expected<JsonValue> Parse(std::string_view input);

}  // namespace spp::json
