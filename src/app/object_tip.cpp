#include "game/app/object_tip.h"

#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"

#include <string>

namespace rich4 {

namespace {

// 结构 +4 的 20 字节 BIG5 名字 → UTF-8
std::string nameAt(const uint8_t* p) {
    char buf[24] = {};
    int n = 0;
    while (n < 20 && p[n]) {
        buf[n] = static_cast<char>(p[n]);
        ++n;
    }
    return big5ToUtf8(buf, n);
}

std::string ownerName(const GameState& state, uint8_t owner) {
    if (owner >= 1 && owner <= 4 && state.players[owner - 1].name) {
        return state.players[owner - 1].name;
    }
    return {};
}

// [RE 0x417559] 按物件 ID 生成提示文本（与原版分支一致）
std::string buildTipText(const GameState& state, uint16_t id) {
    if (id == 0) {
        return {};
    }
    if ((id & 0xF000) == 0xF000) { // 玩家棋子（0xF000|玩家号）
        return ownerName(state, static_cast<uint8_t>((id & 0xF) + 1));
    }
    if (id < 2000) { // cellEnt 名称（+4）
        if (id < state.cellEnts.size()) {
            return nameAt(state.cellEnts[id].pad4);
        }
        return {};
    }
    if (id < 4000) { // 地产：地主 + 建筑名 + 等级名
        const int i = id - 2000;
        if (i <= 0 || i >= static_cast<int>(state.estates.size())) {
            return {};
        }
        const Estate& es = state.estates[i];
        std::string s = ownerName(state, es.owner);
        if (!s.empty()) {
            s += "\n";
        }
        s += kBuildingNames[es.type < 11 ? es.type : 0];
        s += " ";
        s += kBuildingNames[11 + (es.level < 6 ? es.level : 0)];
        return s;
    }
    if (id < 6000) { // 公司：地主 + 公司名 + 等级名
        const int i = id - 4000;
        if (i <= 0 || i >= static_cast<int>(state.corps.size())) {
            return {};
        }
        const Corp& cp = state.corps[i];
        std::string s = ownerName(state, cp.owner);
        if (!s.empty()) {
            s += "\n";
        }
        s += kBuildingNames[6 + (cp.type < 5 ? cp.type : 0)];
        s += " ";
        s += kBuildingNames[11 + (cp.sub < 6 ? cp.sub : 0)];
        return s;
    }
    if (id < 8000) { // 特殊点：地主 + 名称
        const int i = id - 6000;
        if (i <= 0 || i >= static_cast<int>(state.specPts.size())) {
            return {};
        }
        const SpecPt& sp = state.specPts[i];
        std::string s = ownerName(state, sp.owner);
        if (!s.empty()) {
            s += "\n";
        }
        s += nameAt(sp.pad4);
        return s;
    }
    if (id < 10000) { // 事件格名称
        const int i = id - 8000;
        if (i <= 0 || i >= static_cast<int>(state.evtCells.size())) {
            return {};
        }
        return nameAt(state.evtCells[i].pad4);
    }
    if ((id & 0xFF) == 0) { // cellTable 物件（槽号<<8）→ 神明/商店名
        const int slot = (id >> 8) & 0x7F;
        if (slot >= 1 && slot <= 46) {
            const int type = state.cellTable[24 * (slot - 1)];
            if (type >= 0 && type < 19) {
                return kObjectNames[type];
            }
        }
    }
    return {};
}

} // namespace

// [RE 0x417559] showObjectTip：命中测试（取最上层 = 最后绘制）+ 文本生成
void showObjectTip(Application& app, int x, int y) {
    GameState& state = app.gameState();
    for (auto it = state.mapHitRegions.rbegin(); it != state.mapHitRegions.rend(); ++it) {
        if (x < it->x || x >= it->x + it->w || y < it->y || y >= it->y + it->h) {
            continue;
        }
        const std::string text = buildTipText(state, it->id);
        if (text.empty()) {
            continue;
        }
        state.objectTipText = text;
        state.objectTipX = it->x + it->w / 2;
        state.objectTipY = it->y;
        app.audio().playEffect(1); // [RE 0x4175CB] g_uiSoundClick
        return;
    }
    state.objectTipText.clear();
}

// [RE 0x417559] drawObjectTip：框 = data.mkf[517] 帧 0，文本居中于框内
void drawObjectTip(Application& app) {
    GameState& state = app.gameState();
    if (state.objectTipText.empty()) {
        return;
    }
    const UiImage& ui = state.estateTiles; // [RE 0x48BAD8] data.mkf[517]
    if (ui.frameCount() <= 0) {
        return;
    }
    // [RE 0x417559] 方向 v5（0..3）：y < 框高 → +1；x > 440-框宽 → +2
    const UiFrameView& base = ui.frame(0);
    int v5 = 0;
    if (state.objectTipY < static_cast<int>(base.height)) {
        v5 += 1;
    }
    if (state.objectTipX > 440 - static_cast<int>(base.width)) {
        v5 += 2;
    }
    const UiFrameView& f = ui.frame(v5 < ui.frameCount() ? v5 : 0);
    Surface& dst = app.surface();
    int x = state.objectTipX;
    int y = state.objectTipY;
    if (v5 & 2) {
        x -= f.width;
    }
    if (v5 & 1) {
        y -= f.height;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 40) {
        y = 40;
    }
    if (x + f.width > 440) {
        x = 440 - f.width;
    }
    if (y + f.height > 480) {
        y = 480 - f.height;
    }
    blitElement(dst, f, x, y, false);
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, state.objectTipText.c_str(), x + f.width / 2, y + 27, 2);
}

} // namespace rich4
