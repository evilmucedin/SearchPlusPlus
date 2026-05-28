#include "spp/json/json_parser.h"

#include "spp/json/json_value.h"

#include <string>

#include <gtest/gtest.h>

using spp::json::JsonValue;

TEST(JsonParseTest, Primitives) {
    EXPECT_TRUE(spp::json::Parse("null")->is_null());
    EXPECT_TRUE(spp::json::Parse("true")->as_bool());
    EXPECT_FALSE(spp::json::Parse("false")->as_bool());
    EXPECT_EQ(spp::json::Parse("42")->as_int(), 42);
    EXPECT_DOUBLE_EQ(spp::json::Parse("3.5")->as_double(), 3.5);
    EXPECT_EQ(spp::json::Parse(R"("hi")")->as_string(), "hi");
}

TEST(JsonParseTest, EscapeSequences) {
    auto v = spp::json::Parse(R"("a\nb\tc\"d")");
    ASSERT_TRUE(v.ok());
    EXPECT_EQ(v->as_string(), "a\nb\tc\"d");
}

TEST(JsonParseTest, ArrayAndObject) {
    auto v = spp::json::Parse(R"({"a":1,"b":[2,3]})");
    ASSERT_TRUE(v.ok());
    ASSERT_TRUE(v->is_object());
    EXPECT_EQ(v->find("a")->as_int(), 1);
    ASSERT_TRUE(v->find("b")->is_array());
    EXPECT_EQ(v->find("b")->as_array().size(), 2u);
}

TEST(JsonParseTest, RejectsTrailingComma) {
    EXPECT_FALSE(spp::json::Parse(R"([1,2,])").ok());
    EXPECT_FALSE(spp::json::Parse(R"({"a":1,})").ok());
}

TEST(JsonParseTest, RejectsComment) {
    EXPECT_FALSE(spp::json::Parse(R"(// no
1)")
                     .ok());
}

TEST(JsonParseTest, RejectsUnquotedKey) {
    EXPECT_FALSE(spp::json::Parse(R"({a:1})").ok());
}
