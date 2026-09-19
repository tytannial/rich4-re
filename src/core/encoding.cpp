#include "game/core/encoding.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rich4 {

#ifdef _WIN32

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
    const int wideLen = MultiByteToWideChar(950, 0, text, length, nullptr, 0);
    if (wideLen <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(950, 0, text, length, wide.data(), wideLen);
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr,
                                            nullptr);
    if (utf8Len <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

#else // !_WIN32

// 非 Windows 平台占位（跨平台转码待办）。
std::string big5ToUtf8(const char*, int) {
    return {};
}

#endif

} // namespace rich4
