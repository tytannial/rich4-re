#include "game/app/debug/debug.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include "game/app/bank_stay_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/dividend.h"
#include "game/app/fate_event.h"
#include "game/app/lottery.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/month_settle.h"
#include "game/app/news_dialog.h"
#include "game/app/query_dialog.h"
#include "game/app/shop_dialog.h"
#include "game/app/stock_market_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/clock.h"
#include "game/core/debug_hooks.h"
#include "game/core/log.h"
#include "game/core/trace.h"

// [NEW] 统一调试命令层 + headless 脚本执行器（docs/testing.md §3；无原版对应）。
// 断言优先级：state > trace > log；失败计数供 main 退出码。脚本执行器在 renderFrame
// 尾部 tick，阻塞命令（land/day 等内部 runModal）期间重入 tick 继续消费后续
// click/wait/assert 步骤（深度守卫防失控）。正常模式（无脚本且非 debug）零开销。
namespace rich4 {
namespace debug {

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
    enum Kind { Cmd, WaitFrames, WaitTrace, WaitLog, WaitIdle };
    Kind kind = Cmd;
    std::string a;      // 命令原文 / 等待模式
    std::string b;      // 保留
    int n = 0;          // WaitFrames 帧数
    int limit = 6000;   // 等待类上限帧
    int lineNo = 0;
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
    if (matchPath(path, "players.*.travel", c, 1)) {
        const int p = parseInt(c[0]);
        if (p < 0 || p >= 9) return false;
        out = st.players[p].travel;
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

// ---- 玩家 ----
bool cmdPlayerCash(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 2, err, "player.cash <p> <v>")) return false;
    const int p = parseInt(a[1]);
    if (p < 0 || p >= 9) {
        err = "player.cash: bad index";
        return false;
    }
    app.gameState().players[p].cash = parseInt(a[2]);
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
    Player& pl = st.players[p];
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask &= ~(256u << p);
    }
    pl.cellEntId = static_cast<uint16_t>(cell);
    pl.spriteX = st.cellEnts[cell].x;
    pl.spriteY = st.cellEnts[cell].y;
    st.cellEnts[cell].occMask |= (256u << p);
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

// ---- 事件触发 ----
uint16_t resolveTargetCell(Application& app);

uint16_t resolveTargetCell(Application& app);

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
    Player& pl = st.players[cur];
    if (pl.cellEntId != cell) {
        if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
            st.cellEnts[pl.cellEntId].occMask &= ~(256u << cur);
        }
        pl.cellEntId = cell;
        pl.spriteX = st.cellEnts[cell].x;
        pl.spriteY = st.cellEnts[cell].y;
        st.cellEnts[cell].occMask |= (256u << cur);
        buildMiniMapMarks(app);
    }
    RICH4_LOGI("debug: land player %d -> cell %u", cur, cell);
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
        try {
            want = std::stoll(a[i]);
        } catch (...) {
            err = "assert state: bad value " + a[i];
            return false;
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
};
std::vector<QueuedInput> g_queue;
int g_inputTick = 0;

void queueInput(int x, int y, bool right, bool isMove) {
    g_queue.push_back({g_inputTick + 1, x, y, right, true});   // 先 move
    if (!isMove) {
        g_queue.push_back({g_inputTick + 2, x, y, right, false}); // 再 click
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
        app.events().dispatch(ev);
        if (!q.isMove) {
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = q.right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = static_cast<float>(q.x);
            ev.button.y = static_cast<float>(q.y);
            if (!q.right) {
                app.setUiClicked(); // 与真实左键按下语义一致（转盘提前停等 consumeUiClick）
            }
            app.events().dispatch(ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            app.events().dispatch(ev);
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

bool cmdStockBuy(Application& app, const std::vector<std::string>& a, std::string& err) {
    if (!argCount(a, 3, err, "stock.buy <p> <stock> <n>")) return false;
    const int changed = buyStock(app, parseInt(a[1]), parseInt(a[2]), parseInt(a[3]), false);
    RICH4_LOGI("debug: stock.buy p=%s stock=%s n=%s -> control=%d", a[1].c_str(), a[2].c_str(),
               a[3].c_str(), changed);
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
    Player& pl = st.players[p];
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask &= ~(256u << p);
    }
    pl.cellEntId = cell;
    pl.spriteX = st.cellEnts[cell].x;
    pl.spriteY = st.cellEnts[cell].y;
    st.cellEnts[cell].occMask |= (256u << p);
    buildMiniMapMarks(app);
    RICH4_LOGI("debug: sel.goto p%d -> cell %u", p, cell);
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

bool cmdClick(Application& app, const std::vector<std::string>& a, std::string& err) {
    (void)app;
    if (!argCount(a, 2, err, "click <x> <y>")) return false;
    queueInput(parseInt(a[1]), parseInt(a[2]), false, false);
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
    {"corp.build", cmdCorpBuild},
    {"corp.owner", cmdCorpOwner},
    {"corp.level", cmdCorpLevel},
    {"specpt.owner", cmdSpecPtOwner},
    {"all.owned", cmdAllOwned},
    {"all.level1", cmdAllLevel1},
    {"player.cash", cmdPlayerCash},
    {"player.bank", cmdPlayerBank},
    {"player.points", cmdPlayerPoints},
    {"player.state", cmdPlayerState},
    {"player.god", cmdPlayerGod},
    {"player.vehicle", cmdPlayerVehicle},
    {"player.teleport", cmdPlayerTeleport},
    {"player.dice", cmdPlayerDice},
    {"give.card", cmdGiveCard},
    {"give.item", cmdGiveItem},
    {"land", cmdLand},
    {"day", cmdDay},
    {"find.estate", cmdFindEstate},
    {"find.evtcell", cmdFindEvtCell},
    {"stock.buy", cmdStockBuy},
    {"sel.owner", cmdSelOwner},
    {"sel.level", cmdSelLevel},
    {"sel.type", cmdSelType},
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
    {"seed", cmdSeed},
    {"assert", cmdAssert},
    {"quit", cmdQuit},
    {"shot", cmdShot},
    {"dump", cmdDump},
    {"click", cmdClick},
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
                                         "lottery.draw",       "settle",      "dividend", "shop",
                                         "bank",               "jail",        "hosp",     "open.stock",
                                         "open.query"};
    for (const char* b : kBlock) {
        if (name == b) {
            return true;
        }
    }
    return false;
}

bool isInteractiveCmd(const std::string& name) {
    // 阻塞模态期间仍需可执行的应答类命令
    static const char* const kInter[] = {"click", "rclick", "move", "key", "confirm"};
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
        newsDebugFire(app, -1);
        return true;
    }
    if (a[0] == "fate") {
        fateDebugFire(app, -1);
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
        monthlySettle(app);
        return true;
    }
    if (a[0] == "dividend") {
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
    app.events().dispatch(ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    ev.button.clicks = 1;
    ev.button.x = static_cast<float>(x);
    ev.button.y = static_cast<float>(y);
    app.events().dispatch(ev);
    if (!downOnly) {
        ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
        app.events().dispatch(ev);
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
            const bool block = stepFirstIsBlocking(st.a);
            if (g_inBlock && !stepFirstIsInteractive(st.a)) {
                return;
            }
            ++sc.pc;
            (void)block;
            g_inBlock = true;
            std::string err;
            if (!execLine(app, st.a, err)) {
                ++g_failures;
                RICH4_LOGE("script %s line %d: %s", sc.id.c_str(), st.lineNo, err.c_str());
            }
            g_inBlock = false;
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
    }
}

} // namespace debug
} // namespace rich4
