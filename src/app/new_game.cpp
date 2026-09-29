#include "game/app/new_game.h"

#include "game/app/game_loop.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/new_game_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/save_data.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/render/raw_bitmap.h"
#include "game/render/surface.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rich4 {

namespace {

static_assert(sizeof(CellEnt) == 40, "CellEnt 必须匹配原版 40 字节");
static_assert(sizeof(Estate) == 52, "Estate 必须匹配原版 52 字节");
static_assert(sizeof(Corp) == 56, "Corp 必须匹配原版 56 字节");
static_assert(sizeof(SpecPt) == 52, "SpecPt 必须匹配原版 52 字节");
static_assert(sizeof(EvtCell) == 28, "EvtCell 必须匹配原版 28 字节");

// [RE 0x450441] 读取 MKF 资源并指针化为 UiImage（sub_450069）
bool loadUiImage(UiImage& img, const MkfArchive& arc, size_t index) {
    auto blob = arc.read(index);
    if (!blob) {
        RICH4_LOGW("loadUiImage: resource %zu unavailable", index);
        return false;
    }
    return img.load(std::move(*blob));
}

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int32_t readI32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) |
                                (static_cast<uint32_t>(p[3]) << 24));
}

// [RE 0x406DE7] 角色模板拷贝（g_charData[charId] 的 +8..+103）
void applyCharTemplate(Player& p, int charId) {
    p = Player{};
    if (charId < 0 || charId >= 12) {
        return;
    }
    const uint8_t* t = kCharTemplate[charId];
    p.name = kCharNames[charId];
    p.color = kCharColor[charId];
    p.spriteX = readU16(t + 0);  // +8
    p.spriteY = readU16(t + 2);  // +10
    p.cellEntId = readU16(t + 4); // +12
    p.prevCellEnt = readU16(t + 6); // +14
    p.dir = t[8];                // +16
    p.travel = t[9];             // +17
    p.diceCount = t[10];         // +18
    p.charIndex = t[11];         // +19
    p.byte20 = t[12];            // +20
    p.alive = t[13];             // +21
    p.aiCardItem = t[14];        // +22 使用卡片/道具开关
    p.aiPersonality = t[15];     // +23 个性（0/1/2）
    p.aiLoanPct = t[16];         // +24 贷款比例
    p.aiCashPct = t[17];         // +25 现金比例
    p.aiStockPct = t[18];        // +26 股票比例
    p.byte27 = t[19];            // +27 状态恢复朝向
    p.cash = readI32(t + 20);    // +28
    p.bank = readI32(t + 24);    // +32
    p.ally = t[57];              // +65 同盟对象（角色模板恒 0）
    p.kind = t[92];              // +100
}

// [RE 0x448B81 / 0x44BAEA] 洗牌：从未标记索引中取 rand()%remaining 个
// 依据: sub_448B81 清零 36 字节池后按 rand()%remaining 选取并标记
void shuffleIndices(uint8_t* out, int n) {
    uint8_t pool[64] = {};
    int remaining = n;
    for (int i = 0; i < n; ++i) {
        int pick = dbg::roll(dbg::SlotSpawn, remaining);
        int idx = 0;
        for (; idx < n; ++idx) {
            if (!pool[idx]) {
                if (pick == 0) {
                    break;
                }
                --pick;
            }
        }
        pool[idx] = 1;
        out[i] = static_cast<uint8_t>(idx);
        --remaining;
    }
}

// [RE 0x407AD2] mapDat blob → 五表 vector（单源实现见 save_data.cpp parseMapDatBlob）
bool parseMapDat(GameState& state, const std::vector<uint8_t>& d) {
    return parseMapDatBlob(state, d.data(), d.size());
}

// 加载地图预览图（JUMP.MKF[mapIndex + 4*gameMode] → state.mapPreview）。
// [RE 0x406DE7] 依据: sub_4502FE("JUMP.MKF") + sub_450441(handle, word_4991B8 + 4*word_4991B6)
// 加载 614400 字节全屏预览图到 dword_48A358（转换后存 dword_48A354）
bool loadMapPreview(Application& app) {
    GameState& state = app.gameState();
    if (!state.jump.load(app.gameDir() + "/Jump.mkf")) {
        RICH4_LOGE("loadMapPreview: Jump.mkf load failed");
        return false;
    }
    const int previewIndex = state.mapIndex + 4 * state.gameMode;
    auto preview = state.jump.read(static_cast<size_t>(previewIndex));
    if (!preview || preview->size() < 614400) {
        RICH4_LOGE("loadMapPreview: Jump.mkf[%d] invalid", previewIndex);
        return false;
    }
    state.mapPreview = std::move(*preview);
    // TODO(RE 0x4552B7): 原版对预览图做显示格式/调色板转换（dword_48A354 ← dword_48A358）
    //   SDL 侧内部统一 RGB555，无需转换
    return true;
}


