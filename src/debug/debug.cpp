#include "game/app/debug/debug.h"

#include <cctype>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/app/bank_stay_dialog.h"
#include "game/app/economy.h"
#include "game/app/bank_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/card_effects.h"
#include "game/app/item_effects.h"
#include "game/app/dividend.h"
#include "game/app/fate_event.h"
#include "game/app/lottery.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/object_tip.h"
#include "game/app/month_settle.h"
#include "game/app/news_dialog.h"
#include "game/app/query_dialog.h"
#include "game/app/shop_dialog.h"
#include "game/app/stock_market_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/app/game_panel.h"
#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/app/ui_layout.h"
#include "game/core/clock.h"
#include "game/core/debug_hooks.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/core/trace.h"

// [NEW] 统一调试命令层 + headless 脚本执行器（docs/testing.md §3；无原版对应）。
// 断言优先级：state > trace > log；失败计数供 main 退出码。脚本执行器在 renderFrame
// 尾部 tick，阻塞命令（land/day 等内部 runModal）期间重入 tick 继续消费后续
// click/wait/assert 步骤（深度守卫防失控）。正常模式（无脚本且非 debug）零开销。
namespace rich4 {
namespace debug {

// named region 存储（debug 级：产品侧 registerRegion / 脚本 cmdClickR 共享，需外部链接）
std::map<std::string, std::array<int, 4>> g_regions;

namespace {

// ============================ 基础设施 ============================

int g_failures = 0;
int g_asserts = 0;
int g_depth = 0;
uint16_t g_selected = 0;
bool g_doneAll = false; // quit 命令置位：停止脚本推进（不清列表防悬垂引用）
bool g_inBlock = false; // 阻塞命令执行中（重入 tick 冻结后续阻塞 step，放行交互 step）

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

struct Step {
    enum Kind { Cmd, WaitFrames, WaitTrace, WaitLog, WaitIdle, WaitState };
    Kind kind = Cmd;
    std::string a;      // 命令原文 / 等待模式 / wait state 路径
    std::string b;      // wait state 比较符
    int n = 0;          // 帧数 / wait state 比较值
    int limit = 6000;   // 等待类上限帧
    int lineNo = 0;
    bool done = false;  // 阻塞期前瞻已执行（正式经过时跳过）
};

struct Script {
    std::string id = "anon";
    int timeoutFrames = 30000;
    std::vector<Step> steps;
    size_t pc = 0;
    int frames = 0;
    int waited = 0;
};

std::vector<Script> g_scripts;
size_t g_si = 0;
bool g_started = false;

// 拆 token：空白分隔，双引号成组（"" 内可含空格）
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        std::string tok;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                tok += line[i++];
            }
            if (i < line.size()) {
                ++i;
            }
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
                tok += line[i++];
            }
        }
        out.push_back(tok);
    }
    return out;
}

int parseInt(const std::string& s, int def = 0) {
    try {
        return std::stoi(s, nullptr, 0);
    } catch (...) {
        return def;
    }
}

// ============================ 状态路径 getter ============================

bool matchPath(const std::string& path, const char* fmt, std::vector<std::string>& caps,
               int nCap) {
    // fmt 用 '*' 标记捕获段，其余逐字符匹配；捕获以 '.' '[' ']' 为界
    caps.clear();
    std::vector<std::pair<int, int>> spans;
    size_t pi = 0;
    for (size_t fi = 0; fi < std::strlen(fmt);) {
        char c = fmt[fi];
        if (c == '*') {
            size_t si = pi;
            while (pi < path.size() && path[pi] != '.' && path[pi] != '[' && path[pi] != ']') {
                ++pi;
            }
            spans.emplace_back(static_cast<int>(si), static_cast<int>(pi - si));
            ++fi;
        } else {
            if (pi >= path.size() || path[pi] != c) {
                return false;
            }
            ++pi;
            ++fi;
        }
    }
    if (pi != path.size() || static_cast<int>(spans.size()) != nCap) {
        return false;
    }
    for (auto& sp : spans) {
        caps.push_back(path.substr(sp.first, sp.second));
    }
    return true;
}

bool getStateInt(const GameState& st, const std::string& path, long long& out) {
    std::vector<std::string> c;
    if (matchPath(path, "players.*.cash", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].cash;
        return true;
    }
    if (matchPath(path, "players.*.bank", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].bank;
        return true;
    }
    if (matchPath(path, "players.*.points", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].points;
        return true;
    }
    if (matchPath(path, "players.*.loan", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].loan;
        return true;
    }
    if (matchPath(path, "players.*.stateFlags", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].stateFlags;
        return true;
    }
    if (matchPath(path, "players.*.alive", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].alive;
        return true;
    }
    if (matchPath(path, "players.*.state37", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].state37;
        return true;
    }
    if (matchPath(path, "players.*.byte54", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].byte54;
        return true;
    }
    if (matchPath(path, "players.*.skipMove", c, 1)) {  // +56 0x443F80 暫停卡
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].skipMove;
        return true;
    }
    if (matchPath(path, "players.*.fixedStep", c, 1)) {  // +57 0x4458DF 烏龜卡
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].fixedStep;
        return true;
    }
    if (matchPath(path, "players.*.dir", c, 1)) {  // +16 0x442F4D 轉得卡改行進方向
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].dir;
        return true;
    }
    if (matchPath(path, "players.*.ally", c, 1)) {  // +65 0x445710 同盟對象(1-based)
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].ally;
        return true;
    }
    if (matchPath(path, "players.*.allyActive", c, 1)) {  // +61 同盟剩餘天數
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].allyActive;
        return true;
    }
    if (matchPath(path, "players.*.travel", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].travel;
        return true;
    }
    if (matchPath(path, "players.*.dice", c, 1)) {  // +18 diceCount（0x4221C0 骰子调整断言）
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].diceCount;
        return true;
    }
    if (matchPath(path, "players.*.advance", c, 1)) {  // +40 bankAdvance（週轉欠款）
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].bankAdvance;
        return true;
    }
    if (matchPath(path, "players.*.god", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].cellTableIdx;
        return true;
    }
    if (matchPath(path, "players.*.cell", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].cellEntId;
        return true;
    }
    // estates.sel.<owner|level|type>：索引=当前 select（脚本免算硬编码）。
    // 注意：必须排在 "estates.*.xxx" 之前——"sel" 会被通配捕获吞掉
    if (matchPath(path, "estates.sel.*", c, 1) && g_selected >= 2000 && g_selected < 4000) {
        const int i = g_selected - 2000;
        if (i < 1 || i >= static_cast<int>(st.estates.size())) return false;
        const std::string& f = c[0];
        if (f == "owner") out = st.estates[i].owner;
        else if (f == "level") out = st.estates[i].level;
        else if (f == "type") out = st.estates[i].type;
        else if (f == "flag") out = st.estates[i].flag;
        else return false;
        return true;
    }
    if (matchPath(path, "estates.*.owner", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.estates.size())) return false;
        out = st.estates[i].owner;
        return true;
    }
    if (matchPath(path, "estates.*.level", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.estates.size())) return false;
        out = st.estates[i].level;
        return true;
    }
    if (matchPath(path, "estates.*.type", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.estates.size())) return false;
        out = st.estates[i].type;
        return true;
    }
    if (matchPath(path, "estates.*.flag", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.estates.size())) return false;
        out = st.estates[i].flag;
        return true;
    }
    if (matchPath(path, "corps.*.owner", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.corps.size())) return false;
        out = st.corps[i].owner;
        return true;
    }
    if (matchPath(path, "corps.*.sub", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.corps.size())) return false;
        out = st.corps[i].sub;
        return true;
    }
    if (matchPath(path, "corps.*.type", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.corps.size())) return false;
        out = st.corps[i].type;
        return true;
    }
    if (matchPath(path, "specpts.*.owner", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.specPts.size())) return false;
        out = st.specPts[i].owner;
        return true;
    }
    if (matchPath(path, "specpts.*.fund", c, 1)) {
        const int i = parseInt(c[0]);
        if (i < 1 || i >= static_cast<int>(st.specPts.size())) return false;
        out = st.specPts[i].fund;
        return true;
    }
    if (path == "cur") {
        out = st.currentPlayer;
        return true;
    }
    if (path == "turn") {
        out = st.turnCounter;
        return true;
    }
    if (path == "day") {
        out = st.dayCount;
        return true;
    }
    if (path == "month") {
        out = (st.gameDate >> 8) & 0xFF;
        return true;
    }
    if (path == "year") {
        out = st.gameDate >> 16;
        return true;
    }
    if (path == "players") {
        out = st.playerCount;
        return true;
    }
    if (path == "moneyMul") {
        out = st.moneyMul;
        return true;
    }
    if (path == "publicFund") {
        out = st.publicFund;
        return true;
    }
    if (path == "errors") {
        out = logErrorCount();
        return true;
    }
    if (path == "lotteryPool") {
        out = st.publicFund;
        return true;
    }
    if (path == "selected") {
        out = g_selected;
        return true;
    }
    // [NEW] 终局判定调试（docs/testing.md；对应 0x46CAF8/0x49911C/0x499108）
    if (path == "scene") {
        out = st.sceneRequest;
        return true;
    }
    if (path == "daysLimit") {
        out = st.gameDaysLimit;
        return true;
    }
    if (path == "winMoney") {
        out = st.winMoney;
        return true;
    }
    return false;
}

// ============================ 命令实现 ============================

using CmdFn = bool (*)(Application&, const std::vector<std::string>&, std::string&);

struct Cmd {
    const char* name;
    CmdFn fn;
};

int curPlayer(Application& app) {
    const int p = app.gameState().currentPlayer;
    return (p >= 0 && p < 9) ? p : 0;
}

bool argCount(const std::vector<std::string>& a, size_t need, std::string& err, const char* cmd) {
    if (a.size() < need + 1) {
        err = std::string("usage: ") + cmd + " <...>";
        return false;
    }
    return true;
}

// ---- select ----
bool cmdSelect(Application& app, const std::vector<std::string>& a, std::string& err) {
    GameState& st = app.gameState();
    if (a.size() < 2) {
        err = "usage: select <kind> <index>";
        return false;
    }
    const std::string kind = a[1];
    if (kind == "clear") {
        g_selected = 0;
        return true;
    }
    if (!argCount(a, 2, err, "select")) {
        return false;
    }
    const int i = parseInt(a[2]);
    if (kind == "estate" && i >= 1 && i < static_cast<int>(st.estates.size())) {
        g_selected = static_cast<uint16_t>(2000 + i);
    } else if (kind == "corp" && i >= 1 && i < static_cast<int>(st.corps.size())) {
        g_selected = static_cast<uint16_t>(4000 + i);
    } else if (kind == "specpt" && i >= 1 && i < static_cast<int>(st.specPts.size())) {
        g_selected = static_cast<uint16_t>(6000 + i);
    } else if (kind == "cell" && i >= 1 && i < static_cast<int>(st.cellEnts.size())) {
        g_selected = static_cast<uint16_t>(i);
    } else {
        err = "select: bad kind/index: " + a[1] + " " + a[2];
        return false;
    }
    RICH4_LOGI("debug: select %s -> id=%u", a[1].c_str(), g_selected);
    return true;
}

// ---- 场景构造 ----
bool cmdEstateOwner(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "estate.owner")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    const int p = parseInt(a[2]);
    if (i < 1 || i >= static_cast<int>(st.estates.size()) || p < 0 || p > 4) {
        err = "estate.owner: index/player out of range";
        return false;
    }
    st.estates[i].owner = static_cast<uint8_t>(p);
    buildMiniMapMarks(app);
    return true;
}

bool cmdEstateLevel(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "estate.level")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.estates.size())) {
        err = "estate.level: index out of range";
        return false;
    }
    st.estates[i].level = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 5));
    return true;
}

bool cmdEstateType(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "estate.type")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.estates.size())) {
        err = "estate.type: index out of range";
        return false;
    }
    st.estates[i].type = static_cast<uint8_t>(parseInt(a[2]) != 0 ? 1 : 0);
    return true;
}

bool cmdCorpBuild(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "corp.build")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.corps.size())) {
        err = "corp.build: index out of range";
        return false;
    }
    Corp& cp = st.corps[i];
    cp.owner = static_cast<uint8_t>(curPlayer(app) + 1);
    cp.type = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 4));
    cp.sub = a.size() > 3 ? static_cast<uint8_t>(clampi(parseInt(a[3]), 0, 5)) : 1;
    buildMiniMapMarks(app);
    return true;
}

bool cmdCorpLevel(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "corp.level")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.corps.size())) {
        err = "corp.level: index out of range";
        return false;
    }
    st.corps[i].sub = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 5));
    return true;
}

