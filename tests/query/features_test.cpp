#include "spp/query/features.h"

#include "spp/query/bm25_scorer.h"

#include <gtest/gtest.h>

namespace {

using spp::query::Bm25Params;
using spp::query::CandidateContext;
using spp::query::ExtractFeatures;
using spp::query::Feature;
using spp::query::FeatureName;
using spp::query::kFeatureCount;
using spp::query::LeafContextItem;

constexpr std::size_t Idx(Feature f) {
    return static_cast<std::size_t>(f);
}

TEST(FeaturesTest, EmptyLeavesYieldsZerosExceptQueryTerms) {
    CandidateContext ctx;
    ctx.num_query_leaves = 4;
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kBm25Total)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kNumMatchedTerms)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kNumQueryTerms)], 4.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kMatchRatio)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kDocQuality)], 0.0f);
}

TEST(FeaturesTest, MatchRatioIsMatchedOverQueryLeaves) {
    CandidateContext ctx;
    ctx.num_query_leaves = 3;
    LeafContextItem hit;
    hit.matched = true;
    hit.tf = 1;
    hit.idf = 0.5;
    hit.field_len = 10.0;
    hit.avg_field_len = 10.0;
    hit.field_id = 0;
    ctx.leaves = {hit, hit, /*miss=*/LeafContextItem{}};
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kNumMatchedTerms)], 2.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kNumQueryTerms)], 3.0f);
    EXPECT_NEAR(fv[Idx(Feature::kMatchRatio)], 2.0f / 3.0f, 1e-6);
}

TEST(FeaturesTest, Bm25TotalMatchesPerFieldSumForKnownLeaves) {
    CandidateContext ctx;
    ctx.num_query_leaves = 2;
    LeafContextItem a;
    a.matched = true;
    a.tf = 3;
    a.idf = 1.0;
    a.boost = 1.0f;
    a.field_id = 0;
    a.field_len = 20.0;
    a.avg_field_len = 20.0;
    LeafContextItem b = a;
    b.field_id = 2;
    b.tf = 2;
    ctx.leaves = {a, b};

    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    const float sum_fields = fv[Idx(Feature::kBm25Field0)] + fv[Idx(Feature::kBm25Field1)] +
                             fv[Idx(Feature::kBm25Field2)] + fv[Idx(Feature::kBm25Field3)];
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kBm25Total)], sum_fields);
    EXPECT_GT(fv[Idx(Feature::kBm25Field0)], 0.0f);
    EXPECT_GT(fv[Idx(Feature::kBm25Field2)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kBm25Field1)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kBm25Field3)], 0.0f);
}

TEST(FeaturesTest, BoostScalesBm25Contribution) {
    LeafContextItem base;
    base.matched = true;
    base.tf = 2;
    base.idf = 1.0;
    base.field_id = 0;
    base.field_len = 20.0;
    base.avg_field_len = 20.0;

    CandidateContext ctx_a;
    ctx_a.num_query_leaves = 1;
    ctx_a.leaves = {base};

    LeafContextItem boosted = base;
    boosted.boost = 2.5f;
    CandidateContext ctx_b;
    ctx_b.num_query_leaves = 1;
    ctx_b.leaves = {boosted};

    const auto fa = ExtractFeatures(ctx_a, Bm25Params{});
    const auto fb = ExtractFeatures(ctx_b, Bm25Params{});
    EXPECT_NEAR(fb[Idx(Feature::kBm25Total)], fa[Idx(Feature::kBm25Total)] * 2.5f, 1e-4);
}

TEST(FeaturesTest, PositionDecaySumDecreasesForLaterPositions) {
    LeafContextItem early;
    early.matched = true;
    early.tf = 1;
    early.idf = 1.0;
    early.field_len = 100.0;
    early.avg_field_len = 100.0;
    early.position_decay = 1.0f;
    early.first_pos = 0;

    LeafContextItem late = early;
    late.first_pos = 90;

    CandidateContext ctx_a;
    ctx_a.num_query_leaves = 1;
    ctx_a.leaves = {early};
    CandidateContext ctx_b;
    ctx_b.num_query_leaves = 1;
    ctx_b.leaves = {late};

    const auto fa = ExtractFeatures(ctx_a, Bm25Params{});
    const auto fb = ExtractFeatures(ctx_b, Bm25Params{});
    EXPECT_GT(fa[Idx(Feature::kPositionDecaySum)], fb[Idx(Feature::kPositionDecaySum)]);
    EXPECT_NEAR(fa[Idx(Feature::kPositionDecaySum)], 1.0f, 1e-4);
}

