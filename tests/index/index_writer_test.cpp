#include "spp/index/index_writer.h"

#include "spp/index/document.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"

#include <filesystem>
#include <random>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_writer_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

spp::index::Schema MakeSchema() {
    auto v = spp::json::Parse(R"({"title":{"type":"text","stored":true}})").value();
    return spp::index::Schema::FromMappingsJson(v).value();
}

}  // namespace

TEST(IndexWriterTest, RefreshPublishesSegment) {
    auto root = TmpDir();
    auto schema = MakeSchema();
    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &schema;
    auto writer = spp::index::IndexWriter::Open(root, opts).value();

    spp::index::Document d;
    d.id = "x";
    d.fields["title"] = "elasticsearch inspired";
    ASSERT_TRUE(
        writer->AddDocument(d, "{\"_id\":\"x\",\"title\":\"elasticsearch inspired\"}").ok());

    // NRT: not visible until refresh.
    auto pre = writer->CurrentReader();
    EXPECT_EQ(pre->segment_count(), 0u);

    auto gen = writer->Refresh().value();
    EXPECT_GE(gen, 1u);
    auto post = writer->CurrentReader();
    EXPECT_EQ(post->segment_count(), 1u);
    EXPECT_EQ(post->segments()[0]->doc_count(), 1u);
    ASSERT_TRUE(writer->Close().ok());
    std::filesystem::remove_all(root);
}

TEST(IndexWriterTest, ReopenReplaysTranslog) {
    auto root = TmpDir();
    auto schema = MakeSchema();
    {
        spp::index::IndexOpenOptions opts;
        opts.initial_schema = &schema;
        auto writer = spp::index::IndexWriter::Open(root, opts).value();
        spp::index::Document d;
        d.id = "a";
        d.fields["title"] = "before crash";
        ASSERT_TRUE(writer->AddDocument(d, "{\"_id\":\"a\",\"title\":\"before crash\"}").ok());
        // Do NOT refresh. Destructor closes translog without sealing.
        ASSERT_TRUE(writer->Close().ok());
    }
    // Reopen: translog replay should make the doc available after a refresh.
    auto writer = spp::index::IndexWriter::Open(root).value();
    auto gen = writer->Refresh().value();
    EXPECT_GE(gen, 1u);
    auto reader = writer->CurrentReader();
    ASSERT_GE(reader->segment_count(), 1u);
    EXPECT_EQ(reader->total_doc_count(), 1u);
    ASSERT_TRUE(writer->Close().ok());
    std::filesystem::remove_all(root);
}