// [RE 0x406DE7] newGameInit
// 依据: 0x406DE7 反编译; JUMP.MKF 地图预览 + runModal(0x404E44) 选图/选人;
//       确认后（v19==1）初始化玩家/卡片/经济/股票（0x40715F..0x4074C4 段）
//       lParam == byte_46CAFC：再次进入（游戏内重新开始/时空之旅返回）时保留上次配置
// 差异: 卡片系统（sub_445A4D givePlayerCard）与道具/股票洗牌已按原版实现核心逻辑
bool newGameInit(Application& app, bool mode1) {
    GameState& state = app.gameState();
    state.gameMode = mode1 ? 1 : 0;

    // [RE 0x4549CF] sub_4549CF(0x8001)：选人界面场景音乐（off_47E793[1] → track11.ogg）
    // [RE 0x40711A] 原版 musicPlayScene(0x8001)：bit15 = 不压栈（新游戏进入不保存当前曲目）
    app.audio().playSceneMusic(1, false);

    // [RE 0x46CAFC] byte_46CAFC：非 0 时保留上次选人配置（0x406DE7 lParam!=0 分支）
    const bool restore = state.gameInited;
    if (!restore) {
        // 0x406DE7 新游戏分支：清空选项与角色，默认 {玩家数=四人, 资金=200000}
        state.newGameConfig = NewGameConfig{};
        // [NEW] --quickstart：headless 测试开局，预填配置并跳过选图/选人模态
        //   （docs/testing.md；无原版对应——正常路径下此分支永不进入）
        if (state.debugQuickstart) {
            NewGameConfig& cfg = state.newGameConfig;
            cfg.mapIndex = state.quickMap < 0 ? 0 : (state.quickMap > 3 ? 3 : state.quickMap);
            const int np = state.quickPlayers < 2 ? 2 : (state.quickPlayers > 4 ? 4 : state.quickPlayers);
            cfg.playerCountIndex = np - 2;
            const int nh = state.quickHumans < 0 ? 0
                         : (state.quickHumans > np ? np : state.quickHumans);
            for (int i = 0; i < 4; ++i) {
                cfg.charId[i] = i < np ? i : -1;
                cfg.isAi[i] = i >= nh;
            }
            RICH4_LOGI("quickstart: prefill map=%d players=%d humans=%d, dialog skipped (RE 0x404E44 bypass)",
                       cfg.mapIndex, np, nh);
        }
    }

    // [RE 0x404E44] 选图/选人模态（取消返回 false → 主菜单）
    const bool skipDialog = state.debugQuickstart && !restore; // [NEW] headless 开局
    if (!skipDialog && !newGameDialog(app, restore, state.newGameConfig)) {
        RICH4_LOGI("new game cancelled (RE 0x404E44)");
        return false;
    }

    state.mapIndex = state.newGameConfig.mapIndex;
    state.playerCount = state.newGameConfig.playerCount();
    state.currentPlayer = 0;
    state.gameInited = true; // [RE 0x46CAFC] byte_46CAFC = 1
    // 重新开局：重置地图旋转/回合/骰子等残留状态（避免"重新遊戲"后旧局状态串场）
    state.mapRotation = 0;
    state.dicePhase = 0;
    state.remainingSteps = 0;
    state.diceAnimActive = false;
    state.diceAnimOpened = false;
    state.diceLandPlayed = false;
    state.showDice = false;
    state.musicTimer = 0;
    state.pendingSpawnPlayer = 0;
    state.gamePlayerControl = false;
    for (int k = 0; k < 9; ++k) {
        state.playerActionState[k] = 0;
        state.playerMoveGroup[k] = 0;
        state.playerMoveFrame[k] = 0;
    }

    // [RE 0x406DE7] 确认后：玩家数/人类数/当前玩家/起始资金
    state.humanCount = 0;
    state.startMoneyVal = static_cast<int32_t>(kStartMoney[state.newGameConfig.startMoneyIndex]);

    // 卡片状态清零 + 道具/库存初值
    std::memset(state.cardState60, 0, sizeof(state.cardState60));
    std::memset(state.itemStock, 0, sizeof(state.itemStock));
    std::memset(state.tradeSlots, 0, sizeof(state.tradeSlots));  // [RE 0x407485] g_miscTable336 清零
    for (int i = 0; i < 30; ++i) {
        state.propStock[i] = kPropStockInit[i]; // g_propStock[i] = byte_47FDF6[8*i]
    }
    for (int j = 0; j < 8; ++j) {
        state.misc8A[j] = kMisc8Init[j]; // byte_497320[j] = byte_47FEE6[8*j]（道具池）
    }

    // 玩家初始化（每 104 字节；k >= playerCount 时清空）
    for (int k = 0; k < 4; ++k) {
        if (k >= state.playerCount) {
            state.players[k] = Player{};
            continue;
        }
        // [RE 0x407281..0x4072C0] givePlayerCard(k, 1/2/3/4/8/9)（实为**给道具**，见 0x445A4D：
        //   g_playerCards[15k+item]++（基址 0x49915B ≡ 重写 itemStock 基址 0x49915C 的
        //   slot=item-1，与 givePlayerItem 约定一致），item<=8 受道具池
        //   g_cardPool(0x49731F=0x497320-1) 限制，即 misc8A[item-1]；上限 9/种。
        //   开局 6 件 = id1機器娃娃/2路障/3地雷/4定時炸彈/8遙控骰子/9機器工人）
        for (int item : {1, 2, 3, 4, 8, 9}) {
            if (state.itemStock[15 * k + item - 1] >= 9) {
                continue;
            }
            if (item <= 8) {
                if (state.misc8A[item - 1] == 0) {
                    continue; // 池空：原版直接返回不给
                }
                --state.misc8A[item - 1];
            }
            ++state.itemStock[15 * k + item - 1];
        }
        // memcpy(&g_players[26*k], &g_charData + 104*charId, 104)
        const int charId = state.newGameConfig.charId[k];
        applyCharTemplate(state.players[k], charId);
        // v11 = ((charId >> 31) & 1) + 1 → kind（1=人类 2=AI）
        const bool isAi = state.newGameConfig.isAi[k];
        state.players[k].kind = isAi ? 2 : 1;
        if (!isAi) {
            state.players[k].cash = state.startMoneyVal >> 1;
            state.players[k].bank = state.startMoneyVal >> 1;
            ++state.humanCount;
        } else {
            // v8 = (u8)byte_496B81[104*k] * (g_startMoneyVal / 100)
            const int cash =
                state.players[k].aiCashPct * (state.startMoneyVal / 100);
            state.players[k].cash = cash;
            state.players[k].bank = state.startMoneyVal - cash;
        }
        // [RE 0x407219..0x407241] travel=配置值；≠0 时礼物池对应载具道具扣 1
        //   （--byte_497323[travel] ≡ misc8A[travel+3]：1機車→slot4、2汽車→slot5、
        //   3→slot6）；骰子个数 = travel+1（步行1/機車2/汽車3）
        const int travel = state.newGameConfig.travelMode;
        state.players[k].travel = static_cast<uint8_t>(travel);
        if (travel != 0) {
            --state.misc8A[3 + travel];
        }
        state.players[k].diceCount = static_cast<uint8_t>(travel + 1);
        // [RE 0x406DE7/0x418D07] 存活标志沿用 g_charData +21 = 0，**跳伞落地才置位**
        //   （beginPlayerTurn 0x418C55 落地设 g_playerAlive = g_playerKind）——未入场玩家
        //   在原版对所有目标/统计/排行不可见（g_playerAlive 语义）；kind 已在上方设定
        state.players[k].alive = 0;
    }

    // [RE 0x47ECEC] g_miscTable80 = unk_47ECEC：开局关押初值（槽4/5 busy=1 監獄、槽6/7 busy=2 醫院、
    //   槽8 busy=3 機器娃娃）；其余字段全 0（坐标/格 0 → 未释放前渲染跳过）。
    //   [RE 0x498DF2 == 0x498E32] busy 即旧文档误称的"DF2 槽占用"（原版无第二字段）。
    std::memcpy(state.npcSlots, kMiscTable80, sizeof(state.npcSlots));

    // [RE 0x40732D/0x40733E] memset(g_jailFlags, 0, 8) / memset(g_hospitalFlags, 0, 8)
    std::memset(state.jailFlags, 0, sizeof(state.jailFlags));
    std::memset(state.hospitalFlags, 0, sizeof(state.hospitalFlags));
    // [RE 0x40734F..0x407363] 开局默认在押事件槽 NPC（4..7）：
    //   byte_496B34/B35 = g_jailFlags[4]/[5] = 1（小偷/強盜，監獄）
    //   byte_496B66/B67 = g_hospitalFlags[6]/[7] = 1（流氓/間諜，醫院）
    //   关押态由上面 npcSlots[0..3].busy = 1/1/2/2 表示（nextPlayer 跳过 busy!=0 的槽、
    //   渲染不绘制），释放见 releaseEventNpc 0x43D7BF/0x43EE6E。见 498df0-event-slot-npc.md。
    state.jailFlags[4] = 1;
    state.jailFlags[5] = 1;
    state.hospitalFlags[6] = 1;
    state.hospitalFlags[7] = 1;

    // 配置回写
    state.cfgTravel = state.newGameConfig.travelMode;
    state.cfgLandPerm = state.newGameConfig.landPermIndex;
    state.gameDaysLimit = static_cast<int32_t>(kGameDays[state.newGameConfig.gameTimeIndex]);
    state.winMoney = state.startMoneyVal * static_cast<int32_t>(
                                                kWinMoneyMul[state.newGameConfig.winCondIndex]);
    // [NEW] 目标局配置日志（选人界面 gameTimeIndex/winCondIndex → gameDaysLimit/winMoney，
    //   checkVictory 0x41D89E 消费；0=无限不判）
    RICH4_LOGI("new game target: gameTimeIndex=%d winCondIndex=%d -> daysLimit=%d winMoney=%d "
               "(RE 0x407389/0x4073A3)",
               state.newGameConfig.gameTimeIndex, state.newGameConfig.winCondIndex,
               state.gameDaysLimit, state.winMoney);
    state.moneyMul = 1;
    state.dayCount = 0;
    state.turnCounter = 0;
    // [RE 0x497160] 0x406DE7 全函数不写 dword_497160（游戏日期）：
    //   开局日期沿用当前值 = 0x411E8F 启动钳位系统日期 / 0x4119E3 设置-更改日期 / 0x411AA3 重新遊戲

    // [RE 0x47F072] g_stocks = off_47F072[432*mode + 108*map]（12 股票 x 9 字段）
    std::memcpy(state.stocks, kStockInit[state.gameMode * 4 + state.mapIndex],
                sizeof(state.stocks));
    // [RE 0x40744D] dword_49907C = Σ(stocks[i][3] 基准价) × flt_463194(10.0)（股票总市值）
    {
        double total = 0.0;
        for (int i = 0; i < 12; ++i) {
            total += state.stocks[i][3];
        }
        state.stockTotalValue = static_cast<int32_t>(total * 10.0);
    }

    std::memset(state.playerShares, 0, sizeof(state.playerShares));
    std::memset(state.playerAvgCost, 0, sizeof(state.playerAvgCost));
    std::memset(state.stockHistory, 0, sizeof(state.stockHistory));
    std::memset(state.stockHalted, 0, sizeof(state.stockHalted));
    std::memset(state.stockNews, 0, sizeof(state.stockNews));
    std::memset(state.stockVolume, 0, sizeof(state.stockVolume));
    // [RE 0x496988] 保留股份初值 = kStockInit 字段[2] 低 u16（10000/5000）
    for (int i = 0; i < 12; ++i) {
        state.stockReserved[i] = static_cast<int32_t>(
            static_cast<uint16_t>(*reinterpret_cast<const int32_t*>(&state.stocks[i][2])));
    }
    state.stockMarketClosed = 0;
    state.stockDivPeriod = 1;
    state.publicFund = 0;
    state.marketIndex = 0;
    state.stockGlobalDrift = 0.0f;
    refreshBankStockShares(app); // [RE 0x42915A] 初始交易量
    // 注: specPt 初始化与股票→specPt 映射在 loadMap（parseMapDat）之后执行（见下方）

    // [RE 0x448B81/0x44BAEA] 新闻/命运事件顺序洗牌
    shuffleIndices(state.newsOrder, 36);
    shuffleIndices(state.fateOrder, 37);
    state.newsPos = 0;
    state.fatePos = 0;

    RICH4_LOGI("new game: map=%d mode=%d players=%d humans=%d travel=%d money=%d", state.mapIndex,
               state.gameMode, state.playerCount, state.humanCount, state.cfgTravel,
               state.startMoneyVal);
    return true;
}

