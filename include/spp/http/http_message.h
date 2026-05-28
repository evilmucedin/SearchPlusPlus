#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace spp::http {

struct HttpRequest {
    std::string method;
    std::string target;                          // path + query, e.g. "/wiki/_search?q=foo"
    std::string path;                            // just the path
    std::string query;                           // raw query string (without '?')
    std::string version;                         // "HTTP/1.1"
    std::map<std::string, std::string> headers;  // header names lower-cased
    std::string body;

    // Populated by Router before dispatch (e.g. {"index": "wiki"}).
    std::map<std::string, std::string> path_params;

    std::string_view Header(std::string_view name) const noexcept;

    // Parse query string into a map. URL-decoded keys/values.
    std::map<std::string, std::string> QueryParams() const;
};

struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;  // case-insensitive in spirit; v0.1 leaves as-is
    std::string body;

    void SetHeader(std::string name, std::string value) {
        headers[std::move(name)] = std::move(value);
    }
};

// Serialize a response to the wire (status line + headers + CRLF + body).
std::string SerializeResponse(const HttpResponse& resp);

// Percent-decoding (returns input unchanged on malformed escapes).
std::string UrlDecode(std::string_view s);

}  // namespace spp::http
