#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x4291D6] stockTick：每日股票行情（12 支随机涨跌 + 历史 + 大盘指数）
//   差异: sub_428D01 停市判定（dword_4990DC / isSpecialDate）未建模，暂不停市
void stockTick(Application& app);

// [RE 0x428CAF] refreshStockSpecPtMap：按 specPt.index 反查股票→specPt 映射
//   （loadMapData 调用；仅对已有映射的上市公司股票生效）
void refreshStockSpecPtMap(Application& app);

// [RE 0x428D2A] buyStock：购买股票（viaBank=false 现金买 specPt 股 / true 银行买）
//   现金路径：每股价格 = specPt.feeBase / 10000，sharesLeft -= count，现金扣款；
//   末尾更新持股与均价（加权平均）并调 updateSpecPtControl；
//   返回 1 表示该 specPt 经营権变化（原版 sub_428D2A 返回值 = sub_4294D5 结果）
int buyStock(Application& app, int player, int stock, int count, bool viaBank);

// [RE 0x4294D5] updateSpecPtControl：重排 specPt.shareholders[4]（按持股降序，平手归先拥有者）
//   → owner = 第一大股东；返回 1 表示 owner 变化（已刷新小地图标记）
int updateSpecPtControl(Application& app, int player, int stock);

} // namespace rich4
