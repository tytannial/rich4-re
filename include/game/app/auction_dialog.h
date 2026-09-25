#pragma once

namespace rich4 {

class Application;

// [RE 0x43BDE5] runAuction：土地公开拍卖面板（panel.mkf[26]，竞价模态 0x43A2DD）。
//   seller = 卖方玩家（-1 = 系统/公有地 → 价款入公库 g_publicFund）；
//   objId = estate(2000..4000)/corp(4000..6000)；playMusic → musicPlayScene(5) 压栈。
//   起拍价 = moneyMul × (int)(base × (1 + level×0.5))（estate+28 / corp+34，+26 等级）。
//   成交：视口对准 → 1s → 产权转移（空地+地权设置写到期日）→ 小地图重建 → 1s →
//   transferMoney(赢家, seller, 现价, 0)。调用者：新闻 idx 7（-1）、魔法屋惩罚 11。
//   返回 true = 成交，false = 流标。
// 依据: 0x43BDE5/0x43A2DD/0x439F0D 反编译；详见 docs/reverse/functions/43380a-magic-house.md §5
bool runAuction(Application& app, int seller, int objId, bool playMusic);

} // namespace rich4
