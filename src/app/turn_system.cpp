#include "game/app/turn_system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include "game/application.h"
#include "game/app/confirm_dialog.h"
#include "game/app/economy.h"
#include "game/app/facility_dialog.h"
#include "game/app/game_loop.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/roulette_dialog.h"
#include "game/core/encoding.h"
#include "game/core/log.h"

namespace rich4 {

namespace {

// [RE 0x4749D8] byte_4749D8 每格移动帧数（步行/机车/汽车/第四）
constexpr int kMoveSpeed[4] = {8, 12, 16, 8};
// [RE 0x4631DC] flt_4631DC = 0.125（载具/特殊状态固定速度系数）
constexpr float kMoveScale = 0.125f;
// [RE 0x498EC4] 骰子/掷骰动画帧数（原版取资源头 >>3；B4 接入资源前用固定值）
constexpr int kDiceAnimFrames = 12;
// [RE 0x419572] 掷骰后停留帧数（原版 sub_45285E(500) 约 500ms 显示点数）
constexpr int kDiceHoldFrames = 30;
// [RE 0x482414] byte_482414 方向映射表
constexpr uint8_t kDirTable[8] = {2, 3, 4, 5, 6, 7, 0, 1};

int daysInMonth(int year, int month) {
    static const int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4) == 0) {
        return 29;
    }
    return kDays[month];
}

// [RE 0x452117] 日期推进；返回是否跨月（原版返回 v2）
int advanceDate(GameState& st) {
    int year = static_cast<int>((st.gameDate >> 16) & 0xFFFF);
    int month = static_cast<int>((st.gameDate >> 8) & 0xFF);
    int day = static_cast<int>(st.gameDate & 0xFF);
    bool newMonth = false;
    if (++day > daysInMonth(year, month)) {
        day = 1;
        newMonth = true;
        if (++month > 12) {
            month = 1;
            ++year;
        }
    }
    st.gameDate = (static_cast<uint32_t>(year) << 16) | (static_cast<uint32_t>(month) << 8) |
                  static_cast<uint32_t>(day);
    return newMonth ? 1 : 0;
}

// [RE 0x454FE1] sub_454FE1 角度计算（atan2 定点化，0..0xFFFF = 0..360°）
// [RE 0x454FB4] sub_454FB4 朝向 = byte_482414[((sub_454FE1(-dy)>>12)+1)>>1 & 7]
// 依据: 0x454FE1 查表 word_48242C（atan(min/max)），0x454FB4 映射 8 方向
// 差异: 用标准 atan2 等价替代查表（结果一致，避免硬编码 2049 项表）
int facingFromDelta(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    constexpr double kPi = 3.14159265358979;
    double a = std::atan2(-static_cast<double>(dy), static_cast<double>(dx));
    if (a < 0.0) {
        a += 2.0 * kPi;
    }
    const int angle = static_cast<int>(a / (2.0 * kPi) * 65536.0) & 0xFFFF;
    const int idx = (((angle >> 12) + 1) >> 1) & 7;
    return kDirTable[idx];
}

// [RE 0x40FC00] sub_40FC00 玩家携带物件跟随移动（byte_496BA7 / g_playerCellNo）
void updatePlayerCarriedObjects(GameState& st, int p) {
    Player& pl = st.players[p];
    if (pl.cellTableIdx) {
        // word_496D0A[4*(cellTableIdx-1)] = cellEntId（物件位置）
    }
    if (pl.cellNo) {
        // word_496D0A[12*cellNo-12] = cellEntId
    }
}

// [RE 0x41B42D] onPlayerActionPhase 格子类型 11..18 落地事件（M2）
// 依据: 0x41B42D 反编译; 161 基本块，购地/收租/税/卡片/机会等
void onPlayerActionPhase(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= st.playerCount || st.players[p].cellEntId == 0) {
        return;
    }
    RICH4_LOGI("onPlayerActionPhase: player %d cellEnt %u (RE 0x41B42D, M2 TODO)", p,
               st.players[p].cellEntId);
}

// [RE 0x40B93B] 行走资源加载（定义见下方 loadWalkResources；本地辅助）
void loadWalkRes(GameState& st, int p, int slot, size_t index) {
    auto blob = st.data.read(index);
    if (blob) {
        st.walkRes[p][slot].load(std::move(*blob));
    } else {
        RICH4_LOGW("loadWalkRes: data.mkf[%zu] unavailable (RE 0x40B93B)", index);
    }
}

// [RE 0x40B8D8] releaseWalkResources
// 依据: 0x40B8D8 反编译; 释放玩家 a1 的动画组 a2（slot = a2 + 2*i, i<4），
//       并清 byte_498EA0 的动画组位（a2=1 → 低 4 位；a2=0 → 高 4 位）
void releaseWalkResources(GameState& st, int p, int group) {
    for (int i = 0; i < 4; ++i) {
        st.walkRes[p][group + 2 * i] = UiImage{};
    }
    if (group) {
        st.playerActionFlags[p] &= 0x0F;
    } else {
        st.playerActionFlags[p] &= 0xF0;
    }
}

// [RE 0x40B93B] loadWalkResources
// 依据: 0x40B93B 反编译; 普通玩家(0..3):
//   v1 = 21*charIndex + 128
//   state37 → 组5: slot0=v1+16, slot2=v1+17, slot4=v1+2
//   cellEnt+39 最高位（有路）→ 组1: slot1=v1+13, slot3=v1+14, slot5=v1+15
//   travel → slot0=3*travel+v1, slot2=+1, slot4=+2, slot6=+3（travel==3）
// 差异: 槽 4..8（事件槽）分支待补
void loadWalkResources(GameState& st, int p) {
    if (p < 0 || p >= st.playerCount || p >= 4) {
        return;
    }
    Player& pl = st.players[p];
    const int v1 = 21 * pl.charIndex + 128;

    if (!pl.alive || (pl.alive & 0x40)) {
        if ((st.playerActionFlags[p] & 0x30) != 0x20) {
            releaseWalkResources(st, p, 1);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xCF) | 0x20);
            loadWalkRes(st, p, 1, static_cast<size_t>(v1 + 18));
        }
        st.playerMoveGroup[p] = 1;
        if (st.playerActionState[p] == 1) {
            st.playerActionWait[p] = 5;
        }
        st.playerActionState[p] = 0;
        st.playerMoveFrame[p] = 0;
        return;
    }
    if (pl.state37) {
        if ((st.playerActionFlags[p] & 0x0F) != 5) {
            releaseWalkResources(st, p, 0);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) | 5);
            loadWalkRes(st, p, 0, static_cast<size_t>(v1 + 16));
            loadWalkRes(st, p, 2, static_cast<size_t>(v1 + 17));
            loadWalkRes(st, p, 4, static_cast<size_t>(v1 + 2));
        }
        st.playerMoveGroup[p] = 0;
        if (st.playerMoveFrame[p]) {
            st.playerActionExtra[p] = 0;
        }
        st.playerMoveFrame[p] = 0;
        return;
    }
    // cellEnt+39 最高位（bit31）置位 = 有路 → 组1；否则 travel（组0）
    const bool hasRoad = pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size() &&
                         (st.cellEnts[pl.cellEntId].occMask & 0x80000000u) != 0;
    if (hasRoad) {
        if ((st.playerActionFlags[p] & 0x30) != 0x10) {
            releaseWalkResources(st, p, 1);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xCF) | 0x10);
            loadWalkRes(st, p, 1, static_cast<size_t>(v1 + 13));
            loadWalkRes(st, p, 3, static_cast<size_t>(v1 + 14));
            loadWalkRes(st, p, 5, static_cast<size_t>(v1 + 15));
        }
        if (st.playerMoveGroup[p] == 0) {
            st.playerMoveFrame[p] = 0; // 从普通组切入渡水/特殊组时重置动画帧
        }
        st.playerMoveGroup[p] = 1;
        // [RE 0x40B93B] 特殊移动（水上/渡水，cellEnt+39 bit31）→ 音效槽 15
        //   （原版 dword_4749D4 = 15 + audioPlayEffect(&dword_482382[16])）
        st.moveSoundIndex = 15;
        return;
    }
    const int travel = pl.travel & 3;
    if ((travel + 1) != (st.playerActionFlags[p] & 0x0F)) {
        releaseWalkResources(st, p, 0);
        st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) |
                                                       (travel + 1));
        const int v10 = 3 * travel + v1;
        loadWalkRes(st, p, 0, static_cast<size_t>(v10));
        loadWalkRes(st, p, 2, static_cast<size_t>(v10 + 1));
        loadWalkRes(st, p, 4, static_cast<size_t>(v10 + 2));
        if (travel == 3) {
            loadWalkRes(st, p, 6, static_cast<size_t>(v10 + 3));
        }
    }
    // 仅在从特殊移动组(渡水/机车 group=1)切回普通组时重置动画帧；
    //   同组内跨格保持连续，避免每格走路动画重启（原版 sub_40B93B 组0 末尾逻辑）
    if (st.playerMoveGroup[p] == 1) {
        st.playerMoveFrame[p] = 0;
    }
    st.playerMoveGroup[p] = 0;
    st.moveSoundIndex = travel + 11;
}

