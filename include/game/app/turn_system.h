#pragma once

#include <cstdint>

namespace rich4 {

struct GameState;
struct Estate;
class Application;

// [RE 0x419744] estateRouteRent：同路段/连锁店联合租金（object_tip 显示应收租金复用）
// 依据: 0x419744；普通住宅用地 = 同 owner 同名地块 fees[level] 之和；连锁店 = 数量 × 2000；× M
int32_t estateRouteRent(const GameState& st, uint8_t owner, const Estate& es);

// [RE 0x407A8C] 朝向 = sub_454FB4(cellEnt[to]-cellEnt[from])，8 方向 0..7
// 依据: 0x407A8C→0x454FB4(atan2(-dy,dx) 量化)→byte_482414[2,3,4,5,6,7,0,1]
// 供 map_render 首次放置（sub_40829D）与 turn_system 落地/移动复用
int facingBetween(GameState& st, int fromId, int toId);

// 游戏内回合/移动系统（严格对应原版函数边界；定义见 src/app/turn_system.cpp）。
// 调用入口: 0x401B9C WinMain 内层循环
//   if (byte_46CAFA)  updateGameState();       // sub_40D7C4
//   if (state==0 && flags<0) { flags&=0x7F; beginPlayerTurn(); }  // sub_418C55

// [NEW] 进入游戏循环时初始化（对应 0x401B9C 进入游戏后的首次循环）
void initTurnState(Application& app);

// [RE 0x40D7C4] updateGameState（每帧状态机 0/1/2/3）
void updateGameState(Application& app);

// [RE 0x418EBD] nextPlayer（当前玩家 +1 轮换，越过玩家数进入事件槽，回绕过天）
void nextPlayer(Application& app);

// [RE 0x418C55] beginPlayerTurn（回合开始：人类等待前进 / 状态效果自动移动）
void beginPlayerTurn(Application& app);

// [RE 0x418E7F] calcPlayerWait（byte_498EA5 = sub_41982D(当前格) 或 -125）
void calcPlayerWait(Application& app);

// [RE 0x40C912] checkPlayerAction（返回行动类型 0/1/2/-1；a1=1 用于 sub_418E7F）
int checkPlayerAction(Application& app, int a1);

// [RE 0x40DD1F] startPlayerMove（人类进入掷骰 / AI 直接移动）
void startPlayerMove(Application& app);

// [RE 0x4196F1] enablePlayerControl（byte_46CAFD=1 + sub_417191(1) 前进面板）
void enablePlayerControl(Application& app);

// [RE 0x419703] disablePlayerControl（byte_46CAFD=0）
void disablePlayerControl(Application& app);

// [RE 0x41CF67] advanceDay（音乐淡出 + 日期推进 + ++g_dayCount + 胜利判定 + 每日事件）
void advanceDay(Application& app);

// [RE 0x41C84F] updatePlayerStates（回合开始状态倒计时：住宿/出國/監獄/醫院/冬眠/夢遊）
void updatePlayerStates(Application& app, int p);

// [RE 0x40D6BE] endPlayerState（状态结束恢复：alive|=0x10 + 朝向重算 + 恢复格占用）
void endPlayerState(Application& app, int p);

// [RE 0x43D593 / 0x43EC3F] jailPlayer / hospitalizePlayer（入狱/住院 days 天；P2 監獄/醫院）
void jailPlayer(Application& app, int player, int days);
void hospitalizePlayer(Application& app, int player, int days);

// [RE 0x419572] rollDice（点数 = byte_496B7A 个 rand()%6+1 之和）
int rollDice(Application& app);

// [RE 0x40C05C] moveOneStep（沿 cellEnt 相邻出口走一格，返回 true = 到达）
bool moveOneStep(Application& app);

// [RE 0x41982D] landingEvent（落地事件；M2 实现，M1 返回 0x80）
int landingEvent(Application& app, uint16_t cellEntId);

// [RE 0x44808A] saveTurnSnapshot：回合开始快照（時光機回滚；人类玩家掷骰前）
void saveTurnSnapshot(GameState& st, int p);

// [RE 0x448544] restoreTurnSnapshot：時光機全状态回滚（定义见 turn_system.cpp）
bool restoreTurnSnapshot(Application& app, int p);

// [RE 0x40B110] angelUpgrade：免费加盖一级（住宅 estate / 商業 corp；无设施则建设施）；
//   返回 bit0=成功、bit7=封顶（到达最高等级）；機器工人（id9）直接复用（定义见 turn_system.cpp）
int angelUpgrade(Application& app, uint16_t objId);

// [RE 0x419A67] startRouteHighlight：收租联动高亮收集（同 owner 同名 estate / 连锁店）
void startRouteHighlight(GameState& st, const Estate& es);

// [RE 0x451985] playHighlightBlink：阻塞播放联动闪烁（16 帧 + 400ms 停留；
//   调用前需填 st.highlightEstates 且 st.highlightFrame=0；定义见 turn_system.cpp）
void playHighlightBlink(Application& app);

// [RE 0x45144F] playEventFlc：阻塞播放 data.mkf[mkfIndex] FLC（定义见 turn_system.cpp；
//   事件格/神明插图共用；soundId<0 无声；freeze=true 播完冻结末帧至下次全量重绘；
//   switchFrame = 原版 flcOpen flags BYTE2 场景切换帧 [RE 0x450CED]：>0 保帧播放
//   （背景=开始前画面）至该帧全量重绘切换场景（住院 524=30 / 入狱 538=18 / 爆炸拆除=3 /
//   碾过=1），0=逐帧重绘（动画期无状态变化的插图类）
void playEventFlc(Application& app, int mkfIndex, int x, int y, int soundId,
                  bool freeze = false, int switchFrame = 0);

// [RE 0x40B93B] loadWalkResources（行走资源加载，定义见 turn_system.cpp；
//   状态结束/载具损毁/住院等复用）
void loadWalkResources(GameState& st, int p);

// ===== 卡片卡包（0x441262/0x441343/0x4412E4/0x441E12；定义见 turn_system.cpp）=====
int cardBagCount(const GameState& st, int player);
void cardBagRemove(GameState& st, int player, int card);
void giveCardToBag(GameState& st, int player, int card);
int drawFreeCard(GameState& st, int player);

// [RE 0x441F73] showCardGet：卡片获得显示（卡片图 data.mkf[570+id] 165×256 @(138,200)
//   + FloatMessage 提示框 text，1500ms）；定义见 turn_system.cpp，useCardFlow 复用
void showCardGet(Application& app, int cardId, const char* text);

} // namespace rich4
