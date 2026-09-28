#pragma once

#include <string>

namespace rich4 {

enum class LogLevel { Debug, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...);

// [NEW] 测试断言支持（无原版对应）：内存日志环（最近 4096 条，含级别前缀）
// 供脚本 `assert log <substr>` 使用；headless/有头均记录，开销为一次 vsnprintf+push。
bool logContains(const char* substr);
int logCount(const char* substr);
std::string logLastMatch(const char* substr);
void logClear();
// [NEW] ERROR 级日志计数（长跑不变量：assert state errors == 0）
int logErrorCount();

} // namespace rich4

#define RICH4_LOGD(...) ::rich4::logMessage(::rich4::LogLevel::Debug, __VA_ARGS__)
#define RICH4_LOGI(...) ::rich4::logMessage(::rich4::LogLevel::Info, __VA_ARGS__)
#define RICH4_LOGW(...) ::rich4::logMessage(::rich4::LogLevel::Warn, __VA_ARGS__)
#define RICH4_LOGE(...) ::rich4::logMessage(::rich4::LogLevel::Error, __VA_ARGS__)
