#pragma once

#include "spp/base/expected.h"
#include "spp/http/http_message.h"

#include <cstddef>
#include <string_view>

namespace spp::http {

struct ParseLimits {
    std::size_t max_request_line = 8 * 1024;
    std::size_t max_headers_total = 32 * 1024;
    std::size_t max_body = 16 * 1024 * 1024;  // 16 MiB
};

enum class ParseStatus {
    kComplete,
    kNeedMore,  // not enough bytes yet (the caller should keep reading)
    kError,
};

struct ParseResult {
    ParseStatus status = ParseStatus::kNeedMore;
    std::size_t consumed = 0;      // bytes consumed from the input
    int http_status_on_error = 0;  // suggested response status on kError (e.g. 400, 413)
    std::string error_message;
};

// Try to parse a single HTTP request from `buffer`. On kComplete, `out` is populated and
// `consumed` is the byte count of the full message (head + body). On kNeedMore, the caller
// should grow the buffer and call again. On kError, the caller should respond with
// `http_status_on_error` and close.
ParseResult TryParseRequest(std::string_view buffer, const ParseLimits& limits, HttpRequest& out);

}  // namespace spp::http
