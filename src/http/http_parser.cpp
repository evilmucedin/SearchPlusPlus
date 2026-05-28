#include "spp/http/http_parser.h"

#include "spp/http/http_message.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace spp::http {

namespace {

std::string ToLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void RTrimAscii(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
}

void LTrimAscii(std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i > 0)
        s.erase(0, i);
}

std::string_view::size_type FindCrlf(std::string_view sv, std::size_t from) {
    while (from < sv.size()) {
        const auto pos = sv.find('\r', from);
        if (pos == std::string_view::npos)
            return std::string_view::npos;
        if (pos + 1 < sv.size() && sv[pos + 1] == '\n')
            return pos;
        from = pos + 1;
    }
    return std::string_view::npos;
}

ParseResult NeedMore(std::size_t consumed = 0) {
    return ParseResult{ParseStatus::kNeedMore, consumed, 0, {}};
}

ParseResult MakeError(int status, std::string msg) {
    ParseResult pr;
    pr.status = ParseStatus::kError;
    pr.http_status_on_error = status;
    pr.error_message = std::move(msg);
    return pr;
}

}  // namespace

ParseResult TryParseRequest(std::string_view buf, const ParseLimits& limits, HttpRequest& out) {
    // Step 1: locate end of headers ("\r\n\r\n").
    const std::size_t headers_end = buf.find("\r\n\r\n");
    if (headers_end == std::string_view::npos) {
        if (buf.size() > limits.max_request_line + limits.max_headers_total) {
            return MakeError(400, "headers too large");
        }
        return NeedMore();
    }
    const std::size_t body_start = headers_end + 4;

    // Step 2: parse request line.
    const auto first_crlf = FindCrlf(buf, 0);
    if (first_crlf == std::string_view::npos || first_crlf > headers_end) {
        return MakeError(400, "missing request line");
    }
    if (first_crlf > limits.max_request_line) {
        return MakeError(414, "request line too long");
    }
    std::string_view rl = buf.substr(0, first_crlf);
    const std::size_t sp1 = rl.find(' ');
    const std::size_t sp2 = rl.find(' ', sp1 == std::string_view::npos ? 0 : sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
        return MakeError(400, "malformed request line");
    }
    out.method = std::string{rl.substr(0, sp1)};
    out.target = std::string{rl.substr(sp1 + 1, sp2 - sp1 - 1)};
    out.version = std::string{rl.substr(sp2 + 1)};
    if (out.version != "HTTP/1.1" && out.version != "HTTP/1.0") {
        return MakeError(505, "unsupported HTTP version: " + out.version);
    }
    // Split target into path / query.
    const auto qmark = out.target.find('?');
    if (qmark == std::string::npos) {
        out.path = out.target;
        out.query.clear();
    } else {
        out.path = out.target.substr(0, qmark);
        out.query = out.target.substr(qmark + 1);
    }

    // Step 3: parse header lines.
    out.headers.clear();
    std::size_t pos = first_crlf + 2;
    while (pos < headers_end) {
        const auto end = FindCrlf(buf, pos);
        if (end == std::string_view::npos || end > headers_end) {
            return MakeError(400, "malformed header line");
        }
        std::string_view line = buf.substr(pos, end - pos);
        if (line.empty()) {
            pos = end + 2;
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return MakeError(400, "missing ':' in header");
        }
        std::string name = ToLower(std::string{line.substr(0, colon)});
        std::string value = std::string{line.substr(colon + 1)};
        LTrimAscii(value);
        RTrimAscii(value);
        out.headers[std::move(name)] = std::move(value);
        pos = end + 2;
    }

    // Step 4: read body if Content-Length is set.
    std::uint64_t body_len = 0;
    if (auto it = out.headers.find("content-length"); it != out.headers.end()) {
        const std::string& s = it->second;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), body_len);
        if (ec != std::errc{} || p != s.data() + s.size()) {
            return MakeError(400, "invalid content-length");
        }
        if (body_len > limits.max_body) {
            return MakeError(413, "body too large");
        }
    }
    if (out.headers.count("transfer-encoding")) {
        return MakeError(411, "transfer-encoding not supported (use Content-Length)");
    }

    if (buf.size() - body_start < body_len) {
        return NeedMore();
    }
    out.body.assign(buf.data() + body_start, body_len);

    ParseResult pr;
    pr.status = ParseStatus::kComplete;
    pr.consumed = body_start + body_len;
    return pr;
}

}  // namespace spp::http
