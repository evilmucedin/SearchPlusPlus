#include "spp/base/status.h"

#include <gtest/gtest.h>

TEST(StatusTest, OkIsOk) {
    spp::Status s;
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code(), spp::StatusCode::kOk);
}

TEST(StatusTest, NamedFactoriesCarryMessage) {
    auto s = spp::Status::InvalidArgument("bad foo");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), spp::StatusCode::kInvalidArgument);
    EXPECT_EQ(s.message(), "bad foo");
}

TEST(StatusTest, ToStringIncludesCodeAndMessage) {
    auto s = spp::Status::NotFound("missing");
    const auto str = s.ToString();
    EXPECT_NE(str.find("missing"), std::string::npos);
}