// [RE 0x418C55] dword_475114 跳伞入场落地（定义见 beginPlayerTurn）
// 说明: 落地后 g_playerSpriteX/Y = cellEnt 坐标, g_playerAlive = g_playerKind,
//       sub_40B93B 加载行走资源, dword_475114 = 0
void finishParachute(Application& app) {
    GameState& st = app.gameState();
    const int p = st.pendingSpawnPlayer - 1;
    if (p >= 0 && p < st.playerCount) {
        Player& pl = st.players[p];
        if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
            // [RE 0x40829D] 落地仅设坐标；朝向/来向已在放置时设定，跳伞不改
            pl.spriteX = static_cast<uint16_t>(st.cellEnts[pl.cellEntId].x);
            pl.spriteY = static_cast<uint16_t>(st.cellEnts[pl.cellEntId].y);
        }
        pl.alive = pl.kind;
        loadWalkResources(st, p);
    }
    st.pendingSpawnPlayer = 0;
    st.parachuteActive = false;
    st.parachute = FliDecoder{};
}

// [RE 0x45144F] 跳伞动画播放（FLI/FLC 逐帧；原版阻塞循环，重写用状态机）
void updateParachute(Application& app) {
    GameState& st = app.gameState();
    const int p = st.pendingSpawnPlayer - 1;
    if (!st.parachuteActive) {
        const int charIndex = (p >= 0 && p < 9) ? st.players[p].charIndex : 0;
        auto blob = st.data.read(static_cast<size_t>(charIndex + 559));
        if (blob && st.parachute.open(std::move(*blob))) {
            st.parachuteActive = true;
            st.parachuteTimer = 0;
            st.parachuteLastMs = 0;
            st.parachute.nextFrame(); // 跳过纯调色板首帧，立即显示首幅画面
        } else {
            RICH4_LOGW("parachute: data.mkf[%d] unavailable (RE 0x418C55)", charIndex + 559);
            finishParachute(app);
        }
        return;
    }
    // 按真实时间推进（原版 sub_45144F 用 timeGetTime，与帧率无关）
    const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
    const uint32_t interval = static_cast<uint32_t>(std::max(1, st.parachute.speedMs()));
    if (st.parachuteLastMs == 0) {
        st.parachuteLastMs = now;
    }
    if (now - st.parachuteLastMs >= interval) {
        st.parachuteLastMs = now;
        // 帧数据播完时 nextFrame 返回 false（frameCount 含 0xF100 占位帧，不可用作结束条件）
        if (!st.parachute.nextFrame()) {
            finishParachute(app);
        }
    }
}

} // namespace

// [RE 0x419744] estateRouteRent：同路段/连锁店联合租金（导出：landingEvent 收租 + object_tip 显示）
// 依据: 0x419744 反编译; 普通住宅用地（type==0）→ 遍历全部地产，owner 相同且地名相同
//       （strcmp）的 fees[level]（+32+2*level）累加；连锁店（type!=0）→ owner 相同的
//       连锁店每处 +2000；返回 g_moneyMul × 总和
int32_t estateRouteRent(const GameState& st, uint8_t owner, const Estate& es) {
    int32_t sum = 0;
    int count = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& e = st.estates[i];
        if (es.type == 0) {
            if (e.type == 0 && e.owner == owner &&
                std::strncmp(e.name, es.name, sizeof(e.name)) == 0) {
                const int lv = e.level < 6 ? e.level : 0;
                sum += e.fees[lv];
                ++count;
                RICH4_LOGD("  routeRent match estate[%zu] owner=%u level=%u fee=%u", i, e.owner,
                           e.level, e.fees[lv]);
            }
        } else if (e.type != 0 && e.owner == owner) {
            sum += 2000;
            ++count;
            RICH4_LOGD("  routeRent match chain estate[%zu] owner=%u", i, e.owner);
        }
    }
    const std::string curName = big5ToUtf8(es.name, sizeof(es.name));
    RICH4_LOGI("routeRent: owner=%u cur=(%s type=%u) matched=%d sum=%d total=%d (M=%d)",
               owner, curName.c_str(), es.type, count, sum, sum * st.moneyMul, st.moneyMul);
    return sum * st.moneyMul;
}

