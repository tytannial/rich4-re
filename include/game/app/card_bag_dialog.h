#pragma once

#include <cstdint>

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

// [RE 0x44192A] selectCardOrItemFromPlayerDialog：从目标玩家的卡包/道具包选一项（生日收卡）。
//   panel.mkf[11] 卡包帧0 @(14,70) + 道具包帧1 @(14,270)（有道具才画）；
//   命中区 x∈[19,419)、卡 y∈[75,243) / 道具 y∈[275,443)，5×3 网格；
//   按下高亮（highlightRect 78×54）、抬起确认、右键取消（sub_4413EC 回调）。
//   返回 0=取消 / 卡 = 卡 id / 道具 = 0x8000|道具 id。
int selectCardOrItemFromPlayerDialog(Application& app, int targetPlayer);

// [RE 0x44476A] passOnCardDialog：嫁祸卡（19）目标选择（sub_44476A(user, mode, fee)）。
//   先显示卡图 "%s\n\n嫁禍卡生效！"；人类：1 候选 → askDialog「是否嫁禍給%s？」、
//   多候选 → selectPlayerDialog「請選擇嫁禍對象...」；AI：findMaxCreditor（无则随机存活
//   无状态玩家）→ showMessage「嫁禍給%s！」。选定消耗卡 19，返回目标；取消/无目标 -1。
//   mode=0（命运/新闻惩罚/梦游/陷害）：AI 直接嫁祸；
//   mode=1（住宅/商業/行業設施收费 0x419EB8/0x41A683/0x41AF75）：AI 仅当
//     fee > 现金 或 (rand%4000+4000)×M < fee 时嫁祸；
//   mode=2（查税 0x445360）：AI 仅当 4000×M < 现金×0.2 时嫁祸。
int passOnCardDialog(Application& app, int user, int mode = 0, int32_t fee = 0);

}  // namespace rich4
