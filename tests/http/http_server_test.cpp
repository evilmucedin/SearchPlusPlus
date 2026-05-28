#include "spp/http/http_server.h"

#include "spp/http/router.h"

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "tests/http/test_client.h"

namespace {

std::uint16_t PickPort() {
    // Best-effort ephemeral port; OS will accept the bind.
    return static_cast<std::uint16_t>(20000 + (std::random_device{}() % 30000));
}

}  // namespace

TEST(HttpServerTest, RouterDispatchesAndReturnsBody) {
    auto router = std::make_shared<spp::http::Router>();
    router->Add("GET", "/_health", [](const spp::http::HttpRequest&) {
        spp::http::HttpResponse r;
        r.status = 200;
        r.body = R"({"status":"ok"})";
        return r;
    });
    router->Add("POST", "/:id/echo", [](const spp::http::HttpRequest& req) {
        spp::http::HttpResponse r;
        r.status = 200;
        r.body = req.body + "/" + req.path_params.at("id");
        return r;
    });

    spp::http::HttpServerOptions opts;
    opts.port = PickPort();
    opts.worker_threads = 2;
    spp::http::HttpServer server(opts, router);
    ASSERT_TRUE(server.Start().ok());

    std::string resp;
    ASSERT_TRUE(spp_test::HttpExchange(opts.port, spp_test::MakeRequest("GET", "/_health"), resp));
    int status = 0;
    std::string body;
    ASSERT_TRUE(spp_test::SplitResponse(resp, status, body));
    EXPECT_EQ(status, 200);
    EXPECT_NE(body.find("ok"), std::string::npos);

    ASSERT_TRUE(spp_test::HttpExchange(
        opts.port, spp_test::MakeRequest("POST", "/abc/echo", "hello"), resp));
    ASSERT_TRUE(spp_test::SplitResponse(resp, status, body));
    EXPECT_EQ(status, 200);
    EXPECT_EQ(body, "hello/abc");

    ASSERT_TRUE(
        spp_test::HttpExchange(opts.port, spp_test::MakeRequest("GET", "/no_such_route"), resp));
    ASSERT_TRUE(spp_test::SplitResponse(resp, status, body));
    EXPECT_EQ(status, 404);

    server.Stop();
}