// [RE 0x407A8C] 朝向 = sub_454FB4(cellEnt[to]-cellEnt[from])，8 方向 0..7
// 依据: 0x407A8C→0x454FB4（atan2(-dy,dx) 量化）→byte_482414[2,3,4,5,6,7,0,1]
int facingBetween(GameState& st, int fromId, int toId) {
    if (fromId <= 0 || toId <= 0 || fromId >= static_cast<int>(st.cellEnts.size()) ||
        toId >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    const int dx = st.cellEnts[toId].x - st.cellEnts[fromId].x;
    const int dy = st.cellEnts[toId].y - st.cellEnts[fromId].y;
    return facingFromDelta(dx, dy);
}

// [RE 0x419744] estateRouteRent 定义见文件后部（导出供 object_tip 复用）

// [RE 0x43F7C6] 轮盘读数与 UI 已移至 src/app/roulette_dialog.cpp（roulettePrompt，完整转盘动画）

// [RE 0x419A67] startRouteHighlight：收租联动高亮收集与启动
// 依据: 0x419A67 高亮循环（rebuildPickBuffer(1) + sub_456C0A 标记同组地块 →
//       v141 > 1 时 sub_451985 播放 16 帧闪烁）；组定义：
//       住宅用地 = 同 owner + 同名 + type==0；连锁店 = 同 owner + type!=0（跨街道）
// 差异: 原版高亮含同盟地块（v140）→ P4
void startRouteHighlight(GameState& st, const Estate& es) {
    st.highlightEstates.clear();
    if (es.type == 0) {
        for (size_t j = 1; j < st.estates.size(); ++j) {
            const Estate& e = st.estates[j];
            if (e.type == 0 && e.owner == es.owner &&
                std::strncmp(e.name, es.name, sizeof(e.name)) == 0) {
                st.highlightEstates.push_back(static_cast<int>(j));
            }
        }
    } else {
        for (size_t j = 1; j < st.estates.size(); ++j) {
            if (st.estates[j].type != 0 && st.estates[j].owner == es.owner) {
                st.highlightEstates.push_back(static_cast<int>(j));
            }
        }
    }
    st.highlightFrame = (st.highlightEstates.size() > 1) ? 0 : -1;
}

// [RE 0x451985] playHighlightBlink：阻塞播放联动闪烁（先闪烁、后弹收费提示）
// 依据: 0x451985 逐帧 byte_476380[i] 重绘（sub_4554FC），每帧 sub_4528B9(30)，
//       循环后 sub_4528B9(400) 停留；原版在 showMessage(收费) 之前完成
// 迁移: 逐帧 renderGameFrame（画 surface）+ renderFrame（present/音频）+ 分段延时，
//       期间 pumpEvents 保持窗口响应（收租时 gamePlayerControl=0，事件被吞/仅物件提示）
// [PORT] 音频续喂：kTargetQueued=1024 字节（8bit mono @44.1kHz ≈ 23ms）——阻塞循环的
//       30ms 间隔会超过缓冲导致音乐 underrun（卡顿）→ 延时拆为 5ms 小段逐段 audio.update()
void playHighlightBlink(Application& app) {
    GameState& st = app.gameState();
    if (st.highlightFrame < 0) {
        return; // 联动组 ≤ 1（原版 v141 > 1 才播放）
    }
    RICH4_LOGI("route highlight: %zu estates blink (RE 0x451985)", st.highlightEstates.size());
    auto waitAudio = [&app](int ms) {
        for (int t = 0; t < ms; t += 5) {
            app.audio().update();
            SDL_Delay(5);
        }
    };
    for (int f = 0; f < 16; ++f) {
        st.highlightFrame = f;
        renderGameFrame(app);
        app.renderFrame();
        app.pumpEvents();
        waitAudio(30);
    }
    // 停留期（原版 sub_4528B9(400)：最后一帧 byte_476380[15]=0 无偏移）
    st.highlightFrame = 16;
    renderGameFrame(app);
    app.renderFrame();
    app.pumpEvents();
    waitAudio(400);
    st.highlightFrame = -1;
    st.highlightEstates.clear();
}

// [RE 0x41D559] ownerCanCollectRent：地主状态免收租检查（false = 免收，已显示提示）
// 依据: 0x41D559 反编译; 依次判断（命中即 showMessage(文本, 1500) 并返回 false）：
//   flag(+23) 高/低半字节均非 0 = 房屋查封中; 地主 +65 同盟对象 == 付款人+1 = 與%s同盟中;
//   地主携带死神（cellTableIdx==15）= 死神顯靈; stateFlags BYTE0/1/2/3 = 住宿/消失/坐牢/住院中;
//   byte54 = 冬眠中; state37 = 夢遊中
bool ownerCanCollectRent(Application& app, int ownerIdx, uint8_t flag, const char* feeName) {
    GameState& st = app.gameState();
    const Player& owner = st.players[ownerIdx];
    const char* ownerName = owner.name ? owner.name : "";
    char text[192];
    if ((flag & 0xF0) != 0 && (flag & 0x0F) != 0) {
        std::snprintf(text, sizeof(text), "房屋查封中\n\n免收%s！", feeName);
    } else if (owner.ally == static_cast<uint8_t>(st.currentPlayer + 1)) {
        std::snprintf(text, sizeof(text), "與%s同盟中\n\n免收%s！", ownerName, feeName);
    } else if (owner.cellTableIdx == 15) {
        std::snprintf(text, sizeof(text), "死神顯靈\n\n免收%s！", feeName);
    } else if ((owner.stateFlags & 0xFFu) != 0) {
        std::snprintf(text, sizeof(text), "%s住宿中\n\n免收%s！", ownerName, feeName);
    } else if ((owner.stateFlags & 0xFF00u) != 0) {
        std::snprintf(text, sizeof(text), "%s消失中\n\n免收%s！", ownerName, feeName);
    } else if ((owner.stateFlags & 0xFF0000u) != 0) {
        std::snprintf(text, sizeof(text), "%s坐牢中\n\n免收%s！", ownerName, feeName);
    } else if ((owner.stateFlags & 0xFF000000u) != 0) {
        std::snprintf(text, sizeof(text), "%s住院中\n\n免收%s！", ownerName, feeName);
    } else if (owner.byte54 != 0) {
        std::snprintf(text, sizeof(text), "%s冬眠中\n\n免收%s！", ownerName, feeName);
    } else if (owner.state37 != 0) {
        std::snprintf(text, sizeof(text), "%s夢遊中\n\n免收%s！", ownerName, feeName);
    } else {
        return true;
    }
    showMessage(app, text, 1500);
    return false;
}

// [RE 0x41D709] applyGodRentModifier：付款方携带神明对租金的调整
// 依据: 0x41D709 反编译; switch(cellTableIdx)：1 小財神=减半 / 2 大財神=免付 /
//   5 小窮神=加付 50% / 6 大窮神=加倍；金额有变化时 showMessage(提示, 1500)
int32_t applyGodRentModifier(Application& app, int payer, int32_t rent, const char* feeName) {
    const uint8_t slot = app.gameState().players[payer].cellTableIdx;
    int32_t v = rent;
    char text[192];
    switch (slot) {
        case 1:
            std::snprintf(text, sizeof(text), "小財神顯靈\n\n%s減免一半！", feeName);
            v = rent / 2;
            break;
        case 2:
            std::snprintf(text, sizeof(text), "大財神顯靈\n\n免付%s！", feeName);
            v = 0;
            break;
        case 5:
            std::snprintf(text, sizeof(text), "小窮神顯靈\n\n%s加付50％！", feeName);
            v = rent + rent / 2;
            break;
        case 6:
            std::snprintf(text, sizeof(text), "大窮神顯靈\n\n加倍付%s！", feeName);
            v = 2 * rent;
            break;
        default:
            return rent;
    }
    if (v != rent) {
        showMessage(app, text, 1500);
    }
    return v;
}

// [RE 0x41982D] landingEvent
// 依据: 0x41982D 反编译; 停留格结算。switch(cellEnt+36 低字节 格子类型 0..16)：
//   入口先按 g_cellTypeSound[type]（0x475299）播放格子音效（type 2..16）
//   case 0 = 按 objId 分（2000..4000 住宅用地 / 4000..6000 商業用地 / 其他 → loc_41A168）
//   无主住宅用地购地（loc_41A013）：
//     守卫 state37 / cellTableIdx==12 → 价格=(level*priceBase+priceAdd)*moneyMul
//     → 现金检查（不足 → 0x41A159 showMessage"您的現金不足！"）→ sprintf("%s\n\n", estate.name)
//     → 人类 askDialog → sub_40FA61 携带神明检查（showMessage 神明名）→ owner=cur+1
//     → 音效槽 17（dword_4823D2）→ rebuildMiniMap(0) → 期限 expireDate（g_landPermDays）→ 扣现金
//   自己住宅用地升級（0x419911）：见下
//   他人住宅用地收租（loc_419A67）：音效槽 20 → ownerCanCollectRent（0x41D559 地主状态免收）
//     → estateRouteRent（0x419744 同路段 fees[level] 之和 / 连锁店数×2000）× moneyMul
//     → flag(+23) 翻倍 → applyGodRentModifier（0x41D709 付款方神明调整）
//     → showMessage("此地屬%s請付%d元過路費") → transferMoney(flags=0 收款入银行)
// 差异: 事件格 case 2..16、商業用地/行業設施點收費/特殊点（loc_41A168）、AI 决策（sub_41D7D4）、
//       收租的同盟分账/死神代付/免租卡片/同路段高亮/浮动数字（P4）
int landingEvent(Application& app, uint16_t cellEntId) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9 || cellEntId >= st.cellEnts.size()) {
        return 0x80;
    }
    const CellEnt& ce = st.cellEnts[cellEntId];
    const uint16_t objId = ce.special; // +32
    const uint8_t cellType = static_cast<uint8_t>(ce.occMask & 0xFF); // +36 低字节
    // [RE 0x475299] g_cellTypeSound[type] → 音效槽
    static const uint8_t kCellTypeSound[17] = {9, 0, 10, 10, 10, 10, 10, 10, 10,
                                               10, 16, 16, 16, 16, 10, 10, 10};
    if (cellType >= 2 && cellType <= 16) {
        app.audio().playEffectSlot(kCellTypeSound[cellType]);
    }
    if (cellType != 0) {
        RICH4_LOGI("landingEvent: cellType %u objId %u (RE 0x41982D, P2 TODO)", cellType, objId);
        return 0x80;
    }
    // case 0：普通格，按 objId 分（objId == 0 → 无事件返回 0x80；
    //   有 objId 的住宅用地/商業用地/行業設施點路径统一在 loc_41B111 返回等待值后下一位）
    // 差异: 原版 0x88 = 8 帧；重写帧率更高（约 2 倍），暂用 0x90 = 16 帧对齐观感
    constexpr int kLandingWait = 0x90;
    if (objId == 0) {
        return 0x80;
    }
    if (objId > 2000 && objId < 4000) {
        const int idx = static_cast<int>(objId) - 2000;
        if (idx <= 0 || idx >= static_cast<int>(st.estates.size())) {
            return kLandingWait;
        }
        Estate& es = st.estates[idx];
        Player& pl = st.players[p];
        if (es.owner == 0) {
            // ---- 无主住宅用地购地（loc_41A013）----
            if (pl.state37 != 0 || pl.cellTableIdx == 12) {
                return kLandingWait;
            }
            const int32_t price =
                (static_cast<int32_t>(es.level) * es.priceBase + es.priceAdd) * st.moneyMul;
            if (price > pl.cash) {
                // [RE 0x41A159] 现金不足：showMessage(byte_46398B="您的現金不足！", 1500)
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("buy land: cash %d < price %d (RE 0x41A159)", pl.cash, price);
                return kLandingWait;
            }
            const std::string label = big5ToUtf8(es.name, sizeof(es.name));
            char text[160];
            // [RE 0x4639E1] 原版格式串（BIG5）："%s\n\n費用:%d元\n\n是否買下此地?"
            std::snprintf(text, sizeof(text), "%s\n\n費用:%d元\n\n是否買下此地?", label.c_str(),
                          price);
            bool buy = false;
            if ((pl.alive & 6) != 0) {
                buy = false; // TODO [RE 0x41D7D4] AI 决策 sub_41D7D4(price)
            } else if (pl.alive == 1) {
                // [RE 0x440BA8] askDialog：文本居中 (220,140) + YES/NO 框 (220,320)
                buy = confirmDialog(app, text, 220, 320);
            }
            if (!buy) {
                return kLandingWait;
            }
            // [RE 0x40FA61] 携带神明（槽 7/8/15）阻止 → showMessage(神明名, 1500)
            const uint8_t carried = pl.cellTableIdx;
            if (carried == 7 || carried == 8 || carried == 15) {
                showMessage(app, kObjectNames[carried], 1500);
                RICH4_LOGI("buy land blocked by carried god %u (RE 0x40FA61)", carried);
                return kLandingWait;
            }
            es.owner = static_cast<uint8_t>(p + 1);
            // [RE 0x41D476(0,0,1)] UI/地图刷新（重写每帧重绘，无需显式）
            app.audio().playEffectSlot(17); // [RE 0x4823D2] 买地音效
            buildMiniMapMarks(app);         // [RE 0x40A4E1] rebuildMiniMap(0)
            if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3) {
                // [RE 0x4751F0] g_landPermDays（打包日期偏移：年<<16 / 月<<8）
                static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
                // [RE 0x4521CB] 日期加法（月 > 12 时 +0xF400 进位）
                int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
                if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                    d += 0xF400;
                }
                es.expireDate = static_cast<uint32_t>(d);
            }
            pl.cash -= price;
            // [RE 0x41A13E] sub_44F627(estate.name, 0)：同路段住宅用地 ≥3 时角色感想
            //   （sub_44EF41 角色语音/表情，P4 待实现）
            RICH4_LOGI("buy land: estate %d price %d owner %d (RE 0x41982D/0x41A013)", idx, price,
                       p + 1);
        } else if (es.owner == static_cast<uint8_t>(p + 1)) {
            // ---- 自己的住宅用地：升级/加盖（0x419911..0x419A52）----
            // 守卫: level >= 5 / type != 0（大地块走建造菜单）/ state37 != 0 → 无操作
            // 费用 = word[estate+30](priceBase) * g_moneyMul；现金不足 → showMessage("現金不夠支付！")
            // 人类 askDialog 询问（AI alive&6 自动升级）→ checkCarriedGod 阻止 → level++
            //   → 音效槽 18（dword_4823DA）；level==5 → 角色语音（sub_44EF41 索引 0）
            //   → 否则 estateChainSpeech(name, 1)（1/3 概率感想，P4）
            // 差异: 原版询问文本 sprintf("%s\n\n", name)（无费用），重写补费用行便于阅读；
            //       角色语音/吐槽（P4）、showMessage 提示待接入
            if (es.level >= 5 || es.type != 0 || pl.state37 != 0) {
                return kLandingWait;
            }
            const int32_t cost = static_cast<int32_t>(es.priceBase) * st.moneyMul;
            if (cost > pl.cash) {
                // [RE 0x419A52] 现金不足 → showMessage(byte_46398B="您的現金不足！", 1500)
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("upgrade: cash %d < cost %d (RE 0x419A52)", pl.cash, cost);
                return kLandingWait;
            }
            const std::string label = big5ToUtf8(es.name, sizeof(es.name));
            char text[160];
            // [RE 0x46396D] 原版升级询问文本（estate/corp 共用 aS_3）
            std::snprintf(text, sizeof(text), "%s\n\n升級費用:%d元\n\n是否升級？", label.c_str(),
                          cost);
            bool upgrade = false;
            if ((pl.alive & 6) != 0) {
                upgrade = true; // [RE 0x41997D] AI 直接升级
            } else if (pl.alive == 1) {
                upgrade = confirmDialog(app, text, 220, 320); // [RE 0x419996] askDialog
            }
            if (!upgrade) {
                return kLandingWait;
            }
            const uint8_t carried = pl.cellTableIdx;
            if (carried == 7 || carried == 8 || carried == 15) {
                // [RE 0x4199AE] checkCarriedGod 阻止 → showMessage(神明名, 1500)
                showMessage(app, kObjectNames[carried], 1500);
                RICH4_LOGI("upgrade blocked by carried god %u (RE 0x40FA61)", carried);
                return kLandingWait;
            }
            pl.cash -= cost;
            ++es.level;                          // [RE 0x4199D1] inc byte ptr [esi+1Ah]
            app.audio().playEffectSlot(18);      // [RE 0x4823DA] 升级音效
            RICH4_LOGI("upgrade: estate %d level %d cost %d (RE 0x419911)", idx, es.level, cost);
        } else {
            // ---- 他人住宅用地收租（loc_419A67）----
            // [RE 0x419A67] 收租音效 dword_4823EA = 槽 20 → sub_41D559 地主状态免收检查 →
            //   sub_419744 联合租金（同路段 fees[level] 之和 / 连锁店数×2000）→
            //   flag(+23) 非 0 翻倍 → sub_41D709 付款方神明调整 →
            //   showMessage("%s\n\n此地屬%s\n\n請付%d元過路費") → sub_41D2C6 转账（flags=0
            //   收款入银行）→ estate+44 = 本次租金
            // 差异: 原版同盟分账（g_playerVehicle 双方按比例拆分）、死神代付（sub_40FBB8）、
            //       免租/分期卡片（sub_4413AD/sub_444A60/sub_44476A）、同路段高亮
            //       （rebuildPickBuffer + sub_456C0A）与浮动数字（sub_44F354）待 P4
            app.audio().playEffectSlot(20);
            static const char kFeeName[] = "過路費"; // [RE 0x47517C] off_47517C 初值
            if (ownerCanCollectRent(app, es.owner - 1, es.flag, kFeeName)) {
                int32_t rent = estateRouteRent(st, es.owner, es);
                if (es.flag != 0) {
                    rent *= 2;
                }
                // [RE 0x419A67] 先播放联动闪烁（阻塞，原版 sub_451985 在收费提示之前完成）
                startRouteHighlight(st, es);
                playHighlightBlink(app);
                // 收费提示（原版显示神明调整前的金额 v8）
                if (rent > 0) {
                    const std::string label = big5ToUtf8(es.name, sizeof(es.name));
                    const Player& owner = st.players[es.owner - 1];
                    const char* ownerName = owner.name ? owner.name : "";
                    char text[192];
                    std::snprintf(text, sizeof(text), "%s\n\n此地屬%s\n\n請付%d元%s", label.c_str(),
                                  ownerName, rent, kFeeName);
                    showMessage(app, text, 1500);
                }
                // [RE 0x41D709] 付款方神明调整（内部可能弹神明提示）
                rent = applyGodRentModifier(app, p, rent, kFeeName);
                // [RE 0x41A003] 转账：付款人 → 地主（flags=0，收款入银行存款）
                if (rent > 0) {
                    transferMoney(app, p, es.owner - 1, rent, 0);
                    es.price = rent; // [RE 0x41A00B] estate+44 = 本次租金
                    RICH4_LOGI("rent: player %d -> %d amount %d (estate %d, RE 0x419A67)", p,
                               es.owner - 1, rent, idx);
                }
            }
        }
    } else if (objId > 4000 && objId < 6000) {
        // ================= 商業用地 corp（0x41A168..0x41B074）=================
        const int idx = static_cast<int>(objId) - 4000;
        if (idx <= 0 || idx >= static_cast<int>(st.corps.size())) {
            return kLandingWait;
        }
        Corp& cp = st.corps[idx];
        Player& pl = st.players[p];
        const std::string label =
            big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
        if (cp.owner == 0) {
            // ---- 无主商業用地购地（0x41A86B）：价格 = +34 buildPrice × M ----
            if (pl.state37 != 0 || pl.cellTableIdx == 12) {
                return kLandingWait;
            }
            const int32_t price = static_cast<int32_t>(cp.buildPrice) * st.moneyMul;
            if (price > pl.cash) {
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("buy corp: cash %d < price %d (RE 0x41A89D)", pl.cash, price);
                return kLandingWait;
            }
            char text[160];
            // [RE 0x4639E1] "%s\n\n費用:%d元\n\n是否買下此地?"
            std::snprintf(text, sizeof(text), "%s\n\n費用:%d元\n\n是否買下此地?", label.c_str(),
                          price);
            bool buy = false;
            if ((pl.alive & 6) != 0) {
                buy = false; // TODO [RE 0x41D7D4] AI 决策（与 estate 共用）
            } else if (pl.alive == 1) {
                buy = confirmDialog(app, text, 220, 320);
            }
            if (!buy) {
                return kLandingWait;
            }
            const uint8_t carried = pl.cellTableIdx;
            if (carried == 7 || carried == 8 || carried == 15) {
                showMessage(app, kObjectNames[carried], 1500);
                return kLandingWait;
            }
            cp.owner = static_cast<uint8_t>(p + 1);
            app.audio().playEffectSlot(17); // [RE 0x4823D2] 买地音效
            buildMiniMapMarks(app);
            if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3) {
                static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
                int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
                if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                    d += 0xF400;
                }
                cp.expireDate = static_cast<uint32_t>(d);
            }
            pl.cash -= price;
            RICH4_LOGI("buy corp: corp %d price %d owner %d (RE 0x41A86B)", idx, price, p + 1);
        } else if (cp.owner == static_cast<uint8_t>(p + 1)) {
            // ---- 自己的商業用地 ----
            if (cp.type == 0) {
                // 建设施（0x41A1E0）：付 +34×M → selectFacilityDialog（AI rand%4+1）→ type、++sub
                const int32_t cost = static_cast<int32_t>(cp.buildPrice) * st.moneyMul;
                if (cost > pl.cash) {
                    showMessage(app, "您的現金不足！", 1500);
                    RICH4_LOGI("build facility: cash %d < cost %d (RE 0x41A216)", pl.cash, cost);
                    return kLandingWait;
                }
                uint8_t facility = 0;
                bool chosen = false;
                if ((pl.alive & 6) != 0) {
                    facility = static_cast<uint8_t>(std::rand() % 4 + 1); // [RE 0x41A24D]
                    chosen = true;
                } else if (pl.alive == 1) {
                    const int r = selectFacilityDialog(app, 0); // [RE 0x41A22A]
                    if (r >= 0) {
                        facility = static_cast<uint8_t>(r);
                        chosen = true;
                    }
                }
                if (!chosen) {
                    return kLandingWait;
                }
                const uint8_t carried = pl.cellTableIdx;
                if (carried == 7 || carried == 8 || carried == 15) {
                    showMessage(app, kObjectNames[carried], 1500);
                    return kLandingWait;
                }
                cp.type = facility;
                ++cp.sub;                       // [RE 0x41A27C] ++sub（建设施即 1 级）
                app.audio().playEffectSlot(18); // [RE 0x4823DA]
                pl.cash -= cost;
                RICH4_LOGI("build facility: corp %d type %u sub %u cost %d (RE 0x41A1E0)", idx,
                           cp.type, cp.sub, cost);
            } else if (cp.type == 4) {
                // 研究所（type4）owner 停留：研发道具（labDevelopDialog 0x44101D）
                // 条件：sub!=0 且 flag(+28) 低半字节为 0（0x41B0FC）
                if (cp.sub != 0 && (cp.flag & 0x0F) == 0) {
                    RICH4_LOGI("lab develop dialog TODO (corp %d sub %u, RE 0x41B106)", idx,
                               cp.sub);
                }
                return kLandingWait;
            } else {
                // 设施升级（0x41A2B3）：+36 feeTable[0]×M，上限 kFacilityMaxLevel[type]
                const uint8_t maxLv = (cp.type < 5) ? kFacilityMaxLevel[cp.type] : 1;
                if (cp.sub >= maxLv) {
                    return kLandingWait;
                }
                const int32_t cost = static_cast<int32_t>(cp.feeTable[0]) * st.moneyMul;
                if (cost > pl.cash) {
                    showMessage(app, "您的現金不足！", 1500);
                    RICH4_LOGI("upgrade facility: cash %d < cost %d (RE 0x41A2E6)", pl.cash, cost);
                    return kLandingWait;
                }
                char text[160];
                // [RE 0x46396D] "%s\n\n升級費用:%d元\n\n是否升級？"
                std::snprintf(text, sizeof(text), "%s\n\n升級費用:%d元\n\n是否升級？",
                              label.c_str(), cost);
                bool up = false;
                if ((pl.alive & 6) != 0) {
                    up = true; // [RE 0x41A310] AI 直接升级
                } else if (pl.alive == 1) {
                    up = confirmDialog(app, text, 220, 320); // [RE 0x41A31E]
                }
                if (!up) {
                    return kLandingWait;
                }
                const uint8_t carried = pl.cellTableIdx;
                if (carried == 7 || carried == 8 || carried == 15) {
                    showMessage(app, kObjectNames[carried], 1500);
                    return kLandingWait;
                }
                pl.cash -= cost;
                ++cp.sub;                       // [RE 0x41A35D]
                app.audio().playEffectSlot(18); // [RE 0x4823DA]
                RICH4_LOGI("upgrade facility: corp %d type %u sub %u cost %d (RE 0x41A2B3)", idx,
                           cp.type, cp.sub, cost);
            }
        } else {
            // ---- 他人商業用地收费（0x41A370）----
            // 守卫：无设施(sub==0)/公園(type0)/研究所(type4) 不收费
            if (cp.sub == 0 || cp.type == 0 || cp.type >= 4) {
                return kLandingWait;
            }
            app.audio().playEffectSlot(20); // [RE 0x4823EA]
            const uint8_t costIdx = (cp.type < 5) ? kCorpCostMap[cp.type] : 0;
            const char* feeName = kCostNames[costIdx < 13 ? costIdx : 0];
            if (ownerCanCollectRent(app, cp.owner - 1, cp.flag, feeName)) {
                int32_t base =
                    static_cast<int32_t>(cp.feeTable[cp.sub < 6 ? cp.sub : 0]) * st.moneyMul;
                if (cp.flag != 0) {
                    base *= 2; // [RE 0x41A44C] flag 非 0 翻倍
                }
                const Player& owner = st.players[cp.owner - 1];
                const char* ownerName = owner.name ? owner.name : "";
                char text[192];
                int32_t rent = 0;
                int hotelDays = 0;
                if (cp.type == 1) {
                    // 旅館：休息天數 = 转盘(1)（完整 UI：panel[69] + 减速停止）；rent = base × days
                    hotelDays = roulettePrompt(app, 1, ownerName); // [RE 0x41A458/0x44090E]
                    rent = base * hotelDays;
                    // [RE 0x4639FF] "休息%d天\n\n費用%d元！"
                    std::snprintf(text, sizeof(text), "休息%d天\n\n費用%d元！", hotelDays, rent);
                    showMessage(app, text, 1500);
                } else if (cp.type == 2) {
                    // 購物中心：消費倍數 = 转盘(2)（完整 UI：panel[70] + 减速停止）
                    const int mult = roulettePrompt(app, 2, ownerName); // [RE 0x41A4B4/0x44090E]
                    rent = base * mult;
                    // [RE 0x463A14] "您的消費金額為\n\n%dx%d倍=%d元"
                    std::snprintf(text, sizeof(text), "您的消費金額為\n\n%dx%d倍=%d元", base, mult,
                                  rent);
                    showMessage(app, text, 1500);
                } else {
                    // 加油站（type3）：500 × 載具倍率 × 本次步數 × M（步行免付）
                    const int travel = pl.travel & 3;
                    if (travel == 0) {
                        return kLandingWait; // [RE 0x41A4ED] 步行免付
                    }
                    const int mult = 1 << (travel - 1); // [RE 0x41A4FC]
                    rent = 500 * mult * st.diceValue * st.moneyMul;
                    // [RE 0x463A31] "加油站\n\n董事長%s\n\n請付%d元%s"
                    std::snprintf(text, sizeof(text), "加油站\n\n董事長%s\n\n請付%d元%s", ownerName,
                                  rent, feeName);
                    showMessage(app, text, 1500);
                }
                // [RE 0x41A581] 通用后处理：神明调整 → 转账（flags=0 收款入银行）→ corp+48
                rent = applyGodRentModifier(app, p, rent, feeName);
                if (rent > 0) {
                    transferMoney(app, p, cp.owner - 1, rent, 0);
                    cp.lastFee = rent; // [RE 0x41A75E] corp+48 = 最近收费
                    RICH4_LOGI("corp fee: player %d -> %d amount %d (corp %d type %u, RE 0x41A370)",
                               p, cp.owner - 1, rent, idx, cp.type);
                }
                // [RE 0x41A761] 旅館住宿状态：BYTE0 = days-1（0→0x80）、累计天数、住宿欠款
                if (cp.type == 1 && hotelDays > 0 && pl.alive != 0 && st.sceneRequest == 0) {
                    const uint8_t stay = static_cast<uint8_t>(hotelDays - 1);
                    pl.stateFlags = (pl.stateFlags & 0xFFFFFF00u) |
                                    static_cast<uint32_t>(stay != 0 ? stay : 0x80);
                    pl.byte66 = static_cast<uint8_t>(pl.byte66 + hotelDays); // byte_496BAA
                    // [RE 0x41A7BC] 住宿欠款 20×M×days（addPlayerDebt 矩阵未建模，仅日志）
                    RICH4_LOGI("hotel stay: player %d days %d owe %d (RE 0x41A7F4)", p, hotelDays,
                               20 * st.moneyMul * hotelDays);
                }
            }
        }
    }
    // loc_41B077 → sub_40F381/sub_448A7E/refreshGameUi → loc_41B111：等待 kLandingWait 帧
    return kLandingWait;
}

