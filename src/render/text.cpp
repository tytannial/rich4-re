#include "game/render/text.h"

#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rich4 {

#ifdef _WIN32

namespace {

// [RE 0x4762CC] dword_4762CC 文本渲染表面（512x200，sub_44F935 创建）
constexpr int kBufferWidth = 512;
constexpr int kBufferHeight = 200;

// RGB888 → COLORREF(0x00BBGGRR)。
// [RE 0x44F9D8] 依据: ((a2 & 0xFF0000) >> 16) | ((u8)a2 << 16) | (a2 & 0xFF00)
COLORREF toColorRef(uint32_t rgb) {
    return static_cast<COLORREF>(((rgb & 0xFF0000u) >> 16) | ((rgb & 0xFFu) << 16) |
                                 (rgb & 0xFF00u));
}

std::wstring utf8ToWide(const char* text) {
    if (!text || !*text) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 1) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), len);
    wide.resize(static_cast<size_t>(len - 1));
    return wide;
}

} // namespace

struct TextRenderer::Impl {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* bits = nullptr;
    HFONT font = nullptr;

    uint32_t fgRgb = 0;  // dword_4762E0
    uint32_t bgRgb = 0;  // color
    uint32_t style = 0;  // dword_4762D8
    int spacing = 1;     // dword_4762DC
    int fontSize = 16;   // dword_4762D4 字高（竖排行距用）
};

TextRenderer::~TextRenderer() {
    shutdown();
}

bool TextRenderer::init() {
    if (m_impl) {
        return true;
    }
    auto* impl = new Impl();

    // 迁移: 原版对 DDraw 表面 GetDC → SDL 侧用 DIB Section + 内存 DC
    HDC screen = GetDC(nullptr);
    impl->dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!impl->dc) {
        delete impl;
        RICH4_LOGE("TextRenderer: CreateCompatibleDC failed");
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kBufferWidth;
    bmi.bmiHeader.biHeight = -kBufferHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 16;
    bmi.bmiHeader.biCompression = BI_RGB;
    impl->bitmap = CreateDIBSection(impl->dc, &bmi, DIB_RGB_COLORS, &impl->bits, nullptr, 0);
    if (!impl->bitmap) {
        DeleteDC(impl->dc);
        delete impl;
        RICH4_LOGE("TextRenderer: CreateDIBSection failed");
        return false;
    }
    impl->oldBitmap = SelectObject(impl->dc, impl->bitmap);
    SetBkMode(impl->dc, TRANSPARENT);
    std::memset(impl->bits, 0, static_cast<size_t>(kBufferWidth) * kBufferHeight * 2);
    m_impl = impl;
    return true;
}

void TextRenderer::shutdown() {
    auto* impl = m_impl;
    if (!impl) {
        return;
    }
    if (impl->font) {
        DeleteObject(impl->font);
    }
    if (impl->dc && impl->oldBitmap) {
        SelectObject(impl->dc, impl->oldBitmap);
    }
    if (impl->bitmap) {
        DeleteObject(impl->bitmap);
    }
    if (impl->dc) {
        DeleteDC(impl->dc);
    }
    delete impl;
    m_impl = nullptr;
}

void TextRenderer::setFont(int size, uint32_t fgRgb, uint32_t bgRgb, uint32_t style,
                           int spacing) {
    // [RE 0x44F9D8] 依据: CreateFontA(-size, 0,0,0, weight, 0,0,0, 0x88, 0,0,0,0, "細明體");
    // weight = (style & 2) ? 700 : 400
    auto* impl = m_impl;
    if (!impl) {
        return;
    }
    impl->fgRgb = fgRgb;
    impl->bgRgb = bgRgb;
    impl->style = style;
    impl->spacing = spacing;
    impl->fontSize = size;

    if (impl->font) {
        DeleteObject(impl->font);
        impl->font = nullptr;
    }
    const int weight = (style & kTextStyleBold) ? 700 : 400;
    // 字体名 "細明體"（UTF-16；原版为 BIG5 字节 B2D3 A9FA C5E9），
    // charset 0x88 = CHINESEBIG5_CHARSET
    impl->font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, 0x88, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
                             L"\u7D30\u660E\u9AD4");
    if (!impl->font) {
        RICH4_LOGW("TextRenderer: CreateFontW failed, using default font");
    }
}

