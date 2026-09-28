#pragma once

#include <cstdint>

namespace rich4 {

struct GameState;
class Application;

// 经济系统（定义见 src/app/economy.cpp）。M2 起步：仅实现收租/税/罚款共用的
// 转账与物件释放；买地/银行/股票见 docs/reverse/functions/ 各档案。

// [RE 0x40D293] bitScanPlayer（定义见 src/app/economy.cpp）
int bitScanPlayer(uint32_t mask);

// [RE 0x41D2C6] transferMoney（定义见 src/app/economy.cpp）
// from 付给 to：from 0..7 玩家（flags&4 从银行扣，否则从现金扣，不足由另一账户补，
// 仍不足 → 实付额裁剪 + 淘汰）；to -1 = 银行，0..7 = 玩家（flags&1 进现金），
// >100 = 特殊点（索引 to-101）
// 尾：from==当前玩家且存活 → refreshPlayerPanelFor（0x41D433，无模态时立即重绘面板）
void transferMoney(Application& app, int from, int to, int32_t amount, int flags);

// [RE 0x40DF69] addPlayerDebt（定义见 src/app/economy.cpp）
// 欠款矩阵累加 debt[creditor][debtor] += amount（clamp≥0；命中同盟则解除；不扣现金）
void addPlayerDebt(GameState& st, int creditor, int debtor, int32_t amount);

// [RE 0x40CC1A] clearAllyPair：解除 p 与其同盟对象的同盟（清双方 ally(+65)/allyActive(+61)）
//   用于：addPlayerDebt 记债命中同盟、同盟卡重结盟、updatePlayerStates 到期
void clearAllyPair(GameState& st, int p);

// [RE 0x40E14D] releaseCellTableSlot（定义见 src/app/economy.cpp）
// slot 为 1-based cellTable 槽：占用者属性回退 → 清槽 → 前 12 槽重新随机放置
void releaseCellTableSlot(Application& app, int slot);

// [RE 0x40E278] godPairType：槽 idx(0-based, 0..11) 被消耗后应在**对向槽**重建的神明类型。
//   配对 (偶小, 奇大)：type1↔2、3↔4、5↔6、7↔8、9天使↔10惡魔、11惡犬↔12土地公。
//   原版反汇编 `test dl,1 → lea ebx,[edx-1] / lea ebx,[edx+1]` 后 `inc ebx`：
//   偶槽→idx+2（奇槽大形态），奇槽→idx（偶槽小形态）。抽出供 L0 单测锁定方向。
int godPairType(int idx);

// [RE 0x40AC7B] expireAssets：范围资产清算（定义见 src/app/economy.cpp）
//   sub_40A45C 口径：基于**当前视口** mapHitRegions（= g_drawList）的**屏幕空间**采集——
//   radius=-1 全视野、否则视口中心 (220,260) ± radius 方块（调用方须先 refreshGameUi(x,y,0)
//   等价重绘对准目标；cx/cy 仅存参不再用于筛选）；有主挂身物件不收集（0x409DE7 过滤）；
//   flags 2=住宅/4=商業/0x20=玩家与物件；dump≠0 归公（30×M×level 记账）、dump=0 降级
//   （30×M 记账）；payer=-1 不记账
void expireAssets(Application& app, int cx, int cy, int radius, int flags, int dump, int payer);

// [RE 0x40CD87] eliminatePlayer（定义见 src/app/economy.cpp）
// 玩家破产/淘汰：清字段/资产归公（>1 存活时收集+拍卖）、淘汰演出；终局判定 =
//   单人局人类破产 → sceneRequest=1（defeatFlow 近似）；无存活人类 → 1；
//   仅剩 1 存活 → 胜利 sceneRequest=2（单人）/3（多人）；存活统计用 alive（未入场=0）
void eliminatePlayer(Application& app, int p);

// [RE 0x411AE0] surrenderPlayer：認輸投降 = eliminatePlayer(自己) →（humanCount>1 且存活人类
//   非 0）deathGodSummonDialog 选目标 → createMapObject(15,0,7,target) 死神附身复仇
void surrenderPlayer(Application& app);

// [RE 0x4239B9] playerTotalAssets：总资产 = 现金+银行-贷款 + Σ(12股×现价) +
//   地产(priceAdd + type?priceBase:priceBase×level) + 公司(buildPrice + feeTable[0]×sub)
int32_t playerTotalAssets(Application& app, int player);

// [RE 0x423ACF] updateMoneyIndex（物價指數）：g_moneyMul = max(旧值,
//   Σ存活 playerTotalAssets / 存活数 / g_startMoneyVal)（只升不降；advanceDay 每日调用 0x41CFBF）
void updateMoneyIndex(Application& app);

// [RE 0x433BD8] payFromBank（银行扣款）：bank -= amount；不足由 cash 补；再不足破产（eliminatePlayer）
//   用于偿还贷款/贷款强执/週转归还（0x436668 / 0x436A5A / 0x434492 等）
void payFromBank(Application& app, int player, int32_t amount);

// [RE 0x40D2D3] findMaxCreditor：p 行（creditor=p）欠款矩阵中金额最大的存活玩家，无则 -1。
//   注: 矩阵索引 [creditor][debtor]，故语义为「欠 p 最多的人」；原版嫁祸卡 AI 目标 sub_44476A。
int findMaxCreditor(const GameState& st, int player);

// [RE 0x40D31C] pickRandomActiveTarget：随机存活且无状态（stateFlags==0）的非自己玩家，无则 -1。
//   原版 findMaxCreditor 无结果时的嫁祸备选。
int pickRandomActiveTarget(const GameState& st, int exclude);

} // namespace rich4
