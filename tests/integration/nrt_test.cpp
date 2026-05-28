#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/server/search_server.h"

#include <atomic>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "tests/http/test_client.h"

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_nrt_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

TEST(NrtTest, ParallelReadsDuringIngest) {
    auto root = TmpDir();
    spp::server::SearchServerOptions opts;
    opts.index_dir = root;
    opts.port = static_cast<std::uint16_t>(20000 + (std::random_device{}() % 30000));
    opts.worker_threads = 4;
    spp::server::SearchServer server(std::move(opts));
    ASSERT_TRUE(server.Start().ok());
    const auto port = server.Port();

    auto put = [&](std::string_view path, std::string_view body) {
        std::string raw;
        spp_test::HttpExchange(port, spp_test::MakeRequest("PUT", path, body), raw);
    };
    auto post = [&](std::string_view path, std::string_view body) {
        std::string raw;
        spp_test::HttpExchange(port, spp_test::MakeRequest("POST", path, body), raw);
    };

    put("/idx", R"({"mappings":{"body":{"type":"text"}}})");

    std::atomic<bool> stop{false};
    std::atomic<int> ok_reads{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                std::string raw;
                if (spp_test::HttpExchange(
                        port, spp_test::MakeRequest("GET", "/idx/_search?q=needle"), raw)) {
                    int s = 0;
                    std::string body;
                    if (spp_test::SplitResponse(raw, s, body) && s == 200) {
                        ok_reads.fetch_add(1);
                    }
                }
            }
        });
    }

    for (int i = 0; i < 50; ++i) {
        const std::string body = R"({"_id":"d)" + std::to_string(i) +
                                 R"(","body":"needle in haystack )" + std::to_string(i) + "\"}";
        post("/idx/_doc", body);
        if ((i & 7) == 0)
            post("/idx/_refresh", "");
    }
    post("/idx/_refresh", "");

    stop.store(true);
    for (auto& t : readers)
        t.join();
    EXPECT_GT(ok_reads.load(), 0);

    std::string raw;
    spp_test::HttpExchange(
        port, spp_test::MakeRequest("GET", "/idx/_search?q=needle&size=100"), raw);
    int s = 0;
    std::string body;
    ASSERT_TRUE(spp_test::SplitResponse(raw, s, body));
    EXPECT_EQ(s, 200);
    auto json = spp::json::Parse(body).value();
    EXPECT_EQ(json.find("total")->as_int(), 50);

    server.Stop();
    std::filesystem::remove_all(root);
}
