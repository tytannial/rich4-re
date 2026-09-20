#include "game/app/confirm_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x453A32] sub_453A32(320, 200)：确认框中心
constexpr int kDialogCX = 320;
constexpr int kDialogCY = 200;
constexpr int kFrameNormal = 0;
constexpr int kFrameYes = 1;
constexpr int kFrameNo = 2;

struct ConfirmState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    int originX = 0;
    int originY = 0;
    int hover = 0; // 0=无 1=YES 2=NO
};

// [RE 0x45367E] WM_PAINT：帧 0 背景 / 帧 1 YES 高亮 / 帧 2 NO 高亮
void redrawConfirm(ConfirmState& st) {
    Surface& dst = st.app->surface();
    const UiImage& ui = *st.ui;
    const int frame = (st.hover == 1) ? kFrameYes : (st.hover == 2) ? kFrameNo : kFrameNormal;
    if (frame < ui.frameCount()) {
        blitElementOpaque(dst, ui.frame(frame), st.originX, st.originY);
    }
}

// [RE 0x45367E] confirmWndProc
bool confirmEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<ConfirmState*>(user);
    const UiImage& ui = *st.ui;
    const int w = ui.frame(0).width;
    const int h = ui.frame(0).height;
    if (!event) {
        st.hover = 0;
        redrawConfirm(st);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hover = 0;
        if (x >= st.originX && x < st.originX + w && y >= st.originY && y < st.originY + h) {
            hover = (x < st.originX + w / 2) ? 1 : 2;
        }
        if (hover != st.hover) {
            st.hover = hover;
            redrawConfirm(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x45367E] WM_RBUTTONUP(0x205) → postModalExit(0)
            st.app->events().requestExit(0);
            return true;
        }
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x45367E] WM_LBUTTONUP(0x202)：左半 1(YES) / 右半 0(NO)
            const int x = static_cast<int>(event->button.x);
            const int y = static_cast<int>(event->button.y);
            if (x >= st.originX && x < st.originX + w && y >= st.originY && y < st.originY + h) {
                st.app->events().requestExit(x < st.originX + w / 2 ? 1 : 0);
            }
            return true;
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        st.app->events().requestExit(0);
        return true;
    }
    return false;
}

} // namespace

bool confirmDialog(Application& app) {
    UiImage ui;
    if (auto blob = app.gameState().data.read(440)) {
        ui.load(std::move(*blob));
    } else {
        RICH4_LOGW("confirmDialog: data.mkf[440] unavailable");
        return false;
    }
    if (ui.frameCount() == 0) {
        return false;
    }
    ConfirmState st;
    st.app = &app;
    st.ui = &ui;
    st.originX = kDialogCX - ui.frame(0).width / 2;
    st.originY = kDialogCY - ui.frame(0).height / 2;
    RICH4_LOGI("confirm dialog (RE 0x453A32)");
    return runModal(app, &confirmEventHandler, &st) == 1;
}

} // namespace rich4