// [NEW] corp.sub <i> <n>：研發門槛解鎖級數（0x44101D 研究所周邊構造用）
bool cmdCorpSub(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "corp.sub <i> <n>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    const int n = parseInt(a[2]);
    if (i <= 0 || i >= static_cast<int>(st.corps.size()) || n < 0 || n > 5) {
        err = "corp.sub: out of range";
        return false;
    }
    st.corps[i].sub = static_cast<uint8_t>(n);
    return true;
}

bool cmdCorpOwner(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "corp.owner <i> <p>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.corps.size())) {
        err = "corp.owner: index out of range";
        return false;
    }
    st.corps[i].owner = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 4));
    buildMiniMapMarks(app);
    return true;
}

bool cmdSpecPtOwner(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "specpt.owner")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.specPts.size())) {
        err = "specpt.owner: index out of range";
        return false;
    }
    st.specPts[i].owner = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 4));
    buildMiniMapMarks(app);
    return true;
}

bool cmdAllOwned(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "all.owned")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    for (size_t i = 1; i < st.estates.size(); ++i) st.estates[i].owner = static_cast<uint8_t>(p);
    for (size_t i = 1; i < st.corps.size(); ++i) st.corps[i].owner = static_cast<uint8_t>(p);
    for (size_t i = 1; i < st.specPts.size(); ++i) st.specPts[i].owner = static_cast<uint8_t>(p);
    buildMiniMapMarks(app);
    return true;
}

bool cmdAllLevel1(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].owner != 0) {
            st.estates[i].level = 1;
        }
    }
    return true;
}

// ======================= [NEW] 调试热键收口组（docs/debug-keys.md v2） =======================
// 交互热键层只做「鼠标指向→selected→拼命令串→execLine」，状态写入统一在本区实现
// （单一事实源；旧 debug_keys.cpp 与命令层双实现分叉的根因修正）。

uint16_t resolveTargetCell(Application& app);  // 前向（定义见事件触发区）

// select/pointed 对象 id 段分解：1=estate 2=corp 3=specPt（idx 输出表内下标；0=无）
int selKindIndex(uint16_t id, int& idx) {
    if (id >= 2000 && id < 4000) {
        idx = id - 2000;
        return 1;
    }
    if (id >= 4000 && id < 6000) {
        idx = id - 4000;
        return 2;
    }
    if (id >= 6000 && id < 8000) {
        idx = id - 6000;
        return 3;
    }
    idx = 0;
    return 0;
}

std::string nameAt(const uint8_t* p, int maxLen) {
    char buf[24] = {};
    int n = 0;
    while (n < maxLen && n < 23 && p[n]) {
        buf[n] = static_cast<char>(p[n]);
        ++n;
    }
    return big5ToUtf8(buf, n);
}

// [NEW] 调试传送合一（参照 [RE 0x4477C3] 传送机语义，剥离时光机快照/状态机接线）：
//   清旧格占位→置新格 + prevCellEnt=旧格 + dir=facingBetween（**朝向刷新**）+
//   updatePlayerCarriedObjects + 清行走资源组标志 + loadWalkResources。
// 旧 4 处手写块（cmdLand/cmdPlayerTeleport/cmdSelGoto/Ctrl+Shift+G）均不刷新
//   prev/dir = 传送后角色朝向错误的根因（2026-09-29 收口）。
void teleportPlayerToCell(GameState& st, int p, uint16_t cell) {
    if (cell == 0 || cell >= static_cast<int>(st.cellEnts.size()) || p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    const uint32_t mask = 256u << p;
    const uint16_t old = pl.cellEntId;
    if (old > 0 && old < st.cellEnts.size()) {
        st.cellEnts[old].occMask &= ~mask;
    }
    pl.prevCellEnt = old;
    pl.cellEntId = cell;
    pl.spriteX = static_cast<uint16_t>(st.cellEnts[cell].x);
    pl.spriteY = static_cast<uint16_t>(st.cellEnts[cell].y);
    int from = old;
    if (from <= 0 || from >= static_cast<int>(st.cellEnts.size())) {
        for (int k = 0; k < 4; ++k) {  // 无旧格时取新格首出口作朝向基准
            if (st.cellEnts[cell].exits[k] != 0) {
                from = st.cellEnts[cell].exits[k];
                break;
            }
        }
    }
    if (from != 0 && from != cell) {
        pl.dir = static_cast<uint8_t>(facingBetween(st, from, cell));
    }
    st.cellEnts[cell].occMask |= mask;
    updatePlayerCarriedObjects(st, p);  // [RE 0x40FC00]
    st.playerActionFlags[p] &= 0xF0;    // 清行走资源组标志（travel/朝向组重载）
    loadWalkResources(st, p);           // [RE 0x40B93B]
}

// [RE 0x419A67] 联动组收集（原版收租高亮循环同定义）：
//   住宅用地 = 同名 + type==0（同街）；连锁店 = 全部 type!=0（跨街道）
int collectLinkedGroup(GameState& st, int estateIdx, std::vector<int>& out) {
    out.clear();
    if (estateIdx < 1 || estateIdx >= static_cast<int>(st.estates.size())) {
        return 0;
    }
    char street[sizeof(Estate::name)] = {};
    std::memcpy(street, st.estates[estateIdx].name, sizeof(street));
    const bool chain = st.estates[estateIdx].type != 0;
    for (size_t j = 1; j < st.estates.size(); ++j) {
        const Estate& e = st.estates[j];
        if (chain ? (e.type != 0)
                  : (e.type == 0 && std::strncmp(e.name, street, sizeof(street)) == 0)) {
            out.push_back(static_cast<int>(j));
        }
    }
    return static_cast<int>(out.size());
}

void clearEstateAll(Estate& es) {
    es.owner = 0;
    es.level = 0;
    es.type = 0;
    es.expireDate = 0;
}

void clearCorpAll(Corp& cp) {
    cp.owner = 0;
    cp.sub = 0;
    cp.type = 0;
    cp.researchItem = 0;
    cp.researchLeft = 0;
    cp.expireDate = 0;
}

// [NEW] victory <days> <money>：注入终局目标（天数上限 [RE 0x49911C] / 胜利资金 [RE 0x499108]），0=不判
bool cmdVictory(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "victory <daysLimit> <winMoney>")) return false;
    app.gameState().gameDaysLimit = static_cast<int32_t>(parseInt(a[1]));
    app.gameState().winMoney = static_cast<int32_t>(parseInt(a[2]));
    return true;
}

// ---- get.land：指向地块 owner = 当前回合玩家（旧 Ctrl+1）----
bool cmdGetLand(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    const int owner = curPlayer(app) + 1;
    int i = 0;
    switch (selKindIndex(g_selected, i)) {
        case 1:
            if (i >= 1 && i < static_cast<int>(st.estates.size())) {
                st.estates[i].owner = static_cast<uint8_t>(owner);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: get.land estate[%d] owner -> %d", i, owner);
                return true;
            }
            break;
        case 2:
            if (i >= 1 && i < static_cast<int>(st.corps.size())) {
                st.corps[i].owner = static_cast<uint8_t>(owner);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: get.land corp[%d] owner -> %d", i, owner);
                return true;
            }
            break;
        case 3:
            if (i >= 1 && i < static_cast<int>(st.specPts.size())) {
                st.specPts[i].owner = static_cast<uint8_t>(owner);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: get.land specPt[%d] owner -> %d", i, owner);
                return true;
            }
            break;
        default:
            break;
    }
    err = "get.land: no estate/corp/specPt under cursor";
    return false;
}

// ---- get.street：指向住宅的联动组归当前玩家（旧 Ctrl+9）----
bool cmdGetStreet(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    int i = 0;
    if (selKindIndex(g_selected, i) != 1 || i < 1 || i >= static_cast<int>(st.estates.size())) {
        err = "get.street: no estate under cursor";
        return false;
    }
    std::vector<int> group;
    collectLinkedGroup(st, i, group);
    const uint8_t owner = static_cast<uint8_t>(curPlayer(app) + 1);
    for (int j : group) {
        st.estates[j].owner = owner;  // 联动组仅改归属，保留建筑
    }
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: get.street %d parcels (%s) owner -> %d", static_cast<int>(group.size()),
               st.estates[i].type == 0 ? "同街" : "连锁店", owner);
    return true;
}

// ---- clear.cell：指向地块清空归属（estate owner/level/type、corp 设施、specpt 经营权；
//      格上物件属 cellTable 生命周期，用 obj.kick 另行处理）----
bool cmdClearCell(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    int i = 0;
    switch (selKindIndex(g_selected, i)) {
        case 1:
            if (i >= 1 && i < static_cast<int>(st.estates.size())) {
                clearEstateAll(st.estates[i]);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: clear.cell estate[%d] -> 无主空地", i);
                return true;
            }
            break;
        case 2:
            if (i >= 1 && i < static_cast<int>(st.corps.size())) {
                clearCorpAll(st.corps[i]);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: clear.cell corp[%d] -> 无主无设施", i);
                return true;
            }
            break;
        case 3:
            if (i >= 1 && i < static_cast<int>(st.specPts.size())) {
                st.specPts[i].owner = 0;
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: clear.cell specPt[%d] owner -> 0", i);
                return true;
            }
            break;
        default:
            break;
    }
    err = "clear.cell: no estate/corp/specPt under cursor";
    return false;
}

// ---- clear.street：指向住宅的联动组全部归无主（get.street 反操作）----
bool cmdClearStreet(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    int i = 0;
    if (selKindIndex(g_selected, i) != 1 || i < 1 || i >= static_cast<int>(st.estates.size())) {
        err = "clear.street: no estate under cursor";
        return false;
    }
    std::vector<int> group;
    collectLinkedGroup(st, i, group);
    for (int j : group) {
        clearEstateAll(st.estates[j]);
    }
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: clear.street %d parcels -> 无主空地", static_cast<int>(group.size()));
    return true;
}

// ---- get.all / clear.all：全图归属（get=归当前玩家；clear=全归无主+清建筑）----
bool cmdGetAll(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    const uint8_t owner = static_cast<uint8_t>(curPlayer(app) + 1);
    for (size_t i = 1; i < st.estates.size(); ++i) st.estates[i].owner = owner;
    for (size_t i = 1; i < st.corps.size(); ++i) st.corps[i].owner = owner;
    for (size_t i = 1; i < st.specPts.size(); ++i) st.specPts[i].owner = owner;
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: get.all owner -> %d", owner);
    return true;
}

bool cmdClearAll(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    for (size_t i = 1; i < st.estates.size(); ++i) clearEstateAll(st.estates[i]);
    for (size_t i = 1; i < st.corps.size(); ++i) clearCorpAll(st.corps[i]);
    for (size_t i = 1; i < st.specPts.size(); ++i) st.specPts[i].owner = 0;
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: clear.all -> 全图无主空地");
    return true;
}

// ---- level.up / level.down：指向地块等级 ±1（旧 Ctrl+2/3；estate level / corp sub，0..5）----
bool cmdLevelRel(Application& app, int delta, std::string& err) {
    GameState& st = app.gameState();
    int i = 0;
    switch (selKindIndex(g_selected, i)) {
        case 1:
            if (i >= 1 && i < static_cast<int>(st.estates.size())) {
                st.estates[i].level =
                    static_cast<uint8_t>(clampi(st.estates[i].level + delta, 0, 5));
                RICH4_LOGI("debug: level estate[%d] -> %d", i, st.estates[i].level);
                return true;
            }
            break;
        case 2:
            if (i >= 1 && i < static_cast<int>(st.corps.size())) {
                st.corps[i].sub = static_cast<uint8_t>(clampi(st.corps[i].sub + delta, 0, 5));
                RICH4_LOGI("debug: level corp[%d].sub -> %d", i, st.corps[i].sub);
                return true;
            }
            break;
        default:
            break;
    }
    err = "level: no estate/corp under cursor";
    return false;
}

bool cmdLevelUp(Application& app, const std::vector<std::string>&, std::string& err) {
    return cmdLevelRel(app, +1, err);
}
bool cmdLevelDown(Application& app, const std::vector<std::string>&, std::string& err) {
    return cmdLevelRel(app, -1, err);
}

// [NEW] holiday <mode> <map> <y> <m> <d>：设日期/地图并直呼 holidayDaily（测 sub_452444 节日效果）
bool cmdHoliday(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 5, err, "holiday <mode> <map> <y> <m> <d>")) return false;
    GameState& st = app.gameState();
    st.gameMode = parseInt(a[1]);
    st.mapIndex = parseInt(a[2]);
    st.gameDate = (static_cast<uint32_t>(parseInt(a[3])) << 16) |
                  (static_cast<uint32_t>(parseInt(a[4])) << 8) |
                  static_cast<uint32_t>(parseInt(a[5]));
    holidayDaily(app);
    return true;
}

