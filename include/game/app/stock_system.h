#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x4291D6] stockTick：每日股票行情（12 支随机涨跌 + 历史 + 大盘指数）
//   休市（stockMarketClosed || isSpecialDate）时直接返回，不推进行情/历史
void stockTick(Application& app);

// [RE 0x429040] stockNewsApply：新闻对股价的冲击立即应用（stock==0 全部 12 支，
//   否则第 stock-1 支）：stockNews[i] 高 4 位 != 0 → 动量 +10 否则 -10 →
//   现价 = stockPriceUpdate(昨收, 动量) + 写历史（turnCounter-1 环形槽）
void stockNewsApply(Application& app, int stock);

// [RE 0x428CAF] refreshStockSpecPtMap：按 specPt.index 反查股票→specPt 映射
//   （loadMapData 调用；仅对已有映射的上市公司股票生效）
void refreshStockSpecPtMap(Application& app);

// [RE 0x42915A] refreshBankStockShares：银行可售股数（发行 <=1000 全可售，否则随机 10%~30%）
//   调用点: loadMapData 初始化 + sub_41C84F 每回合
void refreshBankStockShares(Application& app);

// [RE 0x428D2A] buyStock：购买股票（viaBank=false 现金买 specPt 股 / true 银行买）
//   现金路径：每股价格 = specPt.feeBase / 10000，sharesLeft -= count，现金扣款；
//   末尾更新持股与均价（加权平均）并调 updateSpecPtControl；
//   返回 1 表示该 specPt 经营権变化（原版 sub_428D2A 返回值 = sub_4294D5 结果）
int buyStock(Application& app, int player, int stock, int count, bool viaBank);

// [RE 0x428E23] sellStock：卖出股票（toBank=true 入银行 / false 入系统）；
//   持股 -= count、银行股可售/发行 += count（回归市场）；返回 1 表示经营権变化
int sellStock(Application& app, int player, int stock, int count, bool toBank);

// [RE 0x4294D5] updateSpecPtControl：重排 specPt.shareholders[4]（按持股降序，平手归先拥有者）
//   → owner = 第一大股东；返回 1 表示 owner 变化（已刷新小地图标记）
int updateSpecPtControl(Application& app, int player, int stock);

// [RE 0x42BF03] aiStockBuy：AI 买股票（beginPlayerTurn 0x418DE6）。门槛 rand()%3、
//   aiStockPct、休市、贷款剩余 <15 天；预算 = aiStockPct%×(cash+bank+持仓市值) − 市值，
//   超存款则 = 存款（只用 bank 买）；12 支评分（停牌/涨停/无在售剔除；无 specPt 组=
//   技术面 24/6 日均线，上市组=公库比/发行价折价/抢控制权加分）降序 + 首位概率
//   (13-i)/24 选定 → buyStock(viaBank) + "%s\n\n買進%s%d股"。见 418c55-ai-turn-actions.md §2
void aiStockBuy(Application& app, int player);

// [RE 0x42C79F] aiStockSell：AI 卖股票（beginPlayerTurn 0x418DF4）。贷款到期 ≤6 天且
//   存款+现金 < 贷款 → 强制（评分翻倍/+1，循环卖至 ≥ loan×1.1）否则 rand()%3；
//   休市跳过；逐持仓评分（上市公司组=基本面负公库/集中度/超发行价，投机组=技术面
//   均线/144 天最低×8/涨幅比加分）→ 最高者 sellStock(toBank) 全仓 + 消息。§3
void aiStockSell(Application& app, int player);

} // namespace rich4
