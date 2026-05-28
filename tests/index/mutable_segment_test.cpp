#include "spp/index/mutable_segment.h"

#include "spp/index/document.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"

#include <gtest/gtest.h>

namespace {

spp::index::Schema MakeTitleBodySchema() {
    auto v = spp::json::Parse(R"({"title":{"type":"text"},"body":{"type":"text","stored":true}})")
                 .value();
    return spp::index::Schema::FromMappingsJson(v).value();
}

}  // namespace

TEST(MutableSegmentTest, IndexesPostingsAndTfs) {
    auto schema = MakeTitleBodySchema();
    spp::index::MutableSegment seg(schema);

    spp::index::Document d1;
    d1.id = "a";
    d1.fields["title"] = "hello world";
    d1.fields["body"] = "hello again hello";
    ASSERT_TRUE(seg.AddDocument(d1).ok());

    spp::index::Document d2;
    d2.id = "b";
    d2.fields["title"] = "world peace";
    d2.fields["body"] = "peace and love";
    ASSERT_TRUE(seg.AddDocument(d2).ok());

    EXPECT_EQ(seg.doc_count(), 2u);
    const auto title_id = schema.GetFieldId("title");
    const auto body_id = schema.GetFieldId("body");
    const auto& title_data = seg.field_data()[title_id];
    const auto& body_data = seg.field_data()[body_id];

    auto it = title_data.postings.find("hello");
    ASSERT_NE(it, title_data.postings.end());
    EXPECT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0].tf, 1u);

    auto h = body_data.postings.find("hello");
    ASSERT_NE(h, body_data.postings.end());
    EXPECT_EQ(h->second.size(), 1u);
    EXPECT_EQ(h->second[0].tf, 2u);
}