// ---- dump.pointed：打印指向地块详情（旧 Ctrl+8，诊断主入口）----
bool cmdDumpPointed(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    const uint16_t id = g_selected;
    int i = 0;
    switch (selKindIndex(id, i)) {
        case 1: {
            if (i < 1 || i >= static_cast<int>(st.estates.size())) break;
            const Estate& es = st.estates[i];
            const std::string nm = nameAt(reinterpret_cast<const uint8_t*>(es.name), 19);
            RICH4_LOGI("debug: estate[%d] id=%u name=%s type=%u owner=%u level=%u flag=%u "
                       "dir=%u priceAdd=%u priceBase=%u fees=[%u,%u,%u,%u,%u,%u] "
                       "price=%d expire=%u",
                       i, id, nm.c_str(), es.type, es.owner, es.level, es.flag, es.dir,
                       es.priceAdd, es.priceBase, es.fees[0], es.fees[1], es.fees[2], es.fees[3],
                       es.fees[4], es.fees[5], es.price, es.expireDate);
            return true;
        }
        case 2: {
            if (i < 1 || i >= static_cast<int>(st.corps.size())) break;
            const Corp& cp = st.corps[i];
            RICH4_LOGI("debug: corp[%d] id=%u type=%u owner=%u sub=%u flag=%u "
                       "buildPrice=%u fees=[%u,%u,%u,%u,%u,%u] lastFee=%d research=%u/%u "
                       "expire=%u",
                       i, id, cp.type, cp.owner, cp.sub, cp.flag, cp.buildPrice, cp.feeTable[0],
                       cp.feeTable[1], cp.feeTable[2], cp.feeTable[3], cp.feeTable[4],
                       cp.feeTable[5], cp.lastFee, cp.researchItem, cp.researchLeft, cp.expireDate);
            return true;
        }
        case 3: {
            if (i < 1 || i >= static_cast<int>(st.specPts.size())) break;
            const SpecPt& sp = st.specPts[i];
            const std::string nm = nameAt(sp.pad4, 20);
            RICH4_LOGI("debug: specPt[%d] id=%u name=%s owner=%u stockNo=%u costType=%u "
                       "sprite=%u fund=%d feeBase=%u capital=%d sharesLeft=%d",
                       i, id, nm.c_str(), sp.owner, sp.stockNo, sp.costType, sp.sprite, sp.fund,
                       sp.feeBase, sp.capital, sp.sharesLeft);
            return true;
        }
        default:
            break;
    }
    if (id >= 8000 && id < 10000) {
        const int ei = id - 8000;
        if (ei > 0 && ei < static_cast<int>(st.evtCells.size())) {
            const EvtCell& ev = st.evtCells[ei];
            const std::string nm = nameAt(ev.pad4, 20);
            RICH4_LOGI("debug: evtCell[%d] id=%u name=%s sprite=%u", ei, id, nm.c_str(),
                       ev.sprite);
            return true;
        }
    }
    if (id >= 1 && id < 2000 && id < st.cellEnts.size()) {
        const CellEnt& ce = st.cellEnts[id];
        RICH4_LOGI("debug: cellEnt[%u] type=%u objId=%u sprite=%u occMask=0x%08X", id,
                   ce.occMask & 0xFF, ce.special, ce.sprite, ce.occMask);
        return true;
    }
    err = "dump.pointed: no object under cursor";
    return false;
}

// ---- dir.cycle：当前玩家朝向 +45°（朝向敏感渲染/前 1 格生成测试；资源组立即重载）----
bool cmdDirCycle(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    const int p = curPlayer(app);
    Player& pl = st.players[p];
    pl.dir = static_cast<uint8_t>((pl.dir + 1) & 7);
    st.playerActionFlags[p] &= 0xF0;  // 清行走资源组标志
    loadWalkResources(st, p);         // [RE 0x40B93B] 按新朝向重载
    RICH4_LOGI("debug: dir.cycle p%d dir=%u", p, pl.dir);
    return true;
}

// ---- obj.createAhead：面朝前 1 格生成物件（旧 Ctrl+Shift+O「当前格」改「前 1 格」，
//      观察飞行/附身来向；类型 1..18 循环）----
bool cmdObjCreateAhead(Application& app, const std::vector<std::string>&, std::string& err) {
    static int sType = 1;
    GameState& st = app.gameState();
    Player& pl = st.players[curPlayer(app)];
    uint16_t ahead = 0;
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        const CellEnt& ce = st.cellEnts[pl.cellEntId];
        for (int k = 0; k < 4; ++k) {
            if (ce.exits[k] != 0 && facingBetween(st, pl.cellEntId, ce.exits[k]) == pl.dir) {
                ahead = ce.exits[k];
                break;
            }
        }
        if (ahead == 0) {  // 朝向无出口（转角/朝路外）→ 回退首出口
            for (int k = 0; k < 4; ++k) {
                if (ce.exits[k] != 0) {
                    ahead = ce.exits[k];
                    break;
                }
            }
        }
    }
    if (ahead == 0) {
        err = "obj.createAhead: no exit cell ahead";
        return false;
    }
    const int obj = createMapObject(app, sType, ahead, 0, 0);  // [RE 0x40E033]
    RICH4_LOGI("debug: obj.createAhead type %d -> obj %d cell %u (dir=%u)", sType, obj, ahead,
               pl.dir);
    sType = sType % 18 + 1;
    return true;
}

// ---- obj.kick：踢飞指向格上的物件（机器娃娃沿路踢除子步复用 [RE 0x41B42D p==8]：
//      bounceObject 0x40FAFD + releaseCellTableSlot 0x40E14D 含配对轮替/附身解除）----
bool cmdObjKick(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    const uint16_t cell = resolveTargetCell(app);
    if (cell < 1 || cell >= static_cast<int>(st.cellEnts.size())) {
        err = "obj.kick: no cell target (point/select a cell or object)";
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>((st.cellEnts[cell].occMask >> 16) & 0xFF);
    if (slot == 0) {
        RICH4_LOGI("debug: obj.kick cell %u has no object", cell);
        return true;
    }
    const int p = curPlayer(app);
    bounceObject(st, slot, cell, st.players[p].cellEntId);  // 以玩家位为基准弹飞
    releaseCellTableSlot(app, slot);                        // [RE 0x40E14D]
    RICH4_LOGI("debug: obj.kick slot %u cell %u -> bounced (RE 0x40FAFD/0x41B42D)", slot, cell);
    return true;
}

// ---- 玩家 ----
bool cmdPlayerCash(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.cash <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.cash: bad index";
        return false;
    }
    app.gameState().players[p].cash = parseInt(a[2]);
    refreshPlayerPanelFor(app, p); // [NEW] 右栏金额立即刷新（脚本改钱后避免旧值残留）
    return true;
}

bool cmdPlayerBank(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.bank <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.bank: bad index";
        return false;
    }
    app.gameState().players[p].bank = parseInt(a[2]);
    refreshPlayerPanelFor(app, p); // [NEW] 右栏金额立即刷新
    return true;
}

bool cmdPlayerAdvance(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.advance <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.advance: bad index";
        return false;
    }
    app.gameState().players[p].bankAdvance = parseInt(a[2]);  // [RE 0x496B90] 週轉欠款
    return true;
}

bool cmdPlayerPoints(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.points <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.points: bad index";
        return false;
    }
    app.gameState().players[p].points = static_cast<uint16_t>(parseInt(a[2]));
    return true;
}

bool cmdPlayerState(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.state <p> <0..6>")) return false;
    const int p = parseInt(a[1]);
    const int s = parseInt(a[2]);
    if (p < 0 || p >= 9 || s < 0 || s > 6) {
        err = "player.state: bad args";
        return false;
    }
    Player& pl = app.gameState().players[p];
    pl.stateFlags = 0;
    pl.byte54 = 0;
    pl.state37 = 0;
    switch (s) {
        case 1: pl.stateFlags |= 3u; break;
        case 2: pl.stateFlags |= 3u << 8; break;
        case 3: pl.stateFlags |= 3u << 16; break;
        case 4: pl.stateFlags |= 3u << 24; break;
        case 5: pl.byte54 = 3; break;
        case 6: pl.state37 = 3; break;
        default: break;
    }
    return true;
}

bool cmdPlayerGod(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.god <p> <slot>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.god: bad index";
        return false;
    }
    app.gameState().players[p].cellTableIdx = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 18));
    return true;
}

bool cmdPlayerVehicle(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.vehicle <p> <0|1|2>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.vehicle: bad index";
        return false;
    }
    Player& pl = app.gameState().players[p];
    pl.travel = static_cast<uint8_t>(clampi(parseInt(a[2]), 0, 2));
    pl.diceCount = static_cast<uint8_t>(pl.travel + 1); // [RE 0x407236]
    app.gameState().playerActionFlags[p] &= 0xF0;
    return true;
}

bool cmdPlayerTeleport(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.teleport <p> <cell>")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    const int cell = parseInt(a[2]);
    if (p < 0 || p >= 9 || cell < 1 || cell >= static_cast<int>(st.cellEnts.size())) {
        err = "player.teleport: bad args";
        return false;
    }
    teleportPlayerToCell(st, p, static_cast<uint16_t>(cell));  // [NEW] 合一 helper（含朝向刷新）
    buildMiniMapMarks(app);
    return true;
}

bool cmdPlayerDice(Application& app, const std::vector<std::string>& a, std::string& err) {
    // land 直落不产生步数；加油站/石油等「載具×步數」公式需显式设 diceValue
    // [RE 0x48BAFC] g_diceValue 为**全局**（GameState 字段，本次移动总步数），非 per-player
    if (!argCount(a, 2, err, "player.dice <p ignored> <v>")) return false;
    app.gameState().diceValue = parseInt(a[2]);
    return true;
}

bool cmdPlayerAlive(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 置入场标志（统计/目标过滤以 alive!=0 为准；1=已入场人类形态，alive&6=0 不触发 AI 自动）
    if (!argCount(a, 2, err, "player.alive <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.alive: bad index";
        return false;
    }
    app.gameState().players[p].alive = static_cast<uint8_t>(parseInt(a[2]));
    return true;
}

bool cmdGiveCard(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "give.card <p> <id> [n]")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    const int id = parseInt(a[2]);
    const int n = a.size() > 3 ? parseInt(a[3]) : 1;
    if (p < 0 || p >= 4 || id < 1 || id > 30) {
        err = "give.card: bad args";
        return false;
    }
    for (int k = 0; k < n; ++k) {
        giveCardToBag(st, p, id);
    }
    return true;
}

bool cmdGiveItem(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "give.item <p> <id> [n]")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    const int id = parseInt(a[2]);
    const int n = a.size() > 3 ? parseInt(a[3]) : 1;
    if (p < 0 || p >= 4 || id < 1 || id > 13) {
        err = "give.item: bad args";
        return false;
    }
    for (int k = 0; k < n; ++k) {
        givePlayerItem(st, p, id);
    }
    return true;
}

// ---- player.give <p> cash|bank|points <v>：增量注入（旧 Ctrl+7/Shift+B/Shift+P；
//      现金/存款走 addMoney [RE 0x41D3F4] 原生入账，含月度意外之财统计+面板刷新）----
bool cmdPlayerGive(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 3, err, "player.give <p> cash|bank|points <v>")) return false;
    const int p = parseInt(a[1]);
    const int v = parseInt(a[3]);
    if (p < 0 || p >= 9) {
        err = "player.give: bad index";
        return false;
    }
    const std::string kind = a[2];
    if (kind == "cash") {
        addMoney(app, p, v, true);   // [RE 0x41D3F4] addMoney cash
    } else if (kind == "bank") {
        addMoney(app, p, v, false);  // [RE 0x41D3F4] addMoney bank
    } else if (kind == "points") {
        app.gameState().players[p].points =
            static_cast<uint16_t>(app.gameState().players[p].points + v);
    } else {
        err = "player.give: cash|bank|points";
        return false;
    }
    RICH4_LOGI("debug: player.give p%d %s %+d -> cash=%d bank=%d points=%u", p, kind.c_str(), v,
               app.gameState().players[p].cash, app.gameState().players[p].bank,
               app.gameState().players[p].points);
    return true;
}

// ---- refill.cards <p> <first>：清卡包并连填 15 种卡（旧 Ctrl+Shift+I/H；直写卡槽，
//      不走 giveCardToBag 赠卡池扣减/满弃最低价——纯测试发牌，propStock 不动）----
bool cmdRefillCards(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "refill.cards <p> <first 1|16>")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    const int first = parseInt(a[2]);
    if (p < 0 || p >= 4 || first < 1 || first + 14 > 30) {
        err = "refill.cards: bad args";
        return false;
    }
    const size_t off = static_cast<size_t>(15 * p);
    for (int i = 0; i < 15; ++i) {
        st.cardState60[off + i] = static_cast<uint8_t>(first + i);
    }
    RICH4_LOGI("debug: refill.cards p%d = id %d..%d (工具条 卡片 case8 逐卡实测)", p, first,
               first + 14);
    return true;
}

