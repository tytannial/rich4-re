#include "game/core/log.h"

#include <cstdarg>
#include <cstdio>

namespace rich4 {

namespace {
const char* levelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

// [PORT] 日志同时落 rich4.log（调试用；>4MB 截断重建）。依据: 实机问题需回传日志
std::FILE* logFile() {
    static std::FILE* f = [] {
        const char* path = "rich4.log";
        if (std::FILE* r = std::fopen(path, "rb")) {
            std::fseek(r, 0, SEEK_END);
            const long sz = std::ftell(r);
            std::fclose(r);
            if (sz > 4 * 1024 * 1024) {
                if (std::FILE* t = std::fopen(path, "w")) {
                    std::fclose(t);
                }
            }
        }
        return std::fopen(path, "a");
    }();
    return f;
}
} // namespace

void logMessage(LogLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[rich4][%s] %s\n", levelTag(level), buf);
    if (std::FILE* f = logFile()) {
        std::fprintf(f, "[rich4][%s] %s\n", levelTag(level), buf);
        std::fflush(f);
    }
}

} // namespace rich4
