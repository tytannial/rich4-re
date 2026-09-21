#pragma once

#include <cstdint>

namespace rich4 {

// 游戏内（0x417E26）静态表，由 tools/gen_map_tables.py 从 rich4.exe 生成。
// 来源: DGROUP 段 0x415D1D / 0x46CCF0..0x473610 / 0x474910..0x474991 /
//       0x475208..0x4752AA / 0x47E80C / 0x47ECEC..0x47FDF6 等

// [RE 0x46CCF0] word_46CCF0 等距投影表（8 方向 x 29x29 格 x {i16 x, i16 y}）
extern const int16_t kIsoProjection[8][29][29][2];
// [RE 0x473610] byte_473610 绘制遍历序（8 方向 x {i8 row, i8 col}，0x80 结束）
extern const int8_t kIsoDrawOrder[8][296][2];
// [RE 0x474910] byte_474910 等距投影系数
extern const int8_t kIsoCoeff[8][4];
// [RE 0x474951] dword_474951 棋子绘制偏移（普通）
extern const int32_t kPieceOffset[8][2];
// [RE 0x474991] dword_474991 棋子绘制偏移（cellTable==18）
extern const int32_t kPieceOffset18[8][2];
// [RE 0x47ED3C] g_cellTypeInit 格子类型初值
extern const uint8_t kCellTypeInit[46];

// [RE 0x47F072] off_47F072 股票初始表（8 = 4 地图 x 2 模式）
extern const int32_t kStockInit[8][12][9];
// [RE 0x47FDF6] byte_47FDF6 道具初始库存
extern const uint8_t kPropStockInit[30];
// [RE 0x47FEE6] byte_47FEE6
extern const uint8_t kMisc8Init[8];
// [RE 0x47ECEC] unk_47ECEC
extern const uint32_t kMiscTable80[10][2];

// [RE 0x475208] word_475208 日历背景资源索引（4*mode+map）
extern const uint16_t kCalendarRes[8];
// [RE 0x47FF4A] byte_47FF4A.. 节日表 [组=4*mode+map][24]{flag,type,month,day,weekday}
extern const uint8_t kHoliday[8][24][5];
// [RE 0x475218] byte_475218 月份->背景帧组
extern const uint8_t kMonthFrame[12];
// [RE 0x4752AA] dword_4752AA 小地图 y 位置（按 byte_49715D 布局索引）
extern const int32_t kMiniMapY[4];
// [RE 0x475274] off_475274 玩家信息页签文本
extern const char* const kTabText[4];
// [RE 0x47511C] off_47511C 月份名/星期名
extern const char* const kMonthName[12];
// [RE 0x47ED76] off_47ED76 物件/神明名（19 项，cellTable 类型 0..18）
extern const char* const kObjectNames[19];
// [RE 0x475138] off_475138 建筑/费用名（0-10 建筑等级、11-15 费用）
extern const char* const kBuildingNames[16];
// [RE 0x415D1D] byte_415D1D 金额前缀模板
extern const uint8_t kMoneyPrefix[20];

// [RE 0x47E80C] g_charData 角色模板 +4 颜色
extern const uint16_t kCharColor[12];
// [RE 0x47E80C] g_charData 角色模板 +8..+103
extern const uint8_t kCharTemplate[12][96];

// [RE 0x463190] flt_463190（AI 初始资金系数）
extern const float kAiAssetRatio;
// [RE 0x463194] flt_463194（股票总市值系数）
extern const float kStockTotalRatio;

} // namespace rich4
