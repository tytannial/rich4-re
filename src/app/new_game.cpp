#include "game/app/new_game.h"

#include "game/app/game_loop.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/new_game_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/save_data.h"
#include "game/application.h"
#include "game/core/log.h"
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
    p.aiAssetPct = t[17];        // +25
    p.cash = readI32(t + 20);    // +28
    p.bank = readI32(t + 24);    // +32
    p.vehicle = t[57];           // +65
    p.kind = t[92];              // +100
}

// [RE 0x448B81 / 0x44BAEA] 洗牌：从未标记索引中取 rand()%remaining 个
// 依据: sub_448B81 清零 36 字节池后按 rand()%remaining 选取并标记
void shuffleIndices(uint8_t* out, int n) {
    uint8_t pool[64] = {};
    int remaining = n;
    for (int i = 0; i < n; ++i) {
        int pick = std::rand() % remaining;
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

// [RE 0x498E80/0x498E84/0x498E88/0x498E7C/0x498E78] MAPDAT 五表解析
// 依据: 0x407AD2 反编译; 头部 10 个 u32 = count/offset 对，各表 index 0 保留
template <typename T>
bool copyMapTable(std::vector<T>& out, const uint8_t* base, size_t size, uint32_t count,
                  uint32_t offset) {
    if (static_cast<size_t>(offset) + static_cast<size_t>(count + 1) * sizeof(T) > size) {
        return false;
    }
    out.resize(count + 1);
    std::memcpy(out.data(), base + offset, static_cast<size_t>(count + 1) * sizeof(T));
    return true;
}

bool parseMapDat(GameState& state, const std::vector<uint8_t>& d) {
    if (d.size() < 40) {
        return false;
    }
    const uint8_t* b = d.data();
    const uint32_t cellEntCount = readI32(b + 0);
    const uint32_t cellEntOff = readI32(b + 4);
    const uint32_t estateCount = readI32(b + 8);
    const uint32_t estateOff = readI32(b + 12);
    const uint32_t corpCount = readI32(b + 16);
    const uint32_t corpOff = readI32(b + 20);
    const uint32_t specPtCount = readI32(b + 24);
    const uint32_t specPtOff = readI32(b + 28);
    const uint32_t evtCellCount = readI32(b + 32);
    const uint32_t evtCellOff = readI32(b + 36);
    return copyMapTable(state.cellEnts, b, d.size(), cellEntCount, cellEntOff) &&
           copyMapTable(state.estates, b, d.size(), estateCount, estateOff) &&
           copyMapTable(state.corps, b, d.size(), corpCount, corpOff) &&
           copyMapTable(state.specPts, b, d.size(), specPtCount, specPtOff) &&
           copyMapTable(state.evtCells, b, d.size(), evtCellCount, evtCellOff);
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
    app.audio().playSceneMusic(1);

    // [RE 0x46CAFC] byte_46CAFC：非 0 时保留上次选人配置（0x406DE7 lParam!=0 分支）
    const bool restore = state.gameInited;
    if (!restore) {
        // 0x406DE7 新游戏分支：清空选项与角色，默认 {玩家数=四人, 资金=200000}
        state.newGameConfig = NewGameConfig{};
    }

    // [RE 0x404E44] 选图/选人模态（取消返回 false → 主菜单）
    if (!newGameDialog(app, restore, state.newGameConfig)) {
        RICH4_LOGI("new game cancelled (RE 0x404E44)");
        return false;
    }

    state.mapIndex = state.newGameConfig.mapIndex;
    state.playerCount = state.newGameConfig.playerCount();
    state.currentPlayer = 0;
    state.gameInited = true; // [RE 0x46CAFC] byte_46CAFC = 1

    // [RE 0x406DE7] 确认后：玩家数/人类数/当前玩家/起始资金
    state.humanCount = 0;
    state.startMoneyVal = static_cast<int32_t>(kStartMoney[state.newGameConfig.startMoneyIndex]);

    // 卡片状态清零 + 道具/库存初值
    std::memset(state.cardState60, 0, sizeof(state.cardState60));
    std::memset(state.cardPool60, 0, sizeof(state.cardPool60));
    for (int i = 0; i < 30; ++i) {
        state.propStock[i] = kPropStockInit[i]; // g_propStock[i] = byte_47FDF6[8*i]
    }
    for (int j = 0; j < 8; ++j) {
        state.misc8A[j] = kMisc8Init[j]; // byte_497320[j] = byte_47FEE6[8*j]
    }

    // 玩家初始化（每 104 字节；k >= playerCount 时清空）
    for (int k = 0; k < 4; ++k) {
        if (k >= state.playerCount) {
            state.players[k] = Player{};
            continue;
        }
        // [RE 0x445A4D] givePlayerCard(k, 1/2/3/4/8/9)（卡片系统阶段 3 接入，此处仅计数）
        for (int card : {1, 2, 3, 4, 8, 9}) {
            if (card < 9 && state.cardPool60[card] < 9) {
                ++state.cardPool60[card];
                state.players[k].cardCount = static_cast<uint8_t>(state.players[k].cardCount + 1);
            }
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
                state.players[k].aiAssetPct * (state.startMoneyVal / 100);
            state.players[k].cash = cash;
            state.players[k].bank = state.startMoneyVal - cash;
        }
        state.players[k].travel = static_cast<uint8_t>(state.newGameConfig.travelMode);
        // [RE 0x406DE7] 玩家存活标志（g_charData +21 = 0，进入游戏时置 1）
        state.players[k].alive = 1;
    }

    // [RE 0x47ECEC] g_miscTable80 = unk_47ECEC（10 x 2）
    std::memcpy(state.miscTable80, kMiscTable80, sizeof(state.miscTable80));
    std::memset(state.misc8B, 0, sizeof(state.misc8B));

    // 配置回写
    state.cfgTravel = state.newGameConfig.travelMode;
    state.cfgLandPerm = state.newGameConfig.landPermIndex;
    state.gameDaysLimit = static_cast<int32_t>(kGameDays[state.newGameConfig.gameTimeIndex]);
    state.winMoney = state.startMoneyVal * static_cast<int32_t>(
                                                kWinMoneyMul[state.newGameConfig.winCondIndex]);
    state.moneyMul = 1;
    state.dayCount = 0;
    state.turnCounter = 0;
    // [RE 0x497160] dword_497160 游戏日期初值（BYTE0=日, BYTE1=月, HIWORD=年）
    state.gameDate = (1998u << 16) | (1u << 8) | 1u;

    // [RE 0x47F072] g_stocks = off_47F072[432*mode + 108*map]（12 股票 x 9 字段）
    std::memcpy(state.stocks, kStockInit[state.gameMode * 4 + state.mapIndex],
                sizeof(state.stocks));
    // TODO(RE 0x49907C): dword_49907C = sum(flt_49698C[9*i]) * flt_463194（股票总市值）

    std::memset(state.playerShares, 0, sizeof(state.playerShares));
    std::memset(state.stockHistory, 0, sizeof(state.stockHistory));

    // [RE 0x448B81/0x44BAEA] 卡片/事件洗牌
    shuffleIndices(state.cardShuffle, 36);
    shuffleIndices(state.misc37, 37);
    state.cardShufflePos = 0;
    state.misc37Pos = 0;

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

// [RE 0x407AD2] loadMap
// 依据: 0x407AD2 反编译; sub_4502FE("MAP.MKF") + sub_450441(handle, 2*(4*mode+map))
//       取 GND 地图数据; +1 取 MAPDAT（本作无独立 MAPDAT.MKF，走 fallback 分支）;
//       加载图块/棋子精灵并初始化格子表
// 差异: 玩家地图数据（10008B/玩家）与 sub_42915A/sub_428CAF 待后续
bool loadMap(Application& app) {
    GameState& state = app.gameState();
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

    // MAPDAT（MAP.MKF 奇数索引；MAPDAT.MKF 不存在时原版走此 fallback）
    auto mdat = state.map.read(gndIndex + 1);
    if (!mdat || !parseMapDat(state, *mdat)) {
        RICH4_LOGE("loadMap: MAPDAT resource %zu invalid", gndIndex + 1);
        return false;
    }
    RICH4_LOGI("map data loaded: GND %zu B, cellEnts=%zu estates=%zu corps=%zu specPts=%zu "
               "evtCells=%zu (Map.mkf[%zu/%zu])",
               state.gnd.size(), state.cellEnts.size(), state.estates.size(), state.corps.size(),
               state.specPts.size(), state.evtCells.size(), gndIndex, gndIndex + 1);

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

    // [RE 0x48BAD8/0x48BAD4] data.mkf[517]/[519]（地产/公司标记图块）
    loadUiImage(state.estateTiles, state.data, 517);
    loadUiImage(state.mapTiles519, state.data, 519);
    // [RE 0x49692C] cellType -> data.mkf[cellType+395]（索引 1..20）
    for (int t = 1; t <= 20; ++t) {
        loadUiImage(state.cellTypeSprites[t], state.data, static_cast<size_t>(t + 395));
    }

    // [RE 0x496D08] g_cellTable：46 项 x 24 字节，+0 = kCellTypeInit
    state.cellTable.assign(46 * 24, 0);
    for (int j = 0; j < 46; ++j) {
        state.cellTable[24 * j] = kCellTypeInit[j];
    }

    // TODO(RE 0x40E033/0x40AA6C): 随机地图物件放置（sub_40E033 系列）
    // [RE 0x407AD2] 棋子动画资源初始化
    initMapEntities(app);
    // TODO(RE 0x40A4E1): 小地图背景绘制（叠加地产/公司标记）
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

// [RE 0x4291D6] stockTick
// 依据: 0x4291D6 反编译; 12 只股票随机初始涨跌（rand），历史记录 dword_497328
// 差异: 股票行情在游戏内经济阶段接入；当前仅保持 kStockInit 初值
void stockInit(Application& app) {
    (void)app;
    // TODO(RE 0x4291D6): 股票市场随机行情
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

bool loadGameFromSlot(Application& app, int slot) {
    // [RE 0x402AC5] 依据: 0x402AC5 反编译; sprintf("SAVE%d.DAT") → showLoadingScreen →
    // fseek(4) 读取全部状态 → loadMapData → 每玩家棋盘数据; 返回 1 表示成功
    GameState& state = app.gameState();
    char name[32];
    std::snprintf(name, sizeof(name), "SAVE%d.DAT", slot);
    const std::string path = app.gameDir() + "/" + name;

    std::vector<uint8_t> raw;
    if (!readSaveFile(path, raw)) {
        RICH4_LOGE("loadGameFromSlot: %s invalid", name);
        return false;
    }
    state.saveData = std::move(raw);

    SaveSlot info;
    if (!readSaveHeader(path, info)) {
        return false;
    }
    state.mapIndex = info.mapIndex;
    state.gameMode = info.gameMode;
    state.playerCount = static_cast<int>(info.playerCount);
    state.currentPlayer = 0;
    // TODO(RE 0x402AC5): 从存档恢复完整状态（dword_497160 时间戳/玩家/股票/卡片/骰子/
    //   地图物件表/每玩家棋盘数据 10008B+dwSize）——当前仅恢复地图/模式/玩家数
    RICH4_LOGI("load game slot %d: map=%d mode=%d players=%u (saveData %zu bytes)", slot,
               info.mapIndex, info.gameMode, info.playerCount, state.saveData.size());

    // 原版链路：showLoadingScreen → loadMapData → （WinMain case 1）sub_401981(1)
    if (!loadMapPreview(app)) {
        return false;
    }
    showLoading(app);
    if (!loadMap(app)) {
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
    if (!loadMap(app)) {
        return false;
    }
    loadPanelUi(app);
    stockInit(app);
    playIntro(app);
    enterGameLoop(app); // [RE 0x401981]
    return true;
}

} // namespace rich4
