#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

// [NEW M4-A2] 缩放 blit 核心（nearest 采样；各 blit 入口在 scale != 1 时调用）。
//   src = 源像素基址；srcPitchBytes = 源每行字节数；
//   palette != nullptr → 8bit 索引 + 调色板（SPR，索引 0 透明）；
//   palette == nullptr → RGB555（每像素 2B；opaque=false 时像素 == colorKey 透明）。
//   逻辑目标左上 (logX, logY)（调用方已减去帧 offset）；源区域 (srcX, srcY, w, h)，
//   逻辑尺寸 (w, h) 放大为设备 dw×dh。
//   公共导出：UI 层逐像素叠加（FLC/RAW 位图）复用。
bool blitScaled(Surface& dst, const uint8_t* src, int srcPitchBytes, const uint16_t* palette,
                int logX, int logY, int srcX, int srcY, int w, int h, bool useClipRect,
                bool opaque, uint16_t colorKey = 0);

// [RE 0x45620F] sub_45620F
// 依据: 0x45620F 用 memset32 画上下边、循环画左右边的空心矩形（颜色经 convertColor）
// 用于存档槽悬停高亮（0xFFFF00）与地图选择框
void drawRectBorder(Surface& dst, int x, int y, int w, int h, uint16_t color);

// [RE 0x45663E] blitSpriteFrame
// 依据: 0x45663E 反编译; 帧头 = 资源基址 + 12 + 12*帧号; 调色板 = 基址 + data_offset;
//       8bit 索引像素经调色板取色, 索引 0 透明; 目标 = (x - offsetX, y - offsetY) 裁剪 640x480
// 用于选人界面底部玩家角色动画（JUMP.MKF 角色 SPR）
bool blitSpriteFrame(Surface& dst, const UiImage& spr, int frame, int x, int y);

// [RE 0x456770] sub_456770 SPR 帧绘制（裁剪到全局裁剪矩形 dword_4861B8..C4）
// 依据: 0x456770 反编译; 帧头 12 字节 {w,h,offX,offY,size}, 8bit 索引 + 调色板,
//       目标 = (x - offX, y - offY), 裁剪到 dword_4861B8/C0/BC/C4
// 用于游戏内地图精灵（棋子/住宅用地/商業用地/行業設施點/事件格）
bool blitSpriteFrameClipped(Surface& dst, const UiImage& spr, int frame, int x, int y,
                            bool useClipRect);

// [RE 0x4555C5 → funcs_4555DE[2]=0x45566E] 冬眠/梦游棋子"冰冻蓝白"帧绘制
// 依据: 0x40829D 对 byte54(冬眠)/事件槽 timerA(0x4089C8) 的角色调 sub_4555C5 → 按运行像素格式
//       分派 4 变体逐项变换调色板（detectPixelFormat: 0=RGB555 1=RGB565 2=BGR565 3=BGR555）。
//       用户实机对照 = 蓝白（BGR565 分支 0x45566E: gray=(R+G+B+40)>>2，输出 (R=gray,G=2gray,
//       B=31)）→ 重写 RGB555 视觉等价 = (R/G=gray&0x1F, B=31)（gray 可到 33，原版不饱和、
//       位域环绕）。blit 按索引 0 透明（调色板项 0 不参与像素输出）
bool blitSpriteFrameFreezeClipped(Surface& dst, const UiImage& spr, int frame, int x, int y,
                                  bool useClipRect);

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

// [RE 0x451B9E] highlightRect：按下下沉浮雕——矩形内容向右下平移 shift px，再把顶边/左边
//   scaleSurfaceChannels 变暗；仅按住期间绘制，抬起复原（全量重绘面板天然复原）
// 依据: 0x451B9E 反编译（逐行右下移 + scaleElementChannels(-16) 顶/左 1px）；
//       强度按控件大小调（shift 1px/kChannelHalf 小控件，2px/kChannelThird 大按钮）
// 用途: 热键列表/按钮、股市按钮、卡片·道具格（0x44184A/0x445D59 按下反馈）
void pressDown(Surface& dst, int x, int y, int w, int h, int shift, const uint8_t table[32]);

// [RE 0x4553DA] grayscaleImage（经 0x455442 16bit 分支）
// 依据: 0x455442 反汇编; gray = (R + G + B + 16) >> 2, 重组 R=G=B=gray; 0 保持
// 用于 AI 玩家头像灰度化
void grayscaleImage(uint16_t* pixels, size_t pixelCount);

// [RE 0x456180] blitScrolledMap
// 依据: 0x456180 反编译; 每行 1280 字节按 offset(&~3) 环绕拷贝到 640x480 缓冲
//       （dst 行 = src[off..1280) + src[0..off)），实现地图预览水平无限滚动
void blitScrolledMap(Surface& dst, const uint8_t* src, int offset);

// [RE 0x451A97 saveBackground / 0x451EDB 恢复] 区域背景保存/恢复
// 依据: yesNoDialog 0x453A32 进入时 sub_451E7E（内部 saveBackground(word_46CAEC, 0,
//       rect)）保存框矩形，退出时 sub_451EDB 原样写回；悬停信息板等同机制。
// 用途: 嵌套模态（确认框/悬停板）覆盖外层画面后，退出时必须还原，否则残留
//       （重写为单 Surface 事件驱动绘制，无原版逐模态 WM_PAINT 全量重绘）
bool saveRegion(std::vector<uint16_t>& out, const Surface& dst, int x, int y, int w, int h);
void restoreRegion(Surface& dst, const std::vector<uint16_t>& bg, int x, int y, int w, int h);

} // namespace rich4
