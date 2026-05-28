#include "spp/store/varbyte.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

TEST(VarByteTest, RoundTrip32Boundaries) {
    const std::vector<std::uint32_t> values = {
        0u, 1u, 127u, 128u, 16383u, 16384u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (auto v : values) {
        std::string buf;
        spp::store::EncodeVarUint32(v, buf);
        const char* p = buf.data();
        const char* end = buf.data() + buf.size();
        auto d = spp::store::DecodeVarUint32(p, end);
        ASSERT_TRUE(d.ok());
        EXPECT_EQ(*d, v) << "v=" << v;
        EXPECT_EQ(p, end);
    }
}

TEST(VarByteTest, RoundTrip64Random) {
    std::mt19937_64 rng(42);
    std::string buf;
    std::vector<std::uint64_t> values;
    for (int i = 0; i < 1000; ++i) {
        std::uint64_t v = rng();
        v >>= (i % 64);  // varying widths
        values.push_back(v);
        spp::store::EncodeVarUint64(v, buf);
    }
    const char* p = buf.data();
    const char* end = buf.data() + buf.size();
    for (auto v : values) {
        auto d = spp::store::DecodeVarUint64(p, end);
        ASSERT_TRUE(d.ok());
        EXPECT_EQ(*d, v);
    }
    EXPECT_EQ(p, end);
}

TEST(VarByteTest, DecodeTruncatedFails) {
    std::string buf;
    spp::store::EncodeVarUint32(0xFFFFFFFFu, buf);
    // Cut to one byte; should still be a continuation.
    const char* p = buf.data();
    const char* end = buf.data() + 1;
    auto d = spp::store::DecodeVarUint32(p, end);
    EXPECT_FALSE(d.ok());
}

TEST(VarByteTest, EncodedLengthMatchesHelper) {
    for (std::uint32_t v : {0u, 1u, 127u, 128u, 16384u, 0x10000u, 0xFFFFFFFFu}) {
        std::string buf;
        spp::store::EncodeVarUint32(v, buf);
        EXPECT_EQ(buf.size(), spp::store::VarUint32Length(v));
    }
}
