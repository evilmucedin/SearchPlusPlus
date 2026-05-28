#include "spp/store/directory.h"
#include "spp/store/file_io.h"
#include "spp/store/platform.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace spp::store {

namespace {

class FilesystemDirectory final : public Directory {
 public:
    explicit FilesystemDirectory(std::filesystem::path path) : path_(std::move(path)) {}

    const std::filesystem::path& path() const override {
        return path_;
    }

    bool Exists(std::string_view name) const override {
        std::error_code ec;
        return std::filesystem::exists(path_ / std::string{name}, ec);
    }

    Expected<std::vector<std::string>> ListNames() const override {
        std::error_code ec;
        std::vector<std::string> out;
        for (auto& entry : std::filesystem::directory_iterator(path_, ec)) {
            if (ec)
                return Status::IoError("directory_iterator: " + ec.message());
            out.push_back(entry.path().filename().string());
        }
        return out;
    }

    Expected<std::unique_ptr<FileSink>> CreateFile(std::string_view name) override {
        return FileSink::Create(path_ / std::string{name});
    }

    Expected<FileSource> ReadFile(std::string_view name) const override {
        return FileSource::LoadAll(path_ / std::string{name});
    }

    Status AtomicRenameWithin(std::string_view src_name, std::string_view dst_name) override {
        return platform::AtomicRename(path_ / std::string{src_name}, path_ / std::string{dst_name});
    }

    Status DeleteFile(std::string_view name) override {
        std::error_code ec;
        std::filesystem::remove(path_ / std::string{name}, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) {
            return Status::IoError("delete: " + ec.message());
        }
        return Status::Ok();
    }

    Status Sync() override {
        // Sync the directory entry itself (POSIX); no-op on Windows.
        return platform::FsyncDirectoryOf(path_ / "_sync_marker_");
    }

 private:
    std::filesystem::path path_;
};

}  // namespace

Expected<std::unique_ptr<Directory>> OpenFilesystemDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
        return Status::IoError("create_directories: " + ec.message());
    return std::unique_ptr<Directory>(new FilesystemDirectory(path));
}

}  // namespace spp::store
