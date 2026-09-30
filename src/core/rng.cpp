#include "game/core/rng.h"

#include <cstdint>

namespace rich4 {
namespace rng {

namespace {
// CRT 默认 _rand_state = 1（未调用 srand 时的初值）
uint32_t g_state = 1;
} // namespace

void seed(unsigned int s) { g_state = s; }

int next() {
    // MSVC CRT rand(): _holdrand = _holdrand * 214013 + 2531011; (>>16) & 0x7FFF
    g_state = g_state * 214013u + 2531011u;
    return static_cast<int>((g_state >> 16) & 0x7FFFu);
}

} // namespace rng
} // namespace rich4
