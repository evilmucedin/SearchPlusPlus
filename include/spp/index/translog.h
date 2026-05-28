#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/store/file_io.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spp::index {

// Append-only durability log. Each record is:
//   [u32 LE length][payload bytes]
// Append() fsyncs before returning. Replay() walks records from the file head.
class Translog {
 public:
    static Expected<std::unique_ptr<Translog>> Open(const std::filesystem::path& dir,
                                                    std::string_view filename = "translog.bin");

    // Append a record. Fsyncs before returning.
    Status Append(std::string_view payload);

    // Read all records from disk (file is opened read-only for this operation).
    Expected<std::vector<std::string>> Replay() const;

    // Truncate the log to zero length, fsync. Use after a refresh has durably
    // sealed a segment containing the docs that were in the log.
    Status Truncate();

    Status Close();

    const std::filesystem::path& path() const noexcept {
        return path_;
    }
    std::uint64_t bytes_written() const noexcept {
        return bytes_;
    }

 private:
    Translog() = default;
    Status OpenAppend();

    std::filesystem::path path_;
    std::unique_ptr<store::FileSink> sink_;
    std::uint64_t bytes_ = 0;
};

}  // namespace spp::index
