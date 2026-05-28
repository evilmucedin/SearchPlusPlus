#include "spp/index/schema.h"

#include "spp/json/json_parser.h"

#include <gtest/gtest.h>

TEST(SchemaTest, BuiltFromMappingsAutoAddsId) {
    auto v =
        spp::json::Parse(R"({"title":{"type":"text"},"category":{"type":"keyword","stored":true}})")
            .value();
    auto s = spp::index::Schema::FromMappingsJson(v).value();
    EXPECT_TRUE(s.HasField("_id"));
    EXPECT_TRUE(s.HasField("title"));
    EXPECT_TRUE(s.HasField("category"));
    EXPECT_EQ(s.GetField(s.GetFieldId("_id")).type, spp::index::FieldType::kKeyword);
    EXPECT_EQ(s.GetField(s.GetFieldId("title")).type, spp::index::FieldType::kText);
    EXPECT_EQ(s.GetField(s.GetFieldId("category")).type, spp::index::FieldType::kKeyword);
}

TEST(SchemaTest, JsonRoundTrip) {
    auto v = spp::json::Parse(R"({"title":{"type":"text"},"body":{"type":"text","stored":true}})")
                 .value();
    auto s = spp::index::Schema::FromMappingsJson(v).value();
    auto reser = s.ToJson();
    auto s2 = spp::index::Schema::FromJson(reser).value();
    ASSERT_EQ(s2.fields().size(), s.fields().size());
    for (std::size_t i = 0; i < s.fields().size(); ++i) {
        EXPECT_EQ(s2.fields()[i].name, s.fields()[i].name);
        EXPECT_EQ(s2.fields()[i].type, s.fields()[i].type);
        EXPECT_EQ(s2.fields()[i].stored, s.fields()[i].stored);
    }
}