// [RE 0x401543] showLoading
// 依据: 0x401543 反编译; sub_450441(dword_48A0E4, 601) 取 614400 字节全屏图,
//       memcpy 到 dword_48A08C 后 BltFast 显示
void showLoading(Application& app) {
    auto blob = app.gameState().data.read(601);
    if (!blob) {
        RICH4_LOGW("showLoading: Data.mkf[601] unavailable");
        return;
    }
    const RawBitmap raw = decodeRawBitmap(*blob);
    if (!raw.valid()) {
        RICH4_LOGW("showLoading: Data.mkf[601] not a known RAW size (%zu B)", blob->size());
        return;
    }
    std::memcpy(app.surface().pixels(), raw.pixels,
                static_cast<size_t>(raw.width) * raw.height * 2);
}

// [RE 0x4080F5] resetSceneForReload：退出游戏/读档/重开局前复位上局残留场景状态。
// 依据: 0x4080F5（loadMapData 开头与 loadGameFromSlot 0x402B11 调用）；无条件复位
//   dword_475114(pendingSpawnPlayer)/dword_48BE18(manualView)/dword_48BE1C-BE20(视口)。
//   重写补充复位跳伞/事件 FLC/掷骰/高亮/状态机标志，杜绝视角错位、重复跳伞、动画串帧。
//   注：每玩家回合快照（0x4080F5 memset g_playerMapBlocks）在 loadMap(fresh) 内清，
//   读档路径保留存档恢复的快照，故本函数不动 snapshots[]。
void resetSceneForReload(Application& app) {
    GameState& st = app.gameState();
    st.manualView = false;
    st.viewScrolling = false;
    st.viewTargetX = st.viewTargetY = 0;
    st.viewSmoothX = st.viewSmoothY = 0;
    st.viewX = st.viewY = 0;
    st.pendingSpawnPlayer = 0;
    st.parachute = FliDecoder{};
    st.parachuteActive = false;
    st.parachuteTimer = 0;
    st.parachuteLastMs = 0;
    st.eventFlc = FliDecoder{};
    st.eventFlcActive = false;
    st.eventFlcX = st.eventFlcY = 0;
    st.diceAnim = FliDecoder{};
    st.diceAnimActive = false;
    st.diceAnimOpened = false;
    st.dicePhase = 0;
    st.showDice = false;
    st.diceLandPlayed = false;
    st.highlightFrame = -1;
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    st.highlightShapes.clear();
    st.gameStateActive = false;
    st.stepActionPending = false;
}

