#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

namespace rich4 {

// RGB555 系统内存后台缓冲。
// [RE 0x48A08C] dword_48A08C
// 依据: 0x401543 memcpy(dword_48A08C, data.mkf[601], 614400)（614400 = 640*480*2）;
//       0x451B36 sub_456F60(dword_48A08C, 0, 614400); 0x456418/0x4563F5 等以其为绘制目标;
//       0x40257A WM_PAINT 把绘制结果经 DDraw 后台表面 BltFast 到主表面
// 迁移: DDraw 后台表面 Lock/Unlock + BltFast → SDL 纹理每帧上传（RGB555 与原版一致）
// [NEW M4-A1] 画布尺寸运行期化：kWidth/kHeight 仅作设计尺寸（native 保真画布默认值），
//   实际画布尺寸由 create 决定；所有像素步长/边界必须走 width()/height()
class Surface {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    bool create(SDL_Renderer* renderer, int width = kWidth, int height = kHeight);
    void destroy();
    void present(SDL_Renderer* renderer);

    uint16_t* pixels() { return m_pixels.data(); }
    const uint16_t* pixels() const { return m_pixels.data(); }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void clear();

    // [RE 0x4561BE] sub_4561BE
    // 依据: 0x4561BE 以 memset32 按行填充 (x,y,w,h)，颜色由 sub_4551F0 转换
    void fillRect(int x, int y, int w, int h, uint16_t color);

    // [NEW] 调试截图（24bit BMP）；原版无对应功能，用于与原版画面比对
    bool saveBmp(const char* path) const;

private:
    std::vector<uint16_t> m_pixels;
    SDL_Texture* m_texture = nullptr;
    int m_width = kWidth;
    int m_height = kHeight;
};

// [RE 0x45523E] sub_45523E（经 0x4551F0 funcs_455200[0] 分发）
// 依据: 0x45523E 返回 (c>>3)&0x1F | (c>>6)&0x3E0 | (c>>9)&0x7C00（RGB888→RGB555）
// 迁移: SDL 侧内部固定 RGB555，原版 dword_47637C 显示格式分支（sub_45175D 检测）不再需要
constexpr uint16_t rgb888To555(uint32_t c) {
    return static_cast<uint16_t>(((c >> 3) & 0x1Fu) | ((c >> 6) & 0x3E0u) | ((c >> 9) & 0x7C00u));
}

} // namespace rich4
