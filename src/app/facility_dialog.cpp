#include "game/app/facility_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x43FAE4] 图标行：x = 50 + 68*i, y = 286，尺寸 68x68（5 项）
constexpr int kIconX0 = 50;
constexpr int kIconY = 286;
constexpr int kIconStep = 68;
constexpr int kIconCount = 5;
// [RE 0x46CAEC] word_46CAEC 悬停边框色（RGB555 = 0x0280 绿色）
constexpr uint16_t kHoverColor = 0x0280;

struct FacilityState {
    Application* app = nullptr;
    int hover = -1;
};

// [RE 0x43FAE4] WM_PAINT：帧 4 底图 + 帧 5 标题框 + 标题 + 悬停边框/名称
void redrawFacility(FacilityState& st) {
    Surface& dst = st.app->surface();
    const UiImage& tip = st.app->gameState().estateTiles; // [RE 0x48BAD8] data.mkf[517]
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(4), 43, 279, false);  // 帧 4（5 图标底图）
        blitElement(dst, tip.frame(5), 220, 140, false); // 帧 5（标题框）
    }
    st.app->text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    // [RE 0x43FAE4] drawText(0, &byte_465289, 220, 122, 2)（左对齐于 220,122）
    st.app->text().drawText(dst, "請選擇設施類別", 220, 122, 2);
    if (st.hover >= 0 && st.hover < kIconCount) {
        const int x = kIconX0 + kIconStep * st.hover;
        // 原版三层边框 68/66/64（drawRectBorder(word_46CAEC, ...)）
        drawRectBorder(dst, x, kIconY, 68, 68, kHoverColor);
        drawRectBorder(dst, x + 1, kIconY + 1, 66, 66, kHoverColor);
        drawRectBorder(dst, x + 2, kIconY + 2, 64, 64, kHoverColor);
        // 设施名（g_facilityNames = kBuildingNames[6..10]）
        st.app->text().drawText(dst, kBuildingNames[6 + st.hover], 220, 154, 4);
    }
}

// [RE 0x43FAE4] sub_43FAE4 窗口过程
bool facilityEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<FacilityState*>(user);
    if (!event) {
        st.hover = -1;
        redrawFacility(st);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hover = -1;
        // [RE 0x43FAE4] WM_MOUSEMOVE: 命中判定 x∈[50,390) y∈[286,354)
        if (x >= kIconX0 && x < kIconX0 + kIconStep * kIconCount && y >= kIconY &&
            y < kIconY + kIconStep) {
            hover = (x - kIconX0) / kIconStep;
        }
        if (hover != st.hover) {
            // [RE 0x43FBD A] 悬停音效 g_uiSoundHover
            st.app->audio().playEffect(0);
            st.hover = hover;
            redrawFacility(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x43FE88] 左键：postModalExit(hover+1-1 = hover)
            if (st.hover >= 0) {
                st.app->audio().playEffect(1); // [RE 0x43FE95] g_uiSoundClick
                st.app->events().requestExit(st.hover);
            }
            return true;
        }
        if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x43FEC2] 右键：postModalExit(-1)（g_uiSoundCancel）
            st.app->audio().playEffect(4);
            st.app->events().requestExit(-1);
            return true;
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        st.app->audio().playEffect(4);
        st.app->events().requestExit(-1);
        return true;
    }
    return false;
}

} // namespace

int selectFacilityDialog(Application& app, int flag) {
    (void)flag; // 原版 flag 传 lParam 供 sub_43FAE4 初始化（重写无状态差异）
    FacilityState st;
    st.app = &app;
    RICH4_LOGI("select facility dialog (RE 0x440AAC)");
    return runModal(app, &facilityEventHandler, &st);
}

} // namespace rich4