// [RE 0x419572] rollDice
// 依据: 0x419572 反编译; v2 = byte_496B7A[当前玩家]（骰子个数）,
//       v14[i] = rand()%6+1，返回 sum(v14)
// 差异: 骰子精灵绘制与 sub_45285E(500) 延时见 B4
int rollDice(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    const int n = std::max(1, static_cast<int>(st.players[p].diceCount));
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        const int v = std::rand() % 6 + 1;
        st.diceValues[i] = v;
        sum += v;
    }
    st.showDice = true;
    st.diceValue = sum; // [RE 0x48BAFC] g_diceValue = 本次行走总步数（供步数计费公式）
    RICH4_LOGI("roll dice: %d (RE 0x419572)", sum);
    return sum;
}

// [RE 0x40C05C] moveOneStep
// 依据: 0x40C05C 反编译; 普通玩家: 从当前 cellEnt 的 exits[4] 中排除来向
//       （word_496B76）与占用出口，随机选下一格；占用掩码 256<<player；
//       速度 = byte_4749D8[travel&3]（或 flt_4631DC），浮点插值 g_playerSpriteX/Y；
//       到达后 byte_496B78 = sub_407A8C(上一步, 当前)，sub_40FC00 更新携带物件
// 差异: 载具状态 alive&0x30 的上下车分支、AI 槽 4..8 分支、占用位 0x40000000 判定待补
bool moveOneStep(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    Player& pl = st.players[p];
    if (st.stepFrames == 0) {
        if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
            return true;
        }
        // [RE 0x40C05C] 每步开始重载行走资源（原版 sub_40B93B）
        loadWalkResources(st, p);
        // [RE 0x4542CE + 0x48234A] 每步开始播放移动音效（moveSoundIndex 为游戏音效槽 11..15，
        //   经 dword_48234A 映射到 Effect.mkf）
        if (st.moveSoundIndex > 0) {
            app.audio().playEffectSlot(st.moveSoundIndex);
        }
        const int startX = st.cellEnts[pl.cellEntId].x;
        const int startY = st.cellEnts[pl.cellEntId].y;
        // 收集可走出口（排除来向）。cellEnt id 为 u16，候选必须用 uint16_t（原版存 word 数组）
        uint16_t cand[4];
        int n = 0;
        uint32_t exitBit = 0x40000000u; // [RE 0x40C05C] v6 起始位（bit30 起）
        for (int k = 0; k < 4; ++k) {
            const uint16_t exitId = st.cellEnts[pl.cellEntId].exits[k];
            if (exitId && exitId != pl.prevCellEnt && exitId < st.cellEnts.size() &&
                (exitBit & st.cellEnts[pl.cellEntId].occMask) == 0) {
                cand[n++] = exitId;
            }
            exitBit >>= 1;
        }
        uint16_t nextId = n ? cand[std::rand() % n] : pl.prevCellEnt;
        if (nextId == 0 || nextId >= st.cellEnts.size()) {
            nextId = pl.cellEntId;
        }
        // 更新来向/当前格/占用掩码
        pl.prevCellEnt = pl.cellEntId;
        const uint32_t mask = 256u << p;
        st.cellEnts[pl.cellEntId].occMask &= ~mask;
        st.cellEnts[nextId].occMask |= mask;
        pl.cellEntId = nextId;
        // [RE 0x40C05C] 移动中即朝本步方向（原版每帧设 byte_496B78=sub_407A8C(来向,当前)），
        //   否则角色朝上一步方向走、到新格才转向 → 转角混乱
        pl.dir = static_cast<uint8_t>(facingBetween(st, pl.prevCellEnt, pl.cellEntId));
        updatePlayerCarriedObjects(st, p);

        st.stepTargetX = st.cellEnts[nextId].x;
        st.stepTargetY = st.cellEnts[nextId].y;
        const int dx = st.stepTargetX - startX;
        const int dy = st.stepTargetY - startY;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        double frames;
        if (st.playerMoveGroup[p] || (pl.alive & 0x30)) {
            frames = dist * static_cast<double>(kMoveScale);
        } else {
            frames = dist / static_cast<double>(kMoveSpeed[pl.travel & 3]);
        }
        st.playerStepX[p] = static_cast<float>(pl.spriteX);
        st.playerStepY[p] = static_cast<float>(pl.spriteY);
        st.stepFrames = static_cast<int>(frames);
        st.stepHalf = st.stepFrames >> 1;
        if (st.stepFrames == 0) {
            st.stepFrames = 1;
        }
        const double f = static_cast<double>(st.stepFrames);
        st.stepDeltaX = static_cast<float>(static_cast<double>(dx) / f);
        st.stepDeltaY = static_cast<float>(static_cast<double>(dy) / f);
    }
    // [RE 0x40C05C] 行走动画帧（byte_498EA3）递增，按当前资源每方向帧数回绕。
    //   原版游戏循环帧率低，走路动画约 16fps；按真实时间门控换帧，
    //   避免 16ms tick（~60fps）下动画循环偏快。
    {
        const uint64_t now = SDL_GetTicks();
        if (now - st.walkAnimLastMs >= 60) {
            st.walkAnimLastMs = now;
            const int slot = 2 * st.playerActionState[p] + st.playerMoveGroup[p];
            int perDir = 8;
            if (slot >= 0 && slot < 13 && st.walkRes[p][slot].frameCount() > 0) {
                perDir = st.walkRes[p][slot].frameCount() / 8;
            }
            if (perDir <= 0) {
                perDir = 1;
            }
            st.playerMoveFrame[p] = (st.playerMoveFrame[p] + 1) % perDir;
        }
    }
    --st.stepFrames;
    if (st.stepFrames <= 0) {
        pl.spriteX = static_cast<uint16_t>(st.stepTargetX);
        pl.spriteY = static_cast<uint16_t>(st.stepTargetY);
        pl.dir = static_cast<uint8_t>(facingBetween(st, pl.prevCellEnt, pl.cellEntId));
        return true;
    }
    st.playerStepX[p] += st.stepDeltaX;
    st.playerStepY[p] += st.stepDeltaY;
    pl.spriteX = static_cast<uint16_t>(static_cast<int>(st.playerStepX[p]));
    pl.spriteY = static_cast<uint16_t>(static_cast<int>(st.playerStepY[p]));
    return false;
}

