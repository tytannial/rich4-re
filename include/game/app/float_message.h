#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

class Application;
class UiImage;

// 浮动消息板（面板"喊话"：元素底图 + 20 号大字 + 角色语音，消息期间挂起面板交互）。
// 对应原版 0x44EC30（sub_44EC30 设参）/ 0x44ECB6（sub_44ECB6 绘制+存背景+计时）/
// 0x44EE18（sub_44EE18 advance/skip）/ 0x44EF3B（sub_44EF3B active 查询）。
// 语义（依据 0x44EE18 反编译）：显示后**至少停留 2000ms**（0x7D0）；超时后每 tick 查
//   语音播放状态（sub_4544B9），播完才恢复背景结束（语音未完则继续等）；点击 skip 立即结束
//   （sub_44EE18(1)：先停语音 sub_454493）。文本带 '#NNNN' 前缀时由 drawText 触发语音
//   （0x45441A，单通道）。
class FloatMessage {
public:
    // [RE 0x44EC30] 设置目标元素板（elem 帧 frame）与绘制位置/文字偏移/颜色。
    //   原版参数：a1=元素头、a2/a3=位置、a4=文字 x 偏移、a5=文字 y 偏移（惯用 -6）、
    //   a6=文字色、a7=阴影底色（0 = 无阴影 style2，非 0 = style3）
    void setup(const UiImage& elem, int frame, int x, int y, int textDx, int textDy,
               uint32_t color, uint32_t shadow);
    // [RE 0x44ECB6] 显示：保存背景矩形 → blit 板（色键）→ setFont(20) → 居中 drawText
    //   （'#NNNN' 前缀在此触发语音）→ 计时。重复 show 直接覆盖（原版释放旧保存区）
    void show(Application& app, const char* text);
    // [RE 0x44EE18(0)] 定时推进（面板 100ms tick 调用）；true = 消息已结束（背景已恢复，
    //   面板应重绘自身内容，对齐原版调用后 sub_43C8FB + InvalidateRect）
    bool advance(Application& app);
    // [RE 0x44EE18(1)] 立即结束（点击跳过：停语音 + 恢复背景）
    void finish(Application& app);
    // [RE 0x44ECB6 重复绘制] 消息进行中重绘板与文字（面板每帧全量重绘后调用，重置计时；
    //   跳过 '#NNNN' 前缀避免重复播放语音）。不动画的面板可不调用
    void redraw(Application& app);
    // [RE 0x44EF3B] 消息进行中（面板据此屏蔽悬停/点击）
    bool active() const { return m_startMs != 0; }

private:
    void restore(Application& app);

    const UiImage* m_elem = nullptr;
    int m_frame = 0;
    int m_x = 0;
    int m_y = 0;
    int m_dx = 0;
    int m_dy = 0;
    uint32_t m_color = 0;
    uint32_t m_shadow = 0;
    std::string m_text; // 当前消息文本（redraw 复用）
    // 保存的背景（原版 saveBackground word_46CAEC / 释放 sub_456E11）
    std::vector<uint16_t> m_bg;
    int m_bgX = 0;
    int m_bgY = 0;
    int m_bgW = 0;
    int m_bgH = 0;
    uint64_t m_startMs = 0; // 0 = 未显示或已超时待查语音
};

} // namespace rich4
