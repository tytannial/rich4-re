#include "game/core/log.h"

#include <cstdarg>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

namespace rich4 {

namespace {
// [NEW] 内存日志环（测试断言用，无原版对应）
constexpr size_t kLogRingMax = 4096;
std::deque<std::string> g_logRing;
std::mutex g_logRingMutex;
std::atomic<int> g_errorCount{0}; // [NEW] ERROR 级计数（assert state errors == 0 长跑不变量）
} // namespace

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

// [NEW] 墙钟时间戳 "YYYY-MM-DD HH:MM:SS.mmm"（本地时间，毫秒精度）。
//   仅用于人读通道（stderr / rich4.log）；内存日志环保持无时间戳，
//   以免 `assert log`/`wait log` 样本引入时钟非确定性。
//   localtime_s/localtime_r 线程安全（logMessage 可能在持锁外被多线程调用）。
void formatStamp(char* out, size_t cap) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    if (ms < 0) {
        ms += 1000;
    }
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif
    std::snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tmBuf.tm_year + 1900,
                  tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec,
                  static_cast<int>(ms));
}

// [PORT] 日志同时落 rich4.log（调试用；>4MB 截断重建；每次运行写一行会话分隔
//   banner，便于 append 模式下区分多次启动）。依据: 实机问题需回传日志
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
        std::FILE* h = std::fopen(path, "a");
        if (h) {
            char stamp[40];
            formatStamp(stamp, sizeof(stamp));
            std::fprintf(h, "==== rich4 log session @ %s ====\n", stamp);
            std::fflush(h);
        }
        return h;
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
    char stamp[40];
    formatStamp(stamp, sizeof(stamp));
    std::fprintf(stderr, "[rich4][%s][%s] %s\n", stamp, levelTag(level), buf);
    if (std::FILE* f = logFile()) {
        std::fprintf(f, "[rich4][%s][%s] %s\n", stamp, levelTag(level), buf);
        std::fflush(f);
    }
    // [NEW] 同步写入内存环（assert log 检索用；不带时间戳，保持断言确定性）
    {
        std::lock_guard<std::mutex> lk(g_logRingMutex);
        std::string line = "[";
        line += levelTag(level);
        line += "] ";
        line += buf;
        if (g_logRing.size() >= kLogRingMax) {
            g_logRing.pop_front();
        }
        g_logRing.emplace_back(std::move(line));
        if (level == LogLevel::Error && std::strstr(buf, "ASSERT FAIL") == nullptr &&
            (std::strncmp(buf, "script ", 7) != 0 && std::strncmp(buf, "clicksel skip", 13) != 0)) {
            // [NEW] errors 计数只统计**产品** ERROR；断言失败/脚本装载错误本身不计入
            ++g_errorCount;
        }
    }
}

int logErrorCount() { return g_errorCount.load(); }

void logMirror(const char* line) {
    std::lock_guard<std::mutex> lk(g_logRingMutex);
    g_logRing.emplace_back(line);
    if (g_logRing.size() > kLogRingMax) {
        g_logRing.pop_front();
    }
}

bool logContains(const char* substr) {
    std::lock_guard<std::mutex> lk(g_logRingMutex);
    for (const std::string& s : g_logRing) {
        if (s.find(substr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int logCount(const char* substr) {
    std::lock_guard<std::mutex> lk(g_logRingMutex);
    int n = 0;
    for (const std::string& s : g_logRing) {
        if (s.find(substr) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

std::string logLastMatch(const char* substr) {
    std::lock_guard<std::mutex> lk(g_logRingMutex);
    for (auto it = g_logRing.rbegin(); it != g_logRing.rend(); ++it) {
        if (it->find(substr) != std::string::npos) {
            return *it;
        }
    }
    return {};
}

void logClear() {
    std::lock_guard<std::mutex> lk(g_logRingMutex);
    g_logRing.clear();
}

} // namespace rich4
