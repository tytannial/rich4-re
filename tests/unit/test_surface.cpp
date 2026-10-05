#include "microtest.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

// [NEW M4-A1] L0 单测：Surface 运行期画布尺寸（scale 恒 1）
//   锁定 ① create(w,h) 生效（默认 = native 640x480）；
//   ② fillRect 裁剪按运行期尺寸（画布 <640 或越界时不得按设计 640 步长越界）；
//   ③ blit / 区域保存恢复 / blitScrolledMap 全部按画布尺寸工作。

namespace {

class SdlVideo {
public:
    SdlVideo() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return;
        }
        m_window = SDL_CreateWindow("rich4-test", 64, 64, 0);
        if (!m_window) {
            return;
        }
        m_renderer = SDL_CreateRenderer(m_window, nullptr);
    }
    ~SdlVideo() {
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
        }
        if (m_window) {
            SDL_DestroyWindow(m_window);
        }
        SDL_Quit();
    }
    bool ok() const { return m_renderer != nullptr; }
    SDL_Renderer* renderer() { return m_renderer; }

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
};

uint16_t at(const rich4::Surface& s, int x, int y) {
    return s.pixels()[static_cast<size_t>(y) * s.width() + x];
}

} // namespace

MT_TEST(surface_runtime_size_fill_clip) {
    SdlVideo video;
    if (!video.ok()) {
        std::printf("  SKIP (no SDL dummy video)\n");
        return;
    }
    rich4::Surface s;
    MT_CHECK(s.create(video.renderer(), 320, 240));
    MT_EQ(s.width(), 320);
    MT_EQ(s.height(), 240);

    s.clear();
    MT_EQ(at(s, 0, 0), 0);
    s.fillRect(0, 0, 320, 240, 0x7FFF);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x7FFF));
    MT_EQ(at(s, 319, 239), static_cast<uint16_t>(0x7FFF));

    // 右/下越界与负起点：裁剪到 320x240，不得按 640 步长写出
    s.fillRect(300, 230, 100, 100, 0x1234);
    MT_EQ(at(s, 319, 239), static_cast<uint16_t>(0x1234));
    s.fillRect(-10, -10, 12, 12, 0x4321);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x4321));
    MT_EQ(at(s, 1, 1), static_cast<uint16_t>(0x4321));
    MT_EQ(at(s, 2, 2), static_cast<uint16_t>(0x7FFF)); // 负起点填充只影响 (0,0)-(1,1)
    s.destroy();

    // 默认 create = native 设计尺寸
    rich4::Surface d;
    MT_CHECK(d.create(video.renderer()));
    MT_EQ(d.width(), 640);
    MT_EQ(d.height(), 480);
    d.destroy();
}

MT_TEST(surface_blit_clips_to_runtime_canvas) {
    SdlVideo video;
    if (!video.ok()) {
        std::printf("  SKIP (no SDL dummy video)\n");
        return;
    }
    rich4::Surface s;
    MT_CHECK(s.create(video.renderer(), 320, 240));
    s.clear();

    std::vector<uint16_t> px(400 * 10, 0x2A2A);
    rich4::UiFrameView f;
    f.width = 400;
    f.height = 10;
    f.offsetX = 0;
    f.offsetY = 0;
    f.pixels = px.data();

    rich4::setClipRect(0, 0, s.width(), s.height());
    MT_CHECK(rich4::blitElement(s, f, 0, 0, false));
    // 源 400 宽 > 画布 320：仅前 320 列被写；行 0..9 全部画出
    MT_EQ(at(s, 319, 0), static_cast<uint16_t>(0x2A2A));
    MT_EQ(at(s, 319, 9), static_cast<uint16_t>(0x2A2A));
    MT_EQ(at(s, 0, 10), 0);

    // 右下角起点：越界部分被裁剪
    MT_CHECK(rich4::blitElement(s, f, 310, 235, false));
    MT_EQ(at(s, 319, 239), static_cast<uint16_t>(0x2A2A));
    MT_EQ(at(s, 309, 239), 0);
    MT_EQ(at(s, 319, 234), 0);
    s.destroy();
}

MT_TEST(surface_region_save_restore_uses_canvas) {
    SdlVideo video;
    if (!video.ok()) {
        std::printf("  SKIP (no SDL dummy video)\n");
        return;
    }
    rich4::Surface s;
    MT_CHECK(s.create(video.renderer(), 320, 240));
    s.clear();
    s.fillRect(0, 0, 320, 240, 0x1111);
    s.fillRect(318, 238, 2, 2, 0x2222);

    // 请求 (300,230,50,50) 超出画布 → 有效区 20x10，越界部分保持不变
    std::vector<uint16_t> bg;
    MT_CHECK(rich4::saveRegion(bg, s, 300, 230, 50, 50));
    s.fillRect(0, 0, 320, 240, 0x3333);
    rich4::restoreRegion(s, bg, 300, 230, 50, 50);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x3333));
    MT_EQ(at(s, 300, 230), static_cast<uint16_t>(0x1111));
    MT_EQ(at(s, 318, 238), static_cast<uint16_t>(0x2222));
    MT_EQ(at(s, 319, 239), static_cast<uint16_t>(0x2222));
    s.destroy();
}

MT_TEST(surface_scrolled_map_runtime_width) {
    SdlVideo video;
    if (!video.ok()) {
        std::printf("  SKIP (no SDL dummy video)\n");
        return;
    }
    rich4::Surface s;
    MT_CHECK(s.create(video.renderer(), 320, 240));

    // 源 = 640 宽地图预览（行距固定 1280B）：每行左半像素 0x10、右半 0x20
    std::vector<uint16_t> src(240 * 640, 0);
    for (int r = 0; r < 240; ++r) {
        for (int x = 0; x < 640; ++x) {
            src[static_cast<size_t>(r) * 640 + x] =
                static_cast<uint16_t>((r << 8) | (x < 320 ? 0x10 : 0x20));
        }
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(src.data());

    s.clear();
    rich4::blitScrolledMap(s, bytes, 0);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x0010));
    MT_EQ(at(s, 319, 0), static_cast<uint16_t>(0x0010));
    MT_EQ(at(s, 0, 239), static_cast<uint16_t>((239 << 8) | 0x10));

    // offset = 640B（= 320 像素，可被 4 整除）→ 行窗口从源像素 320 起 → 整行 = 源右半
    rich4::blitScrolledMap(s, bytes, 640);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x0020));
    MT_EQ(at(s, 319, 0), static_cast<uint16_t>(0x0020));
    MT_EQ(at(s, 0, 239), static_cast<uint16_t>((239 << 8) | 0x20));
    s.destroy();
}

MT_TEST(surface_scrolled_map_high_canvas_rows_clamped) {
    SdlVideo video;
    if (!video.ok()) {
        std::printf("  SKIP (no SDL dummy video)\n");
        return;
    }
    rich4::Surface s;
    MT_CHECK(s.create(video.renderer(), 320, 520)); // 高 > 源 480：源无第 480+ 行
    s.clear();

    // 完整 640×480 预览源（480 行 x 1280B）
    std::vector<uint16_t> src(480 * 640, 0x7777);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(src.data());
    rich4::blitScrolledMap(s, bytes, 0);
    MT_EQ(at(s, 0, 0), static_cast<uint16_t>(0x7777));
    MT_EQ(at(s, 0, 479), static_cast<uint16_t>(0x7777));
    // 第 480 行起源无数据：必须保持原值（修复前此处按 dst.height() 越界读源内存）
    MT_EQ(at(s, 0, 480), 0);
    MT_EQ(at(s, 319, 519), 0);
    s.destroy();
}
