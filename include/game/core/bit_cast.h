#pragma once

#include <cstdint>
#include <cstring>

namespace rich4 {

// [NEW] float → int32 位模式重解释（无原版对应；原 exe 用 *reinterpret_cast<int32_t*>(&f)*）
// 依据: GCC/Clang -O2 下 int32 别名读 float 属 strict aliasing UB（TBAA 可误编译），
//       C++17 无 std::bit_cast → memcpy 等价语义，跨平台结果一致
inline int32_t floatBits(float v) {
    int32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

} // namespace rich4
