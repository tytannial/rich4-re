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
void transferMoney(Application& app, int from, int to, int32_t amount, int flags);

// [RE 0x40DF69] addPlayerDebt（定义见 src/app/economy.cpp）
// 欠款矩阵累加 debt[creditor][debtor] += amount（clamp≥0；命中同盟则解除；不扣现金）
void addPlayerDebt(GameState& st, int creditor, int debtor, int32_t amount);

// [RE 0x40E14D] releaseCellTableSlot（定义见 src/app/economy.cpp）
// slot 为 1-based cellTable 槽：占用者属性回退 → 清槽 → 前 12 槽重新随机放置
void releaseCellTableSlot(Application& app, int slot);

// [RE 0x40CD87] eliminatePlayer（定义见 src/app/economy.cpp）
// 玩家破产/淘汰：标记占用位、释放携带物件、清空住宅用地/商業用地/行業設施點、全灭时 sceneRequest
void eliminatePlayer(Application& app, int p);

// [RE 0x4239B9] playerTotalAssets：总资产 = 现金+银行-贷款 + Σ(12股×现价) +
//   地产(priceAdd + type?priceBase:priceBase×level) + 公司(buildPrice + feeTable[0]×sub)
int32_t playerTotalAssets(Application& app, int player);

// [RE 0x423ACF] updateMoneyIndex（物價指數）：g_moneyMul = max(旧值,
//   Σ存活 playerTotalAssets / 存活数 / g_startMoneyVal)（只升不降；advanceDay 每日调用 0x41CFBF）
void updateMoneyIndex(Application& app);

// [RE 0x433BD8] payFromBank（银行扣款）：bank -= amount；不足由 cash 补；再不足破产（eliminatePlayer）
//   用于偿还贷款/贷款强执/週轉归还（0x436668 / 0x436A5A / 0x434492 等）
void payFromBank(Application& app, int player, int32_t amount);

} // namespace rich4
