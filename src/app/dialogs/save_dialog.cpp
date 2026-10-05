#include "game/app/save_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/save_data.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/paths.h"
#include "game/app/debug/debug.h"
#include "game/core/trace.h"
#include <cstdio>
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <cstdio>

namespace rich4 {

namespace {

constexpr int kSlotCount = 5;    // SAVE1.DAT .. SAVE5.DAT（SAVE0 = AUTO 不可手动存）
constexpr int kSlotHeight = 72;  // 槽位间距
constexpr int kSlotFirstY = 57;  // 首槽 y（原版 72*(i-1)+57）
constexpr int kSlotFrame = 10;   // 槽位元素帧（原版 dword_48A338+132）
constexpr int kPanelFrame = 1;   // 界面背景帧（555x381，原版 dword_48A338+24）
constexpr int kPanelX = 40;
constexpr int kPanelY = 48;
constexpr uint16_t kHighlightColor = rgb888To555(0xFFFF00);

struct SaveDialogState {
    Application* app = nullptr;
    UiImage* uiImage = nullptr;
    UiImage* avatarImage = nullptr;
    SaveSlot slots[kSlotCount];
    int hoverIndex = -1;
};

// [RE 0x403396] saveDialog：背景帧 1 + 5 槽（SAVE1-5）+ 日期/地图/头像
// 依据: 0x403396 反编译; blit 帧 1 到 (40,48); 槽位元素帧 10 到 (129, 57+72i);
//       年 (165, y+36) / 月日 (165, y+57) / 地图图标 (209, y) / 头像 (289+72k, y)
void redrawSaveDialog(SaveDialogState& state) {
    Surface& surface = state.app->surface();
    if (!state.uiImage) {
        return;
    }
    const UiImage& ui = *state.uiImage;
    // 背景帧 1（555x381）到 (40,48)：色键 0 透明（原版 sub_456418），保留游戏画面
    if (ui.frameCount() > kPanelFrame) {
        blitElement(surface, ui.frame(kPanelFrame), kPanelX, kPanelY, false);
    }
    TextRenderer& text = state.app->text();
    for (int i = 0; i < kSlotCount; ++i) {
        const int y = kSlotHeight * i + kSlotFirstY;
        if (kSlotFrame < ui.frameCount()) {
            blitElement(surface, ui.frame(kSlotFrame), 129, y, false);
        }
        const SaveSlot& slot = state.slots[i];
        if (!slot.valid) {
            continue;
        }
        text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u", slot.datetime >> 16);
        text.drawText(surface, buf, 165, y + 36, 2);
        std::snprintf(buf, sizeof(buf), "%u/%u", (slot.datetime >> 8) & 0xFFu,
                      slot.datetime & 0xFFu);
        text.drawText(surface, buf, 165, y + 57, 2);
        const int mapFrame = 4 * slot.gameMode + slot.mapIndex + 2;
        if (mapFrame < ui.frameCount()) {
            blitElement(surface, ui.frame(mapFrame), 209, y, false);
        }
        if (state.avatarImage) {
            int x = 289;
            for (uint32_t k = 0; k < slot.playerCount && k < 4; ++k) {
                const int role = slot.roles[k];
                if (role < state.avatarImage->frameCount()) {
                    blitElement(surface, state.avatarImage->frame(role), x, y, false);
                }
                x += 72;
            }
        }
    }
    // 悬停高亮（原版两次 drawRectBorder 加粗，黄色）
    if (state.hoverIndex >= 0 && state.hoverIndex < kSlotCount) {
        const int y = kSlotHeight * state.hoverIndex + 55;
        drawRectBorder(surface, 127, y, 0x1C4, 76, kHighlightColor);
        drawRectBorder(surface, 128, y + 1, 0x1C2, 74, kHighlightColor);
    }
}

// [RE 0x4039C2] saveDialogWndProc
// 依据: 0x4039C2 反编译; WM_MOUSEMOVE 列表 (0x81..0x241, 0x39..0x1A1) 计算槽 (y-57)/72;
//       WM_LBUTTONDOWN → saveGameToSlot(槽+1) + postModalExit; WM_RBUTTONUP → postModalExit(-1)
bool saveDialogEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<SaveDialogState*>(user);

    if (!event) {
        state.hoverIndex = -1;
        redrawSaveDialog(state);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hit = -1;
        if (x > 0x81 && x < 0x241 && y > 0x39 && y < 0x1A1) {
            const int index = (y - kSlotFirstY) / kSlotHeight;
            if (index >= 0 && index < kSlotCount) {
                hit = index;
            }
        }
        if (hit != state.hoverIndex) {
            state.hoverIndex = hit;
            state.app->audio().playEffect(0); // [RE 0x4542CE] unk_48231A
            redrawSaveDialog(state);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (state.hoverIndex >= 0) {
            state.app->audio().playEffect(2); // [RE 0x4542CE] unk_48232A
            // [RE 0x402FD1] saveGameToSlot(槽+1)：SAVE1-5
            if (saveGameToSlot(state.app->gameDir(), state.hoverIndex + 1,
                               state.app->gameState())) {
                RICH4_LOGI("saved to slot %d (RE 0x4039C2)", state.hoverIndex + 1);
                state.app->events().requestExit(state.hoverIndex);
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        state.app->audio().playEffect(4); // [RE 0x4542CE] unk_482332
        state.app->events().requestExit(-1);
        return true;
    }
    return false;
}

} // namespace

int saveDialog(Application& app) {
    // [RE 0x404165] 依据: 0x404165 → data.mkf[520]/[2] + runModal(sub_4039C2)
    SaveDialogState state;
    state.app = &app;

    UiImage uiImage;
    if (auto blob = app.gameState().data.read(520)) {
        uiImage.load(std::move(*blob));
    } else {
        RICH4_LOGE("saveDialog: Data.mkf[520] unavailable");
        return -1;
    }
    UiImage avatarImage;
    if (auto blob = app.gameState().data.read(2)) {
        avatarImage.load(std::move(*blob));
    }
    state.uiImage = &uiImage;
    state.avatarImage = &avatarImage;

    for (int i = 0; i < kSlotCount; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "SAVE%d.DAT", i + 1);
        readSaveHeader(readableDataFile(app.gameDir(), name), state.slots[i]);
    }

    // [NEW M4-D 实机 2026-10-05] 删除模态外预绘制（同 loadDialog：无 origin 残影；
    //   handler(nullptr) 进入时以正确 origin 重绘）
    RICH4_LOGI("save dialog (RE 0x404165)");
    // [NEW] named region：槽 0..4（命中帶 x(129,577) y=57+72i 與 MOTION hit 同源）
    {
        static bool s_regSv = false;
        if (!s_regSv) {
            s_regSv = true;
            for (int i = 0; i < kSlotCount; ++i) {
                char nm[14];
                std::snprintf(nm, sizeof(nm), "sv.slot.%d", i);
                debug::registerRegion(nm, 130, kSlotFirstY + kSlotHeight * i, 446, kSlotHeight);
            }
        }
    }
    // [NEW M4-D 实机] 两側保留游戏画面（同大地图处理，不填黑）
    return runModal(app, &saveDialogEventHandler, &state, 0, true, false);
}

} // namespace rich4
