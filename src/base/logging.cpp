#include "spp/base/logging.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace spp {

namespace {

const char* LevelTag(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
    }
    return "?";
}

void DefaultSink(LogLevel level, std::string_view file, int line, std::string_view msg) {
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    const auto now = std::chrono::system_clock::now();
    const auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::fprintf(stderr,
                 "%lld %s %.*s:%d %.*s\n",
                 static_cast<long long>(now_ms),
                 LevelTag(level),
                 static_cast<int>(file.size()),
                 file.data(),
                 line,
                 static_cast<int>(msg.size()),
                 msg.data());
}

std::atomic<LogSink> g_sink{&DefaultSink};
std::atomic<LogLevel> g_level{LogLevel::kInfo};

}  // namespace

void SetLogSink(LogSink sink) {
    g_sink.store(sink, std::memory_order_release);
}
void SetLogLevel(LogLevel level) {
    g_level.store(level, std::memory_order_release);
}
LogLevel CurrentLogLevel() {
    return g_level.load(std::memory_order_acquire);
}

void Log(LogLevel level, const char* file, int line, std::string_view msg) {
    if (level < CurrentLogLevel())
        return;
    LogSink sink = g_sink.load(std::memory_order_acquire);
    if (sink == nullptr)
        return;
    sink(level, file ? file : "", line, msg);
}

void Logf(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < CurrentLogLevel())
        return;
    LogSink sink = g_sink.load(std::memory_order_acquire);
    if (sink == nullptr)
        return;

    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    const int needed = std::vsnprintf(nullptr, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed < 0) {
        va_end(ap);
        sink(level, file ? file : "", line, "<log format error>");
        return;
    }
    std::vector<char> buf(static_cast<std::size_t>(needed) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);
    sink(level,
         file ? file : "",
         line,
         std::string_view(buf.data(), static_cast<std::size_t>(needed)));
}

void CheckFailed(const char* file, int line, std::string_view expr, std::string_view msg) {
    LogSink sink = g_sink.load(std::memory_order_acquire);
    std::string composed = "CHECK failed: ";
    composed.append(expr);
    if (!msg.empty()) {
        composed.append(" — ");
        composed.append(msg);
    }
    if (sink != nullptr)
        sink(LogLevel::kFatal, file ? file : "", line, composed);
    std::abort();
}

}  // namespace spp
