#include "spp/base/expected.h"

#include "spp/base/status.h"

#include <string>

#include <gtest/gtest.h>

namespace {

spp::Expected<int> Halve(int x) {
    if ((x & 1) != 0)
        return spp::Status::InvalidArgument("odd");
    return x / 2;
}

spp::Expected<int> ChainOk() {
    SPP_ASSIGN_OR_RETURN(auto a, Halve(20));
    SPP_ASSIGN_OR_RETURN(auto b, Halve(a));
    return a + b;
}

spp::Expected<int> ChainErr() {
    SPP_ASSIGN_OR_RETURN(auto a, Halve(10));
    SPP_ASSIGN_OR_RETURN(auto b, Halve(7));
    return a + b;
}

spp::Status MaybeFail(bool fail) {
    SPP_RETURN_IF_ERROR(fail ? spp::Status::Aborted("boom") : spp::Status::Ok());
    return spp::Status::Ok();
}

}  // namespace

TEST(ExpectedTest, HoldsValue) {
    auto x = Halve(8);
    ASSERT_TRUE(x.ok());
    EXPECT_EQ(*x, 4);
}

TEST(ExpectedTest, HoldsStatus) {
    auto x = Halve(3);
    EXPECT_FALSE(x.ok());
    EXPECT_EQ(x.status().code(), spp::StatusCode::kInvalidArgument);
}

TEST(ExpectedTest, AssignOrReturnHappyPath) {
    auto x = ChainOk();
    ASSERT_TRUE(x.ok());
    EXPECT_EQ(*x, 10 + 5);
}

TEST(ExpectedTest, AssignOrReturnPropagates) {
    auto x = ChainErr();
    EXPECT_FALSE(x.ok());
    EXPECT_EQ(x.status().code(), spp::StatusCode::kInvalidArgument);
}

TEST(ExpectedTest, ReturnIfErrorOk) {
    EXPECT_TRUE(MaybeFail(false).ok());
}

TEST(ExpectedTest, ReturnIfErrorPropagates) {
    auto st = MaybeFail(true);
    EXPECT_FALSE(st.ok());
    EXPECT_EQ(st.code(), spp::StatusCode::kAborted);
}
