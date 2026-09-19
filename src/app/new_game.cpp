#include "game/app/new_game.h"

#include "game/app/game_loop.h"
#include "game/app/new_game_dialog.h"
#include "game/app/save_data.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {

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
//       确认后 dword_499114 = dword_46CB3C + 2（玩家数）等玩家初始化
//       lParam == byte_46CAFC：再次进入（游戏内重新开始/时空之旅返回）时保留上次配置
// 差异: 玩家/卡片/骰子初始化（sub_445A4D/sub_448B81/sub_44BAEA）待后续
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

    // TODO(RE 0x445A4D/0x448B81/0x44BAEA): 玩家/卡片/骰子初始化
    // TODO(RE 0x47E80C/0x47F072): 玩家与地图配置数据表
    RICH4_LOGI("new game: map=%d mode=%d players=%d travel=%d", state.mapIndex, state.gameMode,
               state.playerCount, state.newGameConfig.travelMode);
    return true;
}

// [RE 0x401543] showLoading
// 依据: 0x401543 反编译; sub_450441(dword_48A0E4, 601) 取 614400 字节全屏图,
//       memcpy 到 dword_48A08C 后 BltFast 显示
void showLoading(Application& app) {
    auto blob = app.gameState().data.read(601);
    if (!blob || blob->size() < 614400) {
        RICH4_LOGW("showLoading: Data.mkf[601] unavailable");
        return;
    }
    std::memcpy(app.surface().pixels(), blob->data(), 614400);
}

// [RE 0x407AD2] loadMap
// 依据: 0x407AD2 反编译; sub_4502FE("MAP.MKF") + sub_450441(handle, 2*(4*mode+map))
//       取 GND 地图数据（dword_47493C）; MAPDAT.MKF 提供玩家初始位置（可选）
// 差异: 玩家/物件初始化（sub_40E033/sub_40B93B/sub_40A4E1）待后续实现
bool loadMap(Application& app) {
    GameState& state = app.gameState();
    if (!state.map.load(app.gameDir() + "/Map.mkf")) {
        RICH4_LOGE("loadMap: Map.mkf load failed");
        return false;
    }
    const size_t gndIndex = static_cast<size_t>(2 * (4 * state.gameMode + state.mapIndex));
    auto gnd = state.map.read(gndIndex);
    if (!gnd) {
        RICH4_LOGE("loadMap: GND resource %zu unavailable", gndIndex);
        return false;
    }
    state.mapData = std::move(*gnd);
    RICH4_LOGI("map data loaded: %zu bytes (Map.mkf[%zu])", state.mapData.size(), gndIndex);

    // TODO(RE 0x4502FE("MAPDAT.MKF")): 玩家初始位置/物件表
    // TODO(RE 0x42915A/0x428CAF/0x40A4E1): 地图索引与棋子绘制初始化
    return true;
}

// [RE 0x4190CF] loadPanelUi
// 依据: 0x4190CF 反编译; panel.mkf 索引 0-8（dword_48BE0C/0x475118/0x48BE10/...）;
//       sub_451A5A(200,200) 与地图尺寸离屏缓冲
// 差异: 面板资源与离屏缓冲在游戏内 UI 阶段接入
void loadPanelUi(Application& app) {
    (void)app;
    // TODO(RE 0x4190CF): panel.mkf UI 资源加载
}

// [RE 0x4291D6] stockInit
// 依据: 0x4291D6 反编译; 12 只股票随机初始涨跌（rand），历史记录 dword_497328
// 差异: 股票系统在游戏内经济阶段接入
void stockInit(Application& app) {
    (void)app;
    // TODO(RE 0x4291D6): 股票市场初始化
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
