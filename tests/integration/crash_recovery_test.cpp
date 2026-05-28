#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_crash_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

spp::index::Schema MakeSchema() {
    auto v = spp::json::Parse(R"({"body":{"type":"text","stored":true}})").value();
    return spp::index::Schema::FromMappingsJson(v).value();
}

}  // namespace

// Simulates: docs are ingested but the process dies before any Refresh.
// On restart, IndexWriter::Open() replays the translog into a fresh MutableSegment.
// A subsequent Refresh() seals it, and queries see the recovered docs.
TEST(CrashRecoveryTest, TranslogReplaysOnRestartWithoutRefresh) {
    auto root = TmpDir();
    auto schema = MakeSchema();

    {
        spp::index::IndexOpenOptions opts;
        opts.initial_schema = &schema;
        auto writer = spp::index::IndexWriter::Open(root, opts).value();
        for (int i = 0; i < 5; ++i) {
            spp::index::Document d;
            d.id = "d" + std::to_string(i);
            d.fields["body"] = "crash test " + std::to_string(i);
            const std::string raw =
                R"({"_id":")" + d.id + R"(","body":"crash test )" + std::to_string(i) + "\"}";
            ASSERT_TRUE(writer->AddDocument(d, raw).ok());
        }
        // Simulate crash: Close without Refresh. Pending docs stay in translog.
        ASSERT_TRUE(writer->Close().ok());
    }

    // Reopen — translog replay should put docs back in the mutable segment.
    {
        auto writer = spp::index::IndexWriter::Open(root).value();

        // Before Refresh, the *published* reader still has zero docs (NRT).
        auto pre = writer->CurrentReader();
        EXPECT_EQ(pre->total_doc_count(), 0u);

        // Now seal — published reader should reflect the recovered docs.
        ASSERT_TRUE(writer->Refresh().ok());
        auto post = writer->CurrentReader();
        EXPECT_EQ(post->total_doc_count(), 5u);

        spp::query::Searcher s(post);
        spp::query::SearchOptions opts;
        opts.size = 10;
        opts.default_field = "body";
        auto ast = spp::query::Parse("crash", "body").value();
        auto res = s.Search(ast, opts).value();
        EXPECT_EQ(res.total_hits, 5u);

        ASSERT_TRUE(writer->Refresh().ok());
        ASSERT_TRUE(writer->Close().ok());
    }
    std::filesystem::remove_all(root);
}
