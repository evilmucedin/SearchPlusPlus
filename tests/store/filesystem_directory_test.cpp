#include "spp/store/directory.h"

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_dir_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

}  // namespace

TEST(DirectoryTest, CreateListDelete) {
    auto root = TmpDir();
    auto dir_e = spp::store::OpenFilesystemDirectory(root);
    ASSERT_TRUE(dir_e.ok());
    auto& dir = *dir_e;
    {
        auto sink = dir->CreateFile("a.txt").value();
        ASSERT_TRUE(sink->Append("hi").ok());
        ASSERT_TRUE(sink->Close().ok());
    }
    auto names_e = dir->ListNames();
    ASSERT_TRUE(names_e.ok());
    EXPECT_NE(std::find(names_e->begin(), names_e->end(), "a.txt"), names_e->end());

    EXPECT_TRUE(dir->Exists("a.txt"));
    EXPECT_FALSE(dir->Exists("nope.txt"));

    ASSERT_TRUE(dir->DeleteFile("a.txt").ok());
    EXPECT_FALSE(dir->Exists("a.txt"));
    std::filesystem::remove_all(root);
}

TEST(DirectoryTest, AtomicRenameWithin) {
    auto root = TmpDir();
    auto dir = spp::store::OpenFilesystemDirectory(root).value();
    {
        auto sink = dir->CreateFile("tmp").value();
        ASSERT_TRUE(sink->Append("payload").ok());
        ASSERT_TRUE(sink->Close().ok());
    }
    ASSERT_TRUE(dir->AtomicRenameWithin("tmp", "final").ok());
    EXPECT_FALSE(dir->Exists("tmp"));
    EXPECT_TRUE(dir->Exists("final"));
    std::filesystem::remove_all(root);
}
