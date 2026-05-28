// End-to-end HTTP test of the two-stage searcher: index two docs, configure a
// LinearRanker via PUT /:index/_ltr/linear, then verify that &rerank=true
// reverses the BM25 ordering by weighting the doc_quality feature heavily.

#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/query/features.h"
#include "spp/server/search_server.h"

#include <filesystem>
#include <random>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "tests/http/test_client.h"

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_rerank_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

std::uint16_t PickPort() {
    return static_cast<std::uint16_t>(20000 + (std::random_device{}() % 30000));
}

std::string LinearWeightsJson(std::size_t slot, float weight) {
    std::ostringstream os;
    os << R"({"bias":0,"weights":[)";
    for (std::size_t i = 0; i < spp::query::kFeatureCount; ++i) {
        if (i)
            os << ',';
        os << (i == slot ? weight : 0.0f);
    }
    os << "]}";
    return os.str();
}

}  // namespace

TEST(RerankTest, LinearRankerReordersByDocQuality) {
    auto root = TmpDir();
    spp::server::SearchServerOptions opts;
    opts.index_dir = root;
    opts.port = PickPort();
    opts.worker_threads = 2;
    spp::server::SearchServer server(std::move(opts));
    ASSERT_TRUE(server.Start().ok());

    const auto port = server.Port();
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

    // Create an index that opts into per-doc quality storage.
    exchange("PUT",
             "/wiki",
             R"({"mappings":{"title":{"type":"text","stored":true}},
                  "store_doc_quality":true})",
             status,
             body);
    ASSERT_EQ(status, 200) << body;

    // Two docs, both matching "hello". Doc B has *more* term matches (higher
    // BM25), but doc A has a much higher static quality. With a ranker that
    // weights only doc_quality, A should beat B even though BM25 prefers B.
    exchange("POST", "/wiki/_doc", R"({"_id":"a","title":"hello","_quality":0.95})", status, body);
    ASSERT_EQ(status, 201);
    exchange("POST",
             "/wiki/_doc",
             R"({"_id":"b","title":"hello hello hello hello","_quality":0.01})",
             status,
             body);
    ASSERT_EQ(status, 201);
    exchange("POST", "/wiki/_refresh", "", status, body);
    ASSERT_EQ(status, 200);

    // Baseline BM25 ordering: B should rank above A.
    exchange("GET", "/wiki/_search?q=title:hello", "", status, body);
    ASSERT_EQ(status, 200);
    {
        auto j = spp::json::Parse(body);
        ASSERT_TRUE(j.ok());
        const auto& hits = j->find("hits")->as_array();
        ASSERT_GE(hits.size(), 2u);
        EXPECT_EQ(hits[0].find("_id")->as_string(), "b");
        EXPECT_EQ(hits[1].find("_id")->as_string(), "a");
    }

    // Configure a LinearRanker that gives 100x weight to doc_quality (slot 15)
    // and nothing else. PUT and re-query with rerank=true.
    const auto put_body =
        LinearWeightsJson(static_cast<std::size_t>(spp::query::Feature::kDocQuality), 100.0f);
    exchange("PUT", "/wiki/_ltr/linear", put_body, status, body);
    ASSERT_EQ(status, 200) << body;

    // GET /_ltr shows the linear ranker is loaded.
    exchange("GET", "/wiki/_ltr", "", status, body);
    ASSERT_EQ(status, 200);
    {
        auto j = spp::json::Parse(body);
        ASSERT_TRUE(j.ok());
        EXPECT_NE(j->find("linear"), nullptr);
    }

    // With rerank enabled, doc A should now beat doc B.
    exchange("GET", "/wiki/_search?q=title:hello&rerank=true&ranker=linear", "", status, body);
    ASSERT_EQ(status, 200);
    {
        auto j = spp::json::Parse(body);
        ASSERT_TRUE(j.ok());
        const auto& hits = j->find("hits")->as_array();
        ASSERT_GE(hits.size(), 2u);
        EXPECT_EQ(hits[0].find("_id")->as_string(), "a");
        EXPECT_EQ(hits[1].find("_id")->as_string(), "b");
    }

    // Unknown ranker name should 400.
    exchange("GET", "/wiki/_search?q=title:hello&rerank=true&ranker=nope", "", status, body);
    EXPECT_EQ(status, 400);

    server.Stop();
    std::filesystem::remove_all(root);
}
