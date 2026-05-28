#pragma once

#include "spp/http/http_message.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace spp::http {

// A Router maps (method, path-pattern) to a handler. Patterns may contain a single
// trailing path parameter of the form ":name" (e.g. "/:index/_doc"). The matched value
// is exposed via HttpRequest::path_params (set by the router before dispatch).
//
// Patterns are matched literally segment-by-segment, with the optional last segment as
// a parameter. Examples:
//   "/_health"             matches "/_health"
//   "/:index"              matches "/wiki" (params["index"]="wiki")
//   "/:index/_doc"         matches "/wiki/_doc"
//   "/:index/_refresh"     matches "/wiki/_refresh"
//   "/:index/_search"      matches "/wiki/_search"
//
// On no match the router returns 404. On wrong-method-for-existing-path it returns 405.
class Router {
 public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    void Add(std::string method, std::string pattern, Handler handler);

    HttpResponse Dispatch(HttpRequest& req) const;

 private:
    struct Route {
        std::string method;
        std::vector<std::string> segments;  // Each entry either literal or ":name".
        Handler handler;
    };

    static std::vector<std::string> Split(std::string_view path);
    static bool Match(const Route& r, const std::vector<std::string>& path_segs, HttpRequest& req);

    std::vector<Route> routes_;
};

}  // namespace spp::http
