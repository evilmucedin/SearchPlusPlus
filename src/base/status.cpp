#include "spp/base/status.h"

namespace spp {

const char* StatusCodeName(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::kOk:
            return "Ok";
        case StatusCode::kInvalidArgument:
            return "InvalidArgument";
        case StatusCode::kNotFound:
            return "NotFound";
        case StatusCode::kAlreadyExists:
            return "AlreadyExists";
        case StatusCode::kOutOfRange:
            return "OutOfRange";
        case StatusCode::kFailedPrecondition:
            return "FailedPrecondition";
        case StatusCode::kCorruption:
            return "Corruption";
        case StatusCode::kIoError:
            return "IoError";
        case StatusCode::kUnimplemented:
            return "Unimplemented";
        case StatusCode::kInternal:
            return "Internal";
        case StatusCode::kAborted:
            return "Aborted";
        case StatusCode::kUnavailable:
            return "Unavailable";
    }
    return "Unknown";
}

std::string Status::ToString() const {
    if (ok())
        return "Ok";
    std::string out = StatusCodeName(code_);
    out += ": ";
    out += message_;
    return out;
}

}  // namespace spp
