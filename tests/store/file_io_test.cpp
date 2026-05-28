#include "spp/store/file_io.h"

#include <filesystem>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_file_io_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

TEST(FileIoTest, AppendCloseRead) {
    auto dir = TmpDir();
    const auto path = dir / "f.bin";
    {
        auto sink_e = spp::store::FileSink::Create(path);
        ASSERT_TRUE(sink_e.ok());
        auto& sink = *sink_e;
        ASSERT_TRUE(sink->Append("hello ").ok());
        ASSERT_TRUE(sink->Append("world").ok());
        ASSERT_TRUE(sink->Close().ok());
        EXPECT_EQ(sink->bytes_written(), 11u);
    }
    auto src = spp::store::FileSource::LoadAll(path);
    ASSERT_TRUE(src.ok());
    EXPECT_EQ(std::string(src->view()), "hello world");
    std::filesystem::remove_all(dir);
}

TEST(FileIoTest, TruncateWipesPriorContents) {
    auto dir = TmpDir();
    const auto path = dir / "trunc.bin";
    {
        auto s = spp::store::FileSink::Create(path).value();
        ASSERT_TRUE(s->Append("AAAA").ok());
        ASSERT_TRUE(s->Close().ok());
    }
    {
        auto s = spp::store::FileSink::Create(path, /*truncate=*/true).value();
        ASSERT_TRUE(s->Append("B").ok());
        ASSERT_TRUE(s->Close().ok());
    }
    auto src = spp::store::FileSource::LoadAll(path);
    ASSERT_TRUE(src.ok());
    EXPECT_EQ(std::string(src->view()), "B");
    std::filesystem::remove_all(dir);
}