// [RE 0x40C912] checkPlayerAction
// 依据: 0x40C912 反编译; a1=1 时返回可行动标志（alive）；
//       a1=0 时有状态效果则 sub_40DD1F（自动移动）或显示状态文本；g_playerState37 → -1
// 差异: 状态文本/语音（sub_44808A/sub_452946/sprintf/sub_440CAC/sub_44EF41）M2 接入
int checkPlayerAction(Application& app, int a1) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    int v1 = 0;
    if (p >= 4) {
        const int s = p - 4;
        if (!st.slotBusy2[s] && !a1 && !st.slotTimerA[s] && !st.slotTimerC[s]) {
            return 2;
        }
    } else {
        const uint8_t v2 = st.players[p].alive;
        if (v2) {
            if (a1) {
                if ((v2 & 0x30) == 0 && !st.players[p].stateFlags && !st.players[p].byte54) {
                    v1 = v2;
                }
            } else if (st.players[p].stateFlags || st.players[p].byte54) {
                if ((v2 & 0x30) != 0) {
                    startPlayerMove(app);
                } else {
                    RICH4_LOGI("player %d status effect active (RE 0x40C912, M2 TODO)", p);
                }
            } else if (st.players[p].state37) {
                startPlayerMove(app);
                return -1;
            } else {
                v1 = v2;
            }
        }
    }
    return v1;
}