// [RE 0x407AD2] loadMap
// 依据: 0x407AD2 反编译; sub_4502FE("MAP.MKF") + sub_450441(handle, 2*(4*mode+map))
//       取 GND 地图数据; +1 取 MAPDAT（本作无独立 MAPDAT.MKF，走 fallback 分支）;
//       加载图块/棋子精灵并初始化格子表。
// 参数: freshMapDat=true（新游戏）从 MAP.MKF 读 MAPDAT + 重置 cellTable/初始神明/specPt 初值
//   （原版 if(!g_mapDat) 守卫）；false（读档）跳过该块，五表 vector 与 cellTable 已由存档填好。
bool loadMap(Application& app, bool freshMapDat) {
    GameState& state = app.gameState();
    resetSceneForReload(app); // [RE 0x4080F5] loadMapData 开头复位
    if (!state.map.load(app.gameDir() + "/Map.mkf")) {
        RICH4_LOGE("loadMap: Map.mkf load failed");
        return false;
    }
    const int base = 4 * state.gameMode + state.mapIndex;
    const size_t gndIndex = static_cast<size_t>(2 * base);
    auto gnd = state.map.read(gndIndex);
    if (!gnd || gnd->size() < 10896) {
        RICH4_LOGE("loadMap: GND resource %zu unavailable", gndIndex);
        return false;
    }
    state.gnd = std::move(*gnd);
    state.gndPalette = reinterpret_cast<const uint16_t*>(state.gnd.data() + 16);
    state.gndCellIndex = reinterpret_cast<const uint16_t*>(state.gnd.data() + 528);
    state.gndBitmaps = state.gnd.data() + 10896;

    if (freshMapDat) {
        // [RE 0x407AD2] 新游戏：MAPDAT 资源 + 格子表/初始神明/specPt 初值（原版 if(!g_mapDat)）
        auto mdat = state.map.read(gndIndex + 1);
        if (!mdat || !parseMapDat(state, *mdat)) {
            RICH4_LOGE("loadMap: MAPDAT resource %zu invalid", gndIndex + 1);
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            state.snapshots[i] = TurnSnapshot{};  // [RE 0x407CCF] 清每玩家回合快照基线
        }
        // [RE 0x496D08] g_cellTable：46 项 x 24 字节，+0 = kCellTypeInit
        state.cellTable.assign(46 * 24, 0);
        for (int j = 0; j < 46; ++j) {
            state.cellTable[24 * j] = kCellTypeInit[j];
        }
        // [RE 0x407D6A] 初始物件放置：神明 1,3,5,7,9,11 + 13/14，位置随机 cellEnt
        for (int t = 1; t <= 11; t += 2) {
            createMapObject(app, t, randomCellEnt(state, 0), 0, 0);
        }
        createMapObject(app, 13, randomCellEnt(state, 0), 0, 0);
        createMapObject(app, 14, randomCellEnt(state, 0), 0, 0);
        // [RE 0x407AD2] specPt+48（sharesLeft）= 10000 - word_496988[18*stockNo]；fund 归零
        {
            const auto& stockInit = kStockInit[state.gameMode * 4 + state.mapIndex];
            for (size_t i = 1; i < state.specPts.size(); ++i) {
                const int sn = state.specPts[i].stockNo;
                const int issued = (sn >= 0 && sn < 12) ? stockInit[sn][2] : 0;
                state.specPts[i].sharesLeft = 10000 - issued;
                state.specPts[i].fund = 0;
                state.specPts[i].fundPaid = 0;
            }
        }
        state.mapRotation = 0; // [RE 0x407E05]
    }

    RICH4_LOGI("map loaded: GND %zu B, cellEnts=%zu estates=%zu corps=%zu specPts=%zu "
               "evtCells=%zu fresh=%d (Map.mkf[%zu/%zu])",
               state.gnd.size(), state.cellEnts.size(), state.estates.size(), state.corps.size(),
               state.specPts.size(), state.evtCells.size(), freshMapDat ? 1 : 0, gndIndex,
               gndIndex + 1);
    // [RE 0x408023..0x408068] 扫描 special 8002（監獄）/ 8001（醫院）格 → word_48BAE0/E2
    state.jailCellEntId = 0;
    state.hospitalCellEntId = 0;
    for (size_t i = 1; i < state.cellEnts.size(); ++i) {
        if (state.cellEnts[i].special == 8002) {
            state.jailCellEntId = static_cast<uint16_t>(i);
        } else if (state.cellEnts[i].special == 8001) {
            state.hospitalCellEntId = static_cast<uint16_t>(i);
        }
    }
    if (state.jailCellEntId == 0 || state.hospitalCellEntId == 0) {
        RICH4_LOGW("loadMap: 監獄/醫院格缺失 jail=%u hospital=%u (RE 0x408023)",
                   state.jailCellEntId, state.hospitalCellEntId);
    }

    // [RE 0x407AD2] 图块资源
    loadUiImage(state.miniMapRaw, state.map, static_cast<size_t>(base + 16));
    loadUiImage(state.miniMap, state.map, static_cast<size_t>(base + 16));
    loadUiImage(state.cellEntTiles, state.map, 24);
    loadUiImage(state.pieceTiles, state.map, 25);
    loadUiImage(state.flagTiles, state.map, 26);
    for (int n = 0; n < 5; ++n) {
        loadUiImage(state.specPointTiles[n], state.map,
                    static_cast<size_t>(n + 5 * base + 39));
    }
    // [RE 0x407ECE] 特殊点/事件格图块：dword_48AE4C[sprite] = map.mkf[sprite + 38]
    state.specTiles.assign(300, UiImage{});
    const auto loadSpecTile = [&](uint16_t sprite) {
        if (sprite == 0 || sprite >= state.specTiles.size() ||
            state.specTiles[sprite].frameCount() > 0) {
            return;
        }
        loadUiImage(state.specTiles[sprite], state.map, static_cast<size_t>(sprite) + 38);
    };
    for (size_t i = 1; i < state.specPts.size(); ++i) {
        loadSpecTile(state.specPts[i].sprite);
    }
    for (size_t i = 1; i < state.evtCells.size(); ++i) {
        loadSpecTile(state.evtCells[i].sprite);
    }
    loadUiImage(state.estateFlag, state.map, static_cast<size_t>(base + 79));
    if (state.gameMode) {
        for (int i = 0; i < 17; ++i) {
            loadUiImage(state.corpTiles[i], state.map,
                        static_cast<size_t>(17 * state.mapIndex + 104 + i));
        }
    } else {
        for (int i = 0; i < 17; ++i) {
            loadUiImage(state.corpTiles[i], state.map, static_cast<size_t>(i + 87));
        }
    }

    // [RE 0x498EB0] 棋子精灵 = map.mkf[charIndex + 27]
    for (int n = 0; n < state.playerCount && n < 9; ++n) {
        const int charIndex = state.players[n].charIndex;
        if (charIndex >= 0 && charIndex < 12) {
            loadUiImage(state.pieceSprites[n], state.map, static_cast<size_t>(charIndex + 27));
        }
    }

    // [RE 0x48BAD8/0x48BAD4] data.mkf[517]/[519]（住宅用地/商業用地标記图块）
    loadUiImage(state.estateTiles, state.data, 517);
    loadUiImage(state.mapTiles519, state.data, 519);
    // [RE 0x49692C] cellType -> data.mkf[cellType+395]（索引 1..20）
    for (int t = 1; t <= 20; ++t) {
        loadUiImage(state.cellTypeSprites[t], state.data, static_cast<size_t>(t + 395));
    }

    // [RE 0x407AD2] 棋子动画资源初始化
    initMapEntities(app);
    refreshStockSpecPtMap(app); // [RE 0x428CAF] 股票→specPt 反查（fresh 初建 / 读档重建）
    buildMiniMapMarks(app);     // [RE 0x40A4E1] rebuildMiniMap(0)
    return true;
}

