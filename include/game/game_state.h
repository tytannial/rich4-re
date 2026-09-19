#pragma once

#include <cstdint>
#include <vector>

#include "game/resource/mkf.h"

namespace rich4 {

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
