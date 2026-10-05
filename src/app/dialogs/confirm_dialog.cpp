#include "game/app/confirm_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/app/ui_layout.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/platform/audio.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

constexpr int kFrameNormal = 0;
constexpr int kFrameYes = 1;
constexpr int kFrameNo = 2;

struct ConfirmState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    int originX = 0;
    int originY = 0;
    int hover = 0; // 0=无 1=YES 2=NO
    const char* text = nullptr; // [RE 0x440BA8] askDialog 形式：非空时居中绘制
    int cx = 320;
    int cy = 200;
};

// [RE 0x45367E] WM_PAINT：帧 0 背景 / 帧 1 YES 高亮 / 帧 2 NO 高亮
void redrawConfirm(ConfirmState& st) {
    Surface& dst = st.app->surface();
    if (st.text != nullptr) {
        // [RE 0x440BA8] askDialog：先画提示框背景 = g_tipFrame 帧 5（sub_456418 → blitElement
        //   色键 blit）于 (220,140)，再以同坐标居中画文本（drawText 标志 4）
        const UiImage& tip = st.app->gameState().estateTiles; // [RE 0x48BAD8] data.mkf[517]
        if (tip.frameCount() > 5) {
            blitElement(dst, tip.frame(5), st.cx, 140, false);
        }
        // 文本居中于 (cx, 140)，setTextFont(16, 0xF0F0F0, 0x101010, 3, 1)
        st.app->text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        st.app->text().drawText(dst, st.text, st.cx, 140, 4);
    }
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
            st.app->audio().playEffect(0); // [RE 0x4537A8] 悬停音效（dword_48231A）
            st.hover = hover;
            redrawConfirm(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x45367E] WM_RBUTTONUP(0x205) → postModalExit(0)
            st.app->audio().playEffect(4); // [RE 0x4534A5] 取消音效（dword_482332）
            st.app->events().requestExit(0);
            return true;
        }
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x45367E] WM_LBUTTONUP(0x202)：左半 1(YES) / 右半 0(NO)
            const int x = static_cast<int>(event->button.x);
            const int y = static_cast<int>(event->button.y);
            if (x >= st.originX && x < st.originX + w && y >= st.originY && y < st.originY + h) {
                // [RE 0x45390F/0x453976] 确定/取消按钮点击音效（dword_48232A=选槽）
                st.app->audio().playEffect(2);
                st.app->events().requestExit(x < st.originX + w / 2 ? 1 : 0);
            }
            return true;
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        st.app->audio().playEffect(4); // [RE 0x4534A5] 取消音效（dword_482332）
        st.app->events().requestExit(0);
        return true;
    }
    return false;
}

} // namespace

bool confirmDialog(Application& app, const char* text, int cx, int cy) {
    trace::logf("dialog open name=confirm");
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
    st.text = text;
    st.cx = cx;
    st.cy = cy;
    st.originX = cx - ui.frame(0).width / 2;
    st.originY = cy - ui.frame(0).height / 2;
    // [RE 0x451E7E/0x451EDB] yesNoDialog 进入保存框区域背景、退出原样恢复：
    //   重写为单 Surface 事件驱动绘制，外层模态不会自动重绘 → 不恢复会残留确认框
    //   （表现为"点 NO 后画面卡住"）
    std::vector<uint16_t> bg;
    const int w = ui.frame(0).width;
    const int h = ui.frame(0).height;
    // [NEW M4-D 实机] save/restore 用画布坐标（含 640 基准模态居中偏移 base）：确认框
    //   绘制在 origin +base 下；此前用 640 坐标保存/恢复 → 宽屏退出后恢复错位并残留面板
    const int base = uiModalBaseX(app.surface());
    saveRegion(bg, app.surface(), st.originX + base, st.originY, w, h);
    RICH4_LOGI("confirm dialog at (%d,%d)%s (RE 0x453A32)", cx, cy,
               text ? " +text (RE 0x440BA8)" : "");
    const int result = runModal(app, &confirmEventHandler, &st, 0, true, false);
    restoreRegion(app.surface(), bg, st.originX + base, st.originY, w, h);
    return result == 1;
}

} // namespace rich4
