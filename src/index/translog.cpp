#include "spp/index/translog.h"

#include "spp/store/file_io.h"
#include "spp/store/platform.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace spp::index {

namespace {

void PackU32LE(std::uint32_t v, char out[4]) {
    out[0] = static_cast<char>(v & 0xFF);
    out[1] = static_cast<char>((v >> 8) & 0xFF);
    out[2] = static_cast<char>((v >> 16) & 0xFF);
    out[3] = static_cast<char>((v >> 24) & 0xFF);
}

std::uint32_t UnpackU32LE(const char* p) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

}  // namespace

Expected<std::unique_ptr<Translog>> Translog::Open(const std::filesystem::path& dir,
                                                   std::string_view filename) {
    auto tl = std::unique_ptr<Translog>(new Translog());
    tl->path_ = dir / std::string{filename};
    SPP_RETURN_IF_ERROR(tl->OpenAppend());
    return tl;
}

Status Translog::OpenAppend() {
    // Open for append without truncating. Existing data is preserved for Replay().
    auto sink = store::FileSink::Create(path_, /*truncate=*/false);
    if (!sink.ok())
        return sink.status();
    sink_ = std::move(*sink);
    bytes_ = sink_->bytes_written();
    return Status::Ok();
}

Status Translog::Append(std::string_view payload) {
    if (sink_ == nullptr)
        return Status::FailedPrecondition("translog not open");
    char hdr[4];
    PackU32LE(static_cast<std::uint32_t>(payload.size()), hdr);
    SPP_RETURN_IF_ERROR(sink_->Append(std::string_view{hdr, 4}));
    SPP_RETURN_IF_ERROR(sink_->Append(payload));
    SPP_RETURN_IF_ERROR(sink_->Sync());
    bytes_ += 4 + payload.size();
    return Status::Ok();
}

Expected<std::vector<std::string>> Translog::Replay() const {
    std::vector<std::string> out;
    auto src = store::FileSource::LoadAll(path_);
    if (!src.ok()) {
        // If the file doesn't exist yet, treat as empty.
        if (src.status().code() == StatusCode::kIoError)
            return out;
        return src.status();
    }
    const std::string_view sv = src->view();
    const char* p = sv.data();
    const char* end = p + sv.size();
    while (p < end) {
        if (end - p < 4) {
            // Partial header — last record was torn. Stop here.
            break;
        }
        const std::uint32_t len = UnpackU32LE(p);
        p += 4;
        if (static_cast<std::uint64_t>(end - p) < len)
            break;  // torn payload
        out.emplace_back(p, len);
        p += len;
    }
    return out;
}

Status Translog::Truncate() {
    // Close, recreate empty, fsync.
    if (sink_ != nullptr) {
        SPP_RETURN_IF_ERROR(sink_->Close());
        sink_.reset();
    }
    auto sink = store::FileSink::Create(path_, /*truncate=*/true);
    if (!sink.ok())
        return sink.status();
    sink_ = std::move(*sink);
    bytes_ = 0;
    return sink_->Sync();
}

Status Translog::Close() {
    if (sink_ == nullptr)
        return Status::Ok();
    Status st = sink_->Close();
    sink_.reset();
    return st;
}

}  // namespace spp::index
