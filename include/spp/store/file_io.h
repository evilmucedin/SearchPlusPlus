#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spp::store {

// Buffered append-only file sink. Closing fsyncs by default.
class FileSink {
 public:
    static Expected<std::unique_ptr<FileSink>> Create(const std::filesystem::path& path,
                                                      bool truncate = true);
    ~FileSink();

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    Status Append(std::string_view bytes);
    Status Flush();  // pushes buffered bytes to the OS (no fsync)
    Status Sync();   // Flush + fsync
    Status Close();  // Flush + fsync + close. Idempotent.

    std::uint64_t bytes_written() const noexcept {
        return written_;
    }
    const std::filesystem::path& path() const noexcept {
        return path_;
    }

 private:
    FileSink(std::filesystem::path path, std::FILE* fp);

    std::filesystem::path path_;
    std::FILE* fp_ = nullptr;
    std::uint64_t written_ = 0;
    bool closed_ = false;
};

// Whole-file read into memory. For v0.1 we keep this simple — mmap can be added later.
class FileSource {
 public:
    static Expected<FileSource> LoadAll(const std::filesystem::path& path);

    std::string_view view() const noexcept {
        return std::string_view{buf_.data(), buf_.size()};
    }
    std::size_t size() const noexcept {
        return buf_.size();
    }
    const std::filesystem::path& path() const noexcept {
        return path_;
    }

 private:
    FileSource() = default;
    std::filesystem::path path_;
    std::vector<char> buf_;
};

}  // namespace spp::store
