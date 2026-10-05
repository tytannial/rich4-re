#include "game/app/float_message.h"

#include <SDL3/SDL.h>
#include "game/core/clock.h"
#include "game/core/trace.h"

#include "game/application.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

// [RE 0x44EC30] 记录元素板与位置（原版仅存全局参数表，绘制在 show 进行）
void FloatMessage::setup(const UiImage& elem, int frame, int x, int y, int textDx, int textDy,
                         uint32_t color, uint32_t shadow) {
    m_elem = &elem;
    m_frame = frame;
    m_x = x;
    m_y = y;
    m_dx = textDx;
    m_dy = textDy;
    m_color = color;
    m_shadow = shadow;
}

// [RE 0x44ECB6] 显示：saveBackground(0x451A97) → blitElement 板（sub_456418）→
//   setFont(20, color, shadow, shadow?3:2, 1) → drawText 居中（+文字偏移）→ 计时
void FloatMessage::show(Application& app, const char* text) {
    Surface& dst = app.surface();
    m_text = text != nullptr ? text : "";
    const UiFrameView& fr = m_elem->frame(m_frame);
    m_bgX = m_x - fr.offsetX;
    m_bgY = m_y - fr.offsetY;
    m_bgW = fr.width;
    m_bgH = fr.height;
    // [NEW M4-A2] 区域快照走公共 API（逻辑区域按画布 scale 设备化；缩放后恢复一致）
    saveRegion(m_bg, dst, m_bgX, m_bgY, m_bgW, m_bgH);
    blitElement(dst, fr, m_x, m_y, false);
    app.text().setFont(20, m_color, m_shadow, m_shadow != 0 ? 3 : 2, 1);
    // 原版文字位置 = (dword_48C618 + left + w/2, dword_48C62C + top + h/2)，align 4 居中
    app.text().drawText(dst, text, m_bgX + m_dx + fr.width / 2,
                        m_bgY + m_dy + fr.height / 2, 4);
    trace::logf("float text=\"%s\"", text);
    m_startMs = nowMs();
}

// [RE 0x44EE18(0)] 计时推进：≥2000ms 后每 tick 查语音（sub_4544B9），播完才恢复
bool FloatMessage::advance(Application& app) {
    if (m_startMs == 0) {
        restore(app); // 原版 LABEL_10：start 为 0 直接恢复并返回完成
        return true;
    }
    if (nowMs() - m_startMs < 2000) {
        return false;
    }
    if (app.audio().voicePlaying()) {
        return false; // 语音未完（原版把 start 置 1 继续下 tick 轮询，语义等价）
    }
    restore(app);
    return true;
}

// [RE 0x44EE18(1)] 点击跳过：stopVoice（sub_454493）+ 立即恢复
void FloatMessage::finish(Application& app) {
    app.audio().stopVoice();
    m_startMs = 0;
    restore(app);
}

// 消息进行中重绘（面板全量重绘后调用）：只画板与文字，不重置计时；
//   跳过 '#NNNN' 前缀，避免 drawText 重复触发语音（原版消息期间面板不重绘，无此问题）
void FloatMessage::redraw(Application& app) {
    if (m_startMs == 0 || m_elem == nullptr) {
        return;
    }
    Surface& dst = app.surface();
    const UiFrameView& fr = m_elem->frame(m_frame);
    blitElement(dst, fr, m_x, m_y, false);
    app.text().setFont(20, m_color, m_shadow, m_shadow != 0 ? 3 : 2, 1);
    const char* t = m_text.c_str();
    if (t[0] == '#') {
        t += 5;
    }
    app.text().drawText(dst, t, m_bgX + m_dx + fr.width / 2, m_bgY + m_dy + fr.height / 2, 4);
}

void FloatMessage::restore(Application& app) {
    m_startMs = 0;
    if (m_bg.empty()) {
        return;
    }
    // [NEW M4-A2] 与 show 的 saveRegion 对应（同一 scale 下区域一致）
    restoreRegion(app.surface(), m_bg, m_bgX, m_bgY, m_bgW, m_bgH);
    m_bg.clear();
}

} // namespace rich4