// [RE 0x418E7F] calcPlayerWait
// 依据: 0x418E7F 反编译; sub_40C912(1) 非 0 → byte_498EA5 = sub_41982D(当前格)，
//       否则 -125（0x83，bit7 置位 = 下一位玩家）
void calcPlayerWait(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (checkPlayerAction(app, 1)) {
        st.playerActionWait[p] = static_cast<int8_t>(landingEvent(app, st.players[p].cellEntId));
    } else {
        st.playerActionWait[p] = -125;
    }
}

// [RE 0x418C55] beginPlayerTurn
// 依据: 0x418C55 反编译; dword_475114 待处理玩家 → sub_40B93B/sub_40B93B 重置；
//       sub_40C912(0) 返回: 0/1 → 等待玩家操作（sub_4196F1 + setPauseDraw(1)）,
//       2/5 → sub_40DD1F（AI/自动）
// 差异: dword_475114（传送/移动待处理玩家）与 sub_41D546 面板刷新待补
void beginPlayerTurn(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    // sub_40C912(0)
    const int v5 = checkPlayerAction(app, 0);
    switch (v5) {
        case 0:
            // sub_418E7F + byte_46CAFB=1（原版 case 0 分支）
            calcPlayerWait(app);
            st.gameStateActive = true;
            break;
        case 1:
            // 等待玩家操作：sub_4196F1（byte_46CAFD=1 + 前进面板）+ setPauseDraw(1)
            enablePlayerControl(app);
            // AI 玩家自动前进（原版由 AI 决策逻辑触发 sub_40DD1F）
            if (st.players[p].kind == 2) {
                disablePlayerControl(app);
                startPlayerMove(app);
            }
            break;
        case 2:
        case 5:
            startPlayerMove(app);
            break;
        default:
            break;
    }
    RICH4_LOGI("begin player %d turn: type=%d (RE 0x418C55)", p, v5);
}

// [RE 0x4196F1] enablePlayerControl
// 依据: 0x4196F1 → byte_46CAFD = 1; sub_417191(1)（绘制前进面板）
void enablePlayerControl(Application& app) {
    app.gameState().gamePlayerControl = true;
    // sub_417191(1)：前进面板绘制（A6 接入，当前由 game_panel 每帧绘制）
}

// [RE 0x419703] disablePlayerControl
// 依据: 0x419703 → byte_46CAFD = 0（并跳转 sub_417191 尾部）
void disablePlayerControl(Application& app) {
    app.gameState().gamePlayerControl = false;
}

// [RE 0x40DD1F] startPlayerMove
// 依据: 0x40DD1F 反编译; 人类(<4): alive&0x30 → dword_48BAF8=1,state=1；
//       byte_496BA0 → state=0,wait=2；byte_496BA1 → dword_48BAF8=1,state=1；
//       否则 state=2（掷骰）。AI(4..7): dword_48BAF8=rand()%9+2,state=1。
//       玩家 8: dword_48BAF8=9,state=1。末尾 byte_498EA3=0, byte_46CAFB=1
void startPlayerMove(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 4) {
        if ((st.players[p].alive & 0x30) != 0) {
            st.remainingSteps = 1;
            st.playerActionState[p] = 1;
        } else if (st.players[p].skipMove) {
            st.playerActionState[p] = 0;
            st.playerActionWait[p] = 2;
        } else if (st.players[p].fixedStep) {
            st.remainingSteps = 1;
            st.playerActionState[p] = 1;
        } else {
            st.playerActionState[p] = 2;
        }
        if (st.playerActionState[p] == 1) {
            st.moveSoundIndex = st.playerMoveGroup[p] ? 15 : (st.players[p].travel & 3) + 11;
        }
    } else if (p >= 8) {
        st.remainingSteps = 9;
        st.playerActionState[p] = 1;
        st.moveSoundIndex = 9;
    } else {
        const int s = p - 4;
        if (st.slotFlag6[s]) {
            st.playerActionState[p] = 0;
            st.playerActionWait[p] = -126;
        } else {
            st.remainingSteps = st.slotFlag7[s] ? 1 : (std::rand() % 9 + 2);
            st.playerActionState[p] = 1;
        }
        if (st.playerActionState[p]) {
            st.moveSoundIndex = st.playerMoveGroup[p] ? 15 : 11;
        }
    }
    st.playerMoveFrame[p] = 0;
    st.gameStateActive = true;
}

