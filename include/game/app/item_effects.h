#pragma once

namespace rich4 {

class Application;

// [RE 0x447D97] g_itemEffectFuncs(0x475DD4) 分派：返回非 0 = 结束道具流程；
//   返回 0 = 等价原版「不可用 → 重弹面板再选、不消耗」
int itemEffect(Application& app, int id);

} // namespace rich4
