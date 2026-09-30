#include "game/render/text.h"

#include "game/core/log.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace rich4 {

namespace {

// [RE 0x4762CC] dword_4762CC 文本渲染表面（512x200，sub_44F935 创建）
constexpr int kBufferWidth = 512;
constexpr int kBufferHeight = 200;

// [PORT] GDI CreateFontA(-size)「字符高度」→ FreeType em size 映射系数。
//   依据: HarmonyOS Sans 汉字 bbox 高 ≈ 0.915em（fontTools 实测），原版 16px 細明體点阵
//   汉字高 ≈ 16px → em ≈ 17.5 → 系数 ≈ 1.09；实机对照（--shot + tools/frame_align.py）可调。
constexpr float kFontSizeScale = 1.09f; // TODO: 实机对照微调

// [PORT] 字体路径（先写死；后续改为配置文件/命令行参数）
//   TODO: 改为配置项（resources/Fonts/ 不入库，见 .gitignore；缺失时文本降级为空操作）
constexpr const char* kFontRegularPath = "resources/Fonts/HarmonyOS_Sans_SC_Regular.ttf";
constexpr const char* kFontBoldPath = "resources/Fonts/HarmonyOS_Sans_SC_Bold.ttf";

constexpr int kMaxGlyphCache = 65536;

struct Glyph {
    bool valid = false;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
    std::vector<uint8_t> cov; // 覆盖度位图（width*height）
};

uint64_t glyphKey(int faceIdx, int px, uint32_t cp) {
    return (static_cast<uint64_t>(faceIdx) << 40) | (static_cast<uint64_t>(px & 0xFF) << 32) | cp;
}

// RGB555 通道插值（cov: 0..255）
inline int blendChannel555(int d, int s, int cov) {
    return (d * (255 - cov) + s * cov + 127) / 255;
}

inline uint16_t blend555(uint16_t dst, uint16_t src, uint8_t cov) {
    if (cov == 0) {
        return dst;
    }
    if (cov == 255) {
        return src;
    }
    const int dr = (dst >> 10) & 0x1F;
    const int dg = (dst >> 5) & 0x1F;
    const int db = dst & 0x1F;
    const int sr = (src >> 10) & 0x1F;
    const int sg = (src >> 5) & 0x1F;
    const int sb = src & 0x1F;
    return static_cast<uint16_t>((blendChannel555(dr, sr, cov) << 10) |
                                 (blendChannel555(dg, sg, cov) << 5) |
                                 blendChannel555(db, sb, cov));
}

// UTF-8 → 码点（p 前进；非法序列返回 false 且 cp=U+FFFD）
bool nextCodepoint(const char*& p, const char* end, uint32_t& cp) {
    const uint8_t b0 = static_cast<uint8_t>(*p);
    ++p;
    if (b0 < 0x80u) {
        cp = b0;
        return true;
    }
    int extra = 0;
    uint32_t acc = 0;
    if ((b0 & 0xE0u) == 0xC0u) {
        extra = 1;
        acc = b0 & 0x1Fu;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        extra = 2;
        acc = b0 & 0x0Fu;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        extra = 3;
        acc = b0 & 0x07u;
    } else {
        cp = 0xFFFDu;
        return false;
    }
    if (p + extra > end) {
        p = end;
        cp = 0xFFFDu;
        return false;
    }
    for (int i = 0; i < extra; ++i) {
        const uint8_t bx = static_cast<uint8_t>(*p);
        if ((bx & 0xC0u) != 0x80u) {
            cp = 0xFFFDu;
            return false;
        }
        acc = (acc << 6) | (bx & 0x3Fu);
        ++p;
    }
    cp = acc;
    return true;
}

} // namespace

struct TextRenderer::Impl {
    FT_Library library = nullptr;
    FT_Face faces[2] = {nullptr, nullptr}; // 0=Regular, 1=Bold
    bool usable = false;

    uint32_t fgRgb = 0; // dword_4762E0
    uint32_t bgRgb = 0; // color
    uint32_t style = 0; // dword_4762D8
    int spacing = 1;    // dword_4762DC
    int fontSize = 16;  // dword_4762D4 字高（竖排行距用）

    std::unordered_map<uint64_t, Glyph> cache;
    std::vector<uint16_t> buffer; // RGB555 文本缓冲（按 scale 放大：512x200 × scale）
    int bufW = kBufferWidth;
    int bufH = kBufferHeight;

    // [NEW M4-A2] 文本缓冲按当前绘制 scale 重建（尺寸变化才重建并清零）
    void ensureBuffer(int w, int h) {
        if (bufW == w && bufH == h && buffer.size() == static_cast<size_t>(w) * h) {
            return;
        }
        bufW = w;
        bufH = h;
        buffer.assign(static_cast<size_t>(w) * h, 0);
    }
};

TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::init() {
    if (m_impl) {
        return true;
    }
    auto* impl = new Impl();
    impl->buffer.assign(static_cast<size_t>(kBufferWidth) * kBufferHeight, 0);

    // [PORT Win32:CreateCompatibleDC/CreateDIBSection + GDI DrawTextW]
    // 替换依据: GDI 为 Windows 独占 → FreeType（本地 third_party 优先构建，见 CMakeLists）
    //   渲染到同一 512x200 RGB555 缓冲，后续 measure/blit/清零语义与原实现一致
    //   （327 处 drawText 调用点零改动；docs/cross-platform.md 阶段4）
    if (FT_Init_FreeType(&impl->library) != 0) {
        RICH4_LOGE("TextRenderer: FT_Init_FreeType failed");
        delete impl;
        return false;
    }
    if (FT_New_Face(impl->library, kFontRegularPath, 0, &impl->faces[0]) != 0) {
        RICH4_LOGW("TextRenderer: font unavailable: %s (text rendering disabled)",
                   kFontRegularPath);
        impl->faces[0] = nullptr;
    }
    if (FT_New_Face(impl->library, kFontBoldPath, 0, &impl->faces[1]) != 0) {
        impl->faces[1] = nullptr; // Bold 缺失 → 回落 Regular
    }
    impl->usable = impl->faces[0] != nullptr;
    if (impl->usable) {
        RICH4_LOGI("TextRenderer: FreeType ready (%s)", kFontRegularPath);
    }
    m_impl = impl;
    return true;
}

void TextRenderer::shutdown() {
    auto* impl = m_impl;
    if (!impl) {
        return;
    }
    for (auto*& face : impl->faces) {
        if (face) {
            FT_Done_Face(face);
            face = nullptr;
        }
    }
    if (impl->library) {
        FT_Done_FreeType(impl->library);
        impl->library = nullptr;
    }
    delete impl;
    m_impl = nullptr;
}

