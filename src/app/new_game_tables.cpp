// 由 tools/gen_newgame_tables.py 从 rich4.exe 生成，勿手改。
#include "game/app/new_game_tables.h"

namespace rich4 {

// [RE 0x46CB88] g_playerCountText（选项：玩家数）
const char* const kPlayerCountText[3] = {"二人", "三人", "四人"};

// [RE 0x46CBAC] g_travelModeText（选项：行进方式）
const char* const kTravelModeText[3] = {"步行", "機車", "汽車"};

// [RE 0x46CBB8] g_landPermText（选项：土地权限）
const char* const kLandPermText[6] = {"無限期", "二年", "一年", "六個月", "三個月", "一個月"};

// [RE 0x46CBD0] g_gameTimeText（选项：游戏时间）
const char* const kGameTimeText[6] = {"無限期", "二年", "一年", "六個月", "三個月", "一個月"};

// [RE 0x463138..0x463165] 面板 6 行标签
const char* const kPanelLabels[6] = {"遊戲人數", "總 資 金", "行進方式", "土地權限", "遊戲時間", "勝利條件"};

// [RE 0x463171] unk_463171 = "無限"（胜利条件 0 倍率显示）
const char* const kInfiniteText = "無限";

// [RE 0x47E80C] g_charData 每项 +0 名字指针（12 角色）
const char* const kCharNames[12] = {"約 翰 喬", "沙隆巴斯", "忍 太 郎", "錢 夫 人", "阿 土 伯", "莎拉公主", "宮本寶藏", "糖  糖", "烏  咪", "孫 小 美", "小 丹 尼", "金 貝 貝"};

// [RE 0x46CB94] g_startMoney
const uint32_t kStartMoney[6] = {300000, 200000, 100000, 50000, 30000, 10000};

// [RE 0x46CBE8] g_gameDays（0=无限）
const uint32_t kGameDays[6] = {0, 730, 365, 182, 91, 30};

// [RE 0x46CC00] g_winMoneyMul（胜利资金 = 起始资金 x 倍率，0=无限）
const uint32_t kWinMoneyMul[6] = {0, 100, 50, 10, 5, 3};

// [RE 0x46CB58] g_playerSlotX[玩家数索引][槽]（底部头像 x，y 固定 440）
const uint32_t kPlayerSlotX[3][4] = {
    {330, 110, 0, 0},
    {366, 220, 74, 0},
    {385, 275, 165, 55},
};

// [RE 0x46CC18] g_ctrlRects[13]（left,top,right,bottom）
const uint16_t kCtrlRects[13][4] = {
    {8, 15, 440, 159},
    {456, 176, 535, 215},
    {544, 176, 623, 215},
    {602, 226, 625, 250},
    {602, 262, 625, 286},
    {602, 298, 625, 322},
    {602, 334, 625, 358},
    {602, 370, 625, 394},
    {602, 406, 625, 430},
    {457, 31, 625, 62},
    {457, 63, 625, 94},
    {457, 95, 625, 126},
    {457, 127, 625, 158},
};

// [RE 0x46CC88] g_listRects[6]
const uint16_t kListRects[6][4] = {
    {561, 251, 602, 320},
    {536, 287, 602, 425},
    {561, 323, 602, 392},
    {536, 195, 602, 333},
    {536, 231, 602, 369},
    {516, 267, 602, 405},
};

// [RE 0x46CC80] g_mapMarkY[4]
const uint16_t kMapMarkY[4] = {20, 52, 84, 116};

// [RE 0x46CCB8] g_listBgFrame[6]
const uint16_t kListBgFrame[6] = {5, 6, 5, 6, 6, 7};

// [RE 0x485B68] 通道缩放表 -16（亮度减半，32 字节）
const uint8_t kChannelHalf[32] = {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0B, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0E, 0x0E, 0x0F, 0x0F};

// [RE 0x485AE8] 通道缩放表 -20（亮度 1/3，32 字节）
const uint8_t kChannelThird[32] = {0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B};

// [RE 0x485BE8] 通道缩放表 -12（按钮按下变暗，32 字节）
const uint8_t kChannelDim[32] = {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0E, 0x0F, 0x0F, 0x10, 0x11, 0x11, 0x12, 0x12, 0x13};

} // namespace rich4