// [RE 0x41CF67] advanceDay
// 依据: 0x41CF67 反编译; byte_46CB06 音乐淡出；sub_452117 日期推进；++g_dayCount；
//       checkVictory(0) 未结束则每日处理（股票/月度事件/住宅用地/商業用地到期）
// 差异: 音乐淡出（sub_454ACB/sub_454D91）、checkVictory（M3）、
//       stockTick/sub_423ACF/sub_428475/sub_452444/sub_42BA97/sub_431712/sub_439BFA 待接入；
//       当前实现日期推进 + 住宅用地/商業用地到期回收 + 小地图标记刷新
void advanceDay(Application& app) {
    GameState& st = app.gameState();
    const int newMonth = advanceDate(st);
    ++st.dayCount;

    // 住宅用地/商業用地到期回收（原版 estates+23/+48、corps+28/+52 与 dword_497160 比较）
    bool refreshed = false;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        Estate& es = st.estates[i];
        if ((es.flag & 0xF0) != 0) {
            es.flag = static_cast<uint8_t>(es.flag - 16);
            if ((es.flag & 0xF0) == 0) {
                es.flag = 0;
            }
        }
        if (es.expireDate == st.gameDate) {
            es.owner = 0;
            es.expireDate = 0;
            refreshed = true;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        Corp& cp = st.corps[i];
        if ((cp.flag & 0xF0) != 0) {
            cp.flag = static_cast<uint8_t>(cp.flag - 16);
            if ((cp.flag & 0xF0) == 0) {
                cp.flag = 0;
            }
        }
        if (cp.expireDate == st.gameDate) {
            cp.owner = 0;
            cp.expireDate = 0;
            refreshed = true;
        }
    }
    (void)newMonth;
    (void)refreshed;
    // [RE 0x41CF67] 音乐切换计时器（byte_46CB06）：低4位递减到0 → 切下一首可选曲目
    if ((st.musicTimer & 0xF) != 0) {
        --st.musicTimer;
        if ((st.musicTimer & 0xF) == 0) {
            app.audio().playNextMusic();
            st.musicTimer = 0x33; // 重置（原版由事件 sub_452444 设 0x33/0x11）
        }
    }
    RICH4_LOGI("advance day: %d/%d/%d dayCount=%d (RE 0x41CF67)",
               static_cast<int>((st.gameDate >> 16) & 0xFFFF),
               static_cast<int>((st.gameDate >> 8) & 0xFF), static_cast<int>(st.gameDate & 0xFF),
               st.dayCount);
}

// [RE 0x418EBD] nextPlayer
// 依据: 0x418EBD 反编译; 当前玩家 +1；越过 g_playerCount 进入事件槽 4；
//       4..7 检查 byte_498DF2 占用；8 → 回 0 并过天；跳过死亡玩家；
//       byte_46CAFF（跳过请求）→ sub_41906A(1) + alive[0]&=~4；
//       v1 时 advanceDay；sub_41C84F 状态效果倒计时；自动存档；末尾 byte_498EA0|=0x80
// 差异: sub_41C84F（状态效果倒计时）、sub_40F381/sub_448A7E（槽渲染）、
//       自动存档（saveGameToSlot）待接入
void nextPlayer(Application& app) {
    GameState& st = app.gameState();
    st.manualView = false;
    int cur = st.currentPlayer;

    // 玩家 8 特殊分支
    if (cur == 8) {
        // byte_498E70/72 事件槽回退（M2）
        st.playerActionFlags[cur] |= 0x80;
        return;
    }
    // 有状态效果 0x30 分支
    if (cur < 4 && (st.players[cur].alive & 0x30) != 0) {
        if ((st.players[cur].alive & 0x10) != 0) {
            const uint8_t v0 = st.players[cur].byte27 & 0xF;
            if (v0 != 15) {
                st.players[cur].dir = v0;
            }
        }
        st.players[cur].alive &= 0x0F;
        st.playerActionFlags[cur] |= 0x80;
        return;
    }

    bool dayPassed = false;
    for (int guard = 0; guard < 64; ++guard) {
        const int v2 = cur + 1;
        cur = v2;
        if (v2 == st.playerCount) {
            // 玩家轮完 → 事件槽 4..7（流氓/间谍/小偷/强盗等特殊 NPC，M2）。
            //   原版 byte_498DF2 默认为 0 会选中空闲槽 4，但原版该槽有 NPC 数据；
            //   本作未实现这些 NPC，槽无数据 → 直接进入会导致 currentPlayer 指向空槽、
            //   渲染回左上角并错误掷骰。故仅当事件槽有激活 NPC 才进入，否则回绕过天。
            int npc = -1;
            for (int s = st.playerCount; s < 8; ++s) {
                if (st.players[s].alive != 0) {
                    npc = s;
                    break;
                }
            }
            if (npc >= 0) {
                cur = npc;
                break;
            }
            cur = 0;
            dayPassed = true;
            break;
        }
        if (v2 == 8) {
            // 事件槽走完 → 回绕到 0 并过天
            cur = 0;
            dayPassed = true;
            if (st.players[cur].alive == 0 && st.players[cur].spriteX != 0) {
                continue;
            }
            break;
        }
        if (v2 >= 4 && v2 < 8) {
            // 事件槽 4..7（LABEL_19）
            if (!st.slotBusy2[cur - 4]) {
                break; // 未占用 → LABEL_20 → cur>=4 → 返回
            }
            continue; // 占用 → 继续找
        }
        // v2 < playerCount（0..playerCount-1）：LABEL_20
        if (st.players[cur].alive != 0 || st.players[cur].spriteX == 0) {
            break;
        }
        // 死亡且已放置 → 跳过
    }
    st.currentPlayer = cur;

    // byte_46CAFF 跳过请求
    if (st.gameSkipRequest) {
        st.gameSkipRequest = false;
        st.players[0].alive &= ~0x04;
    }
    if (dayPassed) {
        advanceDay(app);
    }
    st.playerActionFlags[cur] |= 0x80;
    RICH4_LOGI("next player: %d day=%d (RE 0x418EBD)", cur, dayPassed ? 1 : 0);
}

