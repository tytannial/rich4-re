#include "game/core/encoding.h"

#include "game/core/big5_tables.h"

#include <cstdint>
#include <cstring>

namespace rich4 {

namespace {

// UTF-8 编码追加（1-4 字节；cp 为有效 Unicode 码点）
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80u) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800u) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

} // namespace

// [PORT Win32:MultiByteToWideChar(950) + WideCharToMultiByte(CP_UTF8)]
// 替换依据: 原版（Windows 独占）经系统代码页 950 转换；非 Windows 无此 API，旧实现为
//   空桩 → 所有 BIG5 文本为空。改为内嵌 cp950 映射表（tools/gen_big5_tables.py 生成
//   src/core/big5_tables.cpp）查表 + 手工 UTF-8 编码：两平台行为完全一致，且消除对
//   系统代码页/语言包的依赖（docs/cross-platform.md）。
// 语义保持: length < 0 取 strlen；length 显式时按字节数转换（嵌入 0 字节照转，
//   与原 MultiByteToWideChar 一致）；非法序列 → U+FFFD（等价系统默认替换行为）。
std::string big5ToUtf8(const char* text, int length) {
    if (!text || !*text) {
        return {};
    }
    if (length < 0) {
        length = static_cast<int>(std::strlen(text));
    }
    if (length == 0) {
        return {};
    }
    std::string out;
    out.reserve(static_cast<size_t>(length) * 3 / 2);
    int i = 0;
    while (i < length) {
        const uint8_t b = static_cast<uint8_t>(text[i]);
        if (b < 0x80u) {
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        if (b >= 0x81u && b <= 0xFEu && i + 1 < length) {
            const uint8_t b2 = static_cast<uint8_t>(text[i + 1]);
            const uint32_t cp = kBig5ToUnicode[(b - 0x81) * 256 + b2];
            if (cp != 0) {
                appendUtf8(out, cp);
                i += 2;
                continue;
            }
        }
        // 非法序列 → U+FFFD
        appendUtf8(out, 0xFFFDu);
        ++i;
    }
    return out;
}

} // namespace rich4
