#pragma once

#include <cstdint>

namespace rich4 {

// 选人界面（0x404E44）静态表，由 tools/gen_newgame_tables.py 从 rich4.exe 生成。
// 来源: DGROUP 段 0x46CB88..0x46CCB8（选项文本/矩形/数值）与 0x485AE8/0x485B68（通道表）

// [RE 0x46CB88] g_playerCountText
extern const char* const kPlayerCountText[3];
// [RE 0x46CBAC] g_travelModeText
extern const char* const kTravelModeText[3];
// [RE 0x46CBB8] g_landPermText
extern const char* const kLandPermText[6];
// [RE 0x46CBD0] g_gameTimeText
extern const char* const kGameTimeText[6];
// [RE 0x463138..0x463165] 面板 6 行标签
extern const char* const kPanelLabels[6];
// [RE 0x463171] unk_463171（胜利条件 0 倍率显示）
extern const char* const kInfiniteText;
// [RE 0x47E80C] g_charData 每项 +0 名字指针
extern const char* const kCharNames[12];

// [RE 0x46CB94] g_startMoney
extern const uint32_t kStartMoney[6];
// [RE 0x46CBE8] g_gameDays
extern const uint32_t kGameDays[6];
// [RE 0x46CC00] g_winMoneyMul
extern const uint32_t kWinMoneyMul[6];
// [RE 0x46CB58] g_playerSlotX
extern const uint32_t kPlayerSlotX[3][4];
// [RE 0x46CC18] g_ctrlRects
extern const uint16_t kCtrlRects[13][4];
// [RE 0x46CC88] g_listRects
extern const uint16_t kListRects[6][4];
// [RE 0x46CC80] g_mapMarkY
extern const uint16_t kMapMarkY[4];
// [RE 0x46CCB8] g_listBgFrame
extern const uint16_t kListBgFrame[6];

// [RE 0x485B68] 通道缩放表 -16（亮度减半）
extern const uint8_t kChannelHalf[32];
// [RE 0x485AE8] 通道缩放表 -20（亮度 1/3）
extern const uint8_t kChannelThird[32];
// [RE 0x485BE8] 通道缩放表 -12（按钮按下变暗）
extern const uint8_t kChannelDim[32];

// [RE 0x485C28] 通道缩放表 -10（乐透投注界面已售号码格变暗）
extern const uint8_t kChannelTen[32];

} // namespace rich4
