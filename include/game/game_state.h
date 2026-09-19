#pragma once

#include <cstdint>
#include <vector>

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

    // [RE 0x47493C] dword_47493C 地图 GND 数据（0x407AD2 从 MAP.MKF 加载）
    std::vector<uint8_t> mapData;

    // JUMP.MKF 句柄（0x406DE7 临时打开；保留以复用地图预览图）
    MkfArchive jump;

    // MAP.MKF 句柄（0x407AD2 临时打开；地图数据指针在游戏期间常驻）
    MkfArchive map;
};

} // namespace rich4
