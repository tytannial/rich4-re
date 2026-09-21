#include "game/app/map_dialog.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"

namespace rich4 {

namespace {

struct MapDialogState {
    Application* app = nullptr;
};

// [RE 0x40A801] WM_PAINT：大地图叠加在游戏画面上（不清屏）
void redrawMap(MapDialogState& st) {
    Application& app = *st.app;
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    // 大地图帧 1（400x400）到 (20,60)
    if (state.miniMapRaw.frameCount() > 1) {
        blitElementOpaque(dst, state.miniMapRaw.frame(1), 20, 60);
    }
    // [RE 0x40A801] 玩家标记 = g_pieceSprites[13*i]+72 = 帧 5（72 = 12+12*5），
    //   11392/65536 缩放；注意与小地图标记（sub_416E6D 用 +84 = 帧 6）不是同一帧
    for (int i = 0; i < state.playerCount && i < 9; ++i) {
        const Player& p = state.players[i];
        if (p.spriteX == 0 && p.spriteY == 0) {
            continue;
        }
        const int x = ((11392 * static_cast<int>(p.spriteX)) >> 16) + 20;
        const int y = ((11392 * static_cast<int>(p.spriteY)) >> 16) + 60;
        if (state.pieceSprites[i].frameCount() > 5) {
            blitElement(dst, state.pieceSprites[i].frame(5), x, y, false);
        }
    }
}

// [RE 0x40A801] 事件：WM_RBUTTONUP → postModalExit(0)
bool mapDialogEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<MapDialogState*>(user);
    if (!event) {
        redrawMap(st);
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

void mapDialog(Application& app) {
    // [RE 0x40A9BD] 依据: sub_40A9BD → runModal(sub_40A801) + sub_415E70(1)
    RICH4_LOGI("map dialog (RE 0x40A9BD)");
    MapDialogState st;
    st.app = &app;
    runModal(app, &mapDialogEventHandler, &st);
}

} // namespace rich4
