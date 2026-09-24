#include "game/app/target_select_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/object_tip.h"
#include "game/application.h"
#include "game/render/cursor.h"
#include "game/render/surface.h"

namespace rich4 {
namespace {

// [RE 0x445E4D] 命中筛选（flags 低 16 位；重写拾取 id 编码见 map_render/mapHitRegions）
bool targetAllowed(const GameState& st, uint16_t v, uint16_t flags) {
    if (v == 0) {
        return false;
    }
    if ((flags & 1) != 0 && v < 2000) {
        return true;  // 普通格/地块
    }
    if ((flags & 2) != 0 && v > 2000 && v < 4000) {
        return true;  // 住宅用地
    }
    if ((flags & 4) != 0 && v > 4000 && v < 6000) {
        return true;  // 商業用地
    }
    if ((flags & 0x10) != 0 && (v & 0xF000) == 0xF000) {
        // [RE 0x40D293] 玩家位掩码（低 4 位）；事件槽 4..7 恒可选
        const int pi = v & 0x0F;
        if (pi >= 0 && pi < 4 && st.players[pi].alive != 0) {
            return true;
        }
        if (pi >= 4 && pi < 8) {
            return true;
        }
    }
    if ((flags & 0x20) != 0 && (v & 0xFF00) == 0xA100) {
        return true;  // 挂身物件/神明等 cellTable 物件
    }
    return false;
}

struct TargetSelCtx {
    Application* app = nullptr;
    uint16_t flags = 0;
    int cursorType = 0;
    int selected = 0;   // dword_48C584
    int curCursor = -1; // dword_48C580
};

void drawTargetSel(TargetSelCtx& c) {
    renderGameFrame(*c.app);
}

void setCursor(TargetSelCtx& c, int type, int frames, int delay) {
    if (c.curCursor != type) {
        c.curCursor = type;
        c.app->cursor().select(type, frames, delay);
    }
}

bool targetSelHandler(const SDL_Event* event, void* user) {
    auto& c = *static_cast<TargetSelCtx*>(user);
    GameState& st = c.app->gameState();
    if (!event) {
        drawTargetSel(c);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const int mx = static_cast<int>(event->motion.x);
            const int my = static_cast<int>(event->motion.y);
            const uint16_t v = pickMapObject(st, mx, my);  // [RE 0x40A9D7]
            if (targetAllowed(st, v, c.flags)) {
                // [RE 0x445E4D LABEL_140] 命中：记选中 + 光标换类型（idx=1, 延时 10）
                c.selected = v;
                setCursor(c, c.cursorType, 1, 10);
            } else {
                c.selected = 0;
                setCursor(c, 5, 1, 0);  // 默认指针
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (c.selected != 0) {
                    c.app->audio().playEffect(2);  // [RE g_uiSoundConfirm]
                    c.app->cursor().select(41, 1, 0);
                    c.app->events().requestExit(c.selected);
                } else {
                    c.app->audio().playEffect(3);  // [RE g_uiSoundMusicTip] 无目标提示
                }
            } else if (event->button.button == SDL_BUTTON_RIGHT ||
                       event->button.button == SDL_BUTTON_MIDDLE) {
                c.app->audio().playEffect(4);  // [RE g_uiSoundCancel]
                c.app->cursor().select(41, 1, 0);
                c.app->events().requestExit(0);
            }
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (event->key.key == SDLK_ESCAPE) {
                c.app->cursor().select(41, 1, 0);
                c.app->events().requestExit(0);
            }
            return true;
        }
        default:
            return true;
    }
}

}  // namespace

// [RE 0x445E4D] selectTargetDialog（mode 见头文件）
// 依据: 0x445E4D 反编译；WM_USER+1 解析 mode → flags/光标类型；WM_MOUSEMOVE 拾取 + 光标切换；
//   LBUTTONUP 确认（仅未处于延迟输入状态）；RBUTTON/MBUTTON 取消；光标默认 5、确认 41
// 差异: 0x80 地图滚动模式（飞弹/核弹用；鼠标出视口方向滚动 + 方向箭头光标 34/40/38/36）暂未接入
int selectTargetDialog(Application& app, int mode) {
    TargetSelCtx c;
    c.app = &app;
    c.flags = static_cast<uint16_t>(mode & 0xFFFF);
    if ((c.flags & 0x40) != 0) {
        // [RE 0x445E4D] 0x40 → 全可选 + 滚动模式（0x80 | 0x37）
        c.flags = static_cast<uint16_t>(0x80 | 0x37);
    }
    c.cursorType = (mode >> 16) & 0xFF;
    app.cursor().select(5, 1, 0);
    const int r = runModal(app, &targetSelHandler, &c, 16);
    app.cursor().select(41, 1, 0);
    return r;
}

} // namespace rich4
