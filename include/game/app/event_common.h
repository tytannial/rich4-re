#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

class Application;
class Surface;
struct GameState;
struct Estate;
struct Corp;

// 事件格（新聞 0x44B6DF / 命運 0x44DB81）公共辅助沉淀。
// 原为 news_dialog.cpp 匿名命名空间实现，命运功能复用后提取（2026-09-26）。

// 角色棋子头像帧号：原版 blitElementToCanvas(panel, g_pieceSprites[13*p]+N)（N/12 = 帧号）
inline constexpr int kPieceFrameRest = 3;    // +36（休息/路过）
inline constexpr int kPieceFramePenalty = 4; // +48（受罚/延长）
inline constexpr int kPieceFrameRelease = 5; // +60（释放/奖励）

// 不透明贴一块 RGB555 位图（事件面板底图/插画；原版 blitBackground 无色键）
void blitOpaqueRgb(Surface& dst, const uint16_t* src, int w, int h, int x, int y);

// 事件文本（默认 28 号白字阴影；[RE setTextFont(28, 0xF0F0F0, 0x101010, 3, 0)]）
void drawEventText(Application& app, const char* text, int x, int y, int size = 28);

// 玩家棋子头像（8bit 索引色键；越界/缺帧静默跳过）
void drawPieceFrame(Application& app, int player, int frame, int x, int y);

// [RE 0x41D476] refreshGameUi(x, y, 2)：视口对准目标并全量重绘（收租/破坏共用）
void focusView(Application& app, int x, int y);

// [RE 0x41D546] 视口复位（还原主循环居中玩家）
void resetView(Application& app);

// [RE 0x4528B9] 阻塞延时（音频续喂；事件效果内停顿）
void eventAudioWait(Application& app, int ms);

// [RE 0x40DFFA] 住宿中玩家强制退房（破坏/灾害类事件）
void forceHotelCheckout(GameState& st);

// [RE 0x451985] 联动闪烁：先填 st.highlightEstates/Corps 且 highlightFrame=0 →
//   playHighlightBlink 阻塞播放（16 帧 + 400ms）
void blinkHighlight(Application& app);

// [RE 0x451985] 单块高亮（原版 markPickBuffer(单块) → highlightBlink）
void blinkSingleObj(Application& app, int objId);

// 破坏类事件阶段 1 公共：视口对准 + 输出对象坐标（expireAssets 范围中心）
void focusObjId(Application& app, int objId, int& outX, int& outY);

// 有建筑地块 objId（estate level!=0 / corp sub!=0）
std::vector<int> collectBuiltObjIds(const GameState& st);

// 全部地块 objId（estate + corp）
std::vector<int> collectAllObjIds(const GameState& st);

// 有建筑 estate 索引（不含 corp）
std::vector<int> collectBuiltEstateIndices(const GameState& st);

int pickRandom(const std::vector<int>& list);

std::string estateNameUtf8(const Estate& es);
std::string corpNameUtf8(const Corp& cp);
std::string objNameUtf8(const GameState& st, int objId);
int objOwner(const GameState& st, int objId);

// [RE 0x452946] copyNameNoSpaces：去掉 0x20 半角空格（原版角色名/公司名表按 4 字宽排版补
//   空格；进 sprintf 消息前原版统一去空格，drawText 直绘则保留）
std::string nameNoSpaces(const char* s);

// 玩家名去空格（原版 g_players[26*p] → nameNoSpaces）
std::string playerNameNoSpace(const GameState& st, int p);

} // namespace rich4
