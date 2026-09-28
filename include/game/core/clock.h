#pragma once

#include <cstdint>

namespace rich4 {

// [NEW] 时钟抽象（headless 测试基础设施，无原版对应）
// 正常模式透传 SDL_GetTicks/SDL_Delay（行为零变化）；虚拟时钟模式下
// delayMs 不睡眠仅累加虚拟毫秒，nowMs 返回虚拟时间——阻塞动画/消息循环
// （playEventFlc、showMessage、飞行、骰子停留等 while(now<end){…; delay}）
// 在 headless 下按虚拟时间快速收敛。方案见 docs/testing.md。
uint64_t nowMs();
void delayMs(uint32_t ms);
// 开启/关闭虚拟时钟（--test-clock / --headless 接线）；开启时以当前真实 ticks 为基准
void setVirtualClock(bool on);
bool virtualClock();
// 外部驱动步进（供测试主循环/脚本 wait 使用）
void clockAdvanceMs(uint64_t ms);

} // namespace rich4
