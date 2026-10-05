#include "game/app/message_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/clock.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

struct MessageState {
    Application* app = nullptr;
    const char* text = nullptr;
    int ms = 1500;
    uint64_t startMs = 0;
};

// [RE 0x440CAC] 绘制：g_tipFrame 帧 5 背景框 + 文本居中（与 askDialog 同款）
void redrawMessage(MessageState& st) {
    Surface& dst = st.app->surface();
    const UiImage& tip = st.app->gameState().estateTiles; // [RE 0x48BAD8] data.mkf[517]
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(5), 220, 140, false);
    }
    st.app->text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    st.app->text().drawText(dst, st.text, 220, 140, 4);
}

bool messageEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<MessageState*>(user);
    if (!event) {
        st.startMs = nowMs();
        redrawMessage(st);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (nowMs() - st.startMs >= static_cast<uint64_t>(st.ms)) {
            st.app->events().requestExit(0);
        } else {
            redrawMessage(st);
        }
        return true;
    }
    // [RE 0x440CAC/0x4528B9] 原版延时消息泵检测 WM_LBUTTONUP(514)/WM_MBUTTONDOWN(517)/
    //   WM_KEYUP(257) → 提前结束等待（消息被 PM_REMOVE 吞掉不派发窗口过程，后续流程照常；
    //   原版打断**不停语音**——sub_4528B9 无 voiceStop，与 FloatMessage::finish 不同）;
    //   迁移: 左键按下/Esc/Enter/Space → 退出模态
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        trace::logf("msg skip (RE 0x4528B9)");
        st.app->events().requestExit(0);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN &&
        (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_RETURN ||
         event->key.key == SDLK_SPACE)) {
        trace::logf("msg skip key (RE 0x4528B9)");
        st.app->events().requestExit(0);
        return true;
    }
    return true;
}

} // namespace

void showMessage(Application& app, const char* text, int ms) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    MessageState st;
    st.app = &app;
    st.text = text;
    st.ms = ms;
    RICH4_LOGI("showMessage: %s (%dms, RE 0x440CAC)", text, ms);
    trace::logf("msg text=\"%s\" ms=%d", text, ms);
    runModal(app, &messageEventHandler, &st, 16, true, false);
}

} // namespace rich4
