#include "game/core/trace.h"
#include "game/core/log.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// [NEW] 演出/判定 Trace 实现（无原版对应；方案 docs/testing.md §2）
namespace rich4 {
namespace trace {

namespace {

constexpr size_t kMaxRecords = 8192;

bool g_enabled = false;
std::deque<std::string> g_records;
uint64_t g_seq = 0;
std::mutex g_mutex;

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace

void setEnabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

void logf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // [FIX] 结构化事件文本无条件镜像进 log 环（此前 g_enabled=false 时整条丢弃，
    //   导致 `wait log "xfer from=..."` 类断言仅在 --trace 下生效）
    rich4::logMirror(buf);
    if (!g_enabled) {
        return;
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_records.size() >= kMaxRecords) {
        g_records.pop_front();
    }
    g_records.emplace_back(buf);
    ++g_seq;
}

bool contains(const char* substr) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (const std::string& r : g_records) {
        if (r.find(substr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int count(const char* substr) {
    std::lock_guard<std::mutex> lk(g_mutex);
    int n = 0;
    for (const std::string& r : g_records) {
        if (r.find(substr) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

bool lastMatch(const char* substr, std::string& out) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto it = g_records.rbegin(); it != g_records.rend(); ++it) {
        if (it->find(substr) != std::string::npos) {
            out = *it;
            return true;
        }
    }
    return false;
}

void clear() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_records.clear();
}

bool exportJson(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    std::fprintf(f, "{\"trace\":[\n");
    uint64_t idx = 0;
    for (const std::string& r : g_records) {
        std::fprintf(f, "%s{\"seq\":%llu,\"text\":\"%s\"}", idx ? ",\n" : "",
                     static_cast<unsigned long long>(idx), jsonEscape(r).c_str());
        ++idx;
    }
    std::fprintf(f, "\n]}\n");
    std::fclose(f);
    return true;
}

} // namespace trace
} // namespace rich4
