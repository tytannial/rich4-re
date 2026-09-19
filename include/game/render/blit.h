#pragma once

#include <cstdint>

namespace rich4 {

struct UiFrameView;
class Surface;

// 全局裁剪矩形。
// [RE 0x4861B8] dword_4861B8/C0/BC/C4
// 依据: 0x455C52/0x455FD9/0x455E24 裁剪模式 1 使用该矩形 (left, right, top, bottom)
void setClipRect(int left, int top, int right, int bottom);
void getClipRect(int& left, int& top, int& right, int& bottom);

// [RE 0x455C52] sub_455C52
// 依据: 逐像素 `if (v19) *v16 = v19`（色键 0 透明）; 目标位置 = (x - offsetX, y - offsetY);
//       裁剪模式 0 用目标尺寸, 1 用 dword_4861B8..C4; 返回是否实际绘制
bool blitElement(Surface& dst, const UiFrameView& src, int x, int y, bool useClipRect);

// [RE 0x455FD9] sub_455FD9
// 依据: 色键 blit + 源区域 (srcX, srcY, w, h); 裁剪时同步调整源偏移 (a6/a7)
bool blitElementRegion(Surface& dst, const UiFrameView& src, int x, int y, int srcX, int srcY,
                       int w, int h, bool useClipRect);

// [RE 0x455E24] sub_455E24
// 依据: 不透明 blit + 源区域（qmemcpy 快速拷贝，无色键判断）
bool blitElementRegionOpaque(Surface& dst, const UiFrameView& src, int x, int y, int srcX,
                             int srcY, int w, int h, bool useClipRect);

// [RE 0x455B3A] sub_455B3A
// 依据: 不透明 blit（整元素, qmemcpy 快速拷贝）, 裁剪到目标尺寸
bool blitElementOpaque(Surface& dst, const UiFrameView& src, int x, int y);

// [RE 0x45620F] sub_45620F
// 依据: 0x45620F 用 memset32 画上下边、循环画左右边的空心矩形（颜色经 convertColor）
// 用于存档槽悬停高亮（0xFFFF00）与地图选择框
void drawRectBorder(Surface& dst, int x, int y, int w, int h, uint16_t color);

} // namespace rich4
