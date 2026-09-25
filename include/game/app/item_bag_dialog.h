#pragma once

#include <cstdint>

namespace rich4 {

class Application;
class UiImage;

// [RE 0x447C6E] drawItemBagAt：道具栏绘制（panel.mkf[11] 帧1 背景 + 图标×N）到 (x,y)。
//   visibleMap 非空时写「可见槽序号 → 道具 id」映射（商店卖出区点击用）。
//   工具条 itemPanelFlow 用 (14,130)；百货商店道具包区用 (bagX,293)。不含「下車」按钮
//   （那是 itemPanelFlow 0x447DEE 自己的逻辑，非 drawItemBag 本体）
void drawItemBagAt(Application& app, const UiImage& sheet, int player, int x, int y,
                   uint8_t visibleMap[15] = nullptr);

// [RE 0x447D97] itemPanelFlow：工具条 case 7 道具栏。人类在场 → 显示道具图标 + ×N →
//   选道具 → 道具效果表分派（返回非 0 结束、0 重选、取消结束）。人类分支不弹「使用」提示
//   （showMessage 仅 AI 分支 0x44805E）。详见 docs/reverse/functions/441baa-inventory-panels.md
void itemBagDialog(Application& app);

}  // namespace rich4
