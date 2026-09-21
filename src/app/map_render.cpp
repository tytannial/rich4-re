#include "game/app/map_render.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

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

// [RE 0x475224/0x475228] 骰子面板位置表（8 方向，交错 int32 对）
constexpr int kDicePosX[8] = {4, 12, 8, -4, -12, -24, -20, -12};
constexpr int kDicePosY[8] = {12, 12, 6, -6, -12, -12, -6, 6};

// [RE 0x48B2B4] unk_48B2B4 越界地块空白纹理（32x32 8bit）
const uint8_t kBlankTile[1024] = {};

// [RE 0x40829D] 物件精灵绘制项（对应原版 dword_48A44C/48A84C/word_48A850/854/856 列表）
// 原版按 dword_48A44C[m] 低 16 位（16*y）排序（sub_457E6C + sub_4079F9）
struct SpriteDraw {
    const UiImage* spr = nullptr;
    int frame = 0;
    int x = 0;
    int y = 0;
    int colorIdx = -1; // 调色板索引（-1 无）
    uint16_t id = 0;   // 物件 ID（10000+=玩家 / 2000+=estate / 4000+=corp / 6000+=specPt / 8000+=evtCell）
};

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
        // [RE 0x40829D] 跳伞期间玩家未落地（spriteX/Y=0）：用出生 cellEnt 坐标
        if (centerX == 0 && centerY == 0 && p.cellEntId > 0 &&
            p.cellEntId < state.cellEnts.size()) {
            centerX = state.cellEnts[p.cellEntId].x;
            centerY = state.cellEnts[p.cellEntId].y;
        }
    }
    if (state.currentPlayer < 4 && centerX == 0 && centerY == 0 &&
        state.pendingSpawnPlayer == 0) {
        const int ce = pickSpawnCellEnt(state);
        Player& p = state.players[state.currentPlayer];
        p.cellEntId = static_cast<uint16_t>(ce);
        p.prevCellEnt = 0;
        state.cellEnts[ce].occMask |= (256u << state.currentPlayer);
        // [RE 0x475114] 标记待入场（跳伞动画由 sub_418C55 播放，落地后设 spriteX/Y）
        state.pendingSpawnPlayer = state.currentPlayer + 1;
        centerX = state.cellEnts[ce].x;
        centerY = state.cellEnts[ce].y;
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
        const bool inBounds = gx >= 0 && gx < kGridSize && gy >= 0 && gy < kGridSize;
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
            continue; // 视口外跳过（原版 sub_4557A1 内部裁剪）
        }
        // [RE 0x40829D] 越界地块用空白纹理（unk_48B2B4）填充，避免上一帧残留
        const uint8_t* tile = kBlankTile;
        if (inBounds) {
            const uint16_t tileIndex = state.gndCellIndex[kGridSize * gy + gx];
            tile = state.gndBitmaps + (static_cast<size_t>(tileIndex) << 10);
        }
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

    // 物件精灵收集（对应 sub_40829D 的 dword_48A44C 列表，按 y 排序后绘制）
    std::vector<SpriteDraw> items;
    items.reserve(64);
    auto projectItem = [&](int px, int py, int& sx, int& sy) -> bool {
        const int cr = (py >> 5) - cellY + kViewHalf;
        const int cc = (px >> 5) - cellX + kViewHalf;
        if (cr < 0 || cr > 28 || cc < 0 || cc > 28) {
            return false;
        }
        int ox = 0;
        int oy = 0;
        isoProject(state, px, py, ox, oy);
        const int idx = 3364 * dir + 116 * cr + 4 * cc;
        sx = baseX + isoAt(idx + 2) - ox;
        sy = baseY + isoAt(idx) - oy;
        return true;
    };
    auto addItem = [&](const UiImage* spr, int frame, int px, int py, int colorIdx, uint16_t id) {
        if (!spr || spr->frameCount() <= 0) {
            return;
        }
        int sx = 0;
        int sy = 0;
        if (!projectItem(px, py, sx, sy)) {
            return;
        }
        items.push_back({spr, frame, sx, sy, colorIdx, id});
    };

    // [RE 0x40829D] 玩家棋子：资源 = walkRes[j][2*state + group]，
    //   帧 = byte_498EA3 + ((8 - dword_499088 + byte_496B78) & 7) * 每方向帧数
    for (int j = 0; j < state.playerCount && j < 9; ++j) {
        const Player& p = state.players[j];
        if (p.spriteX == 0 && p.spriteY == 0) {
            continue;
        }
        const int slot = 2 * state.playerActionState[j] + state.playerMoveGroup[j];
        const UiImage* res = nullptr;
        if (slot >= 0 && slot < 13 && state.walkRes[j][slot].frameCount() > 0) {
            res = &state.walkRes[j][slot];
        } else if (state.pieceAnim[j].frameCount() > 0) {
            res = &state.pieceAnim[j];
        }
        if (!res) {
            continue;
        }
        const int dir = (8 - state.mapRotation + p.dir) & 7;
        const int perDir = res->frameCount() / 8;
        int frame = 0;
        if (perDir > 0) {
            frame = state.playerMoveFrame[j] + dir * perDir;
        }
        if (frame >= res->frameCount()) {
            frame = res->frameCount() - 1;
        }
        addItem(res, frame, p.spriteX, p.spriteY, -1,
                static_cast<uint16_t>(0x8000u | (1u << j)));
    }

    // 特殊点（图块 = dword_48AE4C[sprite]，颜色 = 拥有者）
    for (size_t i = 1; i < state.specPts.size(); ++i) {
        const SpecPt& sp = state.specPts[i];
        if (sp.sprite == 0 || sp.sprite >= 5) {
            continue;
        }
        addItem(&state.specPointTiles[sp.sprite], 0, sp.x, sp.y, sp.owner ? sp.owner - 1 : -1,
                static_cast<uint16_t>(6000 + i));
    }

    // 事件格（图块 = dword_48AE4C[sprite]）
    for (size_t i = 1; i < state.evtCells.size(); ++i) {
        const EvtCell& ec = state.evtCells[i];
        if (ec.sprite == 0 || ec.sprite >= 5) {
            continue;
        }
        addItem(&state.specPointTiles[ec.sprite], 0, ec.x, ec.y, -1,
                static_cast<uint16_t>(8000 + i));
    }

    // 地产（无主时不绘制；颜色 = 拥有者）
    for (size_t i = 1; i < state.estates.size(); ++i) {
        const Estate& es = state.estates[i];
        if (es.owner == 0) {
            continue;
        }
        const int frame = (8 - (state.mapRotation + es.dir)) & 7;
        addItem(&state.estateFlag, frame, es.x, es.y, es.owner - 1,
                static_cast<uint16_t>(2000 + i));
    }

    // 公司（无主时不绘制；图块 = dword_48AE64[sub]，颜色 = 拥有者）
    for (size_t i = 1; i < state.corps.size(); ++i) {
        const Corp& cp = state.corps[i];
        if (cp.owner == 0 || cp.sub >= 17) {
            continue;
        }
        const int frame = (8 - (state.mapRotation + cp.dir)) & 7;
        addItem(&state.corpTiles[cp.sub], frame, cp.x, cp.y, cp.owner - 1,
                static_cast<uint16_t>(4000 + i));
    }

    // 按 y 排序（原版 sub_457E6C + sub_4079F9 按 16*y）
    std::sort(items.begin(), items.end(),
              [](const SpriteDraw& a, const SpriteDraw& b) { return a.y < b.y; });

    for (const SpriteDraw& it : items) {
        if (it.spr->isSprite()) {
            blitSpriteFrameClipped(dst, *it.spr, it.frame, it.x, it.y, true);
        } else {
            blitElement(dst, it.spr->frame(it.frame), it.x, it.y, true);
        }
    }

    setClipRect(0, 0, 640, 480);

    // [RE 0x40829D] 移动计步器（剩余步数 dword_48BAF8；定义见 renderMap 头）
    // 说明: sprintf("%d", dword_48BAF8) → dword_48BAD8 帧 (ch-40)，y=400
    if (state.remainingSteps > 0) {
        char text[8];
        std::snprintf(text, sizeof(text), "%d", state.remainingSteps);
        const int len = static_cast<int>(std::strlen(text));
        int x = 245 - 50 * len / 2;
        for (int i = 0; i < len; ++i) {
            const int frame = text[i] - 40; // '0'=48 → 帧 8
            if (frame >= 0 && frame < state.estateTiles.frameCount()) {
                blitElement(dst, state.estateTiles.frame(frame), x, 400, false);
            }
            x += 50;
        }
    }

    // [RE 0x419572] 骰子：状态2 播放滚动 FLC（panel[骰子数+3]，色键绿透明），
    //   滚动结束后的停留期显示最终点数精灵（panel.mkf[3] 帧 = 点数 + 6*j - 1）
    //   位置 = 136/48 + dword_475224/228[方向]
    {
        const int p = state.currentPlayer;
        if (p >= 0 && p < 9) {
            const int n = std::max(1, std::min(3, static_cast<int>(state.players[p].diceCount)));
            const int v6 = (state.players[p].dir + 8 - state.mapRotation) & 7;
            const int dx = 136 + kDicePosX[v6];
            const int dy = 48 + kDicePosY[v6];
            if (state.diceAnimActive && state.diceAnim.valid()) {
                const int w = state.diceAnim.width();
                const int h = state.diceAnim.height();
                const uint16_t* src = state.diceAnim.pixels();
                uint16_t* d = dst.pixels();
                for (int row = 0; row < h; ++row) {
                    const int py = dy + row;
                    if (py < 0 || py >= Surface::kHeight) {
                        continue;
                    }
                    for (int col = 0; col < w; ++col) {
                        const int px = dx + col;
                        if (px < 0 || px >= Surface::kWidth) {
                            continue;
                        }
                        const uint16_t c = src[static_cast<size_t>(row) * w + col];
                        const int cr = (c >> 10) & 0x1F;
                        const int cg = (c >> 5) & 0x1F;
                        const int cb = c & 0x1F;
                        if (cg >= 20 && cr <= 10 && cb <= 10) {
                            continue; // 色键：亮绿透明
                        }
                        d[static_cast<size_t>(py) * Surface::kWidth + px] = c;
                    }
                }
            } else if (state.showDice) {
                for (int j = 0; j < n; ++j) {
                    const int frame = state.diceValues[j] + 6 * j - 1;
                    if (frame >= 0 && frame < state.panelFrame3.frameCount()) {
                        blitSpriteFrameClipped(dst, state.panelFrame3, frame, dx + 85, dy + 145,
                                               false);
                    }
                }
            }
        }
    }
}

