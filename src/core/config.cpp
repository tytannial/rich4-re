#include "game/core/config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game/core/log.h"

namespace rich4 {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// 裸 key（去 section 前缀）
std::string bareKey(const std::string& key) {
    const size_t dot = key.find('.');
    return lower(dot == std::string::npos ? key : key.substr(dot + 1));
}

} // namespace

GameConfig& config() {
    static GameConfig s_config;
    return s_config;
}

bool setConfigValue(const std::string& key, const std::string& value) {
    GameConfig& c = config();
    const std::string k = bareKey(key);
    if (k == "preset") {
        c.preset = lower(trim(value));
        return true;
    }
    if (k == "canvas") {
        int w = 0;
        int h = 0;
        if (std::sscanf(value.c_str(), "%dx%d", &w, &h) == 2) {
            c.canvasW = w;
            c.canvasH = h;
            return true;
        }
        c.canvasW = 0;
        c.canvasH = 0;
        return true;
    }
    if (k == "uiscale") {
        c.uiScale = static_cast<float>(std::atof(value.c_str()));
        if (!(c.uiScale > 0.0f)) {
            c.uiScale = 1.0f;
        }
        return true;
    }
    if (k == "filter") {
        c.filter = lower(trim(value));
        return true;
    }
    if (k == "vsync") {
        c.vsync = std::atoi(value.c_str()) != 0 ? 1 : 0;
        return true;
    }
    if (k == "fullscreen") {
        c.fullscreen = std::atoi(value.c_str()) != 0 ? 1 : 0;
        return true;
    }
    if (k == "fontregular") {
        c.fontRegular = trim(value);
        return true;
    }
    if (k == "fontbold") {
        c.fontBold = trim(value);
        return true;
    }
    if (k == "sizescale") {
        const float v = static_cast<float>(std::atof(value.c_str()));
        if (v > 0.0f) {
            c.sizeScale = v;
        }
        return true;
    }
    if (k == "mediadir") {
        c.mediaDir = trim(value);
        return true;
    }
    if (k == "logfile") {
        c.logFile = trim(value);
        return true;
    }
    if (k == "stats") {
        c.stats = std::atoi(value.c_str()) != 0 ? 1 : 0;
        return true;
    }
    return false;
}

std::string defaultIniText() {
    return
        "# rich4.ini —— 重写版扩展配置（与快捷键/存档无关；原版 RICH4.CFG 不新增字段）\n"
        "# 优先级：命令行参数 > 本文件 > RICH4.CFG > 内置默认。\n"
        "# 键名不区分大小写；`#` 或 `;` 起始为注释；`--set section.key=value` 可临时覆盖。\n"
        "\n"
        "[display]\n"
        "# 显示 preset：native（640x480 原始）/ wide（16:9 宽屏视野）/ free（窗口自由缩放）\n"
        "preset = native\n"
        "# 显式画布尺寸（仅 preset=native 生效；0x0 = 自动）\n"
        "canvas = 0x0\n"
        "uiScale = 1.0\n"
        "# 呈现放大过滤：linear（平滑，默认）/ nearest（像素完美）\n"
        "filter = linear\n"
        "# 垂直同步：1 自适应 / 0 关闭\n"
        "vsync = 1\n"
        "# 全屏：1 全屏 / 0 窗口\n"
        "fullscreen = 0\n"
        "\n"
        "[text]\n"
        "# 字体文件（相对 gameDir 或绝对路径；缺失时文本降级为空操作）\n"
        "fontRegular = resources/Fonts/HarmonyOS_Sans_SC_Regular.ttf\n"
        "fontBold = resources/Fonts/HarmonyOS_Sans_SC_Bold.ttf\n"
        "# 字号全局缩放（实机对照微调）\n"
        "sizeScale = 1.09\n"
        "\n"
        "[paths]\n"
        "# 媒体目录（空 = <gameDir>/Media，回退 <gameDir>/../Media）\n"
        "mediaDir =\n"
        "# 日志文件（空 = rich4.log；cwd 不可写时回退用户数据目录）\n"
        "logFile =\n"
        "\n"
        "[debug]\n"
        "# 每 120 帧输出性能统计（同 --stats）\n"
        "stats = 0\n";
}

bool loadConfig(const std::string& iniPath, bool createIfMissing) {
    std::ifstream in(iniPath);
    if (!in.is_open()) {
        if (!createIfMissing) {
            return false;
        }
        std::ofstream out(iniPath);
        if (!out.is_open()) {
            RICH4_LOGW("config: cannot create %s", iniPath.c_str());
            return false;
        }
        out << defaultIniText();
        RICH4_LOGI("config: default rich4.ini written (%s)", iniPath.c_str());
        return true;
    }
    std::string section;
    std::string line;
    int applied = 0;
    int unknown = 0;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') {
            continue;
        }
        if (t[0] == '[') {
            const size_t end = t.find(']');
            section = end != std::string::npos ? lower(t.substr(1, end - 1)) : std::string();
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));
        const std::string full = section.empty() ? key : (section + "." + key);
        if (setConfigValue(full, value)) {
            ++applied;
        } else {
            ++unknown;
            RICH4_LOGW("config: unknown key '%s' (section [%s])", key.c_str(), section.c_str());
        }
    }
    RICH4_LOGI("config: loaded %s (applied=%d unknown=%d)", iniPath.c_str(), applied, unknown);
    return true;
}

} // namespace rich4