void TextRenderer::setFont(int size, uint32_t fgRgb, uint32_t bgRgb, uint32_t style,
                           int spacing) {
    // [RE 0x44F9D8] 依据: CreateFontA(-size, 0,0,0, weight, 0,0,0, 0x88, 0,0,0,0, "細明體");
    //   weight = (style & 2) ? 700 : 400；spacing → dword_4762DC；
    //   fg/bg 经 ((c&0xFF0000)>>16)|((u8)c<<16)|(c&0xFF00) 转 COLORREF
    // [PORT] GDI 字体参数 → FreeType：字体名固定 HarmonyOS Sans SC（Regular/Bold 两 face，
    //   weight 映射 face 选择），charset/quality 等不再适用
    auto* impl = m_impl;
    if (!impl) {
        return;
    }
    impl->fgRgb = fgRgb;
    impl->bgRgb = bgRgb;
    impl->style = style;
    impl->spacing = spacing;
    impl->fontSize = size;
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

    // [RE 0x45441A] '#NNNN' 前缀 = 语音编号（Speaking.mkf，十进制 4 位），触发播放后文本从 +5 开始绘制
    if (text[0] == '#') {
        int id = 0;
        bool valid = true;
        for (int i = 1; i <= 4; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                valid = false;
                break;
            }
            id = id * 10 + (text[i] - '0');
        }
        if (valid && m_audio) {
            m_audio->playVoice(id);
        }
        text += 5;
    }
    if (!*text) {
        return;
    }
    if (!impl->usable) {
        return; // 字体缺失降级：静默跳过（init 已 WARN 一次）
    }

    const int faceIdx = (impl->style & kTextStyleBold) ? 1 : 0;
    FT_Face face = impl->faces[faceIdx] ? impl->faces[faceIdx] : impl->faces[0];
    if (!face) {
        return;
    }
    // [NEW M4-A2] 文本按画布 scale 1:1 光栅化（设备像素字号 = 设计字号 × kFontSizeScale × scale）
    const float surfScale = dst.scale();
    const int px = static_cast<int>(impl->fontSize * kFontSizeScale * surfScale + 0.5f);
    const int bufW =
        surfScale == 1.0f ? kBufferWidth
                          : static_cast<int>(kBufferWidth * surfScale + 0.5f);
    const int bufH =
        surfScale == 1.0f ? kBufferHeight
                          : static_cast<int>(kBufferHeight * surfScale + 0.5f);
    impl->ensureBuffer(bufW, bufH);
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px)) != 0) {
        return;
    }

    // 字形加载（LRU 上限清理；缓存键 = face|px|码点）
    auto loadGlyph = [&](uint32_t cp) -> const Glyph& {
        static Glyph empty;
        const uint64_t key = glyphKey(faceIdx, px, cp);
        auto it = impl->cache.find(key);
        if (it != impl->cache.end()) {
            return it->second;
        }
        if (impl->cache.size() >= kMaxGlyphCache) {
            impl->cache.clear();
        }
        Glyph g;
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) == 0) {
            FT_GlyphSlot slot = face->glyph;
            g.width = static_cast<int>(slot->bitmap.width);
            g.height = static_cast<int>(slot->bitmap.rows);
            g.bearingX = slot->bitmap_left;
            g.bearingY = slot->bitmap_top;
            g.advance = static_cast<int>(slot->advance.x >> 6);
            g.cov.resize(static_cast<size_t>(g.width) * g.height);
            for (int r = 0; r < g.height; ++r) {
                std::memcpy(g.cov.data() + static_cast<size_t>(r) * g.width,
                            slot->bitmap.buffer + static_cast<size_t>(r) * slot->bitmap.pitch,
                            static_cast<size_t>(g.width));
            }
            g.valid = true;
        }
        return impl->cache.emplace(key, std::move(g)).first->second;
    };

    auto blendGlyph = [&](const Glyph& g, int gx, int gy, uint16_t color555) {
        if (!g.valid || g.width <= 0 || g.height <= 0) {
            return;
        }
        uint16_t* buf = impl->buffer.data();
        for (int r = 0; r < g.height; ++r) {
            const int py = gy + r;
            if (py < 0 || py >= impl->bufH) {
                continue;
            }
            for (int c = 0; c < g.width; ++c) {
                const int pxx = gx + c;
                if (pxx < 0 || pxx >= impl->bufW) {
                    continue;
                }
                const uint8_t cov = g.cov[static_cast<size_t>(r) * g.width + c];
                if (cov == 0) {
                    continue;
                }
                uint16_t& d = buf[static_cast<size_t>(py) * impl->bufW + pxx];
                d = blend555(d, color555, cov);
            }
        }
    };

    const uint16_t fg555 = rgb888To555(impl->fgRgb);
    const uint16_t bg555 = rgb888To555(impl->bgRgb);
    // [NEW M4-A2] 字距/边距/阴影描边步长按 scale 设备化（scale=1 恒等）
    const int u = surfScale == 1.0f ? 1 : static_cast<int>(surfScale + 0.5f);
    const int pad = surfScale == 1.0f ? 10 : static_cast<int>(10 * surfScale + 0.5f);
    const int charSpacing =
        surfScale == 1.0f ? impl->spacing - 1
                          : static_cast<int>((impl->spacing - 1) * surfScale + 0.5f);
    const int ascent = static_cast<int>(face->size->metrics.ascender >> 6);
    const int descent = static_cast<int>(-(face->size->metrics.descender >> 6));
    const int lineHeight = ascent + descent;

    // 布局：\n 显式换行 + 按缓冲宽 512 自动折行（原版 DrawTextW DT_CALCRECT 无 DT_SINGLELINE）
    struct Line {
        std::vector<uint32_t> cps;
    };
    std::vector<Line> lines(1);
    int textWidth = 0;
    {
        int penX = 0;
        const char* p = text;
        const char* end = text + std::strlen(text);
        while (p < end) {
            uint32_t cp = 0;
            nextCodepoint(p, end, cp);
            if (cp == '\n') {
                lines.emplace_back();
                penX = 0;
                continue;
            }
            if (cp == '\r') {
                continue;
            }
            const Glyph& g = loadGlyph(cp);
            const int adv = g.advance + charSpacing;
            if (penX > 0 && penX + adv > impl->bufW) {
                lines.emplace_back();
                penX = 0;
            }
            lines.back().cps.push_back(cp);
            penX += adv;
            if (penX > textWidth) {
                textWidth = penX;
            }
        }
    }

    // rc = DrawTextW(DT_CALCRECT) 尺寸 + 边距 10（原版 rc.right/bottom += 10；scale 下按 pad）
    int rcW = textWidth + pad;
    int rcH = static_cast<int>(lines.size()) * lineHeight + pad;
    if (align == 3) {
        // 竖排：交换宽高（原版 rc.right = v8; rc.bottom = v7）——逐字符竖排总高 × 单字宽
        int vCount = 0;
        for (const Line& ln : lines) {
            vCount += static_cast<int>(ln.cps.size());
        }
        rcW = vCount * lineHeight + pad;
        rcH = px + pad;
    }
    int rcLeft = 0;
    if (align == 4 || align == 7) {
        // 水平居中到缓冲中心（512 缓冲中心 256；scale 下 bufW/2）
        rcLeft = impl->bufW / 2 - (rcW >> 1);
    }

    if (align == 3) {
        // [RE 0x44F7C7] drawTextVertical：逐字符竖排（行距 = 字高 + 字距 + 描边补偿；scale 下设备化）
        int lineH = surfScale == 1.0f
                        ? impl->fontSize + impl->spacing
                        : static_cast<int>((impl->fontSize + impl->spacing) * surfScale + 0.5f);
        if ((impl->style & 6) != 0) {
            lineH += u;
        }
        auto drawVertical = [&](int ox, int oy) {
            int vy = oy;
            for (const Line& ln : lines) {
                for (uint32_t cp : ln.cps) {
                    const Glyph& g = loadGlyph(cp);
                    // 原版 TextOutA 的 y 为基线；字形绘制 = (x + bearingX, baseline - bearingY)
                    blendGlyph(g, ox + g.bearingX, vy - g.bearingY, fg555);
                    vy += lineH;
                }
            }
        };
        auto drawVerticalBg = [&](int ox, int oy) {
            int vy = oy;
            for (const Line& ln : lines) {
                for (uint32_t cp : ln.cps) {
                    const Glyph& g = loadGlyph(cp);
                    blendGlyph(g, ox + g.bearingX, vy - g.bearingY, bg555);
                    vy += lineH;
                }
            }
        };
        if (impl->style & kTextStyleShadow) {
            drawVerticalBg(u, u);
        } else if (impl->style & kTextStyleOutline) {
            drawVerticalBg(u, 0);
            drawVerticalBg(u, 2 * u);
            drawVerticalBg(0, u);
            drawVerticalBg(2 * u, u);
        }
        drawVertical(0, 0);
    } else {
        // 主文本绘制：基线 = ascent + 行序×行高；笔位 = 字符 advance 累加 + 字距
        auto drawHorizontal = [&](int ox, int oy, uint16_t color555) {
            for (size_t li = 0; li < lines.size(); ++li) {
                const int baseline = oy + ascent + static_cast<int>(li) * lineHeight;
                int penX = ox;
                for (uint32_t cp : lines[li].cps) {
                    const Glyph& g = loadGlyph(cp);
                    blendGlyph(g, penX + g.bearingX, baseline - g.bearingY, color555);
                    penX += g.advance + charSpacing;
                }
            }
        };
        const bool centered = (align == 4 || align == 7);
        const int ox = centered ? (impl->bufW / 2 - (textWidth >> 1)) : 0;
        if (impl->style & kTextStyleShadow) {
            drawHorizontal(ox + u, u, bg555);
        } else if (impl->style & kTextStyleOutline) {
            static const int kDx[4] = {1, 1, 0, 2};
            static const int kDy[4] = {0, 2, 1, 1};
            for (int i = 0; i < 4; ++i) {
                drawHorizontal(ox + kDx[i] * u, kDy[i] * u, bg555);
            }
        }
        drawHorizontal(ox, 0, fg555);
    }

    // 测量实际包围盒 [RE 0x44F70C]
    const int search[4] = {rcLeft, 0, rcLeft + rcW, rcH};
    int box[4] = {0, 0, 0, 0};
    measure(impl->buffer.data(), impl->bufW, search, box);
    const int boxW = box[2] - box[0] + 1;
    const int boxH = box[3] - box[1] + 1;
    if (boxW <= 1 && boxH <= 1) {
        // 无可见像素：仍需清零尝试区域
        for (int row = 0; row < rcH && row < impl->bufH; ++row) {
            const int n = rcW < impl->bufW - rcLeft ? rcW : impl->bufW - rcLeft;
            if (n > 0) {
                std::memset(impl->buffer.data() + static_cast<size_t>(row) * impl->bufW + rcLeft,
                            0, static_cast<size_t>(n) * sizeof(uint16_t));
            }
        }
        return;
    }

    // 按对齐调整目标位置（原版 switch (a5)；boxW/boxH 为设备像素 → 落点先设备化）
    int sx = dst.logicalToDevice(x);
    int sy = dst.logicalToDevice(y);
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
    // [NEW M4-A2] 文本已在设备像素光栅化：以 scale=1 直接 1:1 写入设备坐标（无二次放大）
    UiFrameView view;
    view.width = static_cast<uint16_t>(impl->bufW);
    view.height = static_cast<uint16_t>(impl->bufH);
    view.offsetX = 0;
    view.offsetY = 0;
    view.pixels = impl->buffer.data();
    SurfaceScaleGuard devGuard(dst, 1.0f);
    blitElementRegion(dst, view, sx, sy, box[0], box[1], boxW, boxH, false);

    // 清零已用文本区域（原版 sub_4561BE(&unk_4762E8, left, top, w, h, 0)）
    auto* buffer = impl->buffer.data();
    for (int row = box[1]; row <= box[3]; ++row) {
        if (row < 0 || row >= impl->bufH) {
            continue;
        }
        std::memset(buffer + static_cast<size_t>(row) * impl->bufW + box[0], 0,
                    static_cast<size_t>(boxW) * sizeof(uint16_t));
    }
}

} // namespace rich4
