#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

// LZHUF 解压（自适应哈夫曼 + LZSS），详见 docs/formats/lzhuf.md。
// 每次调用内部重置工作区，可重复调用。
std::vector<uint8_t> lzhufDecompress(const uint8_t* src, size_t srcSize, size_t outSize);

} // namespace rich4
