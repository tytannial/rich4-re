#pragma once

#include <cstdint>
#include <vector>

#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {

// [RE 0x46CB3C] g_newGameOptions 选人界面（0x404E44）配置结果
// 依据: 0x406DE7 中 dword_46CB3C[0..5] 为 6 项选项索引、dword_46CB54 为地图索引;
//       dword_48A35C[3*k] 为玩家 k 的角色 ID（bit31 = AI，状态 1 随机补齐时置位）
struct NewGameConfig {
    int playerCountIndex = 2; // 0=二人 1=三人 2=四人
    int startMoneyIndex = 1;  // kStartMoney 索引
    int travelMode = 0;       // 0=步行 1=機車 2=汽車
    int landPermIndex = 0;    // kLandPermText 索引
    int gameTimeIndex = 0;    // kGameTimeText / kGameDays 索引
    int winCondIndex = 0;     // kWinMoneyMul 索引
    int mapIndex = 0;         // 0-3
    int charId[4] = {-1, -1, -1, -1}; // 玩家槽角色 ID（-1 = 未选）
    bool isAi[4] = {false, false, false, false}; // 对应原版 charId bit31（AI 标志）

    // [RE 0x4990F4=2] 上一局 AI 角色遗留标记（跨局保留）
    // 依据: 0x406DE7 结尾清理把"本轮已渲染 AI"（状态 4）转 2、其余转 0;
    //       下次进入选人界面时状态 2 = 灰度 + 红X + 不可选（原版 byte_4990F4）
    bool aiUsed[12] = {};

    int playerCount() const { return playerCountIndex + 2; }
};

// [RE 0x496B68] g_players 玩家数据（原版 104 字节/项，字段按原偏移标注）
// 依据: 0x406DE7 memcpy(&g_players[26*k], &g_charData + 104*charId, 104);
//       0x417E26/0x40829D/0x415F69 等以 g_playerCash/g_playerSpriteX/... 偏移访问
struct Player {
    const char* name = nullptr; // +0  角色名（BIG5→UTF-8）
    uint16_t color = 0;         // +4  g_playerColor 0x496B6C
    uint16_t spriteX = 0;       // +8  g_playerSpriteX 0x496B70
    uint16_t spriteY = 0;       // +10 g_playerSpriteY 0x496B72
    uint16_t cellEntId = 0;     // +12 g_playerCellEntId 0x496B74
    uint16_t facing = 0;        // +14 word_496B76（移动方向）
    uint8_t dir = 0;            // +16 byte_496B78（绘制朝向 0-7）
    uint8_t travel = 0;         // +17 byte_496B79（行进方式）
    uint8_t pad18 = 0;          // +18 byte_496B7A
    uint8_t charIndex = 0;      // +19 g_playerCharIndex 0x496B7B（角色 ID）
    uint8_t pad20 = 0;          // +20
    uint8_t alive = 0;          // +21 g_playerAlive 0x496B7D
    uint8_t aiAssetPct = 0;     // +25 byte_496B81（AI 初始资金百分比）
    int32_t cash = 0;           // +28 g_playerCash 0x496B84
    int32_t bank = 0;           // +32 g_playerBank 0x496B88
    int32_t loan = 0;           // +36 g_playerLoan 0x496B8C
    uint32_t loanDate = 0;      // +44 g_playerLoanDate 0x496B94
    uint16_t points = 0;        // +48 g_playerPoints 0x496B98
    uint32_t dword50 = 0;       // +50 dword_496B9A
    uint8_t byte54 = 0;         // +54 byte_496B9E
    uint8_t state37 = 0;        // +55 g_playerState37 0x496B9F
    uint8_t cardCount = 0;      // +62 g_playerCardCnt 0x496BA6
    uint8_t cellTableIdx = 0;   // +63 byte_496BA7
    uint8_t vehicle = 0;        // +65 g_playerVehicle 0x496BA9
    uint8_t kind = 0;           // +100 g_playerKind 0x496BCC（1=人类 2=AI）
};