// ---- items.all <p>：全 13 种道具 ×2（旧 Ctrl+Shift+F；givePlayerItem [RE 0x445A4D]
//      原生发放，id≤8 受礼物池 misc8A 限制）----
bool cmdItemsAll(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "items.all <p>")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 4) {
        err = "items.all: bad index";
        return false;
    }
    for (int id = 1; id <= 13; ++id) {
        givePlayerItem(st, p, id);
        givePlayerItem(st, p, id);
    }
    RICH4_LOGI("debug: items.all p%d 13 types x2 (工具条 case7 使用)", p);
    return true;
}

// ---- lottery.fixwin [p]：保送中奖构造（旧 Ctrl+Shift+R：清号→11 注>10 → 开奖从
//      已售号随机必中；奖金池 <3000 注入便于观察金额）----
bool cmdLotteryFixwin(Application& app, const std::vector<std::string>& a, std::string& err) {
    GameState& st = app.gameState();
    const int p = a.size() > 1 ? parseInt(a[1]) : curPlayer(app);
    if (p < 0 || p >= 4) {
        err = "lottery.fixwin: bad index";
        return false;
    }
    std::memset(st.lotteryNumbers, 0, sizeof(st.lotteryNumbers));
    for (int i = 0; i < 11; ++i) {
        st.lotteryNumbers[i] = static_cast<uint8_t>(p + 1);
    }
    if (st.publicFund < 3000) {
        st.publicFund = 3000;
    }
    RICH4_LOGI("debug: lottery.fixwin p%d 11 notes pool=%d -> draw (RE 0x431712)", p,
               st.publicFund);
    lotteryDrawMeeting(app);
    return true;
}

// ---- dump.celltable：活动槽/附身/池/道具库存全景（旧 Ctrl+Shift+Y）----
bool cmdDumpCellTable(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    for (int slot = 0; slot < 46; ++slot) {
        const uint8_t* e = &st.cellTable[24 * slot];
        if (e[2] == 0 && e[3] == 0 && e[5] == 0 && e[6] == 0) {
            continue;
        }
        const uint16_t ent = static_cast<uint16_t>(e[2] | (e[3] << 8));
        RICH4_LOGI("  cellTable[%d] type %u(%s) ent %u dir %u life %u owner %u fly %u", slot, e[0],
                   e[0] < 19 ? kObjectNames[e[0]] : "?", ent, e[1], e[4], e[5], e[6]);
    }
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        const Player& pl = st.players[i];
        RICH4_LOGI("  player %d attached %u cellNo %u luckA/B/C %d/%d/%d", i, pl.cellTableIdx,
                   pl.cellNo, pl.luckA, pl.luckB, pl.luckC);
    }
    RICH4_LOGI("  giftPool %u,%u,%u,%u,.. trapStock %u,%u,%u", st.misc8A[0], st.misc8A[1],
               st.misc8A[2], st.misc8A[3], st.trapStock[0], st.trapStock[1], st.trapStock[2]);
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        RICH4_LOGI("  itemStock p%d = 娃娃%u 路障%u 地雷%u 炸彈%u 機車%u 汽車%u 飛彈%u "
                   "骰子%u 工人%u 時光機%u 傳送機%u 工程車%u 核彈%u | travel=%u dice=%u",
                   i, st.itemStock[15 * i + 0], st.itemStock[15 * i + 1], st.itemStock[15 * i + 2],
                   st.itemStock[15 * i + 3], st.itemStock[15 * i + 4], st.itemStock[15 * i + 5],
                   st.itemStock[15 * i + 6], st.itemStock[15 * i + 7], st.itemStock[15 * i + 8],
                   st.itemStock[15 * i + 9], st.itemStock[15 * i + 10], st.itemStock[15 * i + 11],
                   st.itemStock[15 * i + 12], st.players[i].travel, st.players[i].diceCount);
    }
    RICH4_LOGI("debug: dump.celltable done");
    return true;
}

// ---- 事件触发 ----
uint16_t resolveTargetCell(Application& app);  // 前向（定义见文件后；收口区另有声明）

// [NEW] roll: scriptable human dice entry = 0x40DD1F startPlayerMove
//   (dice value via rng.dice; turn snapshot 0x44808A saved inside, same as real path)
bool cmdRoll(Application& app, const std::vector<std::string>&, std::string&) {
    GameState& st = app.gameState();
    trace::logf("roll p=%d", st.currentPlayer);
    startPlayerMove(app);
    RICH4_LOGI("debug: roll -> steps=%d state=%u (RE 0x40DD1F)", st.remainingSteps,
               st.playerActionState[st.currentPlayer]);
    return true;
}

bool cmdLand(Application& app, const std::vector<std::string>& a, std::string& err) {
    GameState& st = app.gameState();
    uint16_t cell = 0;
    if (a.size() >= 2) {
        cell = static_cast<uint16_t>(parseInt(a[1]));
    } else {
        cell = resolveTargetCell(app);
    }
    if (cell < 1 || cell >= static_cast<int>(st.cellEnts.size())) {
        err = "land: no target cell (pass cell or select first)";
        return false;
    }
    const int cur = curPlayer(app);
    if (st.players[cur].cellEntId != cell) {
        teleportPlayerToCell(st, cur, cell);  // [NEW] 合一：含 prev/dir 朝向刷新
        buildMiniMapMarks(app);
    }
    RICH4_LOGI("debug: land player %d -> cell %u dir=%u", cur, cell, st.players[cur].dir);
    const int wait = landingEvent(app, cell);
    RICH4_LOGI("debug: land landingEvent -> wait=%d", wait);
    return true;
}

bool cmdDay(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)err;
    const int n = a.size() > 1 ? clampi(parseInt(a[1]), 1, 400) : 1;
    for (int i = 0; i < n; ++i) {
        advanceDay(app);
    }
    RICH4_LOGI("debug: day advance %d -> dayCount=%d", n, app.gameState().dayCount);
    return true;
}

bool cmdJail(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "jail <p> [days]")) return false;
    const int p = parseInt(a[1]);
    const int days = a.size() > 2 ? clampi(parseInt(a[2]), 1, 30) : 3;
    if (p < 0 || p >= 9) {
        err = "jail: bad index";
        return false;
    }
    if (app.gameState().eventFlcActive) {
        return true;
    }
    jailPlayer(app, p, days);
    return true;
}

bool cmdHosp(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "hosp <p> [days]")) return false;
    const int p = parseInt(a[1]);
    const int days = a.size() > 2 ? clampi(parseInt(a[2]), 1, 30) : 3;
    if (p < 0 || p >= 9) {
        err = "hosp: bad index";
        return false;
    }
    if (app.gameState().eventFlcActive) {
        return true;
    }
    hospitalizePlayer(app, p, days);
    return true;
}

// ---- 注入/设置 ----
bool cmdRng(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (a.size() < 2) {
        err = "usage: rng <slot|clear> [value]";
        return false;
    }
    if (a[1] == "clear") {
        dbg::clearInject();
        return true;
    }
    if (a.size() < 3) {
        err = "rng: missing value";
        return false;
    }
    dbg::Slot slot;
    if (!dbg::slotByName(a[1].c_str(), slot)) {
        err = "rng: unknown slot " + a[1];
        return false;
    }
    dbg::inject(slot, parseInt(a[2]));
    return true;
}

bool cmdSettingsAnim(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "settings.anim <0|1>")) return false;
    app.gameState().settings[1] = static_cast<uint8_t>(parseInt(a[1]) & 1);
    return true;
}

// [NEW] settings.layout <0|1|2>：运行期改画面布局（0/1=完整信息面板 / 2=玩家条；
//   测试右栏各布局用；下一帧全量重绘生效）
bool cmdSettingsLayout(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "settings.layout <0|1|2>")) return false;
    const int v = parseInt(a[1]);
    if (v < 0 || v > 2) {
        err = "settings.layout: 0..2";
        return false;
    }
    app.gameState().settings[5] = static_cast<uint8_t>(v);
    return true;
}

bool cmdSeed(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 1, err, "seed <n>")) return false;
    std::srand(static_cast<unsigned>(parseInt(a[1])));
    return true;
}

// ---- 断言 ----
bool cmpOp(long long got, const std::string& op, long long want) {
    if (op == "==") return got == want;
    if (op == "!=") return got != want;
    if (op == ">=") return got >= want;
    if (op == "<=") return got <= want;
    if (op == ">") return got > want;
    if (op == "<") return got < want;
    return false;
}

void fail(const std::string& what, const std::string& detail) {
    ++g_failures;
    RICH4_LOGE("ASSERT FAIL %s: %s", what.c_str(), detail.c_str());
}

void pass() { ++g_asserts; }

bool cmdAssert(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (a.size() < 3) {
        err = "usage: assert <state|log|trace> ... | assert no <log|trace> <pat>";
        return false;
    }
    bool negate = false;
    size_t i = 1;
    if (a[i] == "no") {
        negate = true;
        ++i;
    }
    if (i + 1 >= a.size()) {
        err = "assert: missing kind";
        return false;
    }
    const std::string kind = a[i++];
    if (kind == "state") {
        if (a.size() < i + 3) {
            err = "usage: assert state <path> <op> <value>";
            return false;
        }
        const std::string path = a[i++];
        const std::string op = a[i++];
        long long want = 0;
        long long got = 0;
        if (!getStateInt(app.gameState(), a[i], want)) {
            try {
                want = std::stoll(a[i]);
            } catch (...) {
                err = "assert state: bad value/path " + a[i];
                return false;
            }
        }
        if (!getStateInt(app.gameState(), path, got)) {
            err = "assert state: unknown path " + path;
            return false;
        }
        const bool ok = cmpOp(got, op, want);
        if (ok == !negate) {
            pass();
        } else {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s %s %lld (got %lld)", path.c_str(), op.c_str(),
                          want, got);
            fail("state", buf);
        }
        return true;
    }
    if (kind == "log" || kind == "trace") {
        if (i >= a.size()) {
            err = "assert: missing pattern";
            return false;
        }
        // 允许 pattern 含空格：拼回剩余 tokens（trace/log 用双引号原文更准，此处 join）
        std::string pat = a[i++];
        while (i < a.size()) {
            pat += " ";
            pat += a[i++];
        }
        const bool hit = kind == "log" ? logContains(pat.c_str()) : trace::contains(pat.c_str());
        if (hit == !negate) {
            pass();
        } else {
            std::string last;
            if (kind == "log") {
                last = logLastMatch(pat.c_str());
            } else {
                trace::lastMatch(pat.c_str(), last);
            }
            fail(kind, pat + (last.empty() ? " (no sample)" : " | sample: " + last));
        }
        return true;
    }
    err = "assert: unknown kind " + kind;
    return false;
}

// ---- 流程 ----
// [NEW] trace.clear：清空 trace 环形缓冲（脚本断言"某一段之后不再发生 X"的必要前置；
//   无原版对应，测试辅助。L1 场景 308_go_panel_surrender 使用）
bool cmdTraceClear(Application&, const std::vector<std::string>&, std::string&) {
    trace::clear();
    RICH4_LOGI("debug: trace cleared");
    return true;
}

bool cmdQuit(Application& app, const std::vector<std::string>& a, std::string&) {
    const int code = a.size() > 1 ? parseInt(a[1]) : 0;
    RICH4_LOGI("debug: quit code=%d asserts=%d failures=%d", code, g_asserts, g_failures);
    g_doneAll = true;
    trace::logf("test done asserts=%d failures=%d", g_asserts, g_failures);
    RICH4_LOGI("TEST SUMMARY asserts=%d failures=%d -> %s", g_asserts, g_failures,
               g_failures == 0 ? "PASS" : "FAIL");
    app.quit();
    return true;
}

bool cmdShot(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "shot <path>")) return false;
    app.surface().saveBmp(a[1].c_str());
    RICH4_LOGI("debug: shot saved %s", a[1].c_str());
    return true;
}

