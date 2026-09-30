#pragma once

namespace rich4 {

// [NEW] 跨平台定点随机数（无原版对应；原版 exe 直接调用 MSVC CRT rand()）
// 依据: 0x4015D6 srand(GetTickCount()) + 全 exe std::rand() 调用（骰子/轮盘/事件/AI）；
//       MSVC CRT: state = state*214013 + 2531011; return (state >> 16) & 0x7FFF（RAND_MAX=32767）；
//       glibc rand() 算法不同（RAND_MAX=2^31-1）→ 两平台序列不一致，且 `rand()>>N` 类概率
//       写法在 glibc 下概率剧变（如 1/1024 → 1/2^21）。
//       复刻 MSVC LCG 使两平台序列 bit 级一致：--seed 跨平台可复现、既有 91 场景基线
//       在 Linux 可直接复用（docs/cross-platform.md）。
namespace rng {

constexpr int kRandMax = 32767; // 与 MSVC std::rand() 的 RAND_MAX 一致

// [RE CRT:srand] 等价 MSVC std::srand
void seed(unsigned int s);

// [RE CRT:rand] 等价 MSVC std::rand（LCG 214013/2531011, (state>>16)&0x7FFF）
int next();

} // namespace rng
} // namespace rich4
