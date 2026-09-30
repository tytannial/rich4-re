#pragma once

#include <cstdint>
#include <vector>

#include "game/render/ui_image.h"

namespace rich4 {

class Surface;

// 软件光标（原版直接绘制在主表面并保存/恢复背景）。
// [RE 0x4020FA] cursorInit
// 依据: 0x4020FA 加载 data.mkf[0] 光标资源、创建 32x32 背景元素（sub_451A5A）、
//       ShowCursor(0) 隐藏系统光标、timeSetEvent(20ms, fptc) 驱动动画;
//       0x4021F8 选择光标组（dword_48A0F4 = 资源 + 12*index + 12）
class Cursor {
public:
    static constexpr int kSize = 32; // 原版光标包围盒 32x32

    // [RE 0x4020FA] 初始化（resource = Data.mkf[0]）
    bool init(std::vector<uint8_t> resource);
    void shutdown();

    // [RE 0x4021F8] select
    // 依据: 0x4021F8 设置光标组索引、动画帧数 word_48A170、帧间隔 word_48A174
    void select(int cursorIndex, int frameCount, int frameDelay);

    // [RE 0x401F5E] 恢复光标下的背景（dword_48A168 != -1 时）
    void uncompose(Surface& dst);

    // [RE 0x401E59] 保存背景并绘制当前光标帧
    // 依据: 0x401E59 计算 (x-offsetX, y-offsetY)，裁剪到 640x480，先 sub_451A97
    //       保存背景再 sub_4562A5 绘制光标; dword_48A168 记录绘制位置
    void compose(Surface& dst, int x, int y);

    // [RE 0x401F98] fptc 光标定时器（20ms）：动画帧推进 + 鼠标位置跟踪
    // 依据: 0x401F98 中 word_48A176/word_48A174 控制帧间隔, word_48A172 递增回绕;
    //       GetCursorPos 与 dword_48A168/164 比较，变化或换帧时重绘
    // 迁移: GetCursorPos（屏幕坐标）→ 由调用方传入 640x480 逻辑坐标
    void update(Surface& dst, int mouseX, int mouseY);

    // [RE 0x4024C0] sub_4024C0 光标包围矩形（裁剪到运行期画布尺寸）
    void getRect(const Surface& dst, int mouseX, int mouseY, int outRect[4]) const;

    // [NEW] 背景作废：调用方已/即将全量重绘整个表面，m_saved 陈旧——
    //   下一轮 update/compose 不做 uncompose 写回（防"旧背景脏块闪现"，
    //   即时模式每 tick 全量重绘界面必调；增量绘制界面勿调，否则光标拖影）
    void invalidate() { m_visible = false; }

    // [NEW] 演出段隐藏软件光标（原版无此行为，现代化增强）：
    //   隐藏时先恢复背景保持表面干净，compose/update 跳过绘制
    void setHidden(Surface& dst, bool hidden) {
        if (hidden == m_hidden) {
            return;
        }
        m_hidden = hidden;
        if (m_hidden) {
            uncompose(dst);
        }
    }
    bool hidden() const { return m_hidden; }

    bool initialized() const { return m_initialized; }
    bool visible() const { return m_visible; }

private:
    const UiFrameView* currentFrame() const;

    // [NEW M4-A2] 光标帧按画布 scale 放大后的设备化绘制（目标设备坐标；索引 0 透明）
    void blitFrameDevice(Surface& dst, const UiFrameView& frame, int devX, int devY) const;

    UiImage m_image;
    bool m_initialized = false;
    int m_cursorIndex = 0;  // dword_48A0F4 对应组索引（41 = 默认箭头）
    int m_frameCount = 1;   // word_48A170
    int m_frameDelay = 1;   // word_48A174
    int m_frame = 0;        // word_48A172
    int m_frameTick = 0;    // word_48A176
    int m_posX = 0;         // dword_48A164
    int m_posY = 0;         // dword_48A168（x 坐标；-1 表示未绘制）
    bool m_visible = false;
    bool m_hidden = false; // [NEW] 演出段隐藏光标
    int m_saveX = 0;        // dword_48A0EC
    int m_saveY = 0;        // dword_48A0F0
    int m_saveW = 0;
    int m_saveH = 0;
    std::vector<uint16_t> m_saved; // dword_48A0E8 背景元素像素
    int m_saveStride = kSize;      // [NEW M4-A2] 保存缓冲设备行距（= 设备保存宽）
};

} // namespace rich4