bool cmdDump(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "dump <player|estate|corp|state> [i]")) return false;
    GameState& st = app.gameState();
    const std::string what = a[1];
    if (what == "player" && a.size() > 2) {
        const int p = parseInt(a[2]);
        if (p < 0 || p >= 9) {
            err = "dump player: bad index";
            return false;
        }
        const Player& pl = st.players[p];
        RICH4_LOGI("dump player %d: cash=%d bank=%d loan=%d points=%u alive=%u stateFlags=%08X "
                   "byte54=%u state37=%u god=%u cell=%u travel=%u dice=%u",
                   p, pl.cash, pl.bank, pl.loan, pl.points, pl.alive, pl.stateFlags, pl.byte54,
                   pl.state37, pl.cellTableIdx, pl.cellEntId, pl.travel, pl.diceCount);
        return true;
    }
    if (what == "estate" && a.size() > 2) {
        const int i = parseInt(a[2]);
        if (i < 1 || i >= static_cast<int>(st.estates.size())) {
            err = "dump estate: bad index";
            return false;
        }
        const Estate& es = st.estates[i];
        RICH4_LOGI("dump estate %d: owner=%u level=%u type=%u flag=%u priceBase=%u fees=%u,%u,%u,"
                   "%u,%u,%u expire=%u",
                   i, es.owner, es.level, es.type, es.flag, es.priceBase, es.fees[0], es.fees[1],
                   es.fees[2], es.fees[3], es.fees[4], es.fees[5], es.expireDate);
        return true;
    }
    if (what == "corp" && a.size() > 2) {
        const int i = parseInt(a[2]);
        if (i < 1 || i >= static_cast<int>(st.corps.size())) {
            err = "dump corp: bad index";
            return false;
        }
        const Corp& cp = st.corps[i];
        RICH4_LOGI("dump corp %d: owner=%u type=%u sub=%u flag=%u fund=%d lastFee=%d", i, cp.owner,
                   cp.type, cp.sub, cp.flag, 0, cp.lastFee);
        return true;
    }
    if (what == "specpt" && a.size() > 2) {
        const int i = parseInt(a[2]);
        if (i < 1 || i >= static_cast<int>(st.specPts.size())) {
            err = "dump specpt: bad index";
            return false;
        }
        const SpecPt& sp = st.specPts[i];
        RICH4_LOGI("dump specpt %d: owner=%u stockNo=%d costType=%u fund=%d feeBase=%u "
                   "capital=%d sharesLeft=%d",
                   i, sp.owner, sp.stockNo, sp.costType, sp.fund, sp.feeBase, sp.capital,
                   sp.sharesLeft);
        return true;
    }
    if (what == "state") {
        RICH4_LOGI("dump state: turn=%d day=%d date=%u players=%u cur=%d moneyMul=%d humans=%d "
                   "publicFund=%d selected=%u",
                   st.turnCounter, st.dayCount, st.gameDate, st.playerCount, st.currentPlayer,
                   st.moneyMul, st.humanCount, st.publicFund, g_selected);
        return true;
    }
    err = "dump: unknown target";
    return false;
}

// ---- 内联直投（click 下一帧 move、再下一帧 down/up，保证"先悬停后点击"与真实鼠标一致） ----
struct QueuedInput {
    int frame;
    int x;
    int y;
    bool right;
    bool isMove;
    int kind = 0;  // [NEW] 0=down+up（原 click 语义）1=仅按下（按住不放）2=仅释放
};
std::vector<QueuedInput> g_queue;
int g_inputTick = 0;
// [NEW] 最近一次直投的指针位置（release 无参时在该点合成 UP，模拟"按住后松开"）
int g_lastPointerX = 0;
int g_lastPointerY = 0;

// [NEW M4-D] 合成鼠标事件派发：脚本/region 坐标为 640 基准（原版语义）——
//   栈顶为 640 基准模态时 +base 转画布坐标后走 dispatchModalAware（事件 -base +
//   绘制 origin 包裹）；游戏内/非模态 base=0 直通（与原 synthetic dispatch 等价）。
void dispatchSyntheticMouse(Application& app, SDL_Event& ev, int lx, int ly) {
    const int base = (app.events().depth() > 0 && app.events().topCenterBase())
                         ? uiModalBaseX(app.surface())
                         : 0;
    switch (ev.type) {
        case SDL_EVENT_MOUSE_MOTION:
            ev.motion.x = static_cast<float>(lx + base);
            ev.motion.y = static_cast<float>(ly);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            ev.button.x = static_cast<float>(lx + base);
            ev.button.y = static_cast<float>(ly);
            break;
        default:
            break;
    }
    app.dispatchModalAware(&ev);
}

void queueInput(int x, int y, bool right, bool isMove, int kind = 0) {
    g_lastPointerX = x;
    g_lastPointerY = y;
    g_queue.push_back({g_inputTick + 1, x, y, right, true, 0});   // 先 move
    if (!isMove) {
        g_queue.push_back({g_inputTick + 2, x, y, right, false, kind}); // 再按/放/both
    }
}

void dispatchQueuedInputs(Application& app) {
    ++g_inputTick;
    while (!g_queue.empty() && g_queue.front().frame <= g_inputTick) {
        const QueuedInput q = g_queue.front();
        g_queue.erase(g_queue.begin());
        app.setMouseOverride(q.x, q.y);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = static_cast<float>(q.x);
        ev.motion.y = static_cast<float>(q.y);
        dispatchSyntheticMouse(app, ev, q.x, q.y);
        if (!q.isMove) {
            const bool down = q.kind == 0 || q.kind == 1;
            const bool up = q.kind == 0 || q.kind == 2;
            if (down) {
                ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                ev.button.button = q.right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                ev.button.clicks = 1;
                ev.button.x = static_cast<float>(q.x);
                ev.button.y = static_cast<float>(q.y);
                if (!q.right) {
                    app.setUiClicked(); // 与真实左键按下语义一致（转盘提前停等 consumeUiClick）
                }
                dispatchSyntheticMouse(app, ev, q.x, q.y);
            }
            if (up) {
                ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
                ev.button.button = q.right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                ev.button.x = static_cast<float>(q.x);
                ev.button.y = static_cast<float>(q.y);
                dispatchSyntheticMouse(app, ev, q.x, q.y);
            }
        }
    }
}

// ---- 面板查找（脚本免硬编码索引；命中设 g_selected，未中置 0 由 assert 判定） ----
bool cmdFindEstate(Application& app, const std::vector<std::string>& a, std::string& err) {
    GameState& st = app.gameState();
    const int wantOwner = a.size() > 1 ? parseInt(a[1]) : -1;
    const int wantType = a.size() > 2 ? parseInt(a[2]) : -1;
    g_selected = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (wantOwner < 0 || es.owner == wantOwner) {
            (void)wantType;
            g_selected = static_cast<uint16_t>(2000 + i);
            RICH4_LOGI("debug: find.estate -> %zu owner=%u type=%u level=%u", i, es.owner, es.type,
                       es.level);
            return true;
        }
    }
    (void)err;
    RICH4_LOGI("debug: find.estate -> none (owner=%d type=%d)", wantOwner, wantType);
    return true;
}

// ---- 事件格查找（occMask 低字节 cellType；直接得 cellEnt id，land 可无参用） ----
bool cmdFindEvtCell(Application& app, const std::vector<std::string>& a, std::string& err) {
    GameState& st = app.gameState();
    if (!argCount(a, 1, err, "find.evtcell <type>")) return false;
    const int want = parseInt(a[1]);
    g_selected = 0;
    for (size_t i = 1; i < st.cellEnts.size(); ++i) {
        if (static_cast<int>(st.cellEnts[i].occMask & 0xFF) == want) {
            g_selected = static_cast<uint16_t>(i);
            RICH4_LOGI("debug: find.evtcell type=%d -> cell %zu", want, i);
            return true;
        }
    }
    RICH4_LOGI("debug: find.evtcell type=%d -> none", want);
    return true;
}

// [NEW] surrender：當前玩家投降 0x411AE0（淘汰+死神召喚選受害者）
bool cmdSurrender(Application& app, const std::vector<std::string>&, std::string&) {
    // [FIX 234] 經 surrenderRequest 標誌交主循環 tick 處理（0x411AE0 真人路徑），
    //   直呼 surrenderPlayer 會在腳本 step 內嵌套 runModal（死神對話框收不到 TIMER）
    app.gameState().surrenderRequest = true;
    return true;
}

// [NEW] player.bankrupt <p>：指定玩家破产淘汰（0x40CD87 全链：FLC555/台词/终局判定），
//   同 surrender 经 tick 处理避免脚本 step 内嵌套模态/阻塞动画
bool cmdPlayerBankrupt(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "player.bankrupt <p>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 4) {
        err = "player.bankrupt: bad index";
        return false;
    }
    app.gameState().bankruptRequest = p;
    return true;
}

// [NEW] player.spawn <p>：直接入場定位（spawnPlayerAt 0x40829D 純狀態部分，無跳傘演出）
bool cmdPlayerSpawn(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "player.spawn <p>")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.spawn: bad index";
        return false;
    }
    st.pendingSpawnPlayer = 0;  // 構造用：跳過跳傘演出的串行閘（正常由 beginPlayerTurn 清空）
    spawnPlayerAt(st, p);
    // 構造用補全：跳傘 FLC 收尾才寫的 sprite/alive 直接置位（=已入場可選為目標/死神候選）
    Player& sp = st.players[p];
    sp.spriteX = st.cellEnts[sp.cellEntId].x;
    sp.spriteY = st.cellEnts[sp.cellEntId].y;
    sp.alive = static_cast<uint8_t>(sp.alive | 1);
    st.pendingSpawnPlayer = 0;
    RICH4_LOGI("debug: player.spawn %d -> cell %u alive=%u", p, st.players[p].cellEntId,
               st.players[p].alive);
    return true;
}

// [NEW] find.lab：找 type==4 行業設施點(研究所) corp 並選中（selected=4000+ci）
bool cmdFindLab(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].type == 4) {
            g_selected = static_cast<uint16_t>(4000 + static_cast<int>(i));
            RICH4_LOGI("debug: find.lab -> corp %zu (selected=%u)", i, g_selected);
            return true;
        }
    }
    err = "find.lab: no type4 corp";
    return false;
}

bool cmdStockBuy(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 3, err, "stock.buy <p> <stock> <n>")) return false;
    const int changed = buyStock(app, parseInt(a[1]), parseInt(a[2]), parseInt(a[3]), false);
    RICH4_LOGI("debug: stock.buy p=%s stock=%s n=%s -> control=%d", a[1].c_str(), a[2].c_str(),
               a[3].c_str(), changed);
    return true;
}

// ---- 音乐机制调试（0x454D91/0x4549CF/0x454BCC/0x41CF67 系列；trace 断言续播/轮播，
//      产品完整路径由实机验证）----
bool cmdMusicNext(Application& app, const std::vector<std::string>&, std::string&) {
    app.audio().playNextMusic(); // [RE 0x454D91 a1==0] 持久游标 +1&7
    return true;
}
bool cmdMusicScene(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "music.scene <idx>")) return false;
    if (!app.audio().playSceneMusic(parseInt(a[1]))) {
        err = "music.scene blocked (switchDays/volume/open)"; // [RE 0x4549CF 门控]
        return false;
    }
    return true;
}
bool cmdMusicResume(Application& app, const std::vector<std::string>&, std::string&) {
    app.audio().resumeSceneMusic(); // [RE 0x454BCC] play mid from <pos> 续播
    return true;
}
bool cmdMusicDays(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "music.days <v>")) return false;
    app.audio().setSwitchDays(parseInt(a[1])); // [RE word_46CB06] g_musicTimer
    return true;
}

bool cmdFindRoad(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 首个可拾取道路格（mapHitRegions 中 id<2000 者——0xFFFF00 占用格原版不登记，
    // 天然排除站人/已占格；供地雷/路障/炸弹放置目标）
    (void)app;
    (void)a;
    (void)err;
    GameState& st = app.gameState();
    g_selected = 0;
    for (const auto& hr : st.mapHitRegions) {
        if (hr.id >= 1 && hr.id < 2000) {
            g_selected = hr.id;
            RICH4_LOGI("debug: find.road -> cell %u", g_selected);
            return true;
        }
    }
    RICH4_LOGI("debug: find.road -> none in view");
    return true;
}

bool cmdConfirm(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 对当前栈顶模态直接应答（YES=1/NO=0，对应 0x401966 postModalExit lParam）
    (void)err;
    if (a.size() < 2) {
        return false;
    }
    if (a[1] == "yes") {
        app.events().requestExit(1);
    } else if (a[1] == "no") {
        app.events().requestExit(0);
    } else {
        return false;
    }
    RICH4_LOGI("debug: confirm %s", a[1].c_str());
    return true;
}

// ---- sel.*：作用于当前 select 目标（脚本免算索引）----
int selIndex() {
    return g_selected >= 2000 && g_selected < 4000 ? g_selected - 2000 : -1;
}

bool cmdSelOwner(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "sel.owner <p>")) return false;
    const int i = selIndex();
    if (i < 1 || i >= static_cast<int>(app.gameState().estates.size())) {
        err = "sel.owner: no estate selected";
        return false;
    }
    app.gameState().estates[i].owner = static_cast<uint8_t>(clampi(parseInt(a[1]), 0, 4));
    buildMiniMapMarks(app);
    return true;
}

bool cmdSelLevel(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "sel.level <n>")) return false;
    const int i = selIndex();
    if (i < 1 || i >= static_cast<int>(app.gameState().estates.size())) {
        err = "sel.level: no estate selected";
        return false;
    }
    app.gameState().estates[i].level = static_cast<uint8_t>(clampi(parseInt(a[1]), 0, 5));
    return true;
}

bool cmdSelType(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "sel.type <0|1>")) return false;
    const int i = selIndex();
    if (i < 1 || i >= static_cast<int>(app.gameState().estates.size())) {
        err = "sel.type: no estate selected";
        return false;
    }
    app.gameState().estates[i].type = static_cast<uint8_t>(parseInt(a[1]) != 0 ? 1 : 0);
    return true;
}