// MAPDAT 五表（docs/formats/mapdat.md）；原版各表 index 0 保留
// [RE 0x498E80] g_cellEnts 每项 40 字节
struct CellEnt {
    int16_t x = 0;              // +0  像素坐标
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint16_t exits[4] = {};     // +24..+30 连接道路的 cellEnt（出生点候选）
    uint16_t special = 0;       // +32 特殊标记（8001/8002）
    uint16_t sprite = 0;        // +34 图块索引（dword_474949 + 12*(idx-1)）
    uint32_t occMask = 0;       // +36 占用位掩码（256<<player）
};

// [RE 0x498E84] g_estates 每项 52 字节
struct Estate {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[19] = {};      // +4..+22
    uint8_t flag = 0;           // +23 旗帜（&1）
    uint8_t type = 0;           // +24 类型
    uint8_t owner = 0;          // +25 拥有者（玩家号+1）
    uint8_t level = 0;          // +26 等级
    uint8_t dir = 0;            // +27 朝向
    uint8_t pad28[20] = {};     // +28..+47
    int32_t price = 0;          // +48
};

// [RE 0x498E88] g_corps 每项 56 字节
struct Corp {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint8_t type = 0;           // +24
    uint8_t owner = 0;          // +25
    uint8_t sub = 0;            // +26
    uint8_t dir = 0;            // +27
    uint8_t flag = 0;           // +28 旗帜（&1）
    uint8_t pad29[27] = {};     // +29..+55
};

// [RE 0x498E7C] g_specPts 每项 52 字节
struct SpecPt {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint8_t owner = 0;          // +24
    uint8_t index = 0;          // +25
    uint8_t pad26 = 0;          // +26
    uint8_t dir = 0;            // +27
    uint8_t pad28[4] = {};      // +28..+31
    uint16_t sprite = 0;        // +32 图块索引
    uint8_t pad34[14] = {};     // +34..+47
    int32_t price = 0;          // +48
};

// [RE 0x498E78] g_evtCells 每项 28 字节
struct EvtCell {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint8_t dir = 0;            // +24
    uint8_t pad25 = 0;          // +25
    uint16_t sprite = 0;        // +26 图块索引
};

// 全局游戏状态，对应原版 DGROUP 段的全局句柄区（0x48A0xx / 0x4990xx）。
struct GameState {
    // [RE 0x48A0E4] dword_48A0E4
    // 依据: 0x4015D6 中 sub_4502FE("data.mkf") 的返回值; 0x450441 以其为资源库句柄
    MkfArchive data;

    // [RE 0x48A054] dword_48A054
    // 依据: 0x4015D6 中 sub_4502FE("speaking.mkf") 的返回值
    MkfArchive speaking;

    // [RE 0x48A05C] dword_48A05C
    // 依据: 0x4015D6 中 sub_4502FE("panel.mkf") 的返回值
    MkfArchive panel;

    // [RE 0x48A058] dword_48A058
    // 依据: 0x4015D6 中 sub_4502FE("effect.mkf") 的返回值
    MkfArchive effect;

