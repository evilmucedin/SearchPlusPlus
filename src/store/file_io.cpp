#include "spp/store/file_io.h"

#include "spp/store/platform.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace spp::store {

namespace {

Status IoErr(const char* what, const std::filesystem::path& p) {
    std::string msg = what;
    msg += " '";
    msg += p.string();
    msg += "': ";
    msg += std::strerror(errno);
    return Status::IoError(std::move(msg));
}

std::intptr_t NativeHandle(std::FILE* fp) {
    if (fp == nullptr)
        return -1;
#if defined(_WIN32)
    int fd = _fileno(fp);
    if (fd < 0)
        return -1;
    return static_cast<std::intptr_t>(_get_osfhandle(fd));
#else
    return ::fileno(fp);
#endif
}

}  // namespace

FileSink::FileSink(std::filesystem::path path, std::FILE* fp) : path_(std::move(path)), fp_(fp) {}

FileSink::~FileSink() {
    if (!closed_ && fp_ != nullptr) {
        (void)Close();
    }
}

Expected<std::unique_ptr<FileSink>> FileSink::Create(const std::filesystem::path& path,
                                                     bool truncate) {
    const char* mode = truncate ? "wb" : "ab";
    std::FILE* fp = std::fopen(path.string().c_str(), mode);
    if (fp == nullptr)
        return IoErr("open for write", path);
    return std::unique_ptr<FileSink>(new FileSink(path, fp));
}

Status FileSink::Append(std::string_view bytes) {
    if (closed_)
        return Status::FailedPrecondition("FileSink already closed");
    if (bytes.empty())
        return Status::Ok();
    const std::size_t n = std::fwrite(bytes.data(), 1, bytes.size(), fp_);
    if (n != bytes.size())
        return IoErr("write", path_);
    written_ += n;
    return Status::Ok();
}

Status FileSink::Flush() {
    if (closed_)
        return Status::FailedPrecondition("FileSink already closed");
    if (std::fflush(fp_) != 0)
        return IoErr("fflush", path_);
    return Status::Ok();
}

Status FileSink::Sync() {
    SPP_RETURN_IF_ERROR(Flush());
    return platform::FsyncHandle(NativeHandle(fp_));
}

Status FileSink::Close() {
    if (closed_)
        return Status::Ok();
    Status st = Sync();
    closed_ = true;
    if (std::fclose(fp_) != 0) {
        if (st.ok())
            st = IoErr("fclose", path_);
    }
    fp_ = nullptr;
    return st;
}

Expected<FileSource> FileSource::LoadAll(const std::filesystem::path& path) {
    std::FILE* fp = std::fopen(path.string().c_str(), "rb");
    if (fp == nullptr)
        return IoErr("open for read", path);
    FileSource fs;
    fs.path_ = path;
    constexpr std::size_t kChunk = 64 * 1024;
    std::vector<char> tmp(kChunk);
    while (true) {
        std::size_t n = std::fread(tmp.data(), 1, tmp.size(), fp);
        if (n > 0)
            fs.buf_.insert(fs.buf_.end(), tmp.data(), tmp.data() + n);
        if (n < tmp.size()) {
            if (std::feof(fp))
                break;
            if (std::ferror(fp)) {
                std::fclose(fp);
                return IoErr("read", path);
            }
        }
    }
    std::fclose(fp);
    return fs;
}

}  // namespace spp::store
