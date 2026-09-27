#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x480D5A] off_480D5A 角色台词表（12 角色 x 26 列文本指针；道具取列 0..12 = id1..13）
//   字符串含 '#NNNN' 语音前缀（Speaking.mkf 编号）/ '#NNNN@MM' 语音+表情帧
//   由 tools/gen_item_lines.py 生成（src/app/item_lines.cpp）
extern const char* const kItemLines[12][13];

// [RE 0x480D92/96/9A] 被踩中台词表（列14 路障 / 列15 地雷 / 列16 定時炸彈；
//   同 off_480D5A 表，角色 stride 26）；onPlayerActionPhase case16/17/18 播
extern const char* const kItemTouchLines[12][3];

// [RE 0x48084A..8B2] 金额/状态台词表（off_48084A 家族 27 列中的 24 列，见生成器注释）：
//   0/1/2 收款高/中/低、3/4/5 付款高/中/低、6 住宅满级、7 欠债、
//   8/9 同路段≥3 连锁吐槽（必播/1/3 概率）、10/11/12 意外之财高/中/低、
//   13/14/15 状态提示（坐牢/住院/冬眠）、16 神明附身哭、17 神明飘走、
//   18/19/20 倒霉档（住宿/入狱/住院天数 + 新闻业主 3/4 随机）、
//   21 终局胜利大笑、22 淘汰认输、23 失败鼓励（defeatFlow）
extern const char* const kMoneyLines[12][24];

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

// [RE 0x44F354] playCollectorLine：收款方台词（金额档，expr3）——>=9000M 列6；
//   5000..9000M 列6/7 随机；2000..5000M 列8；<2000M 无
void playCollectorLine(Application& app, int player, int32_t amount);

// [RE 0x44F42D] playPayerLine：付款方台词（expr2）——>=9000M 列3；5000..9000M 列3/4 随机；
//   0<金额<5000M 列5；<=0 无
void playPayerLine(Application& app, int player, int32_t amount);

// [RE 0x44F4ED] playDebtorLine：欠债者面对最大债主台词（expr1，50% 概率）——
//   findMaxCreditor(player)==creditor 且金额>=5000M 且 rand&1 → 播列7 并返回 true（调用方
//   不再播付款方台词）；否则返回 false
bool playDebtorLine(Application& app, int player, int creditor, int32_t amount);

// [RE 0x44F2C2] playStatusDaysLine：状态天数台词（expr2；住宿/入狱/住院/出国共用）——
//   days>6 倒霉列3；4..6 列3/4 随机；1..3 列5「死不了人」；0 无
void playStatusDaysLine(Application& app, int player, int days);

// [RE 0x44EF41 off_480856] playUnluckyLine：倒霉台词（expr2；灾害/拆屋/载具损毁/没收）——
//   random=true 列3/4 随机（0x4494CD/0x44BF9F 业主/房主），false 列3 固定
//   （0x44CB30/0x44CC36 载具损毁、0x44D777 没收）
void playUnluckyLine(Application& app, int player, bool random);

// [RE 0x44F567] playWindfallLine：意外之财台词（expr3；大財神附身免付/命运逃过罚款时播）——
//   >=9000M 列12；5000..9000M 列12/13 随机；0<金额<5000M 列14；<=0 无
void playWindfallLine(Application& app, int player, int32_t amount);

// [RE 0x44F627] playEstateChainSpeech：统计与 name 同名的住宅用地中属于当前玩家的块数，
//   >=3 → 角色感想（expr0）：flag!=0 时 1/3 概率列9（列17 台词），flag==0 必播列8（列16）
void playEstateChainSpeech(Application& app, const char* name, int flag);

} // namespace rich4
