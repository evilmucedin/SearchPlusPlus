#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace spp {

enum class LogLevel : std::uint8_t {
    kTrace = 0,
    kDebug = 1,
    kInfo = 2,
    kWarn = 3,
    kError = 4,
    kFatal = 5,
};

using LogSink = void (*)(LogLevel, std::string_view file, int line, std::string_view msg);

// Replace the default stderr sink. Set to nullptr to silence.
void SetLogSink(LogSink sink);

// Below this level, Log() is a no-op. Default: kInfo.
void SetLogLevel(LogLevel level);
LogLevel CurrentLogLevel();

// Logs a single message. Cheap if level is below the threshold.
void Log(LogLevel level, const char* file, int line, std::string_view msg);
void Logf(LogLevel level, const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

[[noreturn]] void CheckFailed(const char* file,
                              int line,
                              std::string_view expr,
                              std::string_view msg);

}  // namespace spp

#define SPP_LOGF(lvl, ...) ::spp::Logf((lvl), __FILE__, __LINE__, __VA_ARGS__)

#define SPP_LOG_TRACE(...) SPP_LOGF(::spp::LogLevel::kTrace, __VA_ARGS__)
#define SPP_LOG_DEBUG(...) SPP_LOGF(::spp::LogLevel::kDebug, __VA_ARGS__)
#define SPP_LOG_INFO(...) SPP_LOGF(::spp::LogLevel::kInfo, __VA_ARGS__)
#define SPP_LOG_WARN(...) SPP_LOGF(::spp::LogLevel::kWarn, __VA_ARGS__)
#define SPP_LOG_ERROR(...) SPP_LOGF(::spp::LogLevel::kError, __VA_ARGS__)

#define SPP_CHECK(cond)                                        \
    do {                                                       \
        if (!(cond))                                           \
            ::spp::CheckFailed(__FILE__, __LINE__, #cond, {}); \
    } while (0)

#define SPP_CHECK_MSG(cond, msg)                                  \
    do {                                                          \
        if (!(cond))                                              \
            ::spp::CheckFailed(__FILE__, __LINE__, #cond, (msg)); \
    } while (0)