    // [RE 0x497158] byte_497158 设置区（16 字节，格式见 docs/formats/cfg.md）
    // 依据: 0x411E8F CFG 不存在时初始化 byte_497158=1/497159=1/49715A=4/49715B=4/
    //       49715C=1/49715D=1；存在时由 CFG 前 16 字节覆盖
    uint8_t settings[16] = {1, 1, 4, 4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // [RE 0x46CB05] byte_46CB05
    // 依据: 0x401815 清理函数的幂等标志（非 0 时跳过清理）
    bool shutdownDone = false;

    // [RE 0x4991B6] word_4991B6
    // 依据: WinMain case 4 设置（新模式）; 0x406DE7 中参与资源索引 4*mode+map
    int gameMode = 0;

    // [RE 0x4991B8] word_4991B8
    // 依据: 0x406DE7 中地图索引; 0x407AD2 中参与 MAP.MKF 索引
    int mapIndex = 0;

    // [RE 0x499114] dword_499114
    // 依据: 0x406DE7 中 dword_499114 = dword_46CB3C + 2（玩家数）
    int playerCount = 4;

    // [RE 0x46CAFC] byte_46CAFC 是否已进入过游戏（新游戏配置保留标志）
    // 依据: WinMain case 0/4 调 sub_406DE7(byte_46CAFC)；进入游戏后置 1，
    //       再次进入时 lParam!=0 → 0x406DE7 保留上次选人配置并自动確定
    bool gameInited = false;

    // 选人界面配置（0x404E44 结果，跨"重新开始"保留）
    NewGameConfig newGameConfig;

    // [RE 0x49910C] dword_49910C
    // 依据: 0x417E26 中 *dword_49910C 为当前玩家索引
    int currentPlayer = 0;

    // 读档对话框选择的槽位（-1 无）；对应 0x40257A case 1 经 postModalExit 回传的索引
    int pendingLoadSlot = -1;

    // [RE 0x47493C] 完整存档数据（0x402AC5 读取的全部状态，供后续恢复）
    std::vector<uint8_t> saveData;

    // [RE 0x497160] dword_497160 游戏内日期（BYTE0=日, BYTE1=月, HIWORD=年）
    // 依据: 0x4119E3 中日期更改确认后写入 dword_48BB50/dword_497160
    uint32_t gameDate = 0;

    // [RE 0x48A354] dword_48A354 地图预览缓冲（614400 = 640*480*2 全屏 RGB555）
    // 依据: 0x406DE7 分配 0x96000 字节并由 JUMP.MKF 地图图填充
    std::vector<uint8_t> mapPreview;

    // JUMP.MKF 句柄（0x406DE7 临时打开；保留以复用地图预览图）
    MkfArchive jump;

    // MAP.MKF 句柄（0x407AD2 临时打开；地图数据指针在游戏期间常驻）
    MkfArchive map;

    // ===== 游戏内状态（0x406DE7 确认后 / 0x407AD2 / 0x4190CF）=====

    // [RE 0x496B68] g_players（原版 9 槽，仅 g_playerCount 有效）
    Player players[9];

    // [RE 0x498E80/0x498E84/0x498E88/0x498E7C/0x498E78] MAPDAT 五表（index 0 保留）
    std::vector<CellEnt> cellEnts;
    std::vector<Estate> estates;
    std::vector<Corp> corps;
    std::vector<SpecPt> specPts;
    std::vector<EvtCell> evtCells;

    // [RE 0x474945] g_gnd GND 地图数据（MAP.MKF[2*(4*mode+map)]）
    // 布局: +16 调色板 u16[256] / +528 格索引 u16[5184] / +10896 图块 8bit[5184*1024]
    std::vector<uint8_t> gnd;
    // [RE 0x48B6B4] g_gndPalette（= gnd + 16）
    const uint16_t* gndPalette = nullptr;
    // [RE 0x48BAC4] g_gndCellIndex（= gnd + 528）
    const uint16_t* gndCellIndex = nullptr;
    // [RE 0x48BACC] g_gndBitmaps（= gnd + 10896）
    const uint8_t* gndBitmaps = nullptr;

    // 地图图块资源（map.mkf）
    UiImage cellEntTiles;         // [RE 0x474949] map.mkf[24]
    UiImage flagTiles;            // [RE 0x47494D] map.mkf[26]
    UiImage pieceTiles;           // [RE 0x48AEA8] map.mkf[25]
    UiImage estateTiles;          // [RE 0x48BAD8] data.mkf[517]（地产/公司标记）
    UiImage mapTiles519;          // [RE 0x48BAD4] data.mkf[519]
    UiImage miniMap;              // [RE 0x48BADC] map.mkf[map+4*mode+16]
    UiImage miniMapRaw;           // [RE 0x48BAD0] map.mkf[map+4*mode+16]
    // [RE 0x48BADC] dword_48BADC 小地图工作副本（200x200，叠加地产/公司标记）
    std::vector<uint16_t> miniMapBuffer;
    UiImage specPointTiles[5];    // [RE 0x48AE4C] map.mkf[n+5*(map+4*mode)+39]
    UiImage estateFlag;           // [RE 0x48AE60] map.mkf[map+79+4*mode]
    UiImage corpTiles[17];        // [RE 0x48AE64] 公司图块组 1
    UiImage corpTiles2[17];       // [RE 0x48AE78] 公司图块组 2
    UiImage corpTiles3[17];       // [RE 0x48AE90] 公司图块组 3
    UiImage pieceSprites[9];      // [RE 0x498EB0] map.mkf[charIndex+27]
    UiImage pieceAnim[9];         // [RE 0x498EB4] data.mkf[21*charIndex+128+3*travel]
    UiImage cellTypeSprites[46];  // [RE 0x49692C] data.mkf[cellType+395]（索引 1-20 有效）

    // 面板资源（panel.mkf）
    UiImage panelFrame0;          // [RE 0x48BE0C] panel.mkf[0]
    UiImage panelTopBar;          // [RE 0x475118] panel.mkf[1]
    UiImage panelFrame2;          // [RE 0x48BE10] panel.mkf[2]
    UiImage panelFrame3;          // [RE 0x48BE14] panel.mkf[3]
    UiImage panelFrame7;          // [RE 0x48BE04] panel.mkf[7]
    UiImage panelFrame8;          // [RE 0x48BE08] panel.mkf[8]
    UiImage panelFrames456[3];    // [RE 0x48BDF8] panel.mkf[4..6]

    // [RE 0x496D08] g_cellTable 每项 24 字节（+0 = 类型）
    std::vector<uint8_t> cellTable;

    // ===== 经济/回合全局 =====
    int32_t startMoneyVal = 0; // [RE 0x49908C] g_startMoneyVal
    int32_t gameDaysLimit = 0; // [RE 0x49911C] g_gameDaysLimit
    int32_t winMoney = 0;      // [RE 0x499108] g_winMoney
    int32_t humanCount = 0;    // [RE 0x499104] g_humanCount
    int32_t moneyMul = 1;      // [RE 0x4990E8] g_moneyMul
    int32_t dayCount = 0;      // [RE 0x4990E4] g_dayCount
    int32_t turnCounter = 0;   // [RE 0x499100] g_turnCounter
    int32_t cfgTravel = 0;     // [RE 0x499118] g_cfgTravel
    int32_t cfgLandPerm = 0;   // [RE 0x499110] g_cfgLandPerm

    int32_t stocks[12][9] = {};      // [RE 0x496980] g_stocks
    uint8_t propStock[30] = {};      // [RE 0x499198] g_propStock
    int32_t playerShares[4][12] = {}; // [RE 0x4971A0] g_playerShares
    int32_t stockHistory[12][144] = {}; // [RE 0x497328] g_stockHistory
    uint8_t cardState60[60] = {};    // [RE 0x499120] g_cardState60
    uint8_t cardPool60[60] = {};     // [RE 0x49915C] byte_49915C
    uint8_t cardShuffle[36] = {};    // [RE 0x499090] g_miscTable36B
    uint8_t misc37[37] = {};         // [RE 0x496B38] g_miscTable37
    int32_t cardShufflePos = 0;      // [RE 0x4990E0] dword_4990E0
    int32_t misc37Pos = 0;           // [RE 0x4990B4] dword_4990B4

    uint32_t miscTable80[10][2] = {}; // [RE 0x498E28] g_miscTable80
    uint8_t misc8A[8] = {};          // [RE 0x496B30] g_misc8A
    uint8_t misc8B[8] = {};          // [RE 0x496B60] g_misc8B

    // [RE 0x499088] dword_499088 地图旋转方向（0-7）
    int mapRotation = 0;
    // [RE 0x48B2AC/0x48B2B0] 地图视口中心像素坐标（上次绘制值）
    int viewX = 0;
    int viewY = 0;
    // [RE 0x475118] dword_475118 面板资源已加载标志（0x4190CF 幂等判断）
    bool panelLoaded = false;

    // ===== 游戏内交互状态（0x417E26）=====
    // [RE 0x48BE18] dword_48BE18：非 0 时用 dword_48BE1C/BE20 作为视口中心（手动视角）
    bool manualView = false;
    // [RE 0x48BE1C/0x48BE20] 手动视口中心（小地图拖拽设置，限幅 220..2084）
    int viewTargetX = 0;
    int viewTargetY = 0;
    // [RE 0x48BE29] byte_48BE29：小地图拖拽中
    bool dragMiniMap = false;
    // [RE 0x48BE28] byte_48BE28：待处理操作（1/2=旋转，>=100=工具条按钮 index-100）
    int pendingAction = 0;
    // [RE 0x48BDE4] dword_48BDE4：工具条悬停按钮索引（-1 无）
    int topBarHover = -1;
};

} // namespace rich4
