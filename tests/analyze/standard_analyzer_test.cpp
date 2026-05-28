#include "spp/analyze/analyzer.h"
#include "spp/analyze/token.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::string> Texts(const std::vector<spp::analyze::Token>& toks) {
    std::vector<std::string> out;
    out.reserve(toks.size());
    for (const auto& t : toks)
        out.push_back(t.text);
    return out;
}

}  // namespace

TEST(StandardAnalyzerTest, LowercasesAndSplitsOnPunctuation) {
    auto a = spp::analyze::MakeStandardAnalyzer();
    std::vector<spp::analyze::Token> out;
    a->Analyze("Hello, World!", out);
    EXPECT_EQ(Texts(out), (std::vector<std::string>{"hello", "world"}));
}

TEST(StandardAnalyzerTest, DropsStopWords) {
    auto a = spp::analyze::MakeStandardAnalyzer();
    std::vector<spp::analyze::Token> out;
    a->Analyze("The quick brown fox jumps over the lazy dog of a hen", out);
    const auto t = Texts(out);
    for (const auto& w : {"the", "of", "a"}) {
        for (const auto& got : t)
            EXPECT_NE(got, w);
    }
    EXPECT_NE(std::find(t.begin(), t.end(), "quick"), t.end());
    EXPECT_NE(std::find(t.begin(), t.end(), "fox"), t.end());
}

TEST(StandardAnalyzerTest, AlphaNumericTogether) {
    auto a = spp::analyze::MakeStandardAnalyzer();
    std::vector<spp::analyze::Token> out;
    a->Analyze("v0.1 release2024", out);
    const auto t = Texts(out);
    EXPECT_NE(std::find(t.begin(), t.end(), "v0"), t.end());
    EXPECT_NE(std::find(t.begin(), t.end(), "release2024"), t.end());
}

TEST(KeywordAnalyzerTest, EmitsOneTokenUnchanged) {
    auto a = spp::analyze::MakeKeywordAnalyzer();
    std::vector<spp::analyze::Token> out;
    a->Analyze("Hello WORLD!", out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].text, "Hello WORLD!");
}