bool cmdSelFlag(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "sel.flag <v>")) return false;
    const int i = selIndex();
    if (i < 1 || i >= static_cast<int>(app.gameState().estates.size())) {
        err = "sel.flag: no estate selected";
        return false;
    }
    app.gameState().estates[i].flag = static_cast<uint8_t>(parseInt(a[1]));
    return true;
}

bool cmdSpecPtType(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "specpt.type <i> <costType>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.specPts.size())) {
        err = "specpt.type: index out of range";
        return false;
    }
    st.specPts[i].costType = static_cast<uint8_t>(parseInt(a[2]));
    return true;
}

bool cmdSpecPtFund(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "specpt.fund <i> <v>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.specPts.size())) {
        err = "specpt.fund: index out of range";
        return false;
    }
    st.specPts[i].fund = parseInt(a[2]);
    return true;
}

bool cmdSelGoto(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 当前玩家站到 select 目标对应的 cellEnt（不触发结算；配合 card.rebuild 等"脚下地"命令）
    GameState& st = app.gameState();
    if (g_selected == 0) {
        err = "sel.goto: nothing selected";
        return false;
    }
    const int p = a.size() > 1 ? parseInt(a[1]) : curPlayer(app);
    uint16_t cell = 0;
    if (g_selected < 2000) {
        cell = g_selected;
    } else {
        for (size_t i = 1; i < st.cellEnts.size(); ++i) {
            if (st.cellEnts[i].special == g_selected) {
                cell = static_cast<uint16_t>(i);
                break;
            }
        }
    }
    if (cell < 1 || cell >= static_cast<int>(st.cellEnts.size()) || p < 0 || p >= 9) {
        err = "sel.goto: no target cell";
        return false;
    }
    teleportPlayerToCell(st, p, cell);  // [NEW] 合一：含 prev/dir 朝向刷新
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: sel.goto p%d -> cell %u dir=%u", p, cell, st.players[p].dir);
    return true;
}

bool cmdCardRebuild(Application& app, const std::vector<std::string>&, std::string& err) {
    const int r = cardRebuildEffect(app); // [RE 0x44309B] 脚下格改建（与 Ctrl+4/卡片栏同路径）
    RICH4_LOGI("debug: card.rebuild -> %d", r);
    (void)err;
    return true;
}

bool cmdNpcDump(Application& app, const std::vector<std::string>&, std::string& err) {
    GameState& st = app.gameState();
    for (int i = 0; i < 5; ++i) {
        const NpcSlot80& s = st.npcSlots[i];
        RICH4_LOGI("dump npc %d: busy=%u status=0x%02X bailer=%u cell=%u", i + 4, s.busy, s.status,
                   s.bailer, s.cell);
    }
    (void)err;
    return true;
}

bool cmdNpcRelease(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "npc.release <npc 4..7> [fromJail=1]")) return false;
    const int npc = parseInt(a[1]); // [RE Ctrl+Shift+9 同款] releaseEventNpc 传 4..7
    const bool fromJail = a.size() > 2 ? parseInt(a[2]) != 0 : true;
    if (npc < 4 || npc > 7) {
        err = "npc.release: index must be 4..7";
        return false;
    }
    releaseEventNpc(app, npc, fromJail);
    return true;
}

// ---- named region：产品侧一次性登记控件矩形，脚本 clickr <name> 免坐标脆断 ----
// 存储/实现见文件底部对外 API 区（registerRegion 需外部链接）；此处仅消费。
bool cmdClickR(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 1, err, "clickr <region>")) return false;
    auto it = g_regions.find(a[1]);
    if (it == g_regions.end()) {
        err = "clickr: unknown region " + a[1];
        return false;
    }
    const int cx = it->second[0] + it->second[2] / 2;
    const int cy = it->second[1] + it->second[3] / 2;
    trace::logf("clicked name=%s at=(%d,%d)", a[1].c_str(), cx, cy);
    queueInput(cx, cy, false, false);
    return true;
}

bool cmdEstateFlag(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "estate.flag <i> <v>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.estates.size())) {
        err = "estate.flag: index out of range";
        return false;
    }
    st.estates[i].flag = static_cast<uint8_t>(parseInt(a[2]));
    return true;
}

bool cmdUseGod(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 请神符 0x444E1A / 送神符 0x444C45（先塞卡走完整路径，同 Ctrl+Shift+I/H）
    if (!argCount(a, 1, err, "use.god invite|banish")) return false;
    GameState& st = app.gameState();
    const int cur = curPlayer(app);
    if (a[1] == "invite") {
        giveCardToBag(st, cur, 23);
        const bool ok = useInviteGodCard(app);
        RICH4_LOGI("debug: use.god invite -> %d", ok ? 1 : 0);
        return true;
    }
    if (a[1] == "banish") {
        giveCardToBag(st, cur, 22);
        const bool ok = useBanishGodCard(app);
        RICH4_LOGI("debug: use.god banish -> %d", ok ? 1 : 0);
        return true;
    }
    err = "use.god: invite|banish";
    return false;
}

bool cmdTap(Application&, const std::vector<std::string>& a, std::string& err) {
    // [NEW] 通用"人工干预一下"：对当前阻塞模态任意处合成一次左键
    //   （老虎机 0x43F767 任意左键停滚 / 转盘 0x44090E 点击提前停 / 其余面板中心兜底）
    (void)err;
    const int x = a.size() > 1 ? parseInt(a[1]) : 320;
    const int y = a.size() > 2 ? parseInt(a[2]) : 240;
    queueInput(x, y, false, false);
    trace::logf("clicked tap at=(%d,%d)", x, y);
    return true;
}

bool cmdUseItem(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "use.item <id 1..14>")) return false;
    const int id = parseInt(a[1]);
    if (id < 1 || id > 14) {
        err = "use.item: bad id";
        return false;
    }
    trace::logf("use item id=%d p=%d", id, curPlayer(app));
    const int r = itemEffect(app, id); // [RE 0x475DD4] 0=取消（内部 itemBagRemove）
    RICH4_LOGI("debug: use.item %d -> %d", id, r);
    return true;
}

bool cmdClickCell(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 与 clicksel 同法：在 hr(id==格 id) 内扫 pickMapObject 真实命中的菱形点，
    // 不可见时回退格锚点（由脚本断言暴露落空）
    if (!argCount(a, 1, err, "clickcell <i>")) return false;
    GameState& st = app.gameState();
    const int i = parseInt(a[1]);
    if (i < 1 || i >= static_cast<int>(st.cellEnts.size())) {
        err = "clickcell: bad index";
        return false;
    }
    int bx = -1;
    int by = -1;
    int best = 1 << 30;
    for (const auto& hr : st.mapHitRegions) {
        if (hr.id != i || hr.w <= 0 || hr.h <= 0) {
            continue;
        }
        const int cx = hr.x + hr.w / 2;
        const int cy = hr.y + hr.h / 2;
        for (int y2 = hr.y; y2 < hr.y + hr.h; y2 += 4) {
            for (int x2 = hr.x; x2 < hr.x + hr.w; x2 += 4) {
                if (pickMapObject(st, x2, y2) == i) {
                    const int d = (x2 - cx) * (x2 - cx) + (y2 - cy) * (y2 - cy);
                    if (d < best) {
                        best = d;
                        bx = x2;
                        by = y2;
                    }
                }
            }
        }
    }
    if (bx < 0) {
        bx = st.cellEnts[i].x + 24;
        by = st.cellEnts[i].y + 12;
        RICH4_LOGW("debug: clickcell %d not in hit list, fallback (%d,%d)", i, bx, by);
    }
    queueInput(bx, by, false, false);
    trace::logf("clicked clickcell %d at=(%d,%d)", i, bx, by);
    return true;
}

bool cmdClickPlayer(Application& app, const std::vector<std::string>& a, std::string& err) {
    // 点玩家头像：优先扫 mapHitRegions 的 0xF000|p 命中矩形（与选人 filter 同源），
    // 无命中区（不在视野）则回退 sprite 坐标（可能落空，由调用方 assert 暴露）
    if (!argCount(a, 1, err, "clickplayer <p>")) return false;
    GameState& st = app.gameState();
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "clickplayer: bad index";
        return false;
    }
    const uint16_t want = static_cast<uint16_t>(0xF000 | (p & 0xF));
    int bx = -1;
    int by = -1;
    int best = 1 << 30;
    for (const auto& hr : st.mapHitRegions) {
        if (hr.id != want || hr.w <= 0 || hr.h <= 0) {
            continue;
        }
        const int cx = hr.x + hr.w / 2;
        const int cy = hr.y + hr.h / 2;
        for (int y2 = hr.y; y2 < hr.y + hr.h; y2 += 4) {
            for (int x2 = hr.x; x2 < hr.x + hr.w; x2 += 4) {
                if (pickMapObject(st, x2, y2) == want) {
                    const int d = (x2 - cx) * (x2 - cx) + (y2 - cy) * (y2 - cy);
                    if (d < best) {
                        best = d;
                        bx = x2;
                        by = y2;
                    }
                }
            }
        }
    }
    if (bx < 0) {
        bx = st.players[p].spriteX + 24;
        by = st.players[p].spriteY + 12;
        RICH4_LOGW("debug: clickplayer %d not in view hit list, fallback sprite (%d,%d)", p, bx,
                   by);
    }
    queueInput(bx, by, false, false);
    trace::logf("clicked clickplayer %d at=(%d,%d)", p, bx, by);
    return true;
}

typedef int (*CardEffectFn)(Application&);
struct CardAlias {
    const char* name;
    CardEffectFn fn;
};
// [RE 0x475D5C] g_cardEffectFuncs 直调表（跳过选卡 UI；内部自扣卡需先 give.card）
const CardAlias kCardAliases[] = {
    {"rich", cardEffectEqualRich},      {"poor", cardEffectEqualPoor},
    {"buyLand", cardEffectBuyLand},     {"swapLand", cardEffectSwapLand},
    {"swapHouse", cardEffectSwapHouse}, {"turn", cardEffectTurn},
    {"auction", cardEffectAuction},     {"monster", cardEffectMonster},
    {"demolish", cardEffectDemolish},   {"steal", cardEffectSteal},
    {"angel", cardEffectAngel},         {"devil", cardEffectDevil},
    {"stay", cardEffectStay},           {"hibernate", cardEffectHibernate},
    {"sleepwalk", cardEffectSleepwalk}, {"frame", cardEffectFrame},
    {"tax", cardEffectTaxAudit},        {"priceUp", cardEffectPriceUp},
    {"seal", cardEffectSeal},           {"ally", cardEffectAlly},
    {"turtle", cardEffectTurtle},       {"redStock", cardEffectRedStock},
    {"blackStock", cardEffectBlackStock},
};

bool cmdUseCard(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 1, err, "use.card <alias> (rich/poor/auction/.../turtle)")) return false;
    for (const auto& c : kCardAliases) {
        if (a[1] == c.name) {
            trace::logf("use card name=%s p=%d", c.name, curPlayer(app));
            const int r = c.fn(app);
            RICH4_LOGI("debug: use.card %s -> %d", c.name, r);
            return true;
        }
    }
    err = "use.card: unknown alias " + a[1];
    return false;
}

bool cmdClickSel(Application& app, const std::vector<std::string>&, std::string& err) {
    // 点当前 select 目标：在渲染期命中矩形内**扫描出 pick 真正等于目标**的最近掩码点
    // （mapHitRegions 带 SPR 像素掩码，bounding-rect 中心可能落在掩码外 → hover 不置位）
    GameState& st = app.gameState();
    if (g_selected == 0) {
        err = "clicksel: nothing selected";
        return false;
    }
    for (const auto& hr : st.mapHitRegions) {
        if (hr.id != g_selected || hr.w <= 0 || hr.h <= 0) {
            continue;
        }
        const int cx = hr.x + hr.w / 2;
        const int cy = hr.y + hr.h / 2;
        int bx = -1;
        int by = -1;
        int best = 1 << 30;
        for (int y2 = hr.y; y2 < hr.y + hr.h; y2 += 4) {
            for (int x2 = hr.x; x2 < hr.x + hr.w; x2 += 4) {
                if (pickMapObject(st, x2, y2) == g_selected) {
                    const int d = (x2 - cx) * (x2 - cx) + (y2 - cy) * (y2 - cy);
                    if (d < best) {
                        best = d;
                        bx = x2;
                        by = y2;
                    }
                }
            }
        }
        if (bx >= 0) {
            queueInput(bx, by, false, false);
            trace::logf("clicked clicksel id=%u at=(%d,%d)", g_selected, bx, by);
            return true;
        }
        // 目标在 hit list 但掩码无命中点：极罕见，保持报错
        err = "clicksel: no mask-hit point found in rect";
        return false;
    }
    // 目标不在当前视野命中列表：多为**上一次点击已生效、模态已关**（双点规则下常见）→
    // 宽容跳过并记 trace；真未选中由后续效果断言暴露
    trace::logf("clicksel skip id=%u (not in hit list)", g_selected);
    return true;
}

