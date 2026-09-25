#pragma once

namespace rich4 {

class Application;
class UiImage;

// [RE 0x441B0A] drawCardBagAt：卡片栏绘制（panel.mkf[11] 帧0 背景 + 5×3 卡名网格）到 (x,y)。
//   工具条 useCardFlow 用 (14,130)；百货商店卡包区用 (bagX,293)（0x42D37F 动画位置）
void drawCardBagAt(Application& app, const UiImage& sheet, int player, int x, int y);

// [RE 0x441BAA] useCardFlow：工具条 case 8 卡片栏。人类在场 → 显示 15 槽卡包 →
//   选卡 → showCardGet → 卡片效果表分派（返回非 0 结束、0 重选、取消结束）。
//   详见 docs/reverse/functions/441baa-inventory-panels.md
void useCardDialog(Application& app);

// [RE 0x44309B] cardRebuildEffect：改建卡（卡 id 7）效果——针对当前玩家所站格：
//   住宅用地 type ^= 1（住宅↔连锁店）；商業用地 selectFacilityDialog 变更设施。
//   成功则消耗卡（cardBagRemove(cur,7)）。返回非 0 = 结束使用流程
int cardRebuildEffect(Application& app);

}  // namespace rich4