// [RE 0x40D7C4] updateGameState
// 依据: 0x40D7C4 反编译; byte_498EA2 状态机:
//   0 = 收尾倒计时（byte_498EA5）→ sub_418E7F 重算（landingEvent 停留结算）/ 负数则 sub_418EBD
//   1 = 移动中: sub_40C05C 走一格，byte_48BB00 → onPlayerActionPhase，dword_48BAF8 递减
//   2 = 掷骰动画: 结束设 dword_48BAF8 = sub_419572() 并转状态 1
//   3 = 移动结束特殊移动（sub_448A7E 触发）: addPlayerDebt(owner-1, cur, 30*moneyMul) 仅记账
//       + 面向目标 + FLC 526；实际收租在 landingEvent 0x419A67
// 差异: sub_416E6D 小地图刷新（迁移为每 tick 全量重绘）、移动音效、case 3 的 FLC 视觉
void updateGameState(Application& app) {
    GameState& st = app.gameState();
    st.gameFrameTick = false; // byte_46CAFA = 0
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    // [RE 0x475114] 待入场玩家 → 播放跳伞动画（原版 sub_418C55 阻塞播放）
    if (st.pendingSpawnPlayer != 0) {
        updateParachute(app);
        return;
    }
    // [RE 0x401B9C] WinMain 内层循环：状态 0 且 byte_498EA0 高位 → sub_418C55
    if (st.playerActionState[p] == 0 && (st.playerActionFlags[p] & 0x80)) {
        st.playerActionFlags[p] &= 0x7F;
        beginPlayerTurn(app);
    }
    if (!st.gameStateActive) {
        return;
    }
    switch (st.playerActionState[p]) {
        case 0: {
            if (st.playerActionWait[p] != 0) {
                const int8_t v2 = st.playerActionWait[p];
                if ((v2 & 0x7F) != 0) {
                    st.playerActionWait[p] = static_cast<int8_t>(v2 - 1);
                }
                const int8_t v3 = st.playerActionWait[p];
                if ((v3 & 0x7F) == 0) {
                    if (v3 >= 0) {
                        calcPlayerWait(app); // 普通玩家 v0=false
                    } else {
                        st.gameStateActive = false;
                        st.playerActionWait[p] = 0;
                        nextPlayer(app);
                    }
                }
            }
            break;
        }
        case 1: {
            if (st.remainingSteps == 0) {
                // [RE 0x40D7C4 case1] 移动结束：停止载具/移动音效（原版 sub_4542E9）
                if (st.moveSoundIndex > 0) {
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                }
                // [RE 0x40D7C4] 移动结束 → 状态 3（收租/停留动画）
                st.playerActionState[p] = 3;
                st.playerMoveFrame[p] = 0;
                st.showDice = false; // 移动结束隐藏骰子
                break;
            }
            if (st.stepActionPending) {
                st.stepActionPending = false;
                onPlayerActionPhase(app);
            }
            if (st.remainingSteps && moveOneStep(app)) {
                st.stepActionPending = true;
                --st.remainingSteps;
            }
            break;
        }
        case 2: {
            // [RE 0x40D7C4 case2] 掷骰三阶段：0=角色扔骰动画 → 1=骰子滚动FLC → 2=点数停留
            const int group = st.playerMoveGroup[p];
            if (st.dicePhase == 0) {
                // 阶段0：角色扔骰动画（walkRes slot=2*state+group），帧数=每方向帧数
                //   原版 byte_498EA3 递增到 dword_498EC4[组]>>3；末帧=松手，需保持
                const int slot = 2 * 2 + group;
                int perDir = 4;
                if (slot >= 0 && slot < 13 && st.walkRes[p][slot].frameCount() > 0) {
                    perDir = st.walkRes[p][slot].frameCount() / 8;
                }
                if (perDir <= 0) {
                    perDir = 1;
                }
                if (st.playerMoveFrame[p] < perDir - 1) {
                    ++st.playerMoveFrame[p]; // 逐帧播扔骰动作（原版此阶段无音效）
                } else {
                    st.dicePhase = 1; // 停在末帧（松手），不重置回第0帧
                }
                break; // map_render 按 playerMoveFrame 画角色扔骰帧
            }
            if (st.dicePhase == 1) {
                if (!st.diceAnimOpened) {
                    st.diceAnimOpened = true;
                    st.diceLandPlayed = false;
                    const int n = std::max(1, std::min(3, static_cast<int>(st.players[p].diceCount)));
                    auto blob = st.panel.read(static_cast<size_t>(n + 3));
                    st.diceAnimActive = blob && st.diceAnim.open(std::move(*blob));
                    if (st.diceAnimActive) {
                        st.diceAnim.nextFrame();
                    } else {
                        st.remainingSteps = rollDice(app);
                        st.diceHoldStartMs = SDL_GetTicks();
                        st.dicePhase = 2;
                    }
                }
                if (st.diceAnimActive) {
                    if (!st.diceAnim.nextFrame()) {
                        st.diceAnimActive = false;
                        // [RE 0x41962F] FLC 结束 → 第二次落地音效槽 2（dword_48235A）
                        app.audio().playEffectSlot(2);
                        st.remainingSteps = rollDice(app); // 滚动结束 → 最终点数
                        st.diceHoldStartMs = SDL_GetTicks();
                        st.dicePhase = 2;
                    } else if (!st.diceLandPlayed && st.diceAnim.frameIndex() == 30) {
                        // [RE 0x451387] FLC 第30帧（落地瞬间）→ 第一次落地音效槽 2
                        //   （sub_450F04 用 dword_48C850=0x1E 触发帧 + dword_48C840=槽2）
                        app.audio().playEffectSlot(2);
                        st.diceLandPlayed = true;
                    }
                    break;
                }
            }
            // 阶段2：点数停留 500ms（sub_45285E），角色仍保持扔骰末帧
            st.showDice = true;
            if (SDL_GetTicks() - st.diceHoldStartMs >= 500) {
                // [RE 0x40D7C4 case2] 骰子动画结束、转移动前播放：
                //   ① 载具/移动槽 = 特殊组?15 : (载具&3)+11（dword_4749D4）
                //   ② 音效槽 3（dword_482362，当前玩家<4 且 cellNo 非0）
                const int moveSfx = st.playerMoveGroup[p] ? 15 : (st.players[p].travel & 3) + 11;
                app.audio().playEffectSlot(moveSfx);
                if (p < 4 && st.players[p].cellNo != 0) {
                    app.audio().playEffectSlot(3);
                }
                st.showDice = false; // 移动开始，骰子消失（原版转状态1 后不再显示骰子）
                st.playerMoveFrame[p] = 0;
                st.dicePhase = 0;
                st.diceAnimOpened = false;
                st.playerActionState[p] = 1;
            }
            break;
        }
        case 3: {
            // [RE 0x40DA4B] 移动结束特殊移动（**由 sub_448A7E 有条件触发**）：
            //   触发条件（0x448A7E）：`(travel & 0x83) == 3`（临时载具计时中）且无 state37/
            //   stateFlags.BYTE0 且落在他人有建筑的住宅用地/商業用地
            //   → 保存原朝向（g_playerRestoreDir +27）→ 面向目标格（sub_454FB4）→
            //   FLC 526 画在建筑屏幕坐标（sub_40B066 取绘制列表坐标，x-55/y-55）→
            //   结束后恢复朝向 → 转状态 0；同时记账 30*g_moneyMul（欠款矩阵，仅日志）
            // travel(+17) 编码：低2位=载具类型（0=步行/1=機車/2=汽車）、bit2-7=载具剩余回合；
            //   4n+3 = 临时载具计时中（sub_41C84F 每回合 -4，归零恢复默认载具）
            // 注意: 普通步行/機車/汽車（travel=0/1/2）**不满足条件 → 不面向/不停留**，
            //   直接转状态 0（等价原版 sub_448A7E 不设 state=3）
            // 差异: FLC 526 视觉未接入（载具计时系统未实现，触发路径暂不可达）；
            //   面向停留用 stepDwell=30 近似 FLC 时长
            const bool routeChargeAnim = ((st.players[p].travel & 0x83) == 3);
            if (st.stepDwell > 0) {
                --st.stepDwell;
                if (st.stepDwell == 0) {
                    // 面向结束：恢复原朝向（原版 byte_496B78 = g_playerRestoreDir）
                    st.players[p].dir = st.players[p].byte27;
                    st.playerActionWait[p] = 5; // 转入状态 0 倒计时后 calcPlayerWait（landingEvent）
                    st.playerActionState[p] = 0;
                }
                break;
            }
            bool faced = false;
            if (routeChargeAnim && p < 9 && st.players[p].cellEntId > 0 &&
                st.players[p].cellEntId < st.cellEnts.size()) {
                const CellEnt& ce = st.cellEnts[st.players[p].cellEntId];
                const uint16_t objId = ce.special;
                const int32_t due = 30 * st.moneyMul;
                if (objId > 2000 && objId < 4000) {
                    const int i = objId - 2000;
                    if (i > 0 && i < static_cast<int>(st.estates.size())) {
                        const Estate& es = st.estates[i];
                        // [RE 0x448A7E] 仅他人有建筑（owner≠自己 且 level≠0）才面向
                        if (es.owner != 0 && es.owner - 1 != p && es.level != 0) {
                            RICH4_LOGI("landing due: player %d owes %d (estate %d, RE 0x40DA4B)",
                                       p, due, i);
                            st.players[p].byte27 = st.players[p].dir; // 保存原朝向
                            st.players[p].dir = static_cast<uint8_t>(
                                facingFromDelta(es.x - ce.x, es.y - ce.y)); // 面向住宅用地
                            faced = true;
                        }
                    }
                } else if (objId > 4000 && objId < 6000) {
                    const int i = objId - 4000;
                    if (i > 0 && i < static_cast<int>(st.corps.size())) {
                        const Corp& cp = st.corps[i];
                        if (cp.owner != 0 && cp.owner - 1 != p && cp.sub != 0) {
                            RICH4_LOGI("landing due: player %d owes %d (corp %d, RE 0x40DA4B)", p,
                                       due, i);
                            st.players[p].byte27 = st.players[p].dir; // 保存原朝向
                            st.players[p].dir = static_cast<uint8_t>(
                                facingFromDelta(cp.x - ce.x, cp.y - ce.y)); // 面向商業用地
                            faced = true;
                        }
                    }
                }
            }
            if (faced) {
                st.stepDwell = 30; // 面向停留（近似原版 FLC 526 时长，约 0.5s @16ms tick）
                break;
            }
            st.playerActionWait[p] = 5;
            st.playerActionState[p] = 0;
            break;
        }
        default:
            st.playerActionState[p] = 0;
            break;
    }
}

// [NEW] 进入游戏循环时初始化回合状态机
// 依据: 0x401B9C 进入游戏后首次循环（byte_46CB01=1, byte_46CAFB 由 sub_418C55 置 1）
void initTurnState(Application& app) {
    GameState& st = app.gameState();
    for (int i = 0; i < 9; ++i) {
        st.playerActionState[i] = 0;
        st.playerActionWait[i] = 0;
        st.playerMoveFrame[i] = 0;
        st.playerActionFlags[i] = 0;
        st.players[i].prevCellEnt = 0;
    }
    st.currentPlayer = 0;
    st.remainingSteps = 0;
    st.stepFrames = 0;
    st.stepActionPending = false;
    st.stepDwell = 0;
    st.gamePlayerControl = false;
    st.gameLoopActive = true;
    st.gamePanelReady = true;
    st.gameStateActive = true;
    st.playerActionFlags[0] |= 0x80;
    // [RE 0x475284] 前进面板初始位置（原版 .data 初值 180,120）
    st.advancePanelX = 180;
    st.advancePanelY = 120;
    st.advancePanelBlink = 0;
}

} // namespace rich4
