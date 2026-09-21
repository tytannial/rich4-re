#pragma once

#include <cstdint>

namespace rich4 {

struct GameState;
class Application;

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

// [RE 0x419572] rollDice（点数 = byte_496B7A 个 rand()%6+1 之和）
int rollDice(Application& app);

// [RE 0x40C05C] moveOneStep（沿 cellEnt 相邻出口走一格，返回 true = 到达）
bool moveOneStep(Application& app);

// [RE 0x41982D] landingEvent（落地事件；M2 实现，M1 返回 0x80）
int landingEvent(Application& app, uint16_t cellEntId);

} // namespace rich4
