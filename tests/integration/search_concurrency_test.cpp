#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/server/search_server.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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
             ("spp_concur_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

std::uint16_t PickPort() {
    return static_cast<std::uint16_t>(20000 + (std::random_device{}() % 30000));
}

bool Exchange(std::uint16_t port,
              std::string_view method,
              std::string_view path,
              std::string_view body,
              int& status,
              std::string& bodyout) {
    std::string raw;
    if (!spp_test::HttpExchange(port, spp_test::MakeRequest(method, path, body), raw))
        return false;
    return spp_test::SplitResponse(raw, status, bodyout);
}

}  // namespace

TEST(SearchConcurrencyTest, CapBoundsPeakInFlightAndAllRequestsComplete) {
    auto root = TmpDir();
    spp::server::SearchServerOptions opts;
    opts.index_dir = root;
    opts.port = PickPort();
    // HTTP pool has to be wider than the search cap, otherwise the HTTP queue
    // dominates and the semaphore never blocks.
    opts.worker_threads = 16;
    opts.max_concurrent_searches = 2;
    spp::server::SearchServer server(std::move(opts));
    ASSERT_TRUE(server.Start().ok());
    ASSERT_EQ(server.MaxConcurrentSearches(), 2u);
    const auto port = server.Port();

    int status = 0;
    std::string body;
    ASSERT_TRUE(Exchange(port,
                         "PUT",
                         "/wiki",
                         R"({"mappings":{"title":{"type":"text","stored":true},)"
                         R"("body":{"type":"text","stored":true}}})",
                         status,
                         body));
    ASSERT_EQ(status, 200);

    // Populate with enough documents that each search does observable work —
    // without this, queries finish in microseconds and the test races itself.
    constexpr int kDocs = 400;
    for (int i = 0; i < kDocs; ++i) {
        const auto doc = std::string(R"({"_id":"d)") + std::to_string(i) +
                         R"(","title":"search term )" + std::to_string(i % 7) +
                         R"(","body":"the quick brown fox jumps over the lazy dog )" +
                         std::to_string(i) + R"("})";
        ASSERT_TRUE(Exchange(port, "POST", "/wiki/_doc", doc, status, body));
        ASSERT_EQ(status, 201);
    }
    ASSERT_TRUE(Exchange(port, "POST", "/wiki/_refresh", "", status, body));
    ASSERT_EQ(status, 200);

    constexpr int kThreads = 32;
    std::atomic<int> ok_count{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            int local_status = 0;
            std::string local_body;
            const auto path =
                std::string("/wiki/_search?q=search+term+") + std::to_string(i % 7) + "&size=10";
            if (Exchange(port, "GET", path, "", local_status, local_body) && local_status == 200) {
                ok_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(ok_count.load(), kThreads);
    // The semaphore must never let more than `max_concurrent_searches` runs
    // execute simultaneously, regardless of how many HTTP workers are idle.
    EXPECT_LE(server.PeakInFlightSearches(), 2u);
    // And it should actually have queued at least once under this load — if
    // peak stays at 0/1, the cap is dead code and the test isn't exercising it.
    EXPECT_GE(server.PeakInFlightSearches(), 1u);

    server.Stop();
    std::filesystem::remove_all(root);
}

TEST(SearchConcurrencyTest, AutoCapResolvesAboveFloor) {
    auto root = TmpDir();
    spp::server::SearchServerOptions opts;
    opts.index_dir = root;
    opts.port = PickPort();
    // Both fields left at 0 to exercise the auto branch.
    spp::server::SearchServer server(std::move(opts));
    ASSERT_TRUE(server.Start().ok());

    // Auto formula floors at 4; on every CI runner this evaluates to >= 4.
    EXPECT_GE(server.MaxConcurrentSearches(), 4u);
    server.Stop();
    std::filesystem::remove_all(root);
}
