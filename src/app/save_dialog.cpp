#include "game/app/save_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/save_data.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <cstdio>

namespace rich4 {

namespace {

constexpr int kSlotCount = 6;    // SAVE0.DAT .. SAVE5.DAT
constexpr int kSlotHeight = 72;  // 槽位间距
constexpr int kSlotFirstY = 24;  // 首槽 y
constexpr int kSlotFrame = 10;   // 槽位元素帧
constexpr int kHighlightX = 127;
constexpr int kHighlightW = 452;
constexpr int kHighlightH = 76;
constexpr uint16_t kHighlightColor = rgb888To555(0xFFFF00);

struct SaveDialogState {
    Application* app = nullptr;
    UiImage* uiImage = nullptr;
    UiImage* avatarImage = nullptr;
    SaveSlot slots[kSlotCount];
    int hoverIndex = -1;
};

// [RE 0x4039C2] WM_PAINT：界面 + 各槽（已存在显示信息，空槽仅框）
void redrawSaveDialog(SaveDialogState& state) {
    Surface& surface = state.app->surface();
    if (!state.uiImage) {
        return;
    }
    const UiImage& ui = *state.uiImage;
    blitElement(surface, ui.frame(0), 40, 15, false);

    for (int i = 0; i < kSlotCount; ++i) {
        const int y = kSlotHeight * i + kSlotFirstY;
        if (kSlotFrame < ui.frameCount()) {
            blitElement(surface, ui.frame(kSlotFrame), 129, y, false);
        }
        const SaveSlot& slot = state.slots[i];
        TextRenderer& text = state.app->text();
        text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        if (i == 0) {
            text.drawText(surface, "AUTO", 165, y + 15, 2);
        }
        if (!slot.valid) {
            continue;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u", slot.datetime >> 16);
        text.drawText(surface, buf, 165, kSlotHeight * i + 60, 2);
        std::snprintf(buf, sizeof(buf), "%u/%u", (slot.datetime >> 8) & 0xFFu,
                      slot.datetime & 0xFFu);
        text.drawText(surface, buf, 165, kSlotHeight * i + 81, 2);
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

    if (state.hoverIndex >= 0 && state.hoverIndex < kSlotCount) {
        const int y = kSlotHeight * state.hoverIndex + 22;
        drawRectBorder(surface, kHighlightX, y, kHighlightW, kHighlightH, kHighlightColor);
    }
}

// [RE 0x4039C2] saveDialogWndProc
// 依据: 0x4039C2 反编译; WM_MOUSEMOVE 列表区域计算槽索引 (y-57)/72;
//       WM_LBUTTONDOWN 槽位 → saveGameToSlot; WM_RBUTTONUP → postModalExit(-1)
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
        if (x > 0x81 && x < 0x241 && y > 0x18 && y < 0x1C8) {
            const int index = (y - 24) / kSlotHeight;
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
            // [RE 0x402FD1] saveGameToSlot
            if (saveGameToSlot(state.app->gameDir(), state.hoverIndex, state.app->gameState())) {
                RICH4_LOGI("saved to slot %d (RE 0x402FD1)", state.hoverIndex);
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
        std::snprintf(name, sizeof(name), "SAVE%d.DAT", i);
        readSaveHeader(app.gameDir() + "/" + name, state.slots[i]);
    }

    redrawSaveDialog(state);
    RICH4_LOGI("save dialog (RE 0x404165)");
    return runModal(app, &saveDialogEventHandler, &state);
}

} // namespace rich4