// [RE 0x456384] 彩色掩码绘制（把 src 非零像素画成 color 到目标缓冲）
void blitColorMask(std::vector<uint16_t>& dst, int dstW, int dstH, const UiFrameView& src, int x,
                   int y, uint16_t color) {
    const int dx = x - src.offsetX;
    const int dy = y - src.offsetY;
    for (int row = 0; row < src.height; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= dstH) {
            continue;
        }
        for (int col = 0; col < src.width; ++col) {
            const int px = dx + col;
            if (px < 0 || px >= dstW || src.pixels[row * src.width + col] == 0) {
                continue;
            }
            dst[py * dstW + px] = color;
        }
    }
}

// [RE 0x40A4E1] sub_40A4E1(0)：小地图原图拷贝 + 地产/公司/特殊点玩家颜色标记
// 依据: 0x40A4E1 反编译; blitBackground 拷贝 dword_48BAD0 → dword_48BADC,
//       再对 estates/corps/specPts 用 sub_456384 画 g_playerColor 标记
//       （5696/65536 缩放，帧 = (dir&1) + 22/24/24；specPts 小地图用 +24，见 a1=0 分支）
void buildMiniMapMarks(Application& app) {
    GameState& state = app.gameState();
    constexpr int kSize = 200;
    if (state.miniMapRaw.frameCount() <= 0) {
        return;
    }
    const UiFrameView& src = state.miniMapRaw.frame(0);
    state.miniMapBuffer.assign(static_cast<size_t>(kSize) * kSize, 0);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (x < src.width && y < src.height) {
                state.miniMapBuffer[y * kSize + x] = src.pixels[y * src.width + x];
            }
        }
    }
    auto mark = [&](int frame, int px, int py, uint16_t color) {
        if (frame < 0 || frame >= state.estateTiles.frameCount()) {
            return;
        }
        blitColorMask(state.miniMapBuffer, kSize, kSize, state.estateTiles.frame(frame),
                      (5696 * px) >> 16, (5696 * py) >> 16, color);
    };
    for (size_t i = 1; i < state.estates.size(); ++i) {
        const Estate& es = state.estates[i];
        if (es.owner == 0) {
            continue;
        }
        mark((es.dir & 1) + 22, es.x, es.y, state.players[es.owner - 1].color);
    }
    for (size_t i = 1; i < state.corps.size(); ++i) {
        const Corp& cp = state.corps[i];
        if (cp.owner == 0) {
            continue;
        }
        mark((cp.dir & 1) + 24, cp.x, cp.y, state.players[cp.owner - 1].color);
    }
    for (size_t i = 1; i < state.specPts.size(); ++i) {
        const SpecPt& sp = state.specPts[i];
        if (sp.owner == 0) {
            continue;
        }
        mark((sp.dir & 1) + 24, sp.x, sp.y, state.players[sp.owner - 1].color);
    }
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
    // [RE 0x40A4E1] 小地图背景 + 标记叠加
    buildMiniMapMarks(app);
}

} // namespace rich4
