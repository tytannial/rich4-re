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
} // namespace

void logMessage(LogLevel level, const char* fmt, ...) {
    std::fprintf(stderr, "[rich4][%s] ", levelTag(level));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

} // namespace rich4
