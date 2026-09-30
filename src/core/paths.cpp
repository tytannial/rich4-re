#include "game/core/paths.h"

#include "game/core/log.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <filesystem>

namespace rich4 {

// [NEW] 见 include/game/core/paths.h
std::string resolveResourcePath(const std::string& dir, const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path direct = fs::path(dir) / name;
    std::error_code ec;
    if (fs::exists(direct, ec)) {
        return direct.string();
    }

    // 大小写不敏感匹配（仅 ASCII；资源名为 ASCII）
    std::string needle = name;
    for (char& ch : needle) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const fs::path base(dir);
    if (!fs::is_directory(base, ec)) {
        return direct.string();
    }
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        if (ec) {
            break;
        }
        std::string fname = entry.path().filename().string();
        for (char& ch : fname) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (fname == needle) {
            return entry.path().string();
        }
    }
    return direct.string();
}

namespace {

// 目录可写探测：创建并删除一个探针文件（原版写游戏目录语义；失败 → 需回退）
bool dirWritable(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path probe = fs::path(dir) / ".rich4_write_probe";
    std::FILE* f = std::fopen(probe.string().c_str(), "wb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    fs::remove(probe, ec);
    return true;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

const std::string& fallbackDataDir() {
    static const std::string dir = [] {
        std::string d;
        if (char* p = SDL_GetPrefPath("rich4", "rich4")) {
            d = p;
            SDL_free(p);
            while (!d.empty() && (d.back() == '/' || d.back() == '\\')) {
                d.pop_back();
            }
        }
        return d;
    }();
    return dir;
}

const std::string& writableDataDir(const std::string& gameDir) {
    static std::string cached;
    static std::string cachedFor;
    if (cachedFor == gameDir && !cached.empty()) {
        return cached;
    }
    if (dirWritable(gameDir)) {
        cached = gameDir;
    } else {
        const std::string& fb = fallbackDataDir();
        if (!fb.empty() && dirWritable(fb)) {
            RICH4_LOGW("writableDataDir: '%s' not writable, using fallback '%s'",
                       gameDir.c_str(), fb.c_str());
            cached = fb;
        } else {
            RICH4_LOGW("writableDataDir: no writable location (gameDir='%s')", gameDir.c_str());
            cached = gameDir;
        }
    }
    cachedFor = gameDir;
    return cached;
}

std::string writableDataFile(const std::string& gameDir, const std::string& name) {
    return writableDataDir(gameDir) + "/" + name;
}

std::string readableDataFile(const std::string& gameDir, const std::string& name) {
    const std::string& wd = writableDataDir(gameDir);
    const std::string primary = wd + "/" + name;
    if (fileExists(primary)) {
        return primary;
    }
    const std::string other = (wd == gameDir) ? fallbackDataDir() : gameDir;
    if (!other.empty()) {
        const std::string alt = other + "/" + name;
        if (fileExists(alt)) {
            return alt;
        }
    }
    return primary;
}

} // namespace rich4
