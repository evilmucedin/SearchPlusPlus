#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/store/file_io.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace spp::store {

class Directory {
 public:
    virtual ~Directory() = default;

    virtual const std::filesystem::path& path() const = 0;

    // Returns true if `name` exists in this directory.
    virtual bool Exists(std::string_view name) const = 0;

    // Lists immediate children (file names only, no path).
    virtual Expected<std::vector<std::string>> ListNames() const = 0;

    // Opens a new file for writing. Truncates if it exists.
    virtual Expected<std::unique_ptr<FileSink>> CreateFile(std::string_view name) = 0;

    // Reads `name` entirely into memory.
    virtual Expected<FileSource> ReadFile(std::string_view name) const = 0;

    // Atomically replaces `dst_name` with `src_name`. Both must be in this directory.
    virtual Status AtomicRenameWithin(std::string_view src_name, std::string_view dst_name) = 0;

    // Deletes `name` if present; not-an-error if missing.
    virtual Status DeleteFile(std::string_view name) = 0;

    // fsync the directory entry so renames/creates are durable.
    virtual Status Sync() = 0;
};

// Create or open a directory on the local filesystem. `path` is created if missing.
Expected<std::unique_ptr<Directory>> OpenFilesystemDirectory(const std::filesystem::path& path);

}  // namespace spp::store