// [RE 0x4190CF] loadPanelUi
// 依据: 0x4190CF 反编译; panel.mkf 索引 0-8（dword_48BE0C/0x475118/0x48BE10/0x48BE14/
//       0x48BE04/0x48BE08/0x48BDF8）; dword_475118 非 0 时跳过（幂等）
void loadPanelUi(Application& app) {
    GameState& state = app.gameState();
    if (state.panelLoaded) {
        return;
    }
    loadUiImage(state.panelFrame0, state.panel, 0);
    loadUiImage(state.panelTopBar, state.panel, 1);
    loadUiImage(state.panelFrame2, state.panel, 2);
    loadUiImage(state.panelFrame3, state.panel, 3);
    loadUiImage(state.panelFrame7, state.panel, 7);
    // panel[8] 是 72x67 无头字节控件图（非 SPR/SMP），直接取原始字节供命中
    if (auto b8 = state.panel.read(8)) {
        state.panel8Mask = std::move(*b8);
    }
    // panel.mkf[4/5/6] 是骰子滚动 FLC 动画（非 UiImage），由 turn_system 状态2 按需
    // 以 FliDecoder 播放（原版 sub_419572 → sub_45144F），此处不加载。
    state.panelLoaded = true;
    RICH4_LOGI("panel UI loaded (RE 0x4190CF)");
}

