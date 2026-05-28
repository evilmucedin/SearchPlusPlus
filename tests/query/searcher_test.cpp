#include "spp/query/searcher.h"

#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/query/query_parser.h"

#include <filesystem>
#include <random>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_searcher_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

TEST(SearcherTest, FindsExpectedDoc) {
    auto root = TmpDir();
    auto v = spp::json::Parse(
                 R"({"title":{"type":"text","stored":true},"body":{"type":"text","stored":true}})")
                 .value();
    auto schema = spp::index::Schema::FromMappingsJson(v).value();
    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &schema;
    auto writer = spp::index::IndexWriter::Open(root, opts).value();

    auto add = [&](const std::string& id, const std::string& title, const std::string& body) {
        spp::index::Document d;
        d.id = id;
        d.fields["title"] = title;
        d.fields["body"] = body;
        const std::string raw =
            R"({"_id":")" + id + R"(","title":")" + title + R"(","body":")" + body + "\"}";
        ASSERT_TRUE(writer->AddDocument(d, raw).ok());
    };
    add("a", "elasticsearch inspired", "we build a search engine in c++");
    add("b", "vroom car engine", "a fast car");
    add("c", "search engine theory", "documents postings inverted index");
    ASSERT_TRUE(writer->Refresh().ok());

    spp::query::Searcher s(writer->CurrentReader());
    spp::query::SearchOptions opts2;
    opts2.size = 10;
    opts2.default_field = "body";

    auto q1 = spp::query::Parse("search", "body").value();
    auto r1 = s.Search(q1, opts2).value();
    EXPECT_GE(r1.total_hits, 1u);

    auto q2 = spp::query::Parse("title:engine", "body").value();
    auto r2 = s.Search(q2, opts2).value();
    EXPECT_GE(r2.total_hits, 2u);

    auto q3 = spp::query::Parse("noopnoop", "body").value();
    auto r3 = s.Search(q3, opts2).value();
    EXPECT_EQ(r3.total_hits, 0u);
    EXPECT_TRUE(r3.hits.empty());

    ASSERT_TRUE(writer->Close().ok());
    std::filesystem::remove_all(root);
}
