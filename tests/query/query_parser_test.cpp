#include "spp/query/query_parser.h"

#include "spp/query/query_ast.h"

#include <gtest/gtest.h>

using spp::query::QueryKind;

TEST(QueryParserTest, BareTermUsesDefaultField) {
    auto q = spp::query::Parse("hello", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kTerm);
    EXPECT_EQ(q.field(), "body");
    EXPECT_EQ(q.text(), "hello");
}

TEST(QueryParserTest, FieldQualifier) {
    auto q = spp::query::Parse("title:hello", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kTerm);
    EXPECT_EQ(q.field(), "title");
    EXPECT_EQ(q.text(), "hello");
}

TEST(QueryParserTest, ImplicitConjunction) {
    auto q = spp::query::Parse("foo bar", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kConjunction);
    ASSERT_EQ(q.children().size(), 2u);
    EXPECT_EQ(q.children()[0].text(), "foo");
    EXPECT_EQ(q.children()[1].text(), "bar");
}

TEST(QueryParserTest, ExplicitOR) {
    auto q = spp::query::Parse("foo OR bar", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kDisjunction);
    ASSERT_EQ(q.children().size(), 2u);
}

TEST(QueryParserTest, Parens) {
    auto q = spp::query::Parse("(foo OR bar) AND baz", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kConjunction);
    ASSERT_EQ(q.children().size(), 2u);
    EXPECT_EQ(q.children()[0].kind(), QueryKind::kDisjunction);
}

TEST(QueryParserTest, EmptyQueryIsMatchAll) {
    auto q = spp::query::Parse("", "body").value();
    EXPECT_EQ(q.kind(), QueryKind::kMatchAll);
}
