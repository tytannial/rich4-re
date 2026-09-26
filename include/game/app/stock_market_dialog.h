#pragma once

namespace rich4 {

class Application;

// [RE 0x42B58F / 0x42AAFF] stockMarketDialog：股市面板（工具条第 11 按钮）
//   panel.mkf[75] 帧 0/1/2 = 股價表/持有股數表/走勢圖底图；12 支股票成行；
//   顶部 5 按钮（持有股數表↔股價表 / 買進 / 賣出 / 上市公司資訊 / 離開）；
//   買/賣 = numberInputDialog + buyStock/sellStock；涨停/跌停弹提示；
//   休市（stockMarketClosed || isSpecialDate）→ "本日休市"；走勢圖含饼图+144期折线。
//   详见 docs/reverse/functions/42b58f-stock-panel.md
void stockMarketDialog(Application& app);

// [NEW/RE 0x42B58F stockPanelMain] stockPickDialog：选股模态（红卡 mode=1 / 黑卡 mode=2 复用
//   行情面板绘制；点击股票行 → requestExit(1..12)，EXIT/右键 → 0）。原版
//   cursorSelect(12,15,10) + stockPanelMain(mode)。选股后效果由调用方执行。
int stockPickDialog(Application& app, int mode);

} // namespace rich4
