#include "game/core/debug_hooks.h"
#include "game/core/rng.h"

#include <array>
#include <cstring>
#include <cstdlib>

// [NEW] 测试钩子实现（无原版对应；方案 docs/testing.md §4）
namespace rich4 {
namespace dbg {

namespace {

constexpr int kNoInject = -1;

int g_seed = -1;
// [NEW] 静态零初始化会把每槽填成 0（≠ kNoInject），使 hasInject 误报为「注入了 0」，
//   导致未经 --inject 的正常游玩首个命中各钩子的随机点被钉死为 0（轮盘→cell0→theme1=0xFF=255）。
//   必须在声明处用 kNoInject 填充。
std::array<int, SlotCount> g_inject = [] {
    std::array<int, SlotCount> a{};
    a.fill(kNoInject);
    return a;
}();
const char* const kSlotNames[SlotCount] = {
    "any",   "dice",    "roulette", "card",   "item",     "lottery", "news",  "fate",
    "magic", "minigame", "ai",      "aitrade", "corp",    "god",     "npc",   "spawn",
    "jackpot", "luck",  "shop",     "stock",   "auction",
};

} // namespace

void setSeed(int seed) { g_seed = seed; }
int seed() { return g_seed; }

void inject(Slot s, int value) {
    if (s >= 0 && s < SlotCount) {
        g_inject[static_cast<size_t>(s)] = value;
    }
}

void clearInject() { g_inject.fill(kNoInject); }

bool hasInject(Slot s) {
    return s >= 0 && s < SlotCount && g_inject[static_cast<size_t>(s)] != kNoInject;
}

const char* slotName(Slot s) {
    return (s >= 0 && s < SlotCount) ? kSlotNames[static_cast<size_t>(s)] : "?";
}

bool slotByName(const char* name, Slot& out) {
    for (int i = 0; i < SlotCount; ++i) {
        if (std::strcmp(kSlotNames[i], name) == 0) {
            out = static_cast<Slot>(i);
            return true;
        }
    }
    return false;
}

int raw(Slot s) {
    const int iv = (s >= 0 && s < SlotCount) ? g_inject[static_cast<size_t>(s)] : kNoInject;
    if (iv != kNoInject) {
        g_inject[static_cast<size_t>(s)] = kNoInject;
        return iv;
    }
    const int av = g_inject[SlotAny];
    if (av != kNoInject) {
        g_inject[SlotAny] = kNoInject;
        return av;
    }
    return rng::next();
}

int roll(Slot s, int n) {
    if (n <= 0) {
        return 0;
    }
    return ((raw(s) % n) + n) % n;
}

} // namespace dbg
} // namespace rich4
