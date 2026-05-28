#include "spp/query/ranker.h"

#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/query/features.h"

#include <gtest/gtest.h>

namespace {

using spp::query::Bm25Ranker;
using spp::query::CatboostRanker;
using spp::query::Feature;
using spp::query::FeatureVector;
using spp::query::kFeatureCount;
using spp::query::LinearRanker;

constexpr std::size_t Idx(Feature f) {
    return static_cast<std::size_t>(f);
}

FeatureVector MakeFv() {
    FeatureVector fv{};
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        fv[i] = static_cast<float>(i + 1);
    }
    return fv;
}

TEST(RankerTest, Bm25ReturnsBm25TotalSlot) {
    Bm25Ranker r;
    FeatureVector fv{};
    fv[Idx(Feature::kBm25Total)] = 42.5f;
    fv[Idx(Feature::kTfSum)] = 999.0f;
    EXPECT_FLOAT_EQ(r.Score(fv), 42.5f);
}

TEST(RankerTest, Bm25ToJsonHasType) {
    Bm25Ranker r;
    const auto j = r.ToJson();
    ASSERT_TRUE(j.is_object());
    const auto* t = j.find("type");
    ASSERT_NE(t, nullptr);
    ASSERT_TRUE(t->is_string());
    EXPECT_EQ(t->as_string(), "bm25");
}

TEST(RankerTest, LinearComputesDotPlusBias) {
    std::array<float, kFeatureCount> w{};
    w.fill(0.0f);
    w[Idx(Feature::kBm25Total)] = 2.0f;
    w[Idx(Feature::kTokenWeightSum)] = -0.5f;
    LinearRanker r(/*bias=*/0.25f, w);

    FeatureVector fv{};
    fv[Idx(Feature::kBm25Total)] = 3.0f;
    fv[Idx(Feature::kTokenWeightSum)] = 4.0f;
    const float expected = 0.25f + 2.0f * 3.0f - 0.5f * 4.0f;
    EXPECT_FLOAT_EQ(r.Score(fv), expected);
}

TEST(RankerTest, LinearDefaultsToBiasOnly) {
    LinearRanker r;
    FeatureVector fv = MakeFv();
    EXPECT_FLOAT_EQ(r.Score(fv), 0.0f);
}

TEST(RankerTest, LinearFromJsonRoundtrips) {
    std::array<float, kFeatureCount> w{};
    w.fill(0.0f);
    w[0] = 1.5f;
    w[3] = -2.0f;
    LinearRanker original(/*bias=*/0.75f, w);
    auto j = original.ToJson();

    auto parsed = LinearRanker::FromJson(j);
    ASSERT_TRUE(parsed.ok());
    EXPECT_FLOAT_EQ((*parsed)->bias(), 0.75f);
    EXPECT_FLOAT_EQ((*parsed)->weights()[0], 1.5f);
    EXPECT_FLOAT_EQ((*parsed)->weights()[3], -2.0f);
}

TEST(RankerTest, LinearFromJsonRejectsWrongWeightCount) {
    auto j = spp::json::Parse(R"({"bias": 0.0, "weights": [1.0, 2.0]})");
    ASSERT_TRUE(j.ok());
    auto parsed = LinearRanker::FromJson(*j);
    EXPECT_FALSE(parsed.ok());
}

TEST(RankerTest, LinearFromJsonRejectsMissingWeights) {
    auto j = spp::json::Parse(R"({"bias": 1.0})");
    ASSERT_TRUE(j.ok());
    auto parsed = LinearRanker::FromJson(*j);
    EXPECT_FALSE(parsed.ok());
}

TEST(RankerTest, LinearFromJsonRejectsNonObject) {
    auto j = spp::json::Parse(R"([1, 2, 3])");
    ASSERT_TRUE(j.ok());
    auto parsed = LinearRanker::FromJson(*j);
    EXPECT_FALSE(parsed.ok());
}

TEST(RankerTest, CatboostStubReturnsBm25Slot) {
    // The default checked-in stub at models/catboost_model.cpp returns
    // floatFeatures[0]. CatboostRanker copies the FeatureVector into the
    // vector<float> arg in slot order, so floatFeatures[0] == fv[0] == bm25_total.
    CatboostRanker r;
    FeatureVector fv = MakeFv();
    EXPECT_FLOAT_EQ(r.Score(fv), fv[Idx(Feature::kBm25Total)]);
}

TEST(RankerTest, CatboostToJsonAdvertisesSchemaVersion) {
    CatboostRanker r;
    const auto j = r.ToJson();
    ASSERT_TRUE(j.is_object());
    const auto* t = j.find("type");
    ASSERT_NE(t, nullptr);
    ASSERT_TRUE(t->is_string());
    EXPECT_EQ(t->as_string(), "catboost");
    const auto* v = j.find("feature_schema_version");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(v->is_number());
}

}  // namespace
