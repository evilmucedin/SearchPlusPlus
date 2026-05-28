#pragma once

#include "spp/base/status.h"

#include <filesystem>
#include <string>

namespace spp::store::platform {

// Atomically replace `to` with `from`. Implementation:
//   POSIX: rename(2)
//   Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING)
Status AtomicRename(const std::filesystem::path& from, const std::filesystem::path& to);

// Flush a file's buffers to durable storage.
//   POSIX: fsync(fd)
//   Windows: FlushFileBuffers(handle)
// The caller passes a platform-specific handle obtained from FileSink::native_handle().
Status FsyncHandle(std::intptr_t native_handle);

// fsync the directory entry containing `path`. POSIX-specific; on Windows it's a no-op
// because NTFS makes the directory durable as a side-effect of file durability.
Status FsyncDirectoryOf(const std::filesystem::path& path);

}  // namespace spp::store::platform
