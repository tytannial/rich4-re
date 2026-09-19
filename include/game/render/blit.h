#pragma once

#include <cstddef>
#include <cstdint>

namespace rich4 {

struct UiFrameView;
class Surface;
class UiImage;

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

// [RE 0x45663E] blitSpriteFrame
// 依据: 0x45663E 反编译; 帧头 = 资源基址 + 12 + 12*帧号; 调色板 = 基址 + data_offset;
//       8bit 索引像素经调色板取色, 索引 0 透明; 目标 = (x - offsetX, y - offsetY) 裁剪 640x480
// 用于选人界面底部玩家角色动画（JUMP.MKF 角色 SPR）
bool blitSpriteFrame(Surface& dst, const UiImage& spr, int frame, int x, int y);

// [RE 0x4552B7] convertImageChannels（经 0x455337 16bit 分支）
// 依据: 0x455337 对每像素 R/G/B 各 5bit 查 32 字节表后重组 RGB555
//       （表 = unk_485D68 + 32*a6，-16 = kChannelHalf，-20 = kChannelThird）
// 用于地图预览暗化（选人界面滚动背景）
void convertImageChannels(uint16_t* dst, const uint16_t* src, size_t pixelCount,
                          const uint8_t table[32]);

// [RE 0x4552E7] scaleElementChannels
// 依据: 0x4552E7 对元素内 (x, y, w, h) 区域逐像素做 convertImageChannels 变换
// 用于已选头像变暗（-16）与悬停名字条（-20）
void scaleSurfaceChannels(Surface& dst, int x, int y, int w, int h, const uint8_t table[32]);

// [RE 0x4553DA] grayscaleImage（经 0x455442 16bit 分支）
// 依据: 0x455442 反汇编; gray = (R + G + B + 16) >> 2, 重组 R=G=B=gray; 0 保持
// 用于 AI 玩家头像灰度化
void grayscaleImage(uint16_t* pixels, size_t pixelCount);

// [RE 0x456180] blitScrolledMap
// 依据: 0x456180 反编译; 每行 1280 字节按 offset(&~3) 环绕拷贝到 640x480 缓冲
//       （dst 行 = src[off..1280) + src[0..off)），实现地图预览水平无限滚动
void blitScrolledMap(Surface& dst, const uint8_t* src, int offset);

} // namespace rich4
