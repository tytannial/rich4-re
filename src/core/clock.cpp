#include "game/core/clock.h"

#include <SDL3/SDL.h>

// [NEW] 时钟抽象实现（无原版对应；替换点=全部 SDL_GetTicks/SDL_Delay 调用，
// 正常模式透传，虚拟模式仅累加不睡。方案 docs/testing.md §1）
namespace rich4 {

namespace {
bool g_virtualClock = false;
uint64_t g_virtualMs = 0;
} // namespace

uint64_t nowMs() {
    return g_virtualClock ? g_virtualMs : static_cast<uint64_t>(SDL_GetTicks());
}

void delayMs(uint32_t ms) {
    if (g_virtualClock) {
        g_virtualMs += ms;
        return;
    }
    SDL_Delay(ms);
}

void setVirtualClock(bool on) {
    if (on == g_virtualClock) {
        return;
    }
    if (on) {
        g_virtualMs = static_cast<uint64_t>(SDL_GetTicks());
    }
    g_virtualClock = on;
}

bool virtualClock() { return g_virtualClock; }

void clockAdvanceMs(uint64_t ms) {
    if (g_virtualClock) {
        g_virtualMs += ms;
    }
}

} // namespace rich4
