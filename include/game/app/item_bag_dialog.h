#pragma once

namespace rich4 {

class Application;

// [RE 0x447D97] itemPanelFlow：工具条 case 7 道具栏。人类在场 → 显示道具图标 + ×N →
//   选道具 → 道具效果表分派（返回非 0 结束、0 重选、取消结束）。人类分支不弹「使用」提示
//   （showMessage 仅 AI 分支 0x44805E）。详见 docs/reverse/functions/441baa-inventory-panels.md
void itemBagDialog(Application& app);

}  // namespace rich4
