#include "game/app/map_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/map_render.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

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
    // [RE 0x40A4E1] 大地图底图 = 工作副本（miniMapRaw 帧 1 + 住宅用地/商業用地/行業設施點玩家色块，
    //   11392 缩放、标记帧 26/28/28）；原版由 sub_40A9BD 打开前 rebuildMiniMap(1) 生成，
    //   重写按需重建（面板绘制时已重建，此处兜底）
    if (state.bigMapBuffer.empty()) {
        buildMiniMapMarks(app);
    }
    if (!state.bigMapBuffer.empty() && state.miniMapRaw.frameCount() > 1) {
        const UiFrameView& bf = state.miniMapRaw.frame(1);
        const int w = bf.width;
        const int h = bf.height;
        if (w > 0 && h > 0 && state.bigMapBuffer.size() == static_cast<size_t>(w) * h) {
            // [NEW M4-A2] 大地图工作副本按画布 scale 缩放绘制（scale=1 逐像素等价）
            blitScaled(dst, reinterpret_cast<const uint8_t*>(state.bigMapBuffer.data()), w * 2,
                       nullptr, 20, 60, 0, 0, w, h, false, true);
        }
    } else if (state.miniMapRaw.frameCount() > 1) {
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
    // [NEW M4-D 实机] 大地图是"叠加在游戏画面上"（0x40A801 不清屏；底图 @(20,60)）——
    //   fillBars=false：宽画布下两侧保留游戏画面，不填黑（填黑会裁掉两侧游戏画面）
    runModal(app, &mapDialogEventHandler, &st, 0, true, false);
}

} // namespace rich4