// [RE 0x401CEB] stockInit：WinMain 新游戏链在 loadPanelUi 后调用 stockTick 一次（开局行情）
// 依据: 0x401B9C case 0/4 调用序列（0x401CEB → 0x4291D6）；loadMapData 的 0x428CAF 已先建好
//   specPt 映射；读档链（case 1）不调用（行情由存档恢复，重写完整存档恢复待 P5）
// 差异: 开局若为休市日（周日/节日），stockTick 内部 stockIsClosed 判定后跳过——与原版一致
void stockInit(Application& app) {
    stockTick(app);
    RICH4_LOGI("stockInit: opening tick market=%d turn=%d (RE 0x401CEB)",
               app.gameState().marketIndex, app.gameState().turnCounter);
}

// [RE 0x415872] playIntro
// 依据: 0x415872 反编译; JUMP.MKF 45/46 资源 + sub_451677 播放 AVI
//       （AIRPLANE.AVI 或 off_4750E8[地图索引]）
// 差异: AVI 播放依赖 MCI，SDL 侧暂无替代（跳过）
void playIntro(Application& app) {
    (void)app;
    RICH4_LOGW("TODO: intro AVI playback skipped (RE 0x415872)");
}

} // namespace

bool reloadGameFromSlotData(Application& app, int slot) {
    // [RE 0x402AC5] 依据: 0x402AC5 反编译; sprintf("SAVE%d.DAT") → showLoadingScreen →
    //   sub_4080F5 → fseek(4) 读取全部状态 → mapDat 五表 + 每玩家棋盘数据 → loadMapData。
    //   本函数只做数据/地图重载（不含 enterGameLoop），供主菜单与游戏内读档共用。
    GameState& state = app.gameState();
    char name[32];
    std::snprintf(name, sizeof(name), "SAVE%d.DAT", slot);
    const std::string path = app.gameDir() + "/" + name;

    std::vector<uint8_t> raw;
    if (!readSaveFile(path, raw)) {
        RICH4_LOGE("reloadGameFromSlotData: %s invalid", name);
        return false;
    }
    state.saveData = raw;

    showLoading(app);            // [RE 0x401543 / 0x402B03] 加载画面
    resetSceneForReload(app);    // [RE 0x4080F5 / 0x402B11] 清上局视口/入场/动画

    // [RE 0x402AC5] 全字段流式解析（含 mapDat→五表、每玩家 mapBlocks/mapDatCopies 快照）
    if (!parseSaveBody(state, raw)) {
        RICH4_LOGE("reloadGameFromSlotData: %s truncated/invalid body", name);
        return false;
    }
    // [RE 0x402BAE] charIndex → 角色名（原版读档用 g_charData 指针表重定位 g_players[].name）
    for (int k = 0; k < state.playerCount; ++k) {
        const int ci = state.players[k].charIndex;
        state.players[k].name = (ci >= 0 && ci < 12) ? kCharNames[ci] : nullptr;
    }

    if (!loadMapPreview(app)) {
        return false;
    }
    if (!loadMap(app, false)) {  // mapDat/五表/cellTable 已由存档解析：仅载 GND/图块/walk/扫描
        return false;
    }
    rebuildEventNpcFromSlots(state);  // [PORT] players[4..7] 事件槽 NPC 镜像由 npcSlots 重建
    loadPanelUi(app);          // [RE 0x4190CF] 游戏内面板资源（幂等）；读档链原缺此调用
    initTurnState(app);          // 回合机重置 + flags[currentPlayer]|=0x80（enterGameLoop 再调幂等）
    RICH4_LOGI("load game slot %d: map=%d mode=%d players=%d humans=%d cur=%d date=%u "
               "cellEnts=%zu snapValid=%d/%d/%d/%d (RE 0x402AC5)",
               slot, state.mapIndex, state.gameMode, state.playerCount, state.humanCount,
               state.currentPlayer, state.gameDate, state.cellEnts.size(),
               state.snapshots[0].valid ? 1 : 0, state.snapshots[1].valid ? 1 : 0,
               state.snapshots[2].valid ? 1 : 0, state.snapshots[3].valid ? 1 : 0);
    return true;
}

bool loadGameFromSlot(Application& app, int slot) {
    if (!reloadGameFromSlotData(app, slot)) {
        return false;
    }
    enterGameLoop(app); // [RE 0x401981]
    return true;
}

bool startGame(Application& app, bool mode1) {
    // 对应 WinMain case 0/4 的调用序列（0x401B9C 反编译）：
    // sub_406DE7 → sub_401543 → sub_407AD2 → sub_4190CF → sub_4291D6 → sub_415872
    // → byte_46CAFC = 1; sub_401981(0)
    if (!newGameInit(app, mode1)) {
        return false;
    }
    showLoading(app);
    if (!loadMap(app, true)) {
        return false;
    }
    loadPanelUi(app);
    stockInit(app);
    playIntro(app);
    enterGameLoop(app); // [RE 0x401981]
    return true;
}

} // namespace rich4
