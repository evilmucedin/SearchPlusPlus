#include "spp/index/document.h"
#include "spp/index/mutable_segment.h"
#include "spp/index/schema.h"
#include "spp/index/segment_reader.h"
#include "spp/index/segment_writer.h"
#include "spp/json/json_parser.h"
#include "spp/store/directory.h"

#include <filesystem>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_seg_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

spp::index::Schema MakeSchema() {
    auto v = spp::json::Parse(
                 R"({"title":{"type":"text","stored":true},"body":{"type":"text","stored":true}})")
                 .value();
    return spp::index::Schema::FromMappingsJson(v).value();
}

}  // namespace

TEST(SegmentRoundtripTest, WriteThenRead) {
    auto root = TmpDir();
    auto dir = spp::store::OpenFilesystemDirectory(root).value();
    auto schema = MakeSchema();
    spp::index::MutableSegment seg(schema);

    spp::index::Document a;
    a.id = "doc1";
    a.fields["title"] = "search engines are fun";
    a.fields["body"] = "I like to write search engines in C++";
    ASSERT_TRUE(seg.AddDocument(a).ok());

    spp::index::Document b;
    b.id = "doc2";
    b.fields["title"] = "engine running";
    b.fields["body"] = "vroom vroom";
    ASSERT_TRUE(seg.AddDocument(b).ok());

    auto si = spp::index::SealSegment(seg, *dir, "seg-0").value();
    EXPECT_EQ(si.doc_count, 2u);

    auto reader_e = spp::index::SegmentReader::Open(*dir, "seg-0");
    ASSERT_TRUE(reader_e.ok());
    auto& reader = *reader_e;
    EXPECT_EQ(reader->doc_count(), 2u);

    const auto title_id = reader->GetFieldId("title");
    ASSERT_NE(title_id, spp::kInvalidFieldId);
    const auto* term = reader->FindTerm(title_id, "search");
    ASSERT_NE(term, nullptr);
    EXPECT_GE(term->df, 1u);

    EXPECT_NE(reader->StoredFields(0).find("doc1"), std::string_view::npos);
    EXPECT_NE(reader->StoredFields(1).find("doc2"), std::string_view::npos);

    std::filesystem::remove_all(root);
}
