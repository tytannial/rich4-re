#include "game/app/debug_keys.h"

#include <cstdio>
#include <string>

#include "game/app/game_loop.h"
#include "game/app/object_tip.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"

#include "game/app/debug/debug.h"

namespace rich4 {

namespace {

// [NEW] 鼠标指向的地图物件 ID（复用渲染期拾取记录）；
//   调试命令层 select <kind> <i> 设定的选择器优先（headless 脚本无需真实鼠标指向）
uint16_t pointedObject(Application& app) {
    if (const uint16_t sel = debug::selectedObject()) {
        return sel;
    }
    int x = 0;
    int y = 0;
    app.mouseLogicalPos(x, y);
    // [NEW M4-A2] pickMapObject / mapHitRegions 统一为设计逻辑坐标
    return pickMapObject(app.gameState(), x, y);
}

// [NEW] 调试直调事件（命运/新闻/入狱/住院）后的回合推进：正常路径经 landingEvent 返回
//   0x80（playerActionWait=-128）由状态机切下一位；直调时若事件使当前玩家入院/坐牢/出国
//   （stateFlags != 0），必须走 [RE 0x40DEFE] 对齐的 beginPlayerTurn 状态分支，
//   否则回合卡在等待操作。
void debugAdvanceIfIncapacitated(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4 || st.players[p].stateFlags == 0) {
        return;
    }
    if (st.playerActionState[p] == 0 && st.playerActionWait[p] == 0) {
        disablePlayerControl(app);
        st.playerActionFlags[p] |= 0x80;
        st.gameStateActive = true;
        RICH4_LOGI("debug: event state p=%d flags=0x%X -> skip turn (RE 0x40DEFE)", p,
                   st.players[p].stateFlags);
    }
}

} // namespace

// [NEW] 调试热键（--debug 启用；docs/debug-keys.md v2）。
// 本层只做「指向→selected 桥接 + 键→命令模板」，全部状态写入与游戏逻辑调用收口
// 到命令注册表 debug::execLine（单一事实源；45→30 键精简 + 双实现分叉根因修正）。
bool handleDebugKey(Application& app, int code, bool shift) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    debug::setSelectedObject(pointedObject(app));  // get.land/land/obj.kick 等消费

    char cmd[64];
    cmd[0] = '\0';
    bool advance = false;  // 事件类命令后需 debugAdvanceIfIncapacitated
    if (!shift) {
        // 主键盘构造组：1/2 得、3/4 清空、5/6 全图、7/8 等级、9 诊断
        static const char* const kPlain[10] = {
            "",        "get.land",     "get.street",   "clear.cell",  "clear.street",
            "get.all", "clear.all",    "level.up",     "level.down",  "dump.pointed"};
        if (code >= 1 && code <= 9) {
            std::snprintf(cmd, sizeof(cmd), "%s", kPlain[code]);
        }
    } else {
        switch (code) {
            // 移动/落地组（G/H 相邻）
            case 'g': std::snprintf(cmd, sizeof(cmd), "land"); break;
            case 'h': std::snprintf(cmd, sizeof(cmd), "dir.cycle"); break;
            // 状态事件组
            case 'j': std::snprintf(cmd, sizeof(cmd), "jail %d 3", cur); advance = true; break;
            case 'k': std::snprintf(cmd, sizeof(cmd), "hosp %d 3", cur); advance = true; break;
            case 'a': std::snprintf(cmd, sizeof(cmd), "news"); advance = true; break;
            case 'z': std::snprintf(cmd, sizeof(cmd), "fate"); advance = true; break;
            case 'r': std::snprintf(cmd, sizeof(cmd), "lottery.fixwin %d", cur); break;
            // 建设/财务/日期组
            case 'l': std::snprintf(cmd, sizeof(cmd), "all.level1"); break;
            case 'c': std::snprintf(cmd, sizeof(cmd), "player.give %d cash 100000", cur); break;
            case 'v': std::snprintf(cmd, sizeof(cmd), "player.give %d bank 100000", cur); break;
            case 'b': std::snprintf(cmd, sizeof(cmd), "player.give %d points 1000", cur); break;
            case 'd': std::snprintf(cmd, sizeof(cmd), "day"); break;
            case 'n': std::snprintf(cmd, sizeof(cmd), "settle"); break;
            case 'e': std::snprintf(cmd, sizeof(cmd), "dividend"); break;
            // 物件组（O/P 相邻）+ 诊断
            case 'o': std::snprintf(cmd, sizeof(cmd), "obj.createAhead"); break;
            case 'p': std::snprintf(cmd, sizeof(cmd), "obj.kick"); break;
            case 'y': std::snprintf(cmd, sizeof(cmd), "dump.celltable"); break;
            case 8: std::snprintf(cmd, sizeof(cmd), "npc.dump"); break;
            // 卡道具组（F/I/U 卡片道具栏入口）
            case 'f': std::snprintf(cmd, sizeof(cmd), "items.all %d", cur); break;
            case 'i': std::snprintf(cmd, sizeof(cmd), "refill.cards %d 1", cur); break;
            case 'u': std::snprintf(cmd, sizeof(cmd), "refill.cards %d 16", cur); break;
            default: break;
        }
    }
    if (cmd[0] == '\0') {
        return false;
    }
    std::string err;
    if (!debug::execLine(app, cmd, err)) {
        RICH4_LOGW("debug: key exec '%s' failed: %s", cmd, err.c_str());
    } else {
        RICH4_LOGI("debug: key -> %s", cmd);
    }
    if (advance) {
        debugAdvanceIfIncapacitated(app);
    }
    return true;
}

} // namespace rich4
