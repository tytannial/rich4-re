#include "game/app/ai_dialog.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"

namespace rich4 {

namespace {

constexpr int kPanelFrame = 0;      // panel.mkf[77] 帧 0（435x355 背景）
constexpr int kPlayerBoxFrame = 2;  // 帧 2（116x86 玩家框）
constexpr int kAvatarBase = 6;      // 帧 6+charIndex（角色头像）
constexpr uint32_t kTextLight = 0xF0F0F0;
constexpr uint32_t kTextDark = 0x101010;

struct AiDialogState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    int hoverIndex = -1;
};

// [RE 0x41E345] WM_PAINT：背景 + 文本 + 存活玩家列表
void redrawAiDialog(AiDialogState& st) {
    Application& app = *st.app;
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    dst.clear();
    const UiImage& ui = *st.ui;
    if (ui.frameCount() > kPanelFrame) {
        blitElementOpaque(dst, ui.frame(kPanelFrame), 0, 0);
    }
    TextRenderer& text = app.text();
    text.setFont(20, kTextDark, 0, kTextStyleBold, 1);
    text.drawText(dst, "託管AI", 173, 26, 2);
    text.drawText(dst, "個  性", 173, 114, 2);
    text.drawText(dst, "資金運用比例", 249, 246, 2);
    text.setFont(16, kTextDark, 0, kTextStyleBold, 1);
    text.drawText(dst, "使用卡片", 244, 53, 2);
    text.drawText(dst, "使用道具", 244, 84, 2);
    text.drawText(dst, "乖寶寶", 244, 141, 2);
    text.drawText(dst, "普通人", 244, 173, 2);
    text.drawText(dst, "大老奸", 244, 206, 2);
    text.drawText(dst, "現金", 191, 277, 6);
    text.drawText(dst, "存款", 305, 277, 5);
    text.drawText(dst, "股票", 191, 310, 6);
    text.drawText(dst, "資金", 305, 310, 5);
    text.setFont(20, kTextDark, 0, 0, 1);
    text.drawText(dst, "確定", 397, 125, 3);
    text.drawText(dst, "取消", 397, 213, 3);

    // 存活玩家列表（框 + 头像）
    int y = 8;
    for (int i = 0; i < state.playerCount && i < 4; ++i) {
        const Player& p = state.players[i];
        if ((p.alive & 1) == 0) {
            continue;
        }
        if (ui.frameCount() > kPlayerBoxFrame) {
            blitElement(dst, ui.frame(kPlayerBoxFrame), 8, y, false);
        }
        const int avatarFrame = kAvatarBase + p.charIndex;
        if (avatarFrame < ui.frameCount()) {
            blitElement(dst, ui.frame(avatarFrame), 80, y + 40, false);
        }
        y += 83;
    }
}

// [RE 0x41DDA9] 事件：確定/取消 关闭；玩家槽悬停
bool aiDialogEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<AiDialogState*>(user);
    if (!event) {
        st.hoverIndex = -1;
        redrawAiDialog(st);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        st.app->events().requestExit(0);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        st.app->events().requestExit(0);
        return true;
    }
    return false;
}

} // namespace

void aiDialog(Application& app) {
    // [RE 0x41E345] 依据: 0x41E345 → panel.mkf[77] + runModal(sub_41DDA9)
    UiImage ui;
    if (auto blob = app.gameState().panel.read(77)) {
        ui.load(std::move(*blob));
    } else {
        RICH4_LOGE("aiDialog: panel.mkf[77] unavailable");
        return;
    }
    AiDialogState st;
    st.app = &app;
    st.ui = &ui;
    RICH4_LOGI("ai dialog (RE 0x41E345)");
    runModal(app, &aiDialogEventHandler, &st);
}

} // namespace rich4
