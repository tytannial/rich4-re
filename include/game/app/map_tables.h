#pragma once

#include <cstdint>

namespace rich4 {

// 游戏内（0x417E26）静态表，由 tools/gen_map_tables.py 从 rich4.exe 生成。
// 来源: DGROUP 段 0x415D1D / 0x46CCF0..0x473610 / 0x474910..0x474991 /
//       0x475208..0x4752AA / 0x47E80C / 0x47ECEC..0x47FDF6 等

// [RE 0x46CCF0] word_46CCF0 等距投影表（8 方向 x 65x65 格 x {i16 x, i16 y}）。
//   表内 [-14..14]^2 = exe 原 29x29 逐项保真；外圈为边界步长线性外推（[NEW M4-D]
//   宽地图区/放大模式视野；native 窗口逐项与原表一致）。
extern const int16_t kIsoProjection[8][65][65][2];
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

// [RE 0x4749E2/0x474A06/0x474A2A] 神明三属性修正表（索引 = cellTable 类型；实际仅 1..15 被索引，
//   attachObject 0x40EAD7 += / deleteMapObject 0x40E14D -=，见 map-object-refresh.md §3）
extern const int16_t kLuckA[18];  // 排名评分 10×A（sub_437D1A）
extern const int16_t kLuckB[18];  // AI 建设门槛 + 台词吉凶（sub_41FACC / sub_44B896）
extern const int16_t kLuckC[18];  // 台词吉凶（sub_44B896 a3 分支）

// [RE 0x47F072] off_47F072 股票初始表（8 = 4 地图 x 2 模式）
extern const int32_t kStockInit[8][12][9];
// [RE 0x47F072] off_47F072 股票名（各组 field0 名称指针，UTF-8）
extern const char* const kStockNames[8][12];
// [RE 0x47FDF6] byte_47FDF6 卡片赠卡池初始数量（卡 id 1..30 → propStock[0..29]；
//   0x4071AC newGameInit 初始化；旧称"道具初始库存"有误，详见 41982d-p2-events.md §3）
extern const uint8_t kPropStockInit[30];
// [RE 0x47FDEA] dword_47FDEA 卡片名表（30 项，id 1..30；项 0 为空占位）
extern const char* const kCardNames[31];
// [RE 0x47FDEA+5] byte_47FDEF 卡片价格（点券；0x44128F 舍弃时取最小值）
extern const uint8_t kCardPrices[31];
// [RE 0x47FEE6] byte_47FEE6 礼物池初值（道具表 +0，8 种）
extern const uint8_t kMisc8Init[8];
// [RE 0x47FEE7] byte_47FEE7 道具价格（点券；13 种，道具结构 +1；0x445B3F 没收计价）
extern const uint8_t kItemPrice[13];
// [RE 0x47FEEA] 道具名表（13 种，道具结构 +4 指针；0x445ADA 礼物显示）
extern const char* const kItemNames[13];
// [RE 0x47FEDA] g_itemNames：道具栏用名表，直接对齐道具 id 1..13（[0] 占位空）；
//   与礼物池显示用的 kItemNames（自「路障」起 0-indexed）索引不同，见
//   docs/reverse/functions/441baa-inventory-panels.md
extern const char* const kItemBagNames[14];
// [RE 0x47FDF9] byte_47FDF9 卡片 AI 性格字段（g_cardInfo[id] 结构 +7；id 1..30；
//   sub_42E931 AI 百货买卖：值 - aiPersonality == 2 → 卖出；== 2 表示「不合性格」）
extern const uint8_t kCardAiPersona[31];
// [RE 0x47FEE9] byte_47FEE9 道具 AI 性格字段（g_itemNames[id] 结构 +7；id 1..13；
//   sub_42E931 AI 百货买卖 + sub_4284BE 股票 AI 买卖同用）
extern const uint8_t kItemAiPersona[14];
// [RE 0x47ECEC] unk_47ECEC
extern const uint32_t kMiscTable80[10][2];
// [RE 0x47ED5A] dword_47ED5A 事件槽 NPC 名表（索引 4..7 = 小偷/強盜/流氓/間諜；0..3 占位不用）
extern const char* const kNpcNames[8];

// [RE 0x475208] word_475208 日历背景资源索引（4*mode+map）
extern const uint16_t kCalendarRes[8];
// [RE 0x47FF4A] byte_47FF4A.. 节日表 [组=4*mode+map][24][12]{flag,type,month,day,weekday, effectFlags,resFlc,flcParam,music}
extern const uint8_t kHoliday[8][24][12];
// [RE 0x47639C] 农历浮动节日表（findHoliday type1；越界=不命中）
extern const uint16_t kFloatHoliday[];
extern const int kFloatHolidayLen;
// [RE 0x475218] byte_475218 月份->背景帧组
extern const uint8_t kMonthFrame[12];
// [RE 0x4752AA] dword_4752AA 小地图 y 位置（按 byte_49715D 布局索引）
extern const int32_t kMiniMapY[4];
// [RE 0x475274] off_475274 玩家信息页签文本
extern const char* const kTabText[4];

// [RE 0x47540C] off_47540C 查詢面板 3 页签名（資產清單/地產清單/股票清單）
extern const char* const kQueryPageNames[3];
// [RE 0x4753D4] off_4753D4 地產清單 5 子页签名（全 部/住宅區/商業區/房 屋/連鎖店）
extern const char* const kQuerySubPageNames[5];
// [RE 0x475418] off_475418 資產清單 12 字段名（現金/存款/貸款/總資產/股 票/點 卷/
//   保險期/企 業/土 地/連鎖店/房 屋/設 施）
extern const char* const kQueryFieldNames[12];
// [RE 0x4753E8] off_4753E8 地產清單 5 列头（地點/開發狀況/價 格/收 費/租 期）
extern const char* const kQueryLandCols[5];
// [RE 0x475448] off_475448 股票清單 3 列头（股票名稱/持有張數/總 市 價）
extern const char* const kQueryStockCols[3];
// [RE 0x475464] dword_475464 携带神明槽+1 → panel.mkf[9] 图标帧号
//   （1..10→13..22、12→23、15..17→24；0 = 无图标）
extern const int kGodIconFrame[19];
// [RE 0x47511C] off_47511C 月份名/星期名
extern const char* const kWeekdayNames[7];
// [RE 0x47ED76] off_47ED76 物件/神明名（19 项，cellTable 类型 0..18）
extern const char* const kObjectNames[19];
// [RE 0x475138] off_475138 建筑/费用名（0-10 建筑等级、11-15 费用）
extern const char* const kBuildingNames[17];
// [RE 0x47517C] g_costNames 费用名（13 项；corp 按 kCorpCostMap、specPt 按 g_specPtCostMap 取值）
extern const char* const kCostNames[13];
// [RE 0x474940] g_facilityMaxLevel 设施等级上限（按 corp.type 0..4）
extern const uint8_t kFacilityMaxLevel[5];
// [RE 0x47528B] 商業用地费用名映射（按 corp.type 0..4 → kCostNames 索引）
extern const uint8_t kCorpCostMap[5];
// [RE 0x47528E] g_specPtCostMap 行業設施點费用名映射（specPt.index 0..15 → kCostNames 索引）
extern const uint8_t kSpecPtCostMap[16];
// [RE 0x415D1D] byte_415D1D 金额前缀模板
extern const uint8_t kMoneyPrefix[20];

// [RE 0x47E80C] g_charData 角色模板 +4 颜色（u32 RGB888，写入调色板前经 convertColor 转 555）
extern const uint32_t kCharColor[12];
// [RE 0x47E80C] g_charData 角色模板 +8..+103
extern const uint8_t kCharTemplate[12][96];

// [RE 0x463190] flt_463190（AI 初始资金系数）
extern const float kAiAssetRatio;
// [RE 0x463194] flt_463194（股票总市值系数）
extern const float kStockTotalRatio;

} // namespace rich4
