#include "game/app/map_render.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/map_tables.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/iso_blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"

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
    uint16_t id = 0;   // 物件 ID（0xF000+=玩家 / 2000+=estate / 4000+=corp / 6000+=specPt / 8000+=evtCell）
    const UiImage* hitSpr = nullptr; // [RE 0x409B18] 拾取形状（null = 用 spr）
    int hitFrame = 0;
    bool objLife = false;  // [RE 0x4098BD] cellTable 炸弹挂身 → 头顶画寿命数字
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
        state.cellEnts[ce].occMask |= (256u << state.currentPlayer);
        // [RE 0x40829D] 放置时即设来向(word_496B76=随机出口)与朝向(byte_496B78=面向格内)，
        //   跳伞/落地不改朝向（原版仅此处设一次）
        uint16_t ex[4];
        int ne = 0;
        for (int k = 0; k < 4; ++k) {
            if (state.cellEnts[ce].exits[k]) {
                ex[ne++] = state.cellEnts[ce].exits[k];
            }
        }
        if (ne > 0) {
            const uint16_t from = ex[std::rand() % ne];
            p.prevCellEnt = from;
            p.dir = static_cast<uint8_t>(facingBetween(state, from, ce));
        } else {
            p.prevCellEnt = 0;
        }
        // [RE 0x475114] 标记待入场（跳伞动画由 sub_418C55 播放，落地后设 spriteX/Y）
        state.pendingSpawnPlayer = state.currentPlayer + 1;
        centerX = state.cellEnts[ce].x;
        centerY = state.cellEnts[ce].y;
        RICH4_LOGI("spawn: player %d cellEnt %d at (%d,%d) prevEnt %d dir %d (RE 0x40829D)",
                   state.currentPlayer, ce, centerX, centerY, p.prevCellEnt, p.dir);
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
    // [RE 0x409B18] 所有 cellEnt（占用掩码高 16 位为 0）都可拾取——包括无图块的道路格；
    //   命中形状 = g_pickMask 帧 4（51x51）
    state.mapHitRegions.clear();
    for (size_t i = 1; i < state.cellEnts.size(); ++i) {
        const CellEnt& ce = state.cellEnts[i];
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
        if ((ce.occMask & 0xFFFF00u) == 0 && state.flagTiles.frameCount() > 4) {
            // [RE 0x409246] 格 → 逻辑对象归一化：cellEnt+32 指向地块/设施（2000..7999，住宅
            //   2000+i/商业 4000+i/设施 6000+i，商业/设施每项可占多格）时拾取 id = 对象 id。
            //   原版由 drawList 大菱形（g_pickMask 帧 2/3，143x103）以地块锚点覆盖整块地实现
            //   同一语义；重写用帧 4（51x51）+ 表项锚点盖不满 → 格 id 暴露会使 flags=1 的
            //   路障/地雷/炸弹可放在地产格上、传送机目的地选中后误报（2026-09-26 修）。
            //   医院/监狱（8001/8002）保持格 id = 原版行为（无 drawList 覆盖，可传/可走）。
            uint16_t pickId = static_cast<uint16_t>(i);
            if (ce.special >= 2000 && ce.special <= 7999) {
                pickId = ce.special;
            }
            const UiFrameView& pf = state.flagTiles.frame(4);
            state.mapHitRegions.push_back({sx - pf.offsetX, sy - pf.offsetY, pf.width, pf.height,
                                           pickId, &state.flagTiles, 4, sx, sy,
                                           static_cast<uint16_t>(i)});
        }
        if (ce.sprite == 0) {
            continue;
        }
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
    auto addItem = [&](const UiImage* spr, int frame, int px, int py, int colorIdx, uint16_t id,
                       const UiImage* hitSpr = nullptr, int hitFrame = 0) {
        if (!spr || spr->frameCount() <= 0) {
            return;
        }
        if (frame < 0 || frame >= spr->frameCount()) {
            frame = 0; // 帧数不足时回退首帧（防越界）
        }
        if (hitSpr && (hitFrame < 0 || hitFrame >= hitSpr->frameCount())) {
            hitFrame = 0;
        }
        int sx = 0;
        int sy = 0;
        if (!projectItem(px, py, sx, sy)) {
            return;
        }
        items.push_back({spr, frame, sx, sy, colorIdx, id, hitSpr, hitFrame});
    };

    // [RE 0x40829D] 玩家棋子：资源 = walkRes[j][2*state + group]，
    //   帧 = byte_498EA3 + ((8 - dword_499088 + byte_496B78) & 7) * 每方向帧数
    auto drawPiece = [&](int j) {
        const Player& p = state.players[j];
        if (p.spriteX == 0 && p.spriteY == 0) {
            return;
        }
        // [RE 0x4086A1] 状态效果中（住宿/出國/監獄/醫院/消失）不绘制棋子（"消失"），
        //   例外 = alive&0x20（走进旅館步行中）；走出旅館启动前 stateFlags 已清（0x40D6BE 链路），
        //   到达清 0x20。住院 524/入狱 538 切换帧重绘后角色由此守卫隐藏（2026-09-24 补全：
        //   原仅查 BYTE0 导致住院/入狱角色仍绘制）
        if (p.stateFlags != 0 && (p.alive & 0x20) == 0) {
            return;
        }
        const int slot = 2 * state.playerActionState[j] + state.playerMoveGroup[j];
        const UiImage* res = nullptr;
        if (slot >= 0 && slot < 13 && state.walkRes[j][slot].frameCount() > 0) {
            res = &state.walkRes[j][slot];
        } else if (j >= 4 && j < 8) {
            // 事件槽 NPC：目标 state×group 槽未载时回退同 group 的移动槽(slot2走/3渡水)再
            //   站立槽(slot0/1)；pieceAnim[4..7] 从不加载，不可作 NPC 回退 → 否则棋子消失闪
            const int mg = state.playerMoveGroup[j];
            const int fbMove = 2 + mg;
            if (state.walkRes[j][fbMove].frameCount() > 0) {
                res = &state.walkRes[j][fbMove];
            } else if (state.walkRes[j][mg].frameCount() > 0) {
                res = &state.walkRes[j][mg];
            }
        } else if (state.pieceAnim[j].frameCount() > 0) {
            res = &state.pieceAnim[j];
        }
        if (!res) {
            return;
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
        addItem(res, frame, p.spriteX, p.spriteY, -1, static_cast<uint16_t>(0xF000u | j));
    };
    for (int j = 0; j < state.playerCount && j < 9; ++j) {
        drawPiece(j);
    }
    // [RE 0x40829D] 事件槽 NPC（4..7）：busy==0（已保释上路/自由）才画；在押（1/2）经
    //   0x408B87 判据跳过；坐标 0 由 drawPiece 守卫跳过
    for (int j = 4; j < 8; ++j) {
        if (state.npcSlots[j - 4].busy == 0) {
            drawPiece(j);
        }
    }
    // [RE 0x40829D] 機器娃娃（虚拟槽 8）：槽记录 busy（0x498E72）==0（使用中）才画；
    //   结束 nextPlayer 置 3 → 不再绘制（与 currentPlayer==8 窗口等价，改 busy 判据更忠实）
    if (state.npcSlots[4].busy == 0) {
        drawPiece(8);
    }

    // 特殊点（图块 = dword_48AE4C[sprite] = map.mkf[sprite+38]；帧 = 8 方向；色 = 拥有者/黑）
    for (size_t i = 1; i < state.specPts.size(); ++i) {
        const SpecPt& sp = state.specPts[i];
        if (sp.sprite == 0 || sp.sprite >= state.specTiles.size()) {
            continue;
        }
        const int frame = (8 - (state.mapRotation + sp.dir)) & 7;
        // 同 estate：拾取形状改用帧 4（51x51）
        const int pickFrame = 4;
        addItem(&state.specTiles[sp.sprite], frame, sp.x, sp.y, sp.owner,
                static_cast<uint16_t>(6000 + i), &state.flagTiles, pickFrame);
    }

    // 事件格（图块 = dword_48AE4C[sprite] = map.mkf[sprite+38]；帧 = 8 方向；色 = 原图）
    for (size_t i = 1; i < state.evtCells.size(); ++i) {
        const EvtCell& ec = state.evtCells[i];
        if (ec.sprite == 0 || ec.sprite >= state.specTiles.size()) {
            continue;
        }
        const int frame = (8 - (state.mapRotation + ec.dir)) & 7;
        addItem(&state.specTiles[ec.sprite], frame, ec.x, ec.y, -1,
                static_cast<uint16_t>(8000 + i));
    }

    // [RE 0x40E033/0x40829D 0x408C5E..0x4090AC] cellTable 物件绘制（图块 = cellTypeSprites[类型]，
    //   ID = 0xA100|槽+1）。三分支：①拥有者挂身 → 跟随玩家（屏幕偏移 kPieceOffset/kPieceOffset18、
    //   帧 (+4)、type18 且玩家另有附身时头顶画寿命）；②flyCount(+6)≠0 → 弹飞（f32 坐标
    //   +速度逐帧推进、出视口清除）；③常规 → 逻辑格投影、帧 = (朝向+8−视角)&7
    for (int slot = 0; slot < 46; ++slot) {
        const size_t off = static_cast<size_t>(24 * slot);
        uint8_t* e = &state.cellTable[off];
        const int type = e[0];
        const int cellEntId = e[2] | (e[3] << 8);
        const int owner = e[5];
        uint8_t& fly = e[6];
        if (type <= 0 || type > 45) {
            continue;
        }
        UiImage& img = state.cellTypeSprites[type];
        // [RE 0x40B066] 原版拾取 id = (slot<<8)+33024（0x8160）；重写 hit 区段
        //   2000/4000/6000/8000 已按十进制段分配，直译 (slot+1)<<8 会与 estate/corp/specPt
        //   段重叠 → 用 0xA100|（槽+1）独立段（object_tip 同规则解码）
        const uint16_t objDrawId = static_cast<uint16_t>(0xA100 | (slot + 1));
        // ent(+2)==0 → 不绘制：原版按 ent 格位常规绘制（attachEnd 0x40E3BA 先清 ent 重绘 =
        // 神明从场景消失，随后飘走动画单独 blit）；重写跟随分支须同样以 ent 为门槛，
        // 否则送神动画期间挂身精灵仍跟随玩家（2026-09-24 修正）
        if (owner != 0 && cellEntId != 0 && owner - 1 < 9) {
            const Player& o = state.players[owner - 1];
            if (o.stateFlags != 0) {
                continue;  // [RE 0x408FBD] 挂起态（住院/出国/…）不绘制
            }
            int px = 0;
            int py = 0;
            if (!projectItem(o.spriteX, o.spriteY, px, py)) {
                continue;
            }
            const int dirv = (o.dir + 8 - state.mapRotation) & 7;
            // [RE 0x4098BD] 挂身炸弹（type18）头顶画寿命数字——判定 = 该玩家的挂身道具槽
            //   就是本物件（cellNo +64，非附身神明 cellTableIdx +63）
            const bool bombCarry = type == 18 && o.cellNo == static_cast<uint8_t>(slot + 1);
            const int32_t* oofs = bombCarry ? &kPieceOffset18[dirv][0] : &kPieceOffset[dirv][0];
            SpriteDraw sd;
            sd.spr = &img;
            sd.frame = (dirv + 4) & 7;  // [RE 0x4090A2] 附身帧翻转
            sd.x = px + static_cast<int>(oofs[0]);
            sd.y = py + static_cast<int>(oofs[1]);
            sd.id = objDrawId;
            sd.objLife = bombCarry;
            items.push_back(sd);
            continue;
        }
        if (fly != 0) {
            float wx = 0.0f;
            float wy = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            std::memcpy(&wx, e + 8, 4);
            std::memcpy(&wy, e + 12, 4);
            std::memcpy(&vx, e + 16, 4);
            std::memcpy(&vy, e + 20, 4);
            int px = 0;
            int py = 0;
            if (!projectItem(static_cast<int>(wx), static_cast<int>(wy), px, py)) {
                fly = 0;  // [RE 0x408D3B] 出视口 → 停止飞行绘制
                continue;
            }
            --fly;
            wx += vx;
            wy += vy;
            std::memcpy(e + 8, &wx, 4);
            std::memcpy(e + 12, &wy, 4);
            SpriteDraw sd;
            sd.spr = &img;
            sd.frame = (e[7] + 8 - state.mapRotation) & 7;
            sd.x = px;
            sd.y = py;
            items.push_back(sd);
            continue;
        }
        if (cellEntId <= 0 || cellEntId >= static_cast<int>(state.cellEnts.size())) {
            continue;
        }
        const CellEnt& ce = state.cellEnts[cellEntId];
        addItem(&img, (e[1] + 8 - state.mapRotation) & 7, ce.x, ce.y, -1, objDrawId);
    }

    // 住宅用地：有主绘制旗帜（玩家色）；无主空地不绘制（地形自带虚线框），仅记录命中
    for (size_t i = 1; i < state.estates.size(); ++i) {
        const Estate& es = state.estates[i];
        const int frame = (8 - (state.mapRotation + es.dir)) & 7;
        // [RE 0x409B18] 原版拾取/高亮形状：estate = g_pickMask 帧 (8-(rot+dir))&1（71x47/71x51）、
        //   corp = +2（帧 2/3，143x103 菱形）；实机拾取体感范围过大易误盖道路格 →
        //   命中矩形改用帧 4（51x51，约一个地块），高亮仍用原版帧（见 hl* 赋值）
        const int pickFrame = 4;
        if (es.owner == 0 && es.level == 0) {
            // [RE 0x40829D/0x409B18] 无主空地：tile=0 不绘制但**仍登记 drawList id=2000+n**，
            //   rebuildPickBuffer 照写 g_pickMask → 拾取与 highlightBlink 均有效
            //   （旧注释"无主不写缓冲/hlShape=null"为误读；命运 id1 徵收/山洪归公后
            //   目标变无主，原版高亮照常 → 曾因 hlShape 缺失导致重写无闪烁）
            int sx = 0;
            int sy = 0;
            if (state.flagTiles.frameCount() > pickFrame && projectItem(es.x, es.y, sx, sy)) {
                const UiFrameView& pf = state.flagTiles.frame(pickFrame);
                state.mapHitRegions.push_back(
                    {sx - pf.offsetX, sy - pf.offsetY, pf.width, pf.height,
                     static_cast<uint16_t>(2000 + i), &state.flagTiles, pickFrame, sx, sy});
                GameState::MapHitRegion& hit = state.mapHitRegions.back();
                hit.hlShape = &state.flagTiles;
                hit.hlFrame = (8 - (state.mapRotation + es.dir)) & 1;
                hit.hlAnchorX = sx;
                hit.hlAnchorY = sy;
            }
            continue;
        }
        if (es.level == 0) {
            // [RE 0x40829D] 空地有主：棋子图（dword_48AEA8=map.mkf[25]）帧 = 玩家角色 ID，
            //   调色板不改（原版 g_drawListPal=0xFF 走 0x40985C 跳过分支）
            const int ci = (es.owner >= 1 && es.owner <= 9)
                               ? state.players[es.owner - 1].charIndex
                               : 0;
            addItem(&state.pieceTiles, ci, es.x, es.y, -1, static_cast<uint16_t>(2000 + i),
                    &state.flagTiles, pickFrame);
        } else {
        // [RE 0x40829D] 有建筑：type==0 → dword_48AE48[level]；type!=0 → dword_48AE60
        //   （estateFlag），调色板替换 owner 色（**不检查 owner**：无主加盖 level>0 时
        //   调色板 = 0 → 黑轮廓建筑，0x4091E5 实证）。
            //   dword_48AE48[level]（level 1..5）= g_specTiles[level-1]（地址 0x48AE48+4*level
            //   即 0x48AE4C 起的 g_specTiles 数组）= 地图预加载图块 map.mkf[5*base+38+level]
            //   （重写 specPointTiles[level-1]）；此前误全部画 estateFlag（大地块旗帜外观）
            const UiImage* tile = &state.estateFlag;
            if (es.type == 0 && es.level >= 1 && es.level <= 5) {
                tile = &state.specPointTiles[es.level - 1];
            }
            addItem(tile, frame, es.x, es.y, es.owner, static_cast<uint16_t>(2000 + i),
                    &state.flagTiles, pickFrame);
        }
    }

    // 商業用地绘制（[RE 0x4093F3..0x409484] sub_40829D）：
    //   sub==0 有主 → 角色标记 pieceTiles（map.mkf[25]），帧 = 角色 ID，调色板不改（0xFF）；
    //   sub!=0 → 按 type 选图块（switch 0..4；dword_48AE78/90 实为 dword_48AE64 的偏移别名）：
    //     type0 公園 → corpTiles[0] / type1 旅館 → corpTiles[sub] /
    //     type2 購物中心 → corpTiles[5+sub] / type3 加油站 → corpTiles[11] /
    //     type4 研究所 → corpTiles[11+sub]；owner 调色板
    //   无主不绘制（仅记录命中）
    for (size_t i = 1; i < state.corps.size(); ++i) {
        const Corp& cp = state.corps[i];
        if (cp.sub >= 17) {
            continue;
        }
        const int frame = (8 - (state.mapRotation + cp.dir)) & 7;
        // 同 estate：拾取形状改用帧 4（51x51）
        const int pickFrame = 4;
        if (cp.owner == 0 && cp.sub == 0) {
            // [RE 0x40829D/0x409B18] 无主公司同上：不绘制但登记 id=4000+n，高亮形状 = 原版 corp 帧 2/3
            int sx = 0;
            int sy = 0;
            if (state.flagTiles.frameCount() > pickFrame && projectItem(cp.x, cp.y, sx, sy)) {
                const UiFrameView& pf = state.flagTiles.frame(pickFrame);
                state.mapHitRegions.push_back(
                    {sx - pf.offsetX, sy - pf.offsetY, pf.width, pf.height,
                     static_cast<uint16_t>(4000 + i), &state.flagTiles, pickFrame, sx, sy});
                GameState::MapHitRegion& hit = state.mapHitRegions.back();
                hit.hlShape = &state.flagTiles;
                hit.hlFrame = ((8 - (state.mapRotation + cp.dir)) & 1) + 2;
                hit.hlAnchorX = sx;
                hit.hlAnchorY = sy;
            }
            continue;
        }
        if (cp.sub == 0) {
            // [RE 0x409464] 买下未建设施：角色标记（帧 = 角色 ID），不调色板
            const int ci = (cp.owner >= 1 && cp.owner <= 9)
                               ? state.players[cp.owner - 1].charIndex
                               : 0;
            addItem(&state.pieceTiles, ci, cp.x, cp.y, -1, static_cast<uint16_t>(4000 + i),
                    &state.flagTiles, pickFrame);
            continue;
        }
        // [RE 0x409402] 有设施：按 type 分派图块组（子索引 = sub，别名偏移见上）；
        //   **不检查 owner**（无主但 sub>0 时调色板 = 0 → 黑轮廓设施，0x4093F9 实证）
        int tileIndex = 0;
        switch (cp.type) {
            case 0: tileIndex = 0; break;                        // 公園
            case 1: tileIndex = cp.sub; break;                   // 旅館
            case 2: tileIndex = 5 + cp.sub; break;               // 購物中心（dword_48AE78）
            case 3: tileIndex = 11; break;                       // 加油站（dword_48AE90[0]）
            case 4: tileIndex = 11 + cp.sub; break;              // 研究所（dword_48AE90[sub]）
            default: tileIndex = 0; break;
        }
        if (tileIndex >= 17) {
            tileIndex = 0;
        }
        addItem(&state.corpTiles[tileIndex], frame, cp.x, cp.y, cp.owner,
                static_cast<uint16_t>(4000 + i), &state.flagTiles, pickFrame);
    }

    // 按 y 排序（原版 sub_457E6C + sub_4079F9 按 16*y）
    std::sort(items.begin(), items.end(),
              [](const SpriteDraw& a, const SpriteDraw& b) { return a.y < b.y; });

    for (const SpriteDraw& it : items) {
        // [RE 0x409B18] 命中矩形用拾取形状（g_pickMask 帧），无则用绘制帧
        const UiImage* hs = it.hitSpr ? it.hitSpr : it.spr;
        const int hf = it.hitSpr ? it.hitFrame : it.frame;
        const UiFrameView& hfView = hs->frame(hf);
        state.mapHitRegions.push_back({it.x - hfView.offsetX, it.y - hfView.offsetY,
                                       hfView.width, hfView.height, it.id,
                                       it.hitSpr ? it.hitSpr : nullptr,
                                       it.hitSpr ? it.hitFrame : 0, it.x, it.y});
        // [RE 0x409B18/0x451985] 原版 pickBuffer 是**地图区局部坐标**：写入端
        //   writePickBuffer(..., g_drawListX, g_drawListY - 40)（屏幕→pickBuffer 的 -40 换算，
        //   因 pickBuffer (0,0) = 屏幕 (0,40)），回贴端 highlightBlink 把缓冲画到 backbuffer
        //   +40 行（+40 抵消）→ 最终屏幕落点 = (drawListX - offX, drawListY - offY)，与建筑绘制
        //   实参一致。重写直接画屏幕坐标（drawEstateHighlight 逐像素），**锚点须用 it.y**，
        //   不能再 -40（否则高亮整体上移 40px≈1 格，2026-09-26 实机截图定位）。
        {
            GameState::MapHitRegion& hit = state.mapHitRegions.back();
            if (it.id > 2000 && it.id < 4000 &&
                static_cast<size_t>(it.id - 2000) < state.estates.size()) {
                const Estate& es = state.estates[it.id - 2000];
                hit.hlShape = &state.flagTiles;
                hit.hlFrame = (8 - (state.mapRotation + es.dir)) & 1;
                hit.hlAnchorX = it.x;
                hit.hlAnchorY = it.y;
            } else if (it.id > 4000 && it.id < 6000 &&
                       static_cast<size_t>(it.id - 4000) < state.corps.size()) {
                const Corp& cp = state.corps[it.id - 4000];
                hit.hlShape = &state.flagTiles;
                hit.hlFrame = ((8 - (state.mapRotation + cp.dir)) & 1) + 2;
                hit.hlAnchorX = it.x;
                hit.hlAnchorY = it.y;
            }
        }
        if (it.spr->isSprite()) {
            // [RE 0x40986A] 调色板[255] = 拥有者颜色（0=黑轮廓、1..8=玩家色、-1=原图）
            if (it.colorIdx < 0) {
                it.spr->setPaletteEntry(255, it.spr->basePaletteEntry(255));
            } else if (it.colorIdx == 0) {
                it.spr->setPaletteEntry(255, 0);
            } else if (it.colorIdx <= state.playerCount) {
                it.spr->setPaletteEntry(255, rgb888To555(state.players[it.colorIdx - 1].color));
            }
            blitSpriteFrameClipped(dst, *it.spr, it.frame, it.x, it.y, true);
        } else {
            blitElement(dst, it.spr->frame(it.frame), it.x, it.y, true);
        }
        if (it.objLife) {
            // [RE 0x4098BD] 炸弹挂身：寿命数字（16 号字，y−60，样式 2）
            const int slotIdx = (it.id & 0xFF) - 1;  // id = 0xA100|(槽+1)
            char ltext[8];
            std::snprintf(ltext, sizeof(ltext), "%d",
                          state.cellTable[static_cast<size_t>(24 * slotIdx) + 4]);
            app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow, 0);
            app.text().drawText(dst, ltext, it.x, it.y - 60, 2);
        }
    }

    setClipRect(0, 0, 640, 480);

    // [RE 0x40829D] 移动计步器（剩余步数 dword_48BAF8；定义见 renderMap 头）
    // 说明: sprintf("%d", dword_48BAF8) → dword_48BAD8 帧 (ch-40)，y=400
    //   点数停留期（showDice，sub_419572 已 blit 骰子+sub_45285E(500) 停留）不显示计步器，
    //   停留结束转移动（showDice=false）后才出现行进数字
    if (state.remainingSteps > 0 && !state.showDice) {
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
            // [RE 0x419572 v2] 显示个数 = rollDice 本次的点数个数（遥控单骰 = 1）
            const int n =
                std::max(1, std::min(3, static_cast<int>(state.diceShowCount)));
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

// [PORT 0x40829D] 世界像素坐标 → 当前视口屏幕坐标（与 renderMap 内 projectItem 同源公式）
bool projectMapPoint(const GameState& state, int px, int py, int& sx, int& sy) {
    const int centerX = state.viewX;
    const int centerY = state.viewY;
    const int cellX = centerX >> 5;
    const int cellY = centerY >> 5;
    const int dir = state.mapRotation & 7;
    int projX = 0;
    int projY = 0;
    isoProject(state, centerX, centerY, projX, projY);
    const int baseX = projX + kViewCenterX;
    const int baseY = projY + kViewCenterY;
    const int cr = (py >> 5) - cellY + kViewHalf;
    const int cc = (px >> 5) - cellX + kViewHalf;
    if (cr < 0 || cr > 28 || cc < 0 || cc > 28) {
        return false;
    }
    const uint8_t* isoBytes = reinterpret_cast<const uint8_t*>(&kIsoProjection[0][0][0][0]);
    auto isoAt = [isoBytes](int byteOffset) {
        return *reinterpret_cast<const int16_t*>(isoBytes + byteOffset);
    };
    int ox = 0;
    int oy = 0;
    isoProject(state, px, py, ox, oy);
    const int idx = 3364 * dir + 116 * cr + 4 * cc;
    sx = baseX + isoAt(idx + 2) - ox;
    sy = baseY + isoAt(idx) - oy;
    return true;
}

// [RE 0x40A4E1] 单份工作副本重建（a1=0 小地图 / a1=1 大地图）
// 依据: 0x40A4E1 反编译; 底图 = g_miniMapRaw 帧 a1（尺寸即缓冲尺寸）→ g_miniMapWork 帧 a1;
//       标记 = sub_456384(目标, g_tipFrame 帧, (scale*x)>>16, (scale*y)>>16, g_playerColor[owner-1])
void rebuildMiniMapCopy(GameState& state, int a1, int scale, int esFrame, int cpFrame, int spFrame,
                        std::vector<uint16_t>& out) {
    const UiFrameView& src = state.miniMapRaw.frame(a1);
    const int w = src.width;
    const int h = src.height;
    if (w <= 0 || h <= 0) {
        out.clear();
        return;
    }
    out.assign(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out[static_cast<size_t>(y) * w + x] = src.pixels[y * src.width + x];
        }
    }
    auto mark = [&](int frame, int px, int py, uint16_t color) {
        if (frame < 0 || frame >= state.estateTiles.frameCount()) {
            return;
        }
        blitColorMask(out, w, h, state.estateTiles.frame(frame), (scale * px) >> 16,
                      (scale * py) >> 16, color);
    };
    for (size_t i = 1; i < state.estates.size(); ++i) {
        const Estate& es = state.estates[i];
        if (es.owner == 0) {
            continue;
        }
        mark((es.dir & 1) + esFrame, es.x, es.y, rgb888To555(state.players[es.owner - 1].color));
    }
    for (size_t i = 1; i < state.corps.size(); ++i) {
        const Corp& cp = state.corps[i];
        if (cp.owner == 0) {
            continue;
        }
        mark((cp.dir & 1) + cpFrame, cp.x, cp.y, rgb888To555(state.players[cp.owner - 1].color));
    }
    for (size_t i = 1; i < state.specPts.size(); ++i) {
        const SpecPt& sp = state.specPts[i];
        if (sp.owner == 0) {
            continue;
        }
        mark((sp.dir & 1) + spFrame, sp.x, sp.y, rgb888To555(state.players[sp.owner - 1].color));
    }
}

// [RE 0x40A4E1] sub_40A4E1(0)/sub_40A4E1(1)：小地图/大地图工作副本重建
// 依据: 0x40A4E1 反编译; a1=0 小地图（5696 缩放，标记帧 22/24/24）,
//       a1=1 大地图（11392 缩放，标记帧 26/28/28）
void buildMiniMapMarks(Application& app) {
    GameState& state = app.gameState();
    if (state.miniMapRaw.frameCount() <= 0) {
        return;
    }
    rebuildMiniMapCopy(state, 0, 5696, 22, 24, 24, state.miniMapBuffer);
    if (state.miniMapRaw.frameCount() > 1) {
        rebuildMiniMapCopy(state, 1, 11392, 26, 28, 28, state.bigMapBuffer);
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
    // [RE 0x40A4E1] 小地图背景 + 标记叠加
    buildMiniMapMarks(app);
}

// [RE 0x40E033] createMapObject（物件写入 cellTable 空槽）
// 依据: 0x40E033 反编译; 槽结构 +0 类型、+1 朝向、+2 cellEnt u16、+4 life、+5 owner+1；
//   **空槽判定 = cellEnt(+2) == 0**（原版各槽 +0 类型常驻：loadMapData 预置
//   kCellTypeInit[46]，deleteMapObject 只清 +2/+4/+5 不清类型）；
//   类型搜索段 15→14..15、16→16..25、17→26..35、18→36..45、其他→type-1 单槽；
//   cellEnt 占用位 |= (槽+1)<<16；a4≠0 且 type==15 → 创建即附身 a4-1（请神卡场景，P4 未接）
int createMapObject(GameState& state, int type, int cellEntId, uint8_t a3, uint8_t a4) {
    int lo = type - 1;
    int hi = type;
    if (type == 15) {
        lo = 14;
        hi = 16;
    } else if (type == 16) {
        lo = 16;
        hi = 26;
    } else if (type == 17) {
        lo = 26;
        hi = 36;
    } else if (type == 18) {
        lo = 36;
        hi = 46;
    }
    for (int slot = lo; slot < hi && slot < 46; ++slot) {
        uint8_t* e = &state.cellTable[24 * slot];
        if ((e[2] | e[3]) != 0) {
            continue;
        }
        e[0] = static_cast<uint8_t>(type);
        e[4] = a3;
        e[5] = a4;
        if (cellEntId > 0 && cellEntId < static_cast<int>(state.cellEnts.size())) {
            const CellEnt& ce = state.cellEnts[cellEntId];
            uint16_t exitId = 0;
            for (int i = 0; i < 4; ++i) {
                if (ce.exits[i]) {
                    exitId = ce.exits[i];
                    break;
                }
            }
            e[1] = static_cast<uint8_t>(facingBetween(state, exitId, cellEntId));
            e[2] = static_cast<uint8_t>(cellEntId & 0xFF);
            e[3] = static_cast<uint8_t>((cellEntId >> 8) & 0xFF);
            state.cellEnts[cellEntId].occMask |= static_cast<uint32_t>(slot + 1) << 16;
        }
        // [RE 0x40E0CB] 死神(15) 带拥有者登记 → 创建即附身（请神卡 P4，暂仅同步字段）
        if (a4 != 0 && type == 15) {
            Player& pl = state.players[a4 - 1];
            pl.cellTableIdx = static_cast<uint8_t>(slot + 1);
            e[4] = 13;
        }
        return slot + 1;
    }
    return hi;
}

// [RE 0x40AA6C] randomCellEnt（占用掩码空 + 有路；near>0 时距离 >300）
int randomCellEnt(const GameState& state, int nearCellEnt) {
    uint8_t cand[256];
    int n = 0;
    for (int i = 1; i < static_cast<int>(state.cellEnts.size()) && n < 256; ++i) {
        const CellEnt& ce = state.cellEnts[i];
        if ((ce.occMask & 0x80FFFF00u) == 0 && (ce.exits[0] || ce.exits[1])) {
            cand[n++] = static_cast<uint8_t>(i);
        }
    }
    if (n == 0) {
        return 0;
    }
    for (int guard = 0; guard < 200; ++guard) {
        const int pick = cand[std::rand() % n];
        if (nearCellEnt <= 0) {
            return pick;
        }
        const CellEnt& a = state.cellEnts[pick];
        const CellEnt& b = state.cellEnts[nearCellEnt];
        if (std::abs(a.x - b.x) >= 300 || std::abs(a.y - b.y) >= 300) {
            return pick;
        }
    }
    return cand[std::rand() % n];
}

// [RE 0x451985] captureHighlightShapes：从最后一次渲染的 mapHitRegions 提取高亮形状快照
//   （原版 pickBuffer 标记语义：标记后不再随地图数据变化重建）
void captureHighlightShapes(Application& app) {
    GameState& state = app.gameState();
    state.highlightShapes.clear();
    for (const auto& it : state.mapHitRegions) {
        if (it.hlShape == nullptr) {
            continue;
        }
        bool marked = false;
        if (it.id > 2000 && it.id < 4000) {
            marked = std::find(state.highlightEstates.begin(), state.highlightEstates.end(),
                               static_cast<int>(it.id) - 2000) != state.highlightEstates.end();
        } else if (it.id > 4000 && it.id < 6000) {
            marked = std::find(state.highlightCorps.begin(), state.highlightCorps.end(),
                               static_cast<int>(it.id) - 4000) != state.highlightCorps.end();
        }
        if (!marked) {
            continue;
        }
        const UiFrameView& sf = it.hlShape->frame(it.hlFrame);
        state.highlightShapes.push_back({it.hlShape, it.hlFrame, it.hlAnchorX - sf.offsetX,
                                         it.hlAnchorY - sf.offsetY});
    }
    RICH4_LOGI("highlight shapes: %zu (estates %zu, corps %zu, RE 0x451985)",
               state.highlightShapes.size(), state.highlightEstates.size(),
               state.highlightCorps.size());
}

// [RE 0x451985] drawEstateHighlight：高亮快照闪烁（整条街/连锁店/单块，纯渲染）
// 依据: 0x451985 每帧取 byte_476380[i] 亮度偏移（16 帧，30ms/帧）→ sub_4554FC 按
//       g_pickBuffer 的 0xFFFF 标记重绘高亮像素；循环后 sub_4528B9(400) 停留；
//       byte_476380 = {4,8,12,16,12,8,4,0,252,248,244,240,244,248,252,0}（252..240 = -4..-16）
// 迁移: 重写无 pickBuffer，用 highlightShapes 快照（captureHighlightShapes 提取自
//       mapHitRegions 的原版 g_pickMask 形状）对 RGB555 逐通道偏移（8bit 亮度量 → 5bit: >>2）；
//       帧推进/停留由阻塞播放 playHighlightBlink（turn_system.cpp）负责，本函数仅按
//       highlightFrame 渲染（0..15 有效；16=停留期无偏移）
void drawEstateHighlight(Application& app) {
    GameState& state = app.gameState();
    if (state.highlightFrame < 0 || state.highlightFrame > 15 ||
        state.highlightShapes.empty()) {
        return;
    }
    // [RE 0x476380] 高亮亮度偏移序列（16 帧；i8）
    static const int8_t kHighlightLut[16] = {4,  8,  12, 16, 12, 8,  4,  0,
                                             -4, -8, -12, -16, -12, -8, -4, 0};
    const int off5 = kHighlightLut[state.highlightFrame] >> 2; // 4..16 → 1..4（5bit 通道量）
    if (off5 == 0) {
        return;
    }
    uint16_t* px = app.surface().pixels();
    auto adjust = [px, off5](int x, int y) {
        if (x < 0 || y < 0 || x >= Surface::kWidth || y >= Surface::kHeight) {
            return;
        }
        const size_t o = static_cast<size_t>(y) * Surface::kWidth + x;
        const uint16_t c = px[o];
        const int r = std::clamp(((c >> 10) & 0x1F) + off5, 0, 31);
        const int g = std::clamp(((c >> 5) & 0x1F) + off5, 0, 31);
        const int b = std::clamp((c & 0x1F) + off5, 0, 31);
        px[o] = static_cast<uint16_t>((r << 10) | (g << 5) | b);
    };
    for (const auto& hs : state.highlightShapes) {
        const UiFrameView& sf = hs.shape->frame(hs.frame);
        const uint8_t* mask = hs.shape->frameBytes(hs.frame);
        if (!mask) {
            continue;
        }
        for (int ly = 0; ly < sf.height; ++ly) {
            for (int lx = 0; lx < sf.width; ++lx) {
                if (mask[static_cast<size_t>(ly) * sf.width + lx] != 0) {
                    adjust(hs.x + lx, hs.y + ly);
                }
            }
        }
    }
}

} // namespace rich4
