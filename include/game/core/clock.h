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

// [NEW M4-A2] 帧节拍等待：睡到绝对截止时刻，单次最多 5ms（调用方逐轮喂音频/查打断）。
//   帧截止时刻 = 帧起点累加帧间隔（**绘制耗时计入帧间隔**）→ 动画总时长与绘制快慢无关
//   （画布放大后绘制变慢不再拖慢动画；native 行为变化仅在于绘制 ≥5ms 的帧）。
//   返回 true = 尚未到截止（可继续等待）；false = 已到/超过（立即进入下一帧）。
//   虚拟时钟下 delayMs 仅推进虚拟时间，行为与测试断言（时长/tick 数）保持不变。
inline bool frameWaitStep(uint64_t deadline) {
    const uint64_t now = nowMs();
    if (now >= deadline) {
        return false;
    }
    const uint64_t remain = deadline - now;
    delayMs(remain < 5 ? static_cast<uint32_t>(remain) : 5u);
    return true;
}

} // namespace rich4