bool cmdClick(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 2, err, "click <x> <y>")) return false;
    queueInput(parseInt(a[1]), parseInt(a[2]), false, false);
    return true;
}

// [NEW] presssel：在 clicksel 同源的选中物件命中点合成**单 DOWN（按住不放）**——
//   验证"按住查看物件提示 → 演出/动画吞掉抬起 → 提示不残留"（release 在上次指针处补 UP）
bool cmdPressSel(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)a;
    GameState& st = app.gameState();
    if (g_selected == 0) {
        err = "presssel: nothing selected";
        return false;
    }
    for (const auto& hr : st.mapHitRegions) {
        if (hr.id != g_selected || hr.w <= 0 || hr.h <= 0) {
            continue;
        }
        const int cx = hr.x + hr.w / 2;
        const int cy = hr.y + hr.h / 2;
        int bx = -1;
        int by = -1;
        int best = 1 << 30;
        for (int y2 = hr.y; y2 < hr.y + hr.h; y2 += 4) {
            for (int x2 = hr.x; x2 < hr.x + hr.w; x2 += 4) {
                if (pickMapObject(st, x2, y2) == g_selected) {
                    const int d = (x2 - cx) * (x2 - cx) + (y2 - cy) * (y2 - cy);
                    if (d < best) {
                        best = d;
                        bx = x2;
                        by = y2;
                    }
                }
            }
        }
        if (bx >= 0) {
            queueInput(bx, by, false, false, 1);
            trace::logf("pressed presssel id=%u at=(%d,%d)", g_selected, bx, by);
            return true;
        }
        err = "presssel: no mask-hit point found in rect";
        return false;
    }
    err = "presssel: selected id not in hit list";
    return false;
}

// [NEW] press/release：单 DOWN（按住）/ 单 UP（松开），验证"按住物件提示 + 演出吞抬起"
bool cmdPress(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 2, err, "press <x> <y>")) return false;
    queueInput(parseInt(a[1]), parseInt(a[2]), false, false, 1);
    return true;
}
bool cmdRelease(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 0, err, "release")) return false;
    queueInput(g_lastPointerX, g_lastPointerY, false, false, 2);
    return true;
}

bool cmdRClick(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 2, err, "rclick <x> <y>")) return false;
    queueInput(parseInt(a[1]), parseInt(a[2]), true, false);
    return true;
}

bool cmdMove(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 2, err, "move <x> <y>")) return false;
    queueInput(parseInt(a[1]), parseInt(a[2]), false, true);
    return true;
}

bool cmdKey(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 1, err, "key <spec> (e.g. ctrl+shift+g)")) return false;
    synthesizeKey(app, a[1]);
    return true;
}

// ---- 打开面板（阻塞 UI，供 named region 步骤铺开；先接常用几个） ----
bool cmdOpenStock(Application& app, const std::vector<std::string>&, std::string&) {
    stockMarketDialog(app);
    return true;
}
bool cmdOpenQuery(Application& app, const std::vector<std::string>&, std::string&) {
    queryDialog(app);
    return true;
}

// ============================ 命令表 ============================
// clang-format off
const Cmd kCmds[] = {
    {"select", cmdSelect},
    {"estate.owner", cmdEstateOwner},
    {"estate.level", cmdEstateLevel},
    {"estate.type", cmdEstateType},
    {"estate.flag", cmdEstateFlag},
    {"use.god", cmdUseGod},
    {"corp.build", cmdCorpBuild},
    {"corp.sub", cmdCorpSub},
    {"corp.owner", cmdCorpOwner},
    {"corp.level", cmdCorpLevel},
    {"specpt.owner", cmdSpecPtOwner},
    {"specpt.type", cmdSpecPtType},
    {"all.owned", cmdAllOwned},
    {"all.level1", cmdAllLevel1},
    {"player.cash", cmdPlayerCash},
    {"player.bank", cmdPlayerBank},
    {"player.advance", cmdPlayerAdvance},
    {"player.points", cmdPlayerPoints},
    {"player.state", cmdPlayerState},
    {"player.god", cmdPlayerGod},
    {"player.vehicle", cmdPlayerVehicle},
    {"player.teleport", cmdPlayerTeleport},
    {"player.dice", cmdPlayerDice},
    {"player.alive", cmdPlayerAlive},
    {"player.bankrupt", cmdPlayerBankrupt},
    {"give.card", cmdGiveCard},
    {"give.item", cmdGiveItem},
    {"land", cmdLand},
    {"roll", cmdRoll},
    {"day", cmdDay},
    {"victory", cmdVictory},
    {"holiday", cmdHoliday},
    // [NEW] 热键收口组（docs/debug-keys.md v2 键位映射目标）
    {"get.land", cmdGetLand},
    {"get.street", cmdGetStreet},
    {"clear.cell", cmdClearCell},
    {"clear.street", cmdClearStreet},
    {"get.all", cmdGetAll},
    {"clear.all", cmdClearAll},
    {"level.up", cmdLevelUp},
    {"level.down", cmdLevelDown},
    {"dump.pointed", cmdDumpPointed},
    {"dir.cycle", cmdDirCycle},
    {"obj.createAhead", cmdObjCreateAhead},
    {"obj.kick", cmdObjKick},
    {"player.give", cmdPlayerGive},
    {"refill.cards", cmdRefillCards},
    {"items.all", cmdItemsAll},
    {"lottery.fixwin", cmdLotteryFixwin},
    {"dump.celltable", cmdDumpCellTable},
    {"find.estate", cmdFindEstate},
    {"find.evtcell", cmdFindEvtCell},
    {"find.lab", cmdFindLab},
    {"player.spawn", cmdPlayerSpawn},
    {"surrender", cmdSurrender},
    {"find.road", cmdFindRoad},
    {"stock.buy", cmdStockBuy},
    {"music.next", cmdMusicNext},
    {"music.scene", cmdMusicScene},
    {"music.resume", cmdMusicResume},
    {"music.days", cmdMusicDays},
    {"sel.owner", cmdSelOwner},
    {"sel.level", cmdSelLevel},
    {"sel.type", cmdSelType},
    {"sel.flag", cmdSelFlag},
    {"sel.goto", cmdSelGoto},
    {"specpt.fund", cmdSpecPtFund},
    {"card.rebuild", cmdCardRebuild},
    {"npc.dump", cmdNpcDump},
    {"npc.release", cmdNpcRelease},
    {"confirm", cmdConfirm},
    {"jail", cmdJail},
    {"hosp", cmdHosp},
    {"rng", cmdRng},
    {"settings.anim", cmdSettingsAnim},
{"settings.layout", cmdSettingsLayout},
    {"seed", cmdSeed},
    {"assert", cmdAssert},
    {"trace.clear", cmdTraceClear},
    {"quit", cmdQuit},
    {"shot", cmdShot},
    {"dump", cmdDump},
    {"click", cmdClick},
    {"clickr", cmdClickR},
    {"press", cmdPress},
    {"release", cmdRelease},
    {"tap", cmdTap},
    {"use.item", cmdUseItem},
    {"use.card", cmdUseCard},
    {"clickcell", cmdClickCell},
    {"clickplayer", cmdClickPlayer},
    {"clicksel", cmdClickSel},
    {"presssel", cmdPressSel},
    {"rclick", cmdRClick},
    {"move", cmdMove},
    {"key", cmdKey},
    {"open.stock", cmdOpenStock},
    {"open.query", cmdOpenQuery},
};
// clang-format on

CmdFn findCmd(const std::string& name) {
    for (const auto& c : kCmds) {
        if (name == c.name) {
            return c.fn;
        }
    }
    return nullptr;
}

uint16_t resolveTargetCell(Application& app) {
    GameState& st = app.gameState();
    if (g_selected != 0) {
        if (g_selected < 2000) {
            return g_selected;
        }
        for (size_t i = 1; i < st.cellEnts.size(); ++i) {
            if (st.cellEnts[i].special == g_selected) {
                return static_cast<uint16_t>(i);
            }
        }
    }
    return 0;
}

bool isBlockingCmd(const std::string& name) {
    // 内部会 runModal 阻塞（弹框/动画）的命令：阻塞期间重入 tick 只放行交互 step
    // （click/confirm 等），其它 step 冻结 pc——保证断言严格晚于阻塞命令完成
    static const char* const kBlock[] = {"land",  "day",        "news",       "fate",    "magic",
                                         "lottery.draw",       "lottery.fixwin", "settle", "dividend",
                                         "shop",               "bank",           "jail",   "hosp",
                                         "open.stock",         "open.query"};
    for (const char* b : kBlock) {
        if (name == b) {
            return true;
        }
    }
    return false;
}

bool isInteractiveCmd(const std::string& name) {
    // 阻塞模态期间仍需可执行的应答类命令（点地图/选目标也属应答）
    static const char* const kInter[] = {"click",     "clickr",  "rclick",    "move",
                                         "key",       "confirm", "tap",       "clickcell",
                                         "clickplayer", "clicksel", "press",  "presssel",
                                         "release"};
    for (const char* i : kInter) {
        if (name == i) {
            return true;
        }
    }
    return false;
}

bool stepFirstIsBlocking(const std::string& line) {
    auto a = tokenize(line);
    return !a.empty() && isBlockingCmd(a[0]);
}

bool stepFirstIsInteractive(const std::string& line) {
    auto a = tokenize(line);
    return !a.empty() && isInteractiveCmd(a[0]);
}

// 单条命令执行（含事件触发类）
bool execTokenCmd(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (a.empty()) {
        return true;
    }
    if (a[0] == "wait") {
        err = "wait: only valid as a script step";
        return false;
    }
    if (a[0] == "news") {
        // news [idx 0..35]：无参=随机抽取（idx7 拍卖会开交互面板，脚本场景慎用）
        newsDebugFire(app, a.size() > 1 ? parseInt(a[1]) : -1);
        return true;
    }
    if (a[0] == "fate") {
        // fate [idx 0..48]：无参=随机；指定效果号保证脚本确定性（生日/嫁祸=交互面板）
        fateDebugFire(app, a.size() > 1 ? parseInt(a[1]) : -1);
        return true;
    }
    if (a[0] == "magic") {
        magicHouseVisit(app);
        return true;
    }
    if (a[0] == "lottery.draw") {
        lotteryDrawMeeting(app);
        return true;
    }
    if (a[0] == "settle") {
        if (!app.gameState().gameLoopActive) {
            return true;  // 对局外忽略（旧热键 Ctrl+Shift+N 同语义）
        }
        monthlySettle(app);
        return true;
    }
    if (a[0] == "dividend") {
        if (!app.gameState().gameLoopActive) {
            return true;  // 对局外忽略（旧热键 Ctrl+Shift+E 同语义）
        }
        dividendMeeting(app);
        return true;
    }
    if (a[0] == "shop") {
        // 定位百货格（同 Ctrl+Shift+W 语义）
        GameState& st = app.gameState();
        for (size_t c = 0; c < st.cellEnts.size(); ++c) {
            if ((st.cellEnts[c].occMask & 0xFF) == 15 && st.cellEnts[c].special > 6000 &&
                st.cellEnts[c].special < 8000) {
                shopDialog(app, st.cellEnts[c].special);
                return true;
            }
        }
        err = "shop: no department store cell";
        return false;
    }
    if (a[0] == "atm") {
        GameState& st = app.gameState();
        for (size_t i = 1; i < st.specPts.size(); ++i) {
            if (st.specPts[i].costType == 7) {
                bankVisitDialog(app);
                return true;
            }
        }
        err = "atm: no bank cell";
        return false;
    }
    if (a[0] == "bank") {
        GameState& st = app.gameState();
        for (size_t i = 1; i < st.specPts.size(); ++i) {
            if (st.specPts[i].costType == 7) {
                for (size_t c = 0; c < st.cellEnts.size(); ++c) {
                    if (st.cellEnts[c].special == 6000 + static_cast<int>(i)) {
                        bankStayDialog(app, static_cast<int>(c));
                        return true;
                    }
                }
            }
        }
        err = "bank: no bank cell";
        return false;
    }
    if (auto fn = findCmd(a[0])) {
        return fn(app, a, err);
    }
    err = "unknown command: " + a[0];
    return false;
}

} // namespace

// ============================ 对外 API ============================

void setSelectedObject(uint16_t id) { g_selected = id; }
uint16_t selectedObject() { return g_selected; }

void registerRegion(const char* name, int x, int y, int w, int h) {
    g_regions[name] = {x, y, w, h};
}

int failures() { return g_failures; }

std::string reportSummary() {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "TEST asserts=%d failures=%d", g_asserts, g_failures);
    return buf;
}

bool execLine(Application& app, const std::string& line, std::string& err) {
    // 分号并列多命令
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, ';')) {
        auto a = tokenize(part);
        if (a.empty()) {
            continue;
        }
        if (!execTokenCmd(app, a, err)) {
            return false;
        }
    }
    return true;
}

