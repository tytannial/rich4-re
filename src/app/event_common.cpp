// 事件格公共辅助（新聞 0x44B6DF / 命運 0x44DB81 共用）。
// 依据: 原 news_dialog.cpp 匿名命名空间实现（2026-09-26 提取，行为不变）；
//   各函数原版地址见 event_common.h 注释与 44b6df-news-events.md / 44db81-fate-events.md。

#include "game/app/event_common.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/app/game_loop.h"
#include "game/app/map_objects.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/core/clock.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

void blitOpaqueRgb(Surface& dst, const uint16_t* src, int w, int h, int x, int y) {
    UiFrameView view{};
    view.width = static_cast<uint16_t>(w);
    view.height = static_cast<uint16_t>(h);
    view.pixels = src;
    blitElementOpaque(dst, view, x, y);
}

void drawEventText(Application& app, const char* text, int x, int y, int size) {
    app.text().setFont(size, 0xF0F0F0, 0x101010, 3, 0); // [RE setTextFont]
    app.text().drawText(app.surface(), text, x, y, 0);
}

void drawPieceFrame(Application& app, int player, int frame, int x, int y) {
    const GameState& st = app.gameState();
    if (player < 0 || player >= 9) {
        return;
    }
    if (st.pieceSprites[player].frameCount() <= frame) {
        return;
    }
    blitSpriteFrame(app.surface(), st.pieceSprites[player], frame, x, y);
}

void focusView(Application& app, int x, int y) {
    GameState& st = app.gameState();
    st.manualView = true;
    st.viewSmoothX = x;
    st.viewSmoothY = y;
    st.viewScrolling = false;
    renderGameFrame(app);
    app.renderFrame();
}

void resetView(Application& app) {
    GameState& st = app.gameState();
    st.manualView = false;
    renderGameFrame(app);
    app.renderFrame();
}

void eventAudioWait(Application& app, int ms) {
    for (int t = 0; t < ms; t += 5) {
        app.audio().update();
        delayMs(5);
    }
}

void forceHotelCheckout(GameState& st) {
    for (int i = 0; i < st.playerCount; ++i) {
        if (st.players[i].alive != 0 && (st.players[i].stateFlags & 0xFFu) != 0) {
            st.players[i].stateFlags = (st.players[i].stateFlags & 0xFFFFFF00u) | 0x80u;
        }
    }
}

void blinkHighlight(Application& app) {
    GameState& st = app.gameState();
    if (st.highlightEstates.empty() && st.highlightCorps.empty()) {
        return;
    }
    RICH4_LOGI("event highlight: estates=%zu corps=%zu (RE 0x451985)", st.highlightEstates.size(),
               st.highlightCorps.size());
    st.highlightFrame = 0;
    playHighlightBlink(app);
}

void blinkSingleObj(Application& app, int objId) {
    GameState& st = app.gameState();
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    if (objId > 2000 && objId < 4000) {
        st.highlightEstates.push_back(objId - 2000);
    } else if (objId > 4000 && objId < 6000) {
        st.highlightCorps.push_back(objId - 4000);
    }
    RICH4_LOGI("event highlight single: objId=%d (RE 0x451985)", objId);
    blinkHighlight(app);
}

void focusObjId(Application& app, int objId, int& outX, int& outY) {
    getObjectPosition(app.gameState(), objId, outX, outY);
    focusView(app, outX, outY);
}

std::vector<int> collectBuiltObjIds(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].level != 0) {
            out.push_back(static_cast<int>(i) + 2000);
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].sub != 0) {
            out.push_back(static_cast<int>(i) + 4000);
        }
    }
    return out;
}

std::vector<int> collectAllObjIds(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        out.push_back(static_cast<int>(i) + 2000);
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        out.push_back(static_cast<int>(i) + 4000);
    }
    return out;
}

std::vector<int> collectBuiltEstateIndices(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].level != 0) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

int pickRandom(const std::vector<int>& list) {
    return list.empty() ? 0 : list[static_cast<size_t>(dbg::roll(dbg::SlotAny, static_cast<int>(list.size())))];
}

std::string estateNameUtf8(const Estate& es) {
    return big5ToUtf8(es.name, sizeof(es.name));
}

std::string corpNameUtf8(const Corp& cp) {
    return big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
}

std::string objNameUtf8(const GameState& st, int objId) {
    if (objId > 2000 && objId < 4000 && objId - 2000 < static_cast<int>(st.estates.size())) {
        return estateNameUtf8(st.estates[objId - 2000]);
    }
    if (objId > 4000 && objId < 6000 && objId - 4000 < static_cast<int>(st.corps.size())) {
        return corpNameUtf8(st.corps[objId - 4000]);
    }
    return {};
}

int objOwner(const GameState& st, int objId) {
    if (objId > 2000 && objId < 4000 && objId - 2000 < static_cast<int>(st.estates.size())) {
        return st.estates[objId - 2000].owner;
    }
    if (objId > 4000 && objId < 6000 && objId - 4000 < static_cast<int>(st.corps.size())) {
        return st.corps[objId - 4000].owner;
    }
    return 0;
}

std::string playerNameNoSpace(const GameState& st, int p) {
    std::string out;
    if (p < 0 || p >= 9 || st.players[p].name == nullptr) {
        return out;
    }
    for (const char* q = st.players[p].name; *q != '\0'; ++q) {
        if (*q != ' ') {
            out += *q;
        }
    }
    return out;
}

} // namespace rich4
