#include "spp/json/json_serializer.h"

#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"

#include <gtest/gtest.h>

using spp::json::JsonArray;
using spp::json::JsonObject;
using spp::json::JsonValue;

TEST(JsonSerializeTest, Primitives) {
    EXPECT_EQ(spp::json::Serialize(JsonValue(nullptr)), "null");
    EXPECT_EQ(spp::json::Serialize(JsonValue(true)), "true");
    EXPECT_EQ(spp::json::Serialize(JsonValue(false)), "false");
    EXPECT_EQ(spp::json::Serialize(JsonValue(static_cast<std::int64_t>(7))), "7");
    EXPECT_EQ(spp::json::Serialize(JsonValue(std::string("hi"))), "\"hi\"");
}

TEST(JsonSerializeTest, EscapesQuotesAndControl) {
    EXPECT_EQ(spp::json::Serialize(JsonValue(std::string("a\nb"))), "\"a\\nb\"");
    EXPECT_EQ(spp::json::Serialize(JsonValue(std::string("a\"b"))), "\"a\\\"b\"");
}

TEST(JsonSerializeTest, RoundTrip) {
    const std::string in = R"({"a":1,"b":["x","y"],"c":null})";
    auto v = spp::json::Parse(in);
    ASSERT_TRUE(v.ok());
    EXPECT_EQ(spp::json::Serialize(*v), in);
}

TEST(JsonSerializeTest, PrettyHasNewlines) {
    JsonObject o;
    o["a"] = 1;
    o["b"] = 2;
    const auto pretty = spp::json::SerializePretty(JsonValue(std::move(o)), 2);
    EXPECT_NE(pretty.find('\n'), std::string::npos);
}
