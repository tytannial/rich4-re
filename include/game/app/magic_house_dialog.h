#pragma once

namespace rich4 {

class Application;

// [RE 0x43380A] magicHouseVisit：魔法屋（landingEvent case 16 → 0x41B3CB）。
//   人类：panel[18] 施法 UI（runModal 0x4325C2）——女巫随机"条件"（0x431842 选目标名单），
//   玩家从 12 种惩罚图标中选择；确认 → panel[20] FLC → 对名单逐一执行（0x431CAA）。
//   AI/托管：条件+惩罚双随机（惩罚排除 6 自肥/11 拍卖；自己中招 → 强制 6）+ 消息后直接执行。
// 依据: docs/reverse/functions/43380a-magic-house.md §1..§4
void magicHouseVisit(Application& app);

} // namespace rich4
