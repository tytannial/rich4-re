#pragma once

namespace rich4 {

struct GameState;
class Application;
class UiImage;

// 交易市场「公佈欄」（帮助 idx 11；工具条 case 9，原版 0x4284BE）：
//   玩家间公开挂单交易 股票/地产/道具/卡片；每日清理+age、AI 自动挂卖/改价/买入。
//   详见 docs/reverse/functions/4284be-trade-market.md。

// [RE 0x42483E] clearInvalidTradeOrders：清理失效挂单（股票数量>持股、地产
//   owner/type/level 与快照不符、道具库存 0、卡片袋无卡），每玩家 7 槽前移
void clearInvalidTradeOrders(GameState& st);

// [RE 0x4246C5] queueTradeOrder：写挂单（找空槽或同类型同 objId 槽覆盖；满 7 槽不写）；
//   type=2 时记录 estate/corp 的 type/level 快照（0x4967EA/EB）
void queueTradeOrder(GameState& st, int player, int type, int objId, int price, int count);

// [RE 0x4247D5] removeTradeOrder：移除槽（后续槽前移，末槽清零）
void removeTradeOrder(GameState& st, int player, int slot);

// [RE 0x428475] ageTradeOrders：每日挂单 age++（仅存活玩家；原版字段无读取点，照抄）
void ageTradeOrders(GameState& st);

// [RE 0x4255DA] executeTrade：当前玩家购买 (seller, slot) 挂单。现金不足/道具卡满返回 0；
//   成交按类型转移（股票含均价与经营権、地产改 owner、道具/卡片转袋）→ transferMoney。
//   sheet 非空时用 panel.mkf[73] 帧5 提示框（人类，0x424502 @(227,196)）；AI 传 nullptr 静默
int executeTrade(Application& app, int seller, int slot, const UiImage* sheet = nullptr);

// [RE 0x4284BE AI 分支] tradeAiTurn：AI 自动交易（清挂单 → 1/15 挂卖重复卡或库存≥3 道具
//   → 1/3 卡片/道具改价 → 1/4 扫他人挂单低价买入）；beginPlayerTurn case 2/5 调用
void tradeAiTurn(Application& app);

// [RE 0x4284BE 人类分支 + 0x427C21/0x42704E/0x4258C1/0x42608F/0x4267A4/0x426C2E]
//   tradeMarketDialog：公佈欄主面板（panel.mkf[73]/[74]）+ 投标 4 子对话框 + 详情
void tradeMarketDialog(Application& app);

} // namespace rich4
