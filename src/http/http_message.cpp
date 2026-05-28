#include "spp/http/http_message.h"

#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace spp::http {

namespace {

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const char* StatusText(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 409:
            return "Conflict";
        case 411:
            return "Length Required";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 503:
            return "Service Unavailable";
        case 505:
            return "HTTP Version Not Supported";
        default:
            return "OK";
    }
}

}  // namespace

std::string_view HttpRequest::Header(std::string_view name) const noexcept {
    auto it = headers.find(ToLower(name));
    if (it == headers.end())
        return {};
    return it->second;
}

std::string UrlDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '+') {
            out.push_back(' ');
            continue;
        }
        if (c == '%' && i + 2 < s.size()) {
            auto hexval = [](char h, int& v) {
                if (h >= '0' && h <= '9') {
                    v = h - '0';
                    return true;
                }
                if (h >= 'a' && h <= 'f') {
                    v = h - 'a' + 10;
                    return true;
                }
                if (h >= 'A' && h <= 'F') {
                    v = h - 'A' + 10;
                    return true;
                }
                return false;
            };
            int hi = 0;
            int lo = 0;
            if (hexval(s[i + 1], hi) && hexval(s[i + 2], lo)) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
            // malformed escape — keep '%' literal
        }
        out.push_back(c);
    }
    return out;
}

std::map<std::string, std::string> HttpRequest::QueryParams() const {
    std::map<std::string, std::string> out;
    std::string_view q = query;
    while (!q.empty()) {
        auto amp = q.find('&');
        std::string_view pair = (amp == std::string_view::npos) ? q : q.substr(0, amp);
        const auto eq = pair.find('=');
        if (eq == std::string_view::npos) {
            out[UrlDecode(pair)] = "";
        } else {
            out[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
        }
        if (amp == std::string_view::npos)
            break;
        q.remove_prefix(amp + 1);
    }
    return out;
}

std::string SerializeResponse(const HttpResponse& resp) {
    std::string out;
    out.append("HTTP/1.1 ");
    out.append(std::to_string(resp.status));
    out.push_back(' ');
    out.append(StatusText(resp.status));
    out.append("\r\n");
    bool has_clen = false;
    bool has_ctype = false;
    bool has_conn = false;
    for (const auto& [k, v] : resp.headers) {
        if (ToLower(k) == "content-length")
            has_clen = true;
        if (ToLower(k) == "content-type")
            has_ctype = true;
        if (ToLower(k) == "connection")
            has_conn = true;
        out.append(k);
        out.append(": ");
        out.append(v);
        out.append("\r\n");
    }
    if (!has_clen) {
        out.append("Content-Length: ");
        out.append(std::to_string(resp.body.size()));
        out.append("\r\n");
    }
    if (!has_ctype && !resp.body.empty()) {
        out.append("Content-Type: application/json\r\n");
    }
    if (!has_conn)
        out.append("Connection: close\r\n");
    out.append("\r\n");
    out.append(resp.body);
    return out;
}

}  // namespace spp::http