TEST(FeaturesTest, TokenWeightSumAndMax) {
    LeafContextItem a;
    a.matched = true;
    a.tf = 1;
    a.idf = 0.0;
    a.field_len = 1.0;
    a.avg_field_len = 1.0;
    a.token_weight = 0.5f;

    LeafContextItem b = a;
    b.token_weight = 2.0f;
    LeafContextItem c = a;
    c.token_weight = 1.25f;

    CandidateContext ctx;
    ctx.num_query_leaves = 3;
    ctx.leaves = {a, b, c};
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_NEAR(fv[Idx(Feature::kTokenWeightSum)], 3.75f, 1e-5);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kTokenWeightMax)], 2.0f);
}

TEST(FeaturesTest, MinAndMaxIdfMatched) {
    LeafContextItem a;
    a.matched = true;
    a.tf = 1;
    a.idf = 0.3;
    a.field_len = 10.0;
    a.avg_field_len = 10.0;
    LeafContextItem b = a;
    b.idf = 2.5;
    LeafContextItem c = a;
    c.idf = 1.0;
    CandidateContext ctx;
    ctx.num_query_leaves = 3;
    ctx.leaves = {a, b, c};
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kMinIdfMatched)], 0.3f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kMaxIdfMatched)], 2.5f);
}

TEST(FeaturesTest, IdfExtremesIgnoreUnmatchedLeaves) {
    LeafContextItem hit;
    hit.matched = true;
    hit.tf = 1;
    hit.idf = 1.5;
    hit.field_len = 10.0;
    hit.avg_field_len = 10.0;
    LeafContextItem miss;
    miss.matched = false;
    miss.idf = 99.0;  // would dominate min/max if it leaked in
    CandidateContext ctx;
    ctx.num_query_leaves = 2;
    ctx.leaves = {hit, miss};
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kMinIdfMatched)], 1.5f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kMaxIdfMatched)], 1.5f);
}

TEST(FeaturesTest, FirstPosMinNormGatesOnPositionDecay) {
    // Field without position_decay must not contribute, even if first_pos is 0.
    LeafContextItem no_decay;
    no_decay.matched = true;
    no_decay.tf = 1;
    no_decay.idf = 1.0;
    no_decay.field_len = 100.0;
    no_decay.avg_field_len = 100.0;
    no_decay.position_decay = 0.0f;
    no_decay.first_pos = 0;
    CandidateContext ctx_no;
    ctx_no.num_query_leaves = 1;
    ctx_no.leaves = {no_decay};
    const auto fv_no = ExtractFeatures(ctx_no, Bm25Params{});
    EXPECT_FLOAT_EQ(fv_no[Idx(Feature::kFirstPosMinNorm)], 0.0f);

    // Field with position_decay > 0 reports the normalized first position,
    // and the min is taken across multiple opted-in leaves.
    LeafContextItem early = no_decay;
    early.position_decay = 1.0f;
    early.first_pos = 5;
    LeafContextItem late = early;
    late.first_pos = 80;
    CandidateContext ctx_yes;
    ctx_yes.num_query_leaves = 2;
    ctx_yes.leaves = {late, early};
    const auto fv_yes = ExtractFeatures(ctx_yes, Bm25Params{});
    EXPECT_NEAR(fv_yes[Idx(Feature::kFirstPosMinNorm)], 5.0f / 100.0f, 1e-6);
}

TEST(FeaturesTest, ReservedDocLengthSlotsAreZero) {
    LeafContextItem hit;
    hit.matched = true;
    hit.tf = 1;
    hit.idf = 1.0;
    hit.field_len = 5.0;
    hit.avg_field_len = 5.0;
    CandidateContext ctx;
    ctx.num_query_leaves = 1;
    ctx.leaves = {hit};
    const auto fv = ExtractFeatures(ctx, Bm25Params{});
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kDocLengthAvg)], 0.0f);
    EXPECT_FLOAT_EQ(fv[Idx(Feature::kDocLengthMin)], 0.0f);
}

TEST(FeaturesTest, FeatureNamesAreUniqueAndPopulated) {
    std::array<std::string, kFeatureCount> names;
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        names[i] = FeatureName(static_cast<Feature>(i));
        EXPECT_FALSE(names[i].empty());
    }
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        for (std::size_t j = i + 1; j < kFeatureCount; ++j) {
            EXPECT_NE(names[i], names[j]);
        }
    }
}

}  // namespace
