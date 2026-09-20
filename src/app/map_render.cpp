#include "game/app/map_render.h"

#include <algorithm>
#include <cstdlib>

#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/iso_blit.h"
#include "game/render/surface.h"

namespace rich4 {

namespace {

constexpr int kGridSize = 72;      // [RE docs/formats/map.md] GND 72x72
constexpr int kViewCenterX = 220;  // [RE 0x40829D] v130 += 220
constexpr int kViewCenterY = 260;  // [RE 0x40829D] v125 += 260
constexpr int kViewHalf = 14;      // [RE 0x40829D] (row+14)/(col+14)
constexpr int kOrderCount = 296;   // [RE 0x473610] 每方向有效遍历项

// [RE 0x407A2C] 像素坐标 -> 等距屏幕偏移
// 依据: outX = (xo*coeff[0] + yo*coeff[2])>>5; outY = (xo*coeff[1] + yo*coeff[3])>>5
void isoProject(const GameState& state, int x, int y, int& outX, int& outY) {
    const int dir = state.mapRotation & 7;
    const int xo = x & 0x1F;
    const int yo = y & 0x1F;
    const int8_t* c = kIsoCoeff[dir];
    outX = ((yo * c[2]) >> 5) + ((xo * c[0]) >> 5);
    outY = ((yo * c[3]) >> 5) + ((xo * c[1]) >> 5);
}

// [RE 0x40AA0F] 随机选一个未被占用且有连接道路的 cellEnt
int pickSpawnCellEnt(const GameState& state) {
    uint8_t cand[256];
    int n = 0;
    const int count = static_cast<int>(state.cellEnts.size()) - 1;
    for (int i = 1; i <= count && n < 256; ++i) {
        const CellEnt& ce = state.cellEnts[i];
        if ((ce.occMask & 0x80FFFF00u) == 0 &&
            (ce.exits[0] || ce.exits[1] || ce.exits[2] || ce.exits[3])) {
            cand[n++] = static_cast<uint8_t>(i);
        }
    }
    if (n == 0) {
        return 1;
    }
    return cand[std::rand() % n];
}

} // namespace

void renderMap(Application& app, int centerX, int centerY) {
    // [RE 0x40829D] 依据: 视口中心 = 当前玩家像素坐标; 首次时随机出生点并占用
    GameState& state = app.gameState();
    Surface& dst = app.surface();

    if (centerX == -1) {
        // 非手动视角：以当前玩家位置为中心（原版 sub_415E70 非 dword_48BE18 分支）
        const Player& p = state.players[state.currentPlayer];
        centerX = p.spriteX;
        centerY = p.spriteY;
    }
    if (state.currentPlayer < 4 && centerX == 0 && centerY == 0) {
        const int ce = pickSpawnCellEnt(state);
        Player& p = state.players[state.currentPlayer];
        p.cellEntId = static_cast<uint16_t>(ce);
        p.spriteX = static_cast<uint16_t>(state.cellEnts[ce].x);
        p.spriteY = static_cast<uint16_t>(state.cellEnts[ce].y);
        state.cellEnts[ce].occMask |= (256u << state.currentPlayer);
        centerX = p.spriteX;
        centerY = p.spriteY;
        RICH4_LOGI("spawn: player %d cellEnt %d at (%d,%d) (RE 0x40829D)", state.currentPlayer, ce,
                   centerX, centerY);
    }
    state.viewX = centerX;
    state.viewY = centerY;

    const int cellX = centerX >> 5;
    const int cellY = centerY >> 5;
    const int dir = state.mapRotation & 7;

    int projX = 0;
    int projY = 0;
    isoProject(state, centerX, centerY, projX, projY);
    const int baseX = projX + kViewCenterX;
    const int baseY = projY + kViewCenterY;

    const uint8_t* isoBytes = reinterpret_cast<const uint8_t*>(&kIsoProjection[0][0][0][0]);
    // 原版索引为字节偏移（116 = 29 格 x 4 字节），取 int16 视图访问 CCF0/CCF2/CCF4/CCF6
    auto isoAt = [isoBytes](int byteOffset) {
        return *reinterpret_cast<const int16_t*>(isoBytes + byteOffset);
    };

    // 裁剪到地图区 (0,40)-(440,480)（原版 dword_4861B8..C4）
    setClipRect(0, 40, 440, 480);

    // 地面等距地块（按绘制遍历序）
    for (int i = 0; i < kOrderCount; ++i) {
        const int rowOff = kIsoDrawOrder[dir][i][0];
        const int colOff = kIsoDrawOrder[dir][i][1];
        const int gx = colOff + cellX;
        const int gy = rowOff + cellY;
        if (gx < 0 || gx >= kGridSize || gy < 0 || gy >= kGridSize) {
            continue;
        }
        const int idx = 3364 * dir + 116 * (rowOff + kViewHalf) + 4 * (colOff + kViewHalf);
        const int idx2 = idx + 116;
        int16_t quad[4][2];
        quad[0][0] = static_cast<int16_t>(baseX + isoAt(idx + 2));
        quad[0][1] = static_cast<int16_t>(baseY + isoAt(idx));
        quad[1][0] = static_cast<int16_t>(baseX + isoAt(idx + 6));
        quad[1][1] = static_cast<int16_t>(baseY + isoAt(idx + 4));
        quad[2][0] = static_cast<int16_t>(baseX + isoAt(idx2 + 6));
        quad[2][1] = static_cast<int16_t>(baseY + isoAt(idx2 + 4));
        quad[3][0] = static_cast<int16_t>(baseX + isoAt(idx2 + 2));
        quad[3][1] = static_cast<int16_t>(baseY + isoAt(idx2));
        int yMin = quad[0][1];
        int yMax = quad[0][1];
        for (int k = 1; k < 4; ++k) {
            yMin = std::min(yMin, static_cast<int>(quad[k][1]));
            yMax = std::max(yMax, static_cast<int>(quad[k][1]));
        }
        if (yMax < 40 || yMin > 479) {
            continue;
        }
        const uint16_t tileIndex = state.gndCellIndex[kGridSize * gy + gx];
        const uint8_t* tile = state.gndBitmaps + (static_cast<size_t>(tileIndex) << 10);
        isoBlitQuad(dst, tile, quad, state.gndPalette, true);
    }

    // cellEnts 物件（图块 = map.mkf[24] 帧 sprite-1）
    for (size_t i = 1; i < state.cellEnts.size(); ++i) {
        const CellEnt& ce = state.cellEnts[i];
        if (ce.sprite == 0) {
            continue;
        }
        const int cr = (ce.y >> 5) - cellY + kViewHalf;
        const int cc = (ce.x >> 5) - cellX + kViewHalf;
        if (cr < 0 || cr > 28 || cc < 0 || cc > 28) {
            continue;
        }
        int px = 0;
        int py = 0;
        isoProject(state, ce.x, ce.y, px, py);
        const int idx = 3364 * dir + 116 * cr + 4 * cc;
        const int sx = baseX + isoAt(idx + 2) - px;
        const int sy = baseY + isoAt(idx) - py;
        const int frame = ce.sprite - 1;
        if (frame < 0 || frame >= state.cellEntTiles.frameCount()) {
            continue;
        }
        blitElement(dst, state.cellEntTiles.frame(frame), sx, sy, true);
    }

    // 玩家棋子（阶段 1：静态帧 0；动画/朝向阶段 2 接入）
    for (int j = 0; j < state.playerCount && j < 9; ++j) {
        const Player& p = state.players[j];
        if (p.spriteX == 0 && p.spriteY == 0) {
            continue;
        }
        const int cr = (p.spriteY >> 5) - cellY + kViewHalf;
        const int cc = (p.spriteX >> 5) - cellX + kViewHalf;
        if (cr < 0 || cr > 28 || cc < 0 || cc > 28) {
            continue;
        }
        int px = 0;
        int py = 0;
        isoProject(state, p.spriteX, p.spriteY, px, py);
        const int idx = 3364 * dir + 116 * cr + 4 * cc;
        const int sx = baseX + isoAt(idx + 2) - px;
        const int sy = baseY + isoAt(idx) - py;
        blitSpriteFrameClipped(dst, state.pieceAnim[j], 0, sx, sy, true);
    }

    setClipRect(0, 0, 640, 480);
}

void initMapEntities(Application& app) {
    // [RE 0x407AD2] 依据: dword_498EB4[13*n] = data.mkf[21*charIndex + 128 + 3*travel]
    GameState& state = app.gameState();
    for (int n = 0; n < state.playerCount && n < 9; ++n) {
        const int charIndex = state.players[n].charIndex;
        const int travel = state.players[n].travel & 3;
        if (charIndex < 0 || charIndex >= 12) {
            continue;
        }
        const size_t index = static_cast<size_t>(21 * charIndex + 128 + 3 * travel);
        auto blob = state.data.read(index);
        if (blob) {
            state.pieceAnim[n].load(std::move(*blob));
        } else {
            RICH4_LOGW("initMapEntities: piece anim data.mkf[%zu] unavailable", index);
        }
    }
    // TODO(RE 0x40E033): 随机地图物件放置（空地/平房/店舖/商場/商業大樓）
    // TODO(RE 0x40A4E1): 小地图背景绘制（叠加地产/公司标记）
}

} // namespace rich4
