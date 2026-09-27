// [RE 0x440E1A / 0x43FF56] 玩家列表选择对话框（嫁祸卡 0x44476A 人类多目标分支）。
// 依据: 0x440E1A 反编译; 资源 data.mkf[518]（候选数 2..8 → 帧 0..6 横排框，帧内头像
//   @(12+80i,12)）、data.mkf[2]（角色头像帧 charIndex）、g_tipFrame(data.mkf[517]) 帧 5
//   提示框 @(220,140) + drawText(220,140,align 4)；命中区 x∈[220-offX+12, +80n)、
//   y∈[320-offY+12, +72)；hover 播 g_uiSoundHover + 三层边框 80×78（颜色 0xFFFF00 黄）；
//   左键 g_uiSoundClick → 返回玩家；右键 g_uiSoundCancel → -1。
// 迁移: 原版保存 (0,40,440,480) 背景 + 逐模态 WM_PAINT → 重写单 Surface 每事件重绘。

#include "game/app/player_select_dialog.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {
namespace {

constexpr int kFrameX = 220; // [RE 0x440E1A] blitElementFullscreen(..., 220, 320)
constexpr int kFrameY = 320;
constexpr int kTipX = 220; // 提示框 (g_tipFrame 帧 5)
constexpr int kTipY = 140;
constexpr int kInnerX = 12; // 框内头像起点（原版 blitBackground(..., 12+80i, 12)）
constexpr int kInnerY = 12;
constexpr int kSlotW = 80;
constexpr int kSlotH = 72;
constexpr int kBorderW = 0x4E; // 78 [RE 0x43FF56]
constexpr int kBorderH = 78;
// [RE 0x440114/0x44012C/0x44014A push 0FFFF00h] 悬停三层边框色 = RGB888 黄 0xFFFF00；
//   ⚠ word_46CAEC 是 640x480 surface 描述符（首 word = 宽 640），不是颜色（旧实现误读为 0x0280）
constexpr uint16_t kBorderColor = rgb888To555(0xFFFF00);

struct SelectState {
    Application* app = nullptr;
    UiImage sheet;   // data.mkf[518] 候选框（SMP）
    UiImage avatars; // data.mkf[2] 角色头像（SMP）
    std::vector<int> ids;
    const char* prompt = "";
    int count = 0;
    int boxX = 0; // 头像区屏幕左上（= 220-offX+12 / 320-offY+12）
    int boxY = 0;
    int hover = -1;
};

void redraw(SelectState& s) {
    Application& app = *s.app;
    Surface& dst = app.surface();
    const GameState& st = app.gameState();
    // [RE 0x440E1A] blitElementFullscreen(backbuffer, 框帧, 220, 320)（不透明）
    const UiFrameView& box = s.sheet.frame(s.count - 2);
    blitElementOpaque(dst, box, kFrameX, kFrameY);
    s.boxX = kFrameX - box.offsetX + kInnerX;
    s.boxY = kFrameY - box.offsetY + kInnerY;
    // 头像（帧 = charIndex）
    for (int i = 0; i < s.count; ++i) {
        const int p = s.ids[static_cast<size_t>(i)];
        if (p < 0 || p >= 9) {
            continue;
        }
        const int ch = st.players[p].charIndex;
        if (ch >= 0 && ch < s.avatars.frameCount()) {
            blitElementOpaque(dst, s.avatars.frame(ch), s.boxX + kSlotW * i, s.boxY);
        }
    }
    // [RE 0x43FF56] hover 三层边框（0xFFFF00 黄）
    if (s.hover >= 0 && s.hover < s.count) {
        const int x = s.boxX + kSlotW * s.hover - 3;
        const int y = s.boxY - 3;
        drawRectBorder(dst, x, y, kBorderW, kBorderH, kBorderColor);
        drawRectBorder(dst, x + 1, y + 1, kBorderW - 2, kBorderH - 2, kBorderColor);
        drawRectBorder(dst, x + 2, y + 2, kBorderW - 4, kBorderH - 4, kBorderColor);
    }
    // [RE 0x440E1A] 提示框（g_tipFrame 帧 5，色键）+ 文本居中
    if (st.estateTiles.frameCount() > 5) {
        blitElement(dst, st.estateTiles.frame(5), kTipX, kTipY, false);
    }
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, s.prompt, kTipX, kTipY, 4);
}

// [RE 0x43FF56] 命中：x∈[boxX, boxX+80n)、y∈[boxY, boxY+72)
int hitSlot(const SelectState& s, int x, int y) {
    const int lx = x - s.boxX;
    const int ly = y - s.boxY;
    if (lx < 0 || ly < 0 || ly >= kSlotH) {
        return -1;
    }
    const int i = lx / kSlotW;
    return (i >= 0 && i < s.count) ? i : -1;
}

bool selectHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<SelectState*>(user);
    if (event == nullptr) {
        redraw(s);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const int slot =
                hitSlot(s, static_cast<int>(event->motion.x), static_cast<int>(event->motion.y));
            if (slot != s.hover) {
                if (slot >= 0) {
                    s.app->audio().playEffect(0); // [RE 0x43FF56] g_uiSoundHover
                }
                s.hover = slot;
                redraw(s);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_RIGHT ||
                event->button.button == SDL_BUTTON_MIDDLE) {
                s.app->audio().playEffect(4); // g_uiSoundCancel
                s.app->events().requestExit(-1);
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const int slot =
                hitSlot(s, static_cast<int>(event->button.x), static_cast<int>(event->button.y));
            if (slot >= 0) {
                s.app->audio().playEffect(1); // g_uiSoundClick
                s.app->events().requestExit(s.ids[static_cast<size_t>(slot)]);
            }
            return true;
        }
        default:
            return true;
    }
}

} // namespace

int selectPlayerDialog(Application& app, const int* playerIds, int count, const char* prompt) {
    if (playerIds == nullptr || count < 2 || count > 8) {
        return -1;
    }
    GameState& st = app.gameState();
    SelectState s;
    s.app = &app;
    s.ids.assign(playerIds, playerIds + count);
    s.prompt = prompt != nullptr ? prompt : "";
    s.count = count;
    auto sheetBlob = st.data.read(518); // [RE 0x440E1A] dword_48C510
    if (!sheetBlob || !s.sheet.load(std::move(*sheetBlob))) {
        RICH4_LOGW("selectPlayerDialog: data.mkf[518] unavailable (RE 0x440E1A)");
        return -1;
    }
    if (s.sheet.frameCount() < count - 1) {
        RICH4_LOGW("selectPlayerDialog: data.mkf[518] frames %d < %d (RE 0x440E1A)",
                   s.sheet.frameCount(), count - 1);
        return -1;
    }
    auto avatarBlob = st.data.read(2); // [RE 0x440E1A] dword_48C518
    if (!avatarBlob || !s.avatars.load(std::move(*avatarBlob))) {
        RICH4_LOGW("selectPlayerDialog: data.mkf[2] unavailable (RE 0x440E1A)");
        return -1;
    }
    RICH4_LOGI("selectPlayerDialog: count=%d prompt=%s (RE 0x440E1A)", count, s.prompt);
    const int r = runModal(app, &selectHandler, &s, 0);
    return (r >= 0 && r < 8) ? r : -1;
}

} // namespace rich4
