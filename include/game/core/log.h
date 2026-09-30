#pragma once

#include <string>

namespace rich4 {

enum class LogLevel { Debug, Info, Warn, Error };

// [PORT] GCC/Clang 下启用 printf 格式串编译期检查（MSVC 无此 attribute）
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void logMessage(LogLevel level, const char* fmt, ...);

// [NEW] 测试断言支持（无原版对应）：内存日志环（最近 4096 条，含级别前缀）
// 供脚本 `assert log <substr>` 使用；headless/有头均记录，开销为一次 vsnprintf+push。
bool logContains(const char* substr);

// [NEW] 将一行文本镜像进内存日志环（不写文件/stderr，不计 ERROR 数）——供
//   trace::logf 的结构化事件（xfer/debt/eliminate/...）同时可被 `wait log`/`assert log`
//   匹配；trace 环与 log 环语义打通。
void logMirror(const char* line);
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
