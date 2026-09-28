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
    // [RE 0x440CAC] 原版为纯延时（sub_4528B9），期间不响应输入
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
    runModal(app, &messageEventHandler, &st, 16);
}

} // namespace rich4
