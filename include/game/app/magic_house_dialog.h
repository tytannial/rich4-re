#pragma once

namespace rich4 {

class Application;

// [RE 0x43380A] magicHouseVisit：魔法屋（landingEvent case 16 → 0x41B3CB）。
//   人类：panel[18] 施法 UI（runModal 0x4325C2）——女巫随机"条件"（0x431842 选目标名单），
//   玩家从 12 种惩罚图标中选择；确认 → panel[20] FLC → 对名单逐一执行（0x431CAA）。
//   AI/托管：条件+惩罚双随机（惩罚排除 6 自肥/11 拍卖；自己中招 → 强制 6）+ 消息后直接执行。
// 依据: docs/reverse/functions/43380a-magic-house.md §1..§4
void magicHouseVisit(Application& app);

// [RE 0x4339D9 → 0x433088] deathGodSummonDialog：投降召唤死神对话框（surrenderPlayer
//   0x411AE0 调用）。复用魔法屋场景（panel[18] 帧 0/1 女巫 + 帧 8 选择区 + 帧 2 确认罩，
//   panel[20] 退出 FLC，data.mkf[2] 头像）+ FloatMessage #0042/#0043/#0041。
//   候选 = 存活且非发起者玩家；返回选中玩家+1（createMapObject 附身参数）；0 = 取消/无目标。
int deathGodSummonDialog(Application& app, int from);

} // namespace rich4
