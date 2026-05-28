#pragma once

#include <string>
#include <string_view>

namespace spp {

enum class StatusCode : int {
    kOk = 0,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kOutOfRange,
    kFailedPrecondition,
    kCorruption,
    kIoError,
    kUnimplemented,
    kInternal,
    kAborted,
    kUnavailable,
};

const char* StatusCodeName(StatusCode code) noexcept;

class [[nodiscard]] Status {
 public:
    Status() noexcept : code_(StatusCode::kOk) {}
    Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

    static Status Ok() noexcept {
        return Status{};
    }
    static Status InvalidArgument(std::string m) {
        return {StatusCode::kInvalidArgument, std::move(m)};
    }
    static Status NotFound(std::string m) {
        return {StatusCode::kNotFound, std::move(m)};
    }
    static Status AlreadyExists(std::string m) {
        return {StatusCode::kAlreadyExists, std::move(m)};
    }
    static Status OutOfRange(std::string m) {
        return {StatusCode::kOutOfRange, std::move(m)};
    }
    static Status FailedPrecondition(std::string m) {
        return {StatusCode::kFailedPrecondition, std::move(m)};
    }
    static Status Corruption(std::string m) {
        return {StatusCode::kCorruption, std::move(m)};
    }
    static Status IoError(std::string m) {
        return {StatusCode::kIoError, std::move(m)};
    }
    static Status Unimplemented(std::string m) {
        return {StatusCode::kUnimplemented, std::move(m)};
    }
    static Status Internal(std::string m) {
        return {StatusCode::kInternal, std::move(m)};
    }
    static Status Aborted(std::string m) {
        return {StatusCode::kAborted, std::move(m)};
    }
    static Status Unavailable(std::string m) {
        return {StatusCode::kUnavailable, std::move(m)};
    }

    bool ok() const noexcept {
        return code_ == StatusCode::kOk;
    }
    StatusCode code() const noexcept {
        return code_;
    }
    const std::string& message() const noexcept {
        return message_;
    }

    std::string ToString() const;

 private:
    StatusCode code_;
    std::string message_;
};

}  // namespace spp
