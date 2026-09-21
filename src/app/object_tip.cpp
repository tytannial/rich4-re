#include "game/app/object_tip.h"

#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <string>

namespace rich4 {

namespace {

// 结构 +4 的 BIG5 名字（最多 maxLen 字节）→ UTF-8
std::string nameAt(const uint8_t* p, int maxLen) {
    char buf[24] = {};
    int n = 0;
    while (n < maxLen && n < 23 && p[n]) {
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
            return nameAt(state.cellEnts[id].pad4, 20);
        }
        return {};
    }
    if (id < 4000) { // 地产：地主 + 建筑名 + 等级名 + 费用（原版 0x417559）
        const int i = id - 2000;
        if (i <= 0 || i >= static_cast<int>(state.estates.size())) {
            return {};
        }
        const Estate& es = state.estates[i];
        // [RE 0x417559] 行位固定：第 1 行 = 地主（为空则留空行），建筑名恒在第 2 行
        std::string s = ownerName(state, es.owner);
        s += "\n";
        s += kBuildingNames[es.type < 11 ? es.type : 0];
        s += "\n";
        s += kBuildingNames[11 + (es.level < 6 ? es.level : 0)];
        // 费用 = estate +32 + 2*level（u16，原版 sub_452793 显示）
        const int lv = es.level < 6 ? es.level : 0;
        const uint16_t fee = es.fees[lv];
        s += "\n$";
        s += std::to_string(fee);
        return s;
    }
    if (id < 6000) { // 公司：名称（+4）+ 地主 + 公司类型名 + 等级名
        const int i = id - 4000;
        if (i <= 0 || i >= static_cast<int>(state.corps.size())) {
            return {};
        }
        const Corp& cp = state.corps[i];
        // [RE 0x417559] 行位固定：第 1 行 = 地主（为空则留空行）
        std::string s = ownerName(state, cp.owner);
        s += "\n";
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
        s += nameAt(sp.pad4, 20);
        return s;
    }
    if (id < 10000) { // 事件格名称
        const int i = id - 8000;
        if (i <= 0 || i >= static_cast<int>(state.evtCells.size())) {
            return {};
        }
        return nameAt(state.evtCells[i].pad4, 20);
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
// 依据: 0x417559 反编译; sub_40A9D7 拾取（重写用渲染记录的 mapHitRegions 代替）;
//       命中用拾取形状的 SPR 像素掩码（原版拾取缓冲语义），无形状时用矩形;
//       文本分支 cellEnt 名/estate 地主+建筑名+等级+费用/corp/specPt/evtCell/
//       cellTable 类型名/玩家名; 框 = data.mkf[517] 帧 0..3（四方向）
void showObjectTip(Application& app, int x, int y) {
    GameState& state = app.gameState();
    for (auto it = state.mapHitRegions.rbegin(); it != state.mapHitRegions.rend(); ++it) {
        if (it->shape) {
            // [RE 0x409B18] 像素级命中：形状 SPR 非零像素处才算命中（避免矩形误盖道路格）
            const UiFrameView& sf = it->shape->frame(it->shapeFrame);
            const int lx = x - (it->anchorX - sf.offsetX);
            const int ly = y - (it->anchorY - sf.offsetY);
            if (lx < 0 || ly < 0 || lx >= sf.width || ly >= sf.height) {
                continue;
            }
            const uint8_t* px = it->shape->frameBytes(it->shapeFrame);
            if (!px || px[static_cast<size_t>(ly) * sf.width + lx] == 0) {
                continue;
            }
        } else if (x < it->x || x >= it->x + it->w || y < it->y || y >= it->y + it->h) {
            continue;
        }
        const std::string text = buildTipText(state, it->id);
        if (text.empty()) {
            continue;
        }
        state.objectTipText = text;
        state.objectTipX = x;
        state.objectTipY = y;
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
    // [RE 0x417559] 方向 v5（0..3）：y-40 < 框高 → +1；x > 440-框宽 → +2；
    //   框画在点击点（帧图形自带方向偏移），文本 = 点击点 + (a2, a1+27)
    const UiFrameView& base = ui.frame(0);
    const int vy = state.objectTipY - 40;
    int v5 = 0;
    if (vy < static_cast<int>(base.height)) {
        v5 += 1;
    }
    if (state.objectTipX > 440 - static_cast<int>(base.width)) {
        v5 += 2;
    }
    Surface& dst = app.surface();
    blitElement(dst, ui.frame(v5 < ui.frameCount() ? v5 : 0), state.objectTipX, state.objectTipY,
                false);
    const int a2 = (v5 & 2) ? -(base.width / 2 + 9) : (base.width / 2 + 9);
    const int a1 = (v5 & 1) ? -96 : 42;
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    // [RE 0x417559] 逐行绘制（原版多次 drawText，行距 18）：
    //   单行（cellEnt/specPt 名）首行 = v9+27；多行（estate 3-4 行）自 v9 起
    const std::string& text = state.objectTipText;
    int lineCount = 1;
    for (char ch : text) {
        if (ch == '\n') {
            ++lineCount;
        }
    }
    const int tx = state.objectTipX + a2;
    int ty = state.objectTipY + a1 + (lineCount > 1 ? 0 : 27);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        const std::string line =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) {
            app.text().drawText(dst, line.c_str(), tx, ty, 2);
        }
        ty += 18;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

} // namespace rich4
