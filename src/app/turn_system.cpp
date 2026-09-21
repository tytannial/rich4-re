#include "game/app/turn_system.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/application.h"
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
        st.playerMoveGroup[p] = 1;
        if (st.playerMoveFrame[p] == 1) {
            st.playerMoveFrame[p] = 0;
        }
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
    st.playerMoveGroup[p] = 0;
    if (st.playerMoveFrame[p] == 1) {
        st.playerMoveFrame[p] = 0;
    }
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
    const uint32_t now = SDL_GetTicks();
    const uint32_t interval = static_cast<uint32_t>(std::max(1, st.parachute.speedMs()));
    if (st.parachuteLastMs == 0) {
        st.parachuteLastMs = now;
    }
    if (now - st.parachuteLastMs >= interval) {
        st.parachuteLastMs = now;
        // 帧数据播完时 nextFrame 返回 false（frameCount 含 0xF100 占位帧，不可用作结束条件）
        if (!st.parachute.nextFrame()) {
            finishParachute(app);
        } else {
            RICH4_LOGI("parachute frame %d/%d t=%u", st.parachute.frameIndex(),
                       st.parachute.frameCount(), now);
        }
    }
}

} // namespace

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

// [RE 0x41982D] landingEvent
// 依据: 0x41982D 反编译; 按 cellEnt 物件 ID（2000+ 地产 / 4000+ 公司 / 6000+ 特殊点）与
//       cellEnt+36 格子类型（1..18）处理购地/升级/收租/税/卡片/机会
// 差异: M2 实现；M1 返回 0x80（-128），等价于"无可执行事件，进入下一位"
int landingEvent(Application& app, uint16_t cellEntId) {
    (void)app;
    (void)cellEntId;
    return 0x80;
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
        // [RE 0x4540D8] 每步开始播放移动音效（moveSoundIndex = 载具/步行 11..15 → Effect.mkf）
        if (st.moveSoundIndex > 0) {
            app.audio().playEffect(st.moveSoundIndex);
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
    //   原版游戏循环帧率低，走路动画约 11fps；此处按真实时间门控换帧，
    //   避免 16ms tick（~60fps）下动画循环偏快。
    {
        const uint64_t now = SDL_GetTicks();
        if (now - st.walkAnimLastMs >= 90) {
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
//       checkVictory(0) 未结束则每日处理（股票/月度事件/地产公司到期）
// 差异: 音乐淡出（sub_454ACB/sub_454D91）、checkVictory（M3）、
//       stockTick/sub_423ACF/sub_428475/sub_452444/sub_42BA97/sub_431712/sub_439BFA 待接入；
//       当前实现日期推进 + 地产/公司到期回收 + 小地图标记刷新
void advanceDay(Application& app) {
    GameState& st = app.gameState();
    const int newMonth = advanceDate(st);
    ++st.dayCount;

    // 地产/公司到期回收（原版 estates+23/+48、corps+28/+52 与 dword_497160 比较）
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
            const uint8_t v0 = st.players[cur].byte24 & 0xF;
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
//   0 = 收尾倒计时（byte_498EA5）→ sub_418E7F 重算 / 负数则 sub_418EBD
//   1 = 移动中: sub_40C05C 走一格，byte_48BB00 → onPlayerActionPhase，dword_48BAF8 递减
//   2 = 掷骰动画: 结束设 dword_48BAF8 = sub_419572() 并转状态 1
//   3 = 特殊移动（遥控骰子/传送）
// 差异: sub_416E6D 小地图刷新（迁移为每 tick 全量重绘）、移动音效、状态 3 收租部分待补
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
                st.playerActionState[p] = 0;
                st.playerMoveFrame[p] = 0;
                st.playerActionWait[p] = 5;
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
                    ++st.playerMoveFrame[p]; // 逐帧播扔骰动作
                } else {
                    st.dicePhase = 1; // 停在末帧（松手），不重置回第0帧
                }
                break; // map_render 按 playerMoveFrame 画角色扔骰帧
            }
            if (st.dicePhase == 1) {
                if (!st.diceAnimOpened) {
                    st.diceAnimOpened = true;
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
                        st.remainingSteps = rollDice(app); // 滚动结束 → 最终点数
                        st.diceHoldStartMs = SDL_GetTicks();
                        st.dicePhase = 2;
                    }
                    break;
                }
            }
            // 阶段2：点数停留 500ms（sub_45285E），角色仍保持扔骰末帧
            st.showDice = true;
            if (SDL_GetTicks() - st.diceHoldStartMs >= 500) {
                st.showDice = false; // 移动开始，骰子消失（原版转状态1 后不再显示骰子）
                st.playerMoveFrame[p] = 0;
                st.dicePhase = 0;
                st.diceAnimOpened = false;
                st.playerActionState[p] = 1;
            }
            break;
        }
        case 3: {
            if (st.stepDwell) {
                --st.stepDwell;
                break;
            }
            if (st.playerMoveFrame[p] == 0) {
                // 到达目标格后的停留动画（M2 收租/公司分红）
                st.stepDwell = 6;
            }
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
