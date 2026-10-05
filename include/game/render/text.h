#pragma once

#include <cstdint>
#include <string>

namespace rich4 {

class Surface;
class Audio;

// 文本样式位（原版 dword_4762D8）。
// 依据: 0x44FABC 中 bit0=阴影绘制(偏移 1,1), bit2=四向描边, bit3=特殊 blit 路径
enum TextStyle : uint32_t {
    kTextStyleShadow = 1,
    kTextStyleBold = 2,
    kTextStyleOutline = 4,
    kTextStyleSpecial = 8,
};

// GDI 文本渲染器（Windows 先行；跨平台迁移见 docs/reverse 计划）。
// [RE 0x44F9D8] setFont / [RE 0x44FABC] drawText / [RE 0x44F70C] measure / [RE 0x44F7C7] 竖排
// 迁移: DDraw 512x200 文本表面 GetDC + DrawTextA → CreateDIBSection(RGB555) + GDI DC;
//       原版 dword_4762CC 表面锁定后的像素缓冲由 DIB 承担
class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // [NEW M4-F] 字体路径/字号缩放由 rich4.ini 配置（默认 = 原硬编码值）
    bool init(const char* regularPath = nullptr, const char* boldPath = nullptr,
              float sizeScale = 1.09f);
    void shutdown();

    // [RE 0x45441A] 接入语音播放：drawText 的 '#NNNN' 前缀触发 Audio::playVoice（原版
    //   sub_44FABC→sub_45441A 直调全局 DirectSound 语音 buffer）；未设置时仅跳过前缀
    void setAudio(Audio* audio) { m_audio = audio; }

    // [RE 0x44F9D8] setFont
    // 依据: CreateFontA(-size, 0,0,0, weight, 0,0,0, 0x88(CHINESEBIG5_CHARSET), 0,0,0,0,
    //       "細明體"); style bit1 时 weight=700 否则 400; spacing 存入 dword_4762DC;
    //       fg/bg 经 ((c&0xFF0000)>>16)|((u8)c<<16)|(c&0xFF00) 转 COLORREF
    void setFont(int size, uint32_t fgRgb, uint32_t bgRgb, uint32_t style, int spacing);

    // [RE 0x44FABC] drawText
    // 依据: 0x44FABC 反编译; '#NNNN' 前缀播放语音(sub_45441A)后从 +5 绘制;
    //       DT_CALCRECT 计算尺寸 + 边距 10; 阴影(bit0)/描边(bit2)多次绘制;
    //       测量包围盒后按 align 调整位置 blit 到目标; 绘制后清零文本区域
    // align: 0 左上 / 1 右上 / 2,3,4 居中 / 5 垂直居中 / 6 左下 / 7 底中
    void drawText(Surface& dst, const char* text, int x, int y, int align);

    // [RE 0x44F70C] measure
    // 依据: 0x44F70C 扫描 rect 范围内非零像素，输出 [minX, minY, maxX, maxY]
    void measure(const uint16_t* buffer, int bufferWidth, const int rect[4], int outRect[4]);

private:
    std::string m_fontRegular; // [NEW M4-F] 配置字体
    std::string m_fontBold;
    float m_sizeScale = 1.09f;
    struct Impl;
    Impl* m_impl = nullptr;
    Audio* m_audio = nullptr;
};

} // namespace rich4
