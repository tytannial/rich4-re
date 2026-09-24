#pragma once

namespace rich4 {

class Application;

// [RE 0x480D5A] off_480D5A 角色台词表（12 角色 x 26 列文本指针；道具取列 0..12 = id1..13）
//   字符串含 '#NNNN' 语音前缀（Speaking.mkf 编号）/ '#NNNN@MM' 语音+表情帧
//   由 tools/gen_item_lines.py 生成（src/app/item_lines.cpp）
extern const char* const kItemLines[12][13];

// [RE 0x44EF41] playItemLine：角色气泡台词（tip 框 data.mkf[517] 帧 5 @(220,130)
//   + 头像 pieceSprites[p] 帧 1 @(170,130) + 文本 @(200,130) 或表情 data.mkf[519] 帧 MM-1）
//   1000ms；同一台词指针连续调用只显示一次（dword_4762C8 去重）
void playItemLine(Application& app, int player, int itemId);

} // namespace rich4
