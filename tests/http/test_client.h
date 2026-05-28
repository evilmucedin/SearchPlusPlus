#pragma once

#include "spp/http/platform.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace spp_test {

// One-shot HTTP/1.1 client used by tests. Connects, writes the raw request,
// reads until EOF (server uses Connection: close), returns the full response.
inline bool HttpExchange(std::uint16_t port, std::string_view request, std::string& response_out) {
    auto sock_e = spp::http::platform::Connect("127.0.0.1", port);
    if (!sock_e.ok())
        return false;
    const auto s = *sock_e;
    if (!spp::http::platform::WriteAll(s, request.data(), request.size())) {
        spp::http::platform::Close(s);
        return false;
    }
    response_out.clear();
    char buf[4096];
    while (true) {
        const auto n = spp::http::platform::Read(s, buf, sizeof(buf));
        if (n <= 0)
            break;
        response_out.append(buf, static_cast<std::size_t>(n));
    }
    spp::http::platform::Close(s);
    return true;
}

inline std::string MakeRequest(std::string_view method,
                               std::string_view path,
                               std::string_view body = {}) {
    std::string req;
    req.append(method);
    req.push_back(' ');
    req.append(path);
    req.append(" HTTP/1.1\r\n");
    req.append("Host: 127.0.0.1\r\n");
    req.append("Connection: close\r\n");
    req.append("Content-Type: application/json\r\n");
    req.append("Content-Length: ");
    req.append(std::to_string(body.size()));
    req.append("\r\n\r\n");
    req.append(body);
    return req;
}

// Split a wire response into (status_code, body). Status line is expected.
inline bool SplitResponse(std::string_view raw, int& status, std::string& body) {
    auto first_space = raw.find(' ');
    if (first_space == std::string_view::npos)
        return false;
    auto second_space = raw.find(' ', first_space + 1);
    if (second_space == std::string_view::npos)
        return false;
    status =
        std::atoi(std::string(raw.substr(first_space + 1, second_space - first_space - 1)).c_str());
    auto body_sep = raw.find("\r\n\r\n");
    if (body_sep == std::string_view::npos)
        return false;
    body.assign(raw.substr(body_sep + 4));
    return true;
}

}  // namespace spp_test