void TextRenderer::measure(const uint16_t* buffer, int bufferWidth, const int rect[4],
                           int outRect[4]) {
    // [RE 0x44F70C] 依据: 0x44F70C 扫描 [left, right) x [0, bottom) 的非零像素,
    // 输出 [minX, minY, maxX, maxY]（全零输出 0）；行距固定 512（v9 += 512）
    int minX = 10000;
    int minY = 10000;
    int maxX = -10000;
    int maxY = -10000;
    for (int y = 0; y < rect[3]; ++y) {
        const uint16_t* row = buffer + static_cast<size_t>(y) * bufferWidth;
        for (int x = rect[0]; x < rect[2]; ++x) {
            if (row[x]) {
                if (x < minX) {
                    minX = x;
                }
                if (y < minY) {
                    minY = y;
                }
                if (x > maxX) {
                    maxX = x;
                }
                if (y > maxY) {
                    maxY = y;
                }
            }
        }
    }
    if (minX == 10000) {
        outRect[0] = 0;
        outRect[1] = 0;
        outRect[2] = 0;
        outRect[3] = 0;
        return;
    }
    outRect[0] = minX;
    outRect[1] = minY;
    outRect[2] = maxX;
    outRect[3] = maxY;
}

void TextRenderer::drawText(Surface& dst, const char* text, int x, int y, int align) {
    // [RE 0x44FABC] 依据: 0x44FABC 反编译
    auto* impl = m_impl;
    if (!impl || !text || !*text) {
        return;
    }

    // [RE 0x45441A] '#NNNN' 前缀 = 语音编号（speaking.mkf），文本从 +5 开始绘制
    // TODO(RE 0x45441A): 语音播放（Audio 迁移后接入）
    if (text[0] == '#') {
        text += 5;
    }
    if (!*text) {
        return;
    }
    const std::wstring wide = utf8ToWide(text);
    if (wide.empty()) {
        return;
    }

    HGDIOBJ oldFont = SelectObject(impl->dc, impl->font);
    SetTextCharacterExtra(impl->dc, impl->spacing - 1);

    // DT_CALCRECT 计算文本尺寸, 再加边距 10（原版 rc.right/bottom += 10）
    RECT rc = {0, 0, kBufferWidth, kBufferHeight};
    DrawTextW(impl->dc, wide.c_str(), -1, &rc, DT_CALCRECT | DT_SINGLELINE);
    rc.right += 10;
    rc.bottom += 10;
    if (align == 3) {
        // 竖排：交换宽高（原版 rc.right = v8; rc.bottom = v7）
        const LONG t = rc.right;
        rc.right = rc.bottom;
        rc.bottom = t;
    } else if (align == 4 || align == 7) {
        // 水平居中到 256（512 缓冲中心）
        rc.left = 256 - (rc.right >> 1);
        rc.right = (rc.right >> 1) + 256;
    }

    // 阴影（bit0）或描边（bit2）用背景色绘制, 主文本用前景色
    if (align == 3) {
        // [RE 0x44F7C7] drawTextVertical：逐字符竖排（行距 = 字高 + 字距 + 描边补偿）
        int lineH = impl->fontSize + impl->spacing;
        if ((impl->style & 6) != 0) {
            ++lineH;
        }
        auto drawVertical = [&](int ox, int oy) {
            int vy = rc.top + oy;
            for (wchar_t ch : wide) {
                TextOutW(impl->dc, rc.left + ox, vy, &ch, 1);
                vy += lineH;
            }
        };
        if (impl->style & kTextStyleShadow) {
            SetTextColor(impl->dc, toColorRef(impl->bgRgb));
            drawVertical(1, 1);
        } else if (impl->style & kTextStyleOutline) {
            SetTextColor(impl->dc, toColorRef(impl->bgRgb));
            drawVertical(1, 0);
            drawVertical(1, 2);
            drawVertical(0, 1);
            drawVertical(2, 1);
        }
        SetTextColor(impl->dc, toColorRef(impl->fgRgb));
        drawVertical(0, 0);
    } else {
        const bool centered = (align == 4 || align == 7);
        const UINT flags = (centered ? DT_CENTER : DT_LEFT) | DT_SINGLELINE;
        if (impl->style & kTextStyleShadow) {
            SetTextColor(impl->dc, toColorRef(impl->bgRgb));
            RECT r = rc;
            r.left += 1;
            r.top += 1;
            DrawTextW(impl->dc, wide.c_str(), -1, &r, flags);
        } else if (impl->style & kTextStyleOutline) {
            SetTextColor(impl->dc, toColorRef(impl->bgRgb));
            static const int kDx[4] = {1, 1, 0, 2};
            static const int kDy[4] = {0, 2, 1, 1};
            for (int i = 0; i < 4; ++i) {
                RECT r = rc;
                r.left += kDx[i];
                r.top += kDy[i];
                DrawTextW(impl->dc, wide.c_str(), -1, &r, flags);
            }
        }
        SetTextColor(impl->dc, toColorRef(impl->fgRgb));
        RECT mainRc = rc;
        if (impl->style & kTextStyleOutline) {
            mainRc.left += 1;
            mainRc.top += 1;
        }
        DrawTextW(impl->dc, wide.c_str(), -1, &mainRc, flags);
    }
    GdiFlush();
    SelectObject(impl->dc, oldFont);

    // 测量实际包围盒 [RE 0x44F70C]
    const int search[4] = {rc.left, rc.top, rc.right, rc.bottom};
    int box[4] = {0, 0, 0, 0};
    measure(static_cast<const uint16_t*>(impl->bits), kBufferWidth, search, box);
    const int boxW = box[2] - box[0] + 1;
    const int boxH = box[3] - box[1] + 1;
    if (boxW <= 1 && boxH <= 1) {
        return;
    }

    // 按对齐调整目标位置（原版 switch (a5)）
    int sx = x;
    int sy = y;
    switch (align) {
        case 1:
            sx -= boxW;
            break;
        case 2:
        case 3:
        case 4:
            sx -= boxW >> 1;
            sy -= boxH >> 1;
            break;
        case 5:
            sy -= boxH >> 1;
            break;
        case 6:
            sx -= boxW;
            sy -= boxH >> 1;
            break;
        case 7:
            sx -= boxW >> 1;
            sy -= boxH;
            break;
        default:
            break;
    }

    // blit 到目标（原版 sub_456495/sub_4564E6 → sub_455FD9）
    UiFrameView view;
    view.width = kBufferWidth;
    view.height = kBufferHeight;
    view.offsetX = 0;
    view.offsetY = 0;
    view.pixels = static_cast<const uint16_t*>(impl->bits);
    blitElementRegion(dst, view, sx, sy, box[0], box[1], boxW, boxH, false);

    // 清零已用文本区域（原版 sub_4561BE(&unk_4762E8, left, top, w, h, 0)）
    auto* buffer = static_cast<uint16_t*>(impl->bits);
    for (int row = box[1]; row <= box[3]; ++row) {
        if (row < 0 || row >= kBufferHeight) {
            continue;
        }
        std::memset(buffer + static_cast<size_t>(row) * kBufferWidth + box[0], 0,
                    static_cast<size_t>(boxW) * sizeof(uint16_t));
    }
}

#else // !_WIN32

// 非 Windows 平台占位（跨平台文本迁移待办）。
struct TextRenderer::Impl {};

TextRenderer::~TextRenderer() {
    shutdown();
}

bool TextRenderer::init() {
    RICH4_LOGW("TextRenderer: GDI implementation is Windows-only (migration TODO)");
    return false;
}

void TextRenderer::shutdown() {}

void TextRenderer::setFont(int, uint32_t, uint32_t, uint32_t, int) {}

void TextRenderer::measure(const uint16_t*, int, const int[4], int outRect[4]) {
    outRect[0] = outRect[1] = outRect[2] = outRect[3] = 0;
}

void TextRenderer::drawText(Surface&, const char*, int, int, int) {}

#endif

} // namespace rich4
