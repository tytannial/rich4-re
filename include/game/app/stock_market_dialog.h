#pragma once

namespace rich4 {

class Application;

// [RE 0x42B58F / 0x42AAFF] stockMarketDialog：股票行情界面（工具条按钮 10）
//   12 支股票列表（名称/持股/均价/现价/涨跌/市值）+ 现金/银行/总资产
//   买入 = sub_428D2A(a4=1 银行，上限 min(银行/现价, 银行可售股数))
//   卖出 = sub_428E23(a4=1 入银行，上限 = 持股)
//   差异: 原版 panel 背景/列头排序/K 线走势图（sub_429D65）简化为文本列表
void stockMarketDialog(Application& app);

} // namespace rich4
