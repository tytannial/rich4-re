#pragma once

namespace rich4 {

class Application;

// [RE 0x41D1A9] specPtBuyStockDialog：停留他人/自己行業設施點后的认购股份对话框
//   条件：!state37 && alive && specPt.sharesLeft != 0
//   人类：确认框（"%s\n\n每股售價%d\n\n是否認購股份？"）→ 数字输入框（上限 = min(cash/价, 股数, 1000)）
//   AI：sub_41D839（保留 startMoneyVal × 0.3 × M 现金，其余全买）
//   购买后经营権变化 → 提示（index 12 帮主 / 其他经营权）
void specPtBuyStockDialog(Application& app, int specPtIdx);

} // namespace rich4
