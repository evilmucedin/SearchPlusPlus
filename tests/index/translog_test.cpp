#include "spp/index/translog.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_translog_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

TEST(TranslogTest, AppendAndReplay) {
    auto root = TmpDir();
    auto log = spp::index::Translog::Open(root).value();
    ASSERT_TRUE(log->Append("doc-1").ok());
    ASSERT_TRUE(log->Append("doc-two-longer").ok());
    auto recs = log->Replay().value();
    ASSERT_EQ(recs.size(), 2u);
    EXPECT_EQ(recs[0], "doc-1");
    EXPECT_EQ(recs[1], "doc-two-longer");
    std::filesystem::remove_all(root);
}

TEST(TranslogTest, TruncateClearsLog) {
    auto root = TmpDir();
    auto log = spp::index::Translog::Open(root).value();
    ASSERT_TRUE(log->Append("x").ok());
    ASSERT_TRUE(log->Truncate().ok());
    auto recs = log->Replay().value();
    EXPECT_TRUE(recs.empty());
    std::filesystem::remove_all(root);
}

TEST(TranslogTest, TornLastRecordTolerated) {
    auto root = TmpDir();
    {
        auto log = spp::index::Translog::Open(root).value();
        ASSERT_TRUE(log->Append("good").ok());
        ASSERT_TRUE(log->Close().ok());
    }
    // Corrupt the file: append a partial length prefix that points past EOF.
    const auto path = root / "translog.bin";
    {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        const char garbage[] = {static_cast<char>(0xff), static_cast<char>(0xff), 0x00};
        f.write(garbage, sizeof(garbage));
    }
    auto log2 = spp::index::Translog::Open(root).value();
    auto recs = log2->Replay().value();
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0], "good");
    std::filesystem::remove_all(root);
}
