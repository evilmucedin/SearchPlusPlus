#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/server/search_server.h"

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "tests/http/test_client.h"

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_e2e_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

std::uint16_t PickPort() {
    return static_cast<std::uint16_t>(20000 + (std::random_device{}() % 30000));
}

}  // namespace

TEST(E2eHttpTest, FullCrudFlow) {
    auto root = TmpDir();
    spp::server::SearchServerOptions opts;
    opts.index_dir = root;
    opts.port = PickPort();
    opts.worker_threads = 2;
    spp::server::SearchServer server(std::move(opts));
    ASSERT_TRUE(server.Start().ok());

    auto port = server.Port();
    auto exchange = [&](std::string_view method,
                        std::string_view path,
                        std::string_view body,
                        int& status,
                        std::string& bodyout) {
        std::string raw;
        ASSERT_TRUE(spp_test::HttpExchange(port, spp_test::MakeRequest(method, path, body), raw));
        ASSERT_TRUE(spp_test::SplitResponse(raw, status, bodyout));
    };

    int status = 0;
    std::string body;
    exchange("GET", "/_health", "", status, body);
    EXPECT_EQ(status, 200);
    EXPECT_NE(body.find("ok"), std::string::npos);

    exchange(
        "PUT",
        "/wiki",
        R"({"mappings":{"title":{"type":"text","stored":true},"body":{"type":"text","stored":true}}})",
        status,
        body);
    EXPECT_EQ(status, 200);

    // Add doc but don't refresh; NRT — query should miss.
    exchange("POST",
             "/wiki/_doc",
             R"({"_id":"a","title":"hello search","body":"new document"})",
             status,
             body);
    EXPECT_EQ(status, 201);

    exchange("GET", "/wiki/_search?q=title:hello", "", status, body);
    EXPECT_EQ(status, 200);
    auto json = spp::json::Parse(body);
    ASSERT_TRUE(json.ok());
    EXPECT_EQ(json->find("total")->as_int(), 0);

    // Refresh, query again — should hit.
    exchange("POST", "/wiki/_refresh", "", status, body);
    EXPECT_EQ(status, 200);

    exchange("GET", "/wiki/_search?q=title:hello&size=10", "", status, body);
    EXPECT_EQ(status, 200);
    auto json2 = spp::json::Parse(body);
    ASSERT_TRUE(json2.ok());
    EXPECT_GE(json2->find("total")->as_int(), 1);

    // 404 for nonexistent index.
    exchange("GET", "/nope/_search?q=x", "", status, body);
    EXPECT_EQ(status, 404);

    server.Stop();
    std::filesystem::remove_all(root);
}