void synthesizeClick(Application& app, int x, int y, bool right, bool downOnly) {
    app.setMouseOverride(x, y);
    if (!right) {
        app.setUiClicked();
    }
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = static_cast<float>(x);
    ev.motion.y = static_cast<float>(y);
    dispatchSyntheticMouse(app, ev, x, y);
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    ev.button.clicks = 1;
    ev.button.x = static_cast<float>(x);
    ev.button.y = static_cast<float>(y);
    dispatchSyntheticMouse(app, ev, x, y);
    if (!downOnly) {
        ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
        dispatchSyntheticMouse(app, ev, x, y);
    }
}

void synthesizeKey(Application& app, const std::string& spec) {
    std::string name = spec;
    SDL_Keymod mod = SDL_KMOD_NONE;
    for (;;) {
        if (name.rfind("ctrl+", 0) == 0) {
            mod |= SDL_KMOD_CTRL;
            name = name.substr(5);
            continue;
        }
        if (name.rfind("shift+", 0) == 0) {
            mod |= SDL_KMOD_SHIFT;
            name = name.substr(6);
            continue;
        }
        break;
    }
    const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
    if (key == SDLK_UNKNOWN) {
        RICH4_LOGW("debug: key unknown spec=%s", spec.c_str());
        return;
    }
    auto pushKey = [&](SDL_Keycode k, uint32_t type, SDL_Keycode raw) {
        SDL_Event ev{};
        ev.type = type;
        ev.key.key = k;
        ev.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
        ev.key.mod = mod;
        (void)raw;
        app.events().dispatch(ev);
    };
    if ((mod & SDL_KMOD_CTRL) != 0) {
        pushKey(SDLK_LCTRL, SDL_EVENT_KEY_DOWN, key);
    }
    if ((mod & SDL_KMOD_SHIFT) != 0) {
        pushKey(SDLK_LSHIFT, SDL_EVENT_KEY_DOWN, key);
    }
    pushKey(key, SDL_EVENT_KEY_DOWN, key);
    pushKey(key, SDL_EVENT_KEY_UP, key);
    if ((mod & SDL_KMOD_SHIFT) != 0) {
        pushKey(SDLK_LSHIFT, SDL_EVENT_KEY_UP, key);
    }
    if ((mod & SDL_KMOD_CTRL) != 0) {
        pushKey(SDLK_LCTRL, SDL_EVENT_KEY_UP, key);
    }
}

// ============================ 脚本解析与执行器 ============================

namespace {

bool isAllDigits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

bool parseWaitStep(const std::vector<std::string>& a, Step& st, std::string& err) {
    // wait frames N | wait trace <pat> [max] | wait log <pat> [max] | wait idle [max]
    st.kind = Step::WaitFrames;
    st.n = 0;
    st.limit = 6000;
    if (a.size() < 2) {
        err = "usage: wait <frames|trace|log|idle> ...";
        return false;
    }
    const std::string sub = a[1];
    if (sub == "frames") {
        if (a.size() < 3) {
            err = "wait frames <n>";
            return false;
        }
        st.n = parseInt(a[2]);
        return true;
    }
    if (sub == "trace" || sub == "log") {
        if (a.size() < 3) {
            err = "wait trace|log <pattern> [maxFrames]";
            return false;
        }
        st.kind = sub == "trace" ? Step::WaitTrace : Step::WaitLog;
        size_t last = a.size() - 1;
        if (last > 2 && isAllDigits(a[last])) {
            st.limit = parseInt(a[last]);
            --last;
        }
        st.a = a[2];
        for (size_t i = 3; i <= last; ++i) {
            st.a += " ";
            st.a += a[i];
        }
        return true;
    }
    if (sub == "idle") {
        st.kind = Step::WaitIdle;
        if (a.size() > 2) {
            st.limit = parseInt(a[2]);
        }
        return true;
    }
    if (sub == "state") {
        // wait state <path> <op> <value> [maxFrames]
        if (a.size() < 5) {
            err = "usage: wait state <path> <op> <value> [max]";
            return false;
        }
        st.kind = Step::WaitState;
        st.a = a[2];
        st.b = a[3];
        st.n = parseInt(a[4]);
        if (a.size() > 5) {
            st.limit = parseInt(a[5]);
        }
        return true;
    }
    err = "wait: unknown sub " + sub;
    return false;
}

bool loadScript(const std::string& path, Script& out, std::string& err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open script: " + path;
        return false;
    }
    char line[1024];
    int lineNo = 0;
    while (std::fgets(line, sizeof(line), f)) {
        ++lineNo;
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
            s.pop_back();
        }
        // 元数据注释
        if (s.size() > 1 && s[0] == '#') {
            std::string meta = s.substr(1);
            while (!meta.empty() && meta.front() == ' ') {
                meta.erase(meta.begin());
            }
            if (meta.rfind("id:", 0) == 0) {
                out.id = meta.substr(3);
                while (!out.id.empty() && out.id.front() == ' ') {
                    out.id.erase(out.id.begin());
                }
            } else if (meta.rfind("timeout:", 0) == 0) {
                out.timeoutFrames = parseInt(meta.substr(8).c_str(), out.timeoutFrames);
            }
            continue;
        }
        const size_t hash = s.find('#');
        if (hash != std::string::npos &&
            (hash == 0 || s[hash - 1] == ' ' || s[hash - 1] == '\t')) {
            // 行首/空白后的 # 视为注释（引号内文本中的 # 不截断，如 BIG5 语音前缀）
            s = s.substr(0, hash);
        }
        auto a = tokenize(s);
        if (a.empty()) {
            continue;
        }
        Step st;
        st.lineNo = lineNo;
        if (a[0] == "wait") {
            if (!parseWaitStep(a, st, err)) {
                std::fclose(f);
                err += " (line " + std::to_string(lineNo) + ")";
                return false;
            }
            st.lineNo = lineNo;
        } else {
            st.kind = Step::Cmd;
            st.a = s;
        }
        out.steps.push_back(st);
    }
    std::fclose(f);
    if (out.steps.empty()) {
        err = "empty script: " + path;
        return false;
    }
    return true;
}

} // namespace

bool hasWork() { return !g_scripts.empty() && g_si < g_scripts.size() && !g_doneAll; }

void initFromCommandLine(Application& app) {
    (void)app;
    g_failures = 0;
    g_asserts = 0;
    g_doneAll = false;
    g_si = 0;
    g_selected = 0;
    g_queue.clear();
}

void addScriptFile(const std::string& path) {
    Script sc;
    sc.id = path;
    std::string err;
    if (!loadScript(path, sc, err)) {
        ++g_failures;
        RICH4_LOGE("script error: %s", err.c_str());
        return;
    }
    if (sc.id == path || sc.id.empty()) {
        // id 用文件名
        const size_t slash = path.find_last_of("/\\");
        sc.id = (slash == std::string::npos) ? path : path.substr(slash + 1);
    }
    g_scripts.push_back(std::move(sc));
}

void addExecLines(const std::string& lines) {
    Script sc;
    sc.id = "exec";
    sc.timeoutFrames = 40000; // [NEW] exec 内联默认上限，防弹框挂死无界运行
    std::stringstream ss(lines);
    std::string part;
    int lineNo = 0;
    while (std::getline(ss, part, ';')) {
        auto a = tokenize(part);
        if (a.empty()) {
            continue;
        }
        Step st;
        st.lineNo = ++lineNo;
        if (a[0] == "wait") {
            std::string err;
            if (!parseWaitStep(a, st, err)) {
                ++g_failures;
                RICH4_LOGE("exec: %s", err.c_str());
                return;
            }
        } else {
            st.kind = Step::Cmd;
            st.a = part;
        }
        sc.steps.push_back(st);
    }
    if (!sc.steps.empty()) {
        g_scripts.push_back(std::move(sc));
    }
}

void finishScript(Application& app, Script& sc, const char* why) {
    RICH4_LOGI("script %s: %s (asserts=%d failures=%d frames=%d)", sc.id.c_str(), why, g_asserts,
               g_failures, sc.frames);
    ++g_si;
    if (g_si >= g_scripts.size()) {
        g_scripts.clear();
        g_si = 0;
        trace::logf("test done asserts=%d failures=%d", g_asserts, g_failures);
        RICH4_LOGI("TEST SUMMARY asserts=%d failures=%d -> %s", g_asserts, g_failures,
                   g_failures == 0 ? "PASS" : "FAIL");
        app.quit();
    }
}

void tick(Application& app) {
    dispatchQueuedInputs(app);
    if (g_doneAll || g_scripts.empty() || g_si >= g_scripts.size()) {
        return;
    }
    if (g_depth > 6) {
        return;
    }
    ++g_depth;
    struct Guard {
        ~Guard() { --g_depth; }
    } guard;

    Script& sc = g_scripts[g_si];
    ++sc.frames;
    if (sc.frames > sc.timeoutFrames) {
        fail("timeout", sc.id + " exceeded " + std::to_string(sc.timeoutFrames) + " frames");
        finishScript(app, sc, "TIMEOUT");
        return;
    }
    if (sc.pc >= sc.steps.size()) {
        finishScript(app, sc, "DONE");
        return;
    }
    Step& st = sc.steps[sc.pc];
    switch (st.kind) {
        case Step::Cmd: {
            // 统一规则（2026-09-28）：**任何** Cmd 执行期间都可能弹台词/框阻塞
            // （cardRebuildEffect→playCardLine、showCardGet 等），故执行前置 g_inBlock；
            // 重入帧只放行交互应答 step（click/key/confirm），其余 step 冻结 pc——
            // 保证"数据修改完成后才允许断言/后续命令"的严格时序。
            if (st.done) {
                ++sc.pc; // 阻塞期前瞻已执行
                return;
            }
            if (g_inBlock && !stepFirstIsInteractive(st.a)) {
                // 受限前瞻：冻结步骤之后 ≤8 格内的交互应答 step 先派发
                // （否则 assert 卡 pc → 关不掉打开的模态 → 互等死锁，110 实证）
                for (size_t j = sc.pc + 1; j < sc.steps.size() && j <= sc.pc + 8; ++j) {
                    Step& nx = sc.steps[j];
                    if (nx.kind != Step::Cmd || nx.done) {
                        continue;
                    }
                    if (!stepFirstIsInteractive(nx.a)) {
                        break;  // [FIX 222] 不可越过未执行的同步步骤（如 use.card 开框）
                    }
                    nx.done = true;
                    std::string perr;
                    if (!execLine(app, nx.a, perr)) {
                        ++g_failures;
                        RICH4_LOGE("script %s line %d: %s (prefetch)", sc.id.c_str(),
                                   nx.lineNo, perr.c_str());
                    }
                    break;
                }
                return;
            }
            ++sc.pc;
            const bool prevInBlock = g_inBlock; // 重入白名单 step 结束后须保持外层阻塞
            g_inBlock = true;
            std::string err;
            if (!execLine(app, st.a, err)) {
                ++g_failures;
                RICH4_LOGE("script %s line %d: %s", sc.id.c_str(), st.lineNo, err.c_str());
            }
            g_inBlock = prevInBlock;
            return;
        }
        case Step::WaitFrames:
            if (++sc.waited >= st.n) {
                sc.waited = 0;
                ++sc.pc;
            }
            return;
        case Step::WaitTrace:
            if (trace::contains(st.a.c_str())) {
                sc.waited = 0;
                ++sc.pc;
            } else if (++sc.waited > st.limit) {
                sc.waited = 0;
                ++sc.pc;
                fail("wait trace", st.a + " (timeout)");
            }
            return;
        case Step::WaitLog:
            if (logContains(st.a.c_str())) {
                sc.waited = 0;
                ++sc.pc;
            } else if (++sc.waited > st.limit) {
                sc.waited = 0;
                ++sc.pc;
                fail("wait log", st.a + " (timeout)");
            }
            return;
        case Step::WaitIdle:
            if (app.gameState().gamePlayerControl) {
                sc.waited = 0;
                ++sc.pc;
            } else if (++sc.waited > st.limit) {
                sc.waited = 0;
                ++sc.pc;
                fail("wait idle", "no player control (timeout)");
            }
            return;
        case Step::WaitState: {
            long long got = 0;
            if (!getStateInt(app.gameState(), st.a, got)) {
                sc.waited = 0;
                ++sc.pc;
                fail("wait state", st.a + " (unknown path)");
                return;
            }
            if (cmpOp(got, st.b, st.n)) {
                sc.waited = 0;
                ++sc.pc;
            } else if (++sc.waited > st.limit) {
                sc.waited = 0;
                ++sc.pc;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s %s %d (got %lld)", st.a.c_str(), st.b.c_str(),
                              st.n, got);
                fail("wait state", buf + std::string(" (timeout)"));
            }
            return;
        }
    }
}

} // namespace debug
} // namespace rich4
