#include "spp/http/router.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace spp::http {

void Router::Add(std::string method, std::string pattern, Handler handler) {
    Route r;
    r.method = std::move(method);
    r.segments = Split(pattern);
    r.handler = std::move(handler);
    routes_.push_back(std::move(r));
}

std::vector<std::string> Router::Split(std::string_view path) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < path.size() && path[i] == '/')
        ++i;
    while (i < path.size()) {
        std::size_t j = path.find('/', i);
        if (j == std::string_view::npos) {
            out.emplace_back(path.substr(i));
            break;
        }
        if (j > i)
            out.emplace_back(path.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

bool Router::Match(const Route& r, const std::vector<std::string>& path_segs, HttpRequest& req) {
    if (r.segments.size() != path_segs.size())
        return false;
    std::map<std::string, std::string> captured;
    for (std::size_t i = 0; i < r.segments.size(); ++i) {
        const std::string& s = r.segments[i];
        if (!s.empty() && s[0] == ':') {
            captured[s.substr(1)] = path_segs[i];
        } else if (s != path_segs[i]) {
            return false;
        }
    }
    req.path_params = std::move(captured);
    return true;
}

HttpResponse Router::Dispatch(HttpRequest& req) const {
    const auto path_segs = Split(req.path);
    bool any_path_matched = false;
    for (const auto& r : routes_) {
        HttpRequest copy = req;  // throwaway for capture before method check
        if (!Match(r, path_segs, copy))
            continue;
        any_path_matched = true;
        if (r.method == req.method) {
            req.path_params = std::move(copy.path_params);
            return r.handler(req);
        }
    }
    HttpResponse resp;
    if (any_path_matched) {
        resp.status = 405;
        resp.body =
            R"({"error":{"type":"method_not_allowed","reason":"method not allowed for this path"}})";
    } else {
        resp.status = 404;
        resp.body = R"({"error":{"type":"not_found","reason":"no route matches this path"}})";
    }
    return resp;
}

}  // namespace spp::http
