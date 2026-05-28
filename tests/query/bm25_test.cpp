#include "spp/query/bm25_scorer.h"

#include <cmath>

#include <gtest/gtest.h>

TEST(Bm25Test, IdfMonotonicallyDecreasesWithDf) {
    const auto a = spp::query::Bm25Idf(1000, 10);
    const auto b = spp::query::Bm25Idf(1000, 100);
    EXPECT_GT(a, b);
    EXPECT_GT(a, 0.0);
    EXPECT_GT(b, 0.0);
}

TEST(Bm25Test, ScoreIncreasesWithTf) {
    const double avg_dl = 20.0;
    const double idf = spp::query::Bm25Idf(1000, 10);
    const auto s1 = spp::query::Bm25Score(idf, 1, 20.0, avg_dl);
    const auto s2 = spp::query::Bm25Score(idf, 5, 20.0, avg_dl);
    EXPECT_GT(s2, s1);
}

TEST(Bm25Test, ScoreDecreasesWithLongerDocAtSameTf) {
    const double avg_dl = 20.0;
    const double idf = spp::query::Bm25Idf(1000, 10);
    const auto s_short = spp::query::Bm25Score(idf, 2, 10.0, avg_dl);
    const auto s_long = spp::query::Bm25Score(idf, 2, 200.0, avg_dl);
    EXPECT_GT(s_short, s_long);
}
