#pragma once

namespace rich4 {

enum class LogLevel { Debug, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...);

} // namespace rich4

#define RICH4_LOGD(...) ::rich4::logMessage(::rich4::LogLevel::Debug, __VA_ARGS__)
#define RICH4_LOGI(...) ::rich4::logMessage(::rich4::LogLevel::Info, __VA_ARGS__)
#define RICH4_LOGW(...) ::rich4::logMessage(::rich4::LogLevel::Warn, __VA_ARGS__)
#define RICH4_LOGE(...) ::rich4::logMessage(::rich4::LogLevel::Error, __VA_ARGS__)
