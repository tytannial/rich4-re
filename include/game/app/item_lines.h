#pragma once

namespace rich4 {

class Application;

// [RE 0x480D5A] off_480D5A 角色台词表（12 角色 x 26 列文本指针；道具取列 0..12 = id1..13）
//   字符串含 '#NNNN' 语音前缀（Speaking.mkf 编号）/ '#NNNN@MM' 语音+表情帧
//   由 tools/gen_item_lines.py 生成（src/app/item_lines.cpp）
extern const char* const kItemLines[12][13];

// [RE 0x480D92/96/9A] 被踩中台词表（列14 路障 / 列15 地雷 / 列16 定時炸彈；
//   同 off_480D5A 表，角色 stride 26）；onPlayerActionPhase case16/17/18 播
extern const char* const kItemTouchLines[12][3];

// [RE 0x48084A] off_48084A 角色价值台词表（12 角色 x 27 列，取列 0..2）
//   由 tools/gen_item_lines.py 生成（src/app/item_lines.cpp）
extern const char* const kValueLines[12][3];

// [RE 0x44EF41] playLine：任意行台词气泡（tip 框 data.mkf[517] 帧 6 @(220,130)
//   + 头像 pieceSprites[p] 帧 exprFrame+1 @(170,130) + 文本 @(200,130) 或表情 data.mkf[519] 帧 MM-1）；
//   exprFrame = 原版 sub_44EF41 的 a2（表情帧索引：0=普通/1=哭/2=被炸/3=笑，原版画帧 a2+1）；
//   1000ms；同文本指针连续调用只显示一次（dword_4762C8 去重）；状态中（住宿/出国/冬眠…）不显示
void playLine(Application& app, int player, const char* line, int exprFrame = 0);

// [RE 0x44EF41] playItemLine：角色气泡台词（道具/卡片效果入口调用；exprFrame 见 playLine）
void playItemLine(Application& app, int player, int itemId, int exprFrame = 0);

// [RE 0x44F230] playValueLine：按卡片/道具价值选角色台词（百货中奖/卡片格/道具获得）——
//   price > 100 → kValueLines[char][0]；51..100 → [0]/[1] 随机；<= 50 → [2]；
//   经 playLine 显示气泡并播放语音（原版 sub_44EF41 type=0）
void playValueLine(Application& app, int player, int price);

} // namespace rich4
