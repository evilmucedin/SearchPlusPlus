#include "spp/store/platform.h"

#include <cerrno>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace spp::store::platform {

namespace {

#if !defined(_WIN32)
Status PosixErr(const char* what) {
    std::string msg = what;
    msg += ": ";
    msg += std::strerror(errno);
    return Status::IoError(std::move(msg));
}
#endif

}  // namespace

Status AtomicRename(const std::filesystem::path& from, const std::filesystem::path& to) {
#if defined(_WIN32)
    if (MoveFileExW(from.wstring().c_str(),
                    to.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        return Status::IoError("MoveFileEx failed");
    }
    return Status::Ok();
#else
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return PosixErr("rename");
    }
    return Status::Ok();
#endif
}

Status FsyncHandle(std::intptr_t native_handle) {
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(native_handle);
    if (h == INVALID_HANDLE_VALUE)
        return Status::InvalidArgument("invalid handle");
    if (FlushFileBuffers(h) == 0)
        return Status::IoError("FlushFileBuffers failed");
    return Status::Ok();
#else
    int fd = static_cast<int>(native_handle);
    if (fd < 0)
        return Status::InvalidArgument("invalid fd");
    // On macOS fsync() does not flush the disk cache; F_FULLFSYNC does. For the v0.1
    // durability story we accept fsync()'s guarantees — the translog is robust against
    // process crashes (which is the level we promise); whole-host power loss can still
    // lose the last few records. This is documented in DESIGN.md.
    if (::fsync(fd) != 0)
        return PosixErr("fsync");
    return Status::Ok();
#endif
}

Status FsyncDirectoryOf(const std::filesystem::path& path) {
#if defined(_WIN32)
    (void)path;
    return Status::Ok();
#else
    std::filesystem::path dir = path.parent_path();
    if (dir.empty())
        dir = ".";
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return PosixErr("open(dir)");
    int rc = ::fsync(fd);
    int save_errno = errno;
    ::close(fd);
    if (rc != 0) {
        errno = save_errno;
        return PosixErr("fsync(dir)");
    }
    return Status::Ok();
#endif
}

}  // namespace spp::store::platform
