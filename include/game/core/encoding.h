#pragma once

#include <string>

namespace rich4 {

// BIG5 (cp950) → UTF-8。
// 依据: 游戏内码为 BIG5（docs/formats/text.md），引擎内部统一 UTF-8；
// 迁移: Win32 MultiByteToWideChar(950) + WideCharToMultiByte(CP_UTF8)
std::string big5ToUtf8(const char* text, int length = -1);

} // namespace rich4
