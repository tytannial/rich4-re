#pragma once

#include <cstddef>

namespace rich4 {
namespace dbg {

// [NEW] 测试钩子：随机种子与"结果分支"定点注入（无原版对应；方案 docs/testing.md §4）。
// 未注入时 roll(s,n) 逐位等价 std::rand()%n、raw(s) 等价 std::rand()——默认行为零变化，
// 装饰类随机（眨眼/口型/闪烁相位）不接本钩子，保证 rand 序列与原版一致。
// 注入为 one-shot：命中一次即清除，脚本需重新 set。跨平台注意：MSVC 与 glibc 的
// rand() 序列不同 → 断言不依赖具体随机值，随机分支覆盖一律走注入。
enum Slot {
    SlotAny = 0,   // 通用兜底（未专用注入的 roll 也吃它，便于全局定值）
    SlotDice,      // 骰子点数（值 0..5，调用处 +1）
    SlotRoulette,  // 轮盘停格（0..11）
    SlotCard,      // 赠卡池抽卡
    SlotItem,      // 礼物池抽道具
    SlotLottery,   // 乐透摇号
    SlotNews,      // 新闻随机对象（公司/股）
    SlotFate,      // 命运判定表选择
    SlotMagic,     // 魔法屋条件/惩罚
    SlotMinigame,  // 小游戏埋宝/兜底得分
    SlotAi,        // AI 用卡/用道具/保释等决策随机
    SlotAiTrade,   // AI 交易挂单
    SlotCorp,      // AI 建设施类型
    SlotGod,       // 神明/NPC 行走目标格
    SlotNpc,       // 事件槽 NPC 步数/目标
    SlotSpawn,     // 开局出生点
    SlotJackpot,   // 老虎机/大財神金额
    SlotLuck,      // 运气判定 coin（免付/逃过/加倍）
    SlotShop,      // 百货商品池/赠礼随机
    SlotStock,     // 股价涨跌/交易量
    SlotAuction,   // 拍卖出价
    SlotCount
};

void setSeed(int seed); // -1=原版语义（按 tick 播种）
int seed();

void inject(Slot s, int value);
void clearInject();
bool hasInject(Slot s);
const char* slotName(Slot s);
bool slotByName(const char* name, Slot& out);

// std::rand()%n 的可注入替代（注入值按 %n 取模后使用）
int roll(Slot s, int n);
// std::rand() 的可注入替代（供 >>移位 / double 变换等原始域用法）
int raw(Slot s);

} // namespace dbg
} // namespace rich4
