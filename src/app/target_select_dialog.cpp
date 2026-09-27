#include "game/app/target_select_dialog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/object_tip.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/cursor.h"
#include "game/render/surface.h"

namespace rich4 {

// 拾取 id 规范化：重写 mapHitRegions 编码 → 原版目标编码（下游 getObjectPosition/
//   传送机/飞弹/卡片效果/AI 预选均按原版语义解析）
//   - 玩家 0xF000|j（map_render）→ 0x8000 | (1<<j)（原版玩家位掩码）
//   - 物件 0xA100|(槽+1)（map_render）→ 0x8000 | ((槽+1)<<8)（原版 BYTE1=槽+1）
//   定义见头文件（AI 预选目标也须转换，见 ai_card.cpp tgtDemolish）
int normalizeHitId(uint16_t v) {
    if ((v & 0xF000) == 0xF000) {
        const int j = v & 0x0F;
        return (j >= 0 && j < 9) ? (0x8000 | (1 << j)) : 0;
    }
    if ((v & 0xFF00) == 0xA100) {
        const int slot = v & 0xFF;
        return (slot >= 1 && slot <= 46) ? (0x8000 | (slot << 8)) : 0;
    }
    return v;
}

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

// [RE 0x445E4D BYTE1 switch case 1..8] 高字节过滤：BYTE1 非 0 时**取代**低 flags 判定
//   （原版先 `v14 = 0` 再 switch，仅 case 内 goto LABEL_140 才可选中）
//   依据: 0x445E4D case 1..8 反编译；重写拾取编码：地块 = 归一化对象 id（2000+/4000+）；
//   挂身物件 = 0xA100|槽+1；玩家 = 0xF000|玩家号（原版为位掩码 0x8000|(1<<j)，单玩家等价）
bool byte1FilterPass(const GameState& st, uint16_t v, uint8_t filter) {
    const int cur = st.currentPlayer;
    auto estate = [&](int i) -> const Estate* {
        return (i > 0 && i < static_cast<int>(st.estates.size())) ? &st.estates[i] : nullptr;
    };
    auto corp = [&](int i) -> const Corp* {
        return (i > 0 && i < static_cast<int>(st.corps.size())) ? &st.corps[i] : nullptr;
    };
    const bool isPlayer = (v & 0xF000) == 0xF000;
    const int pi = v & 0x0F;
    switch (filter) {
        case 1:  // 自己的住宅 / 自己的有设施商业 [0x446330 case1]
            if (v > 2000 && v < 4000) {
                const Estate* e = estate(v - 2000);
                return e && e->owner == cur + 1;
            }
            if (v > 4000 && v < 6000) {
                const Corp* c = corp(v - 4000);
                return c && c->owner == cur + 1 && c->sub != 0;
            }
            return false;
        case 2: {  // 换地/换屋：与所站格同类型（住宅/商业）且非同一格 [0x446330 case2]
            if (cur < 0 || cur >= 4) {
                return false;
            }
            const Player& pl = st.players[cur];
            const uint16_t here =
                (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size())
                    ? st.cellEnts[pl.cellEntId].special
                    : 0;
            if (v > 2000 && v < 4000) {
                return here > 2000 && here < 4000 && here != v;
            }
            if (v > 4000 && v < 6000) {
                return here > 4000 && here < 6000 && here != v;
            }
            return false;
        }
        case 4:  // 玩家目标且不是自己（均貧/搶奪/查稅/同盟）[0x446330 case4]
            return isPlayer && pi != cur;
        case 5:  // 他人的有建筑住宅/商业（怪獸 0x0506）[0x446330 case5]
            if (v > 2000 && v < 4000) {
                const Estate* e = estate(v - 2000);
                return e && e->owner != cur + 1 && e->level != 0;
            }
            if (v > 4000 && v < 6000) {
                const Corp* c = corp(v - 4000);
                return c && c->owner != cur + 1 && c->sub != 0;
            }
            return false;
        case 6: {  // case5 + 路面道具（路障16/地雷17/炸彈18）——拆除卡 0x0626 [0x446330 case6]
            if ((v & 0xFF00) == 0xA100) {
                const int slot = v & 0xFF;
                if (slot >= 1 && slot <= 46) {
                    const uint8_t type = st.cellTable[24 * (slot - 1)];
                    return type == 16 || type == 17 || type == 18;
                }
                return false;
            }
            if (v > 2000 && v < 4000) {
                const Estate* e = estate(v - 2000);
                return e && e->owner != cur + 1 && e->level != 0;
            }
            if (v > 4000 && v < 6000) {
                const Corp* c = corp(v - 4000);
                return c && c->owner != cur + 1 && c->sub != 0;
            }
            return false;
        }
        case 7:  // 玩家目标且不是自己（冬眠/陷害 0x0710）[0x446330 case7]
            return isPlayer && pi != cur;
        case 8:  // 无主无建筑地块（传送机房屋目的地 0x0802/0x0804）[0x446330 case8]
            if (v > 2000 && v < 4000) {
                const Estate* e = estate(v - 2000);
                return e && e->owner == 0 && e->level == 0;
            }
            if (v > 4000 && v < 6000) {
                const Corp* c = corp(v - 4000);
                return c && c->owner == 0 && c->sub == 0;
            }
            return false;
        default:
            return true;  // 无 BYTE1 过滤
    }
}

// [USER 魔改] 无主空地判定（傳送機人物/神明/物品目的地；[HELP 94]「空道路/空地」）
//   依据: 住宅 Estate owner==0&&level==0 / 商業 Corp owner==0&&sub==0（与 teleportSourceValid 对称）
bool isEmptyLandObject(const GameState& st, uint16_t v) {
    if (v > 2000 && v < 4000) {
        const int i = v - 2000;
        return i > 0 && i < static_cast<int>(st.estates.size()) && st.estates[i].owner == 0 &&
               st.estates[i].level == 0;
    }
    if (v > 4000 && v < 6000) {
        const int i = v - 4000;
        return i > 0 && i < static_cast<int>(st.corps.size()) && st.corps[i].owner == 0 &&
               st.corps[i].sub == 0;
    }
    return false;
}

// [RE 0x4751B0/0x4751B4] 滚动方向向量表（16.16 定点，8 方向：上/左上/左/左下/下/右下/右/右上）
constexpr int32_t kScrollVecX[8] = {0, -46341, -65536, -46341, 0, 46341, 65536, 46341};
constexpr int32_t kScrollVecY[8] = {-65536, -46341, 0, 46341, 65536, 46341, 0, -46341};
// [RE off_475E0D] 边缘方向光标（dword_48C568: 1上 2左 3下 4右）
constexpr int kEdgeCursor[5] = {5, 34, 40, 38, 36};
// [RE 0x48C56C/0x48C564] 滚动速度（初 8，每 tick +4，上限 64）与 50ms 节流
constexpr int kScrollSpeedInit = 8;
constexpr int kScrollSpeedMax = 64;
constexpr uint64_t kScrollIntervalMs = 50;

struct TargetSelCtx {
    Application* app = nullptr;
    uint16_t flags = 0;
    uint8_t filter = 0;  // [RE 0x445E4D BYTE1] 高字节 case 1..8 过滤（非 0 时取代 flags 判定）
    int cursorType = 0;
    int selected = 0;   // dword_48C584
    int curCursor = -1; // dword_48C580
    // [RE 0x445E4D flags&0x80] 地图滚动模式（飞弹/核弹选屏外目标）
    bool scrollMode = false;
    int edgeDir = 0;      // 0=视口内 1上 2左 3下 4右（dword_48C568）
    int scrollSpeed = kScrollSpeedInit;
    uint64_t lastScrollMs = 0;
    // 进入前视口状态（退出恢复）
    bool savedManualView = false;
    int savedViewX = 0;
    int savedViewY = 0;
    // [USER 魔改] 允许无主空地（傳送機人物/神明/物品目的地）
    bool allowEmptyLand = false;
    // [RE 0x445E4D WM_USER+1] 进入时 PostMessage(WM_MOUSEMOVE) 初始判定（重写等价：
    //   首次绘制后以当前鼠标位置拾取一次）
    bool hoverInit = false;
    // [DIAG 2026-09-27 三道具放置] 拾取结果变化时才打印（避免鼠标移动刷屏）
    int dbgLastV = -1;
    bool dbgLastAllowed = false;
};

// 拾取 id 规范化函数定义见文件顶部（导出供 AI 预选使用）
void setCursor(TargetSelCtx& c, int type, int frames, int delay) {
    if (c.curCursor != type) {
        c.curCursor = type;
        c.app->cursor().select(type, frames, delay);
    }
}

// [RE 0x445E4D LABEL_53..LABEL_140] 悬停判定：拾取 → 筛选 → 记录选中 + 光标
// （模态进入初始判定与 WM_MOUSEMOVE 共用；原版 WM_USER+1 用 PostMessage(0x200) 触发一次）
void updateTargetHover(TargetSelCtx& c, int mx, int my) {
    GameState& st = c.app->gameState();
    const uint16_t v = pickMapObject(st, mx, my);  // [RE 0x40A9D7]
    // [RE 0x445E4D] BYTE1 非 0 → 高字节过滤**取代**低 flags 判定（原版 v14 被清 0）
    bool allowed = (c.filter != 0) ? byte1FilterPass(st, v, c.filter)
                                   : targetAllowed(st, v, c.flags);
    if (!allowed && c.filter == 0 && c.allowEmptyLand && v < 6000) {
        allowed = isEmptyLandObject(st, v);  // [USER] 无主空地（傳送機人物/神明/物品目的地）
    }
    // [DIAG 2026-09-27 三道具放置排查] 拾取结果变化时打印（含来源格占用位）
    if (static_cast<int>(v) != c.dbgLastV || allowed != c.dbgLastAllowed) {
        c.dbgLastV = static_cast<int>(v);
        c.dbgLastAllowed = allowed;
        if (v > 0 && v < static_cast<int>(st.cellEnts.size())) {
            const CellEnt& ce = st.cellEnts[v];
            RICH4_LOGI("targetSel: mouse(%d,%d) pick=%u allowed=%d cell(special=%u sprite=%u "
                       "occ=0x%08X) flags=0x%X filter=%u (RE 0x445E4D)",
                       mx, my, v, allowed ? 1 : 0, ce.special, ce.sprite, ce.occMask, c.flags,
                       c.filter);
        } else {
            RICH4_LOGI("targetSel: mouse(%d,%d) pick=%u allowed=%d flags=0x%X filter=%u "
                       "(RE 0x445E4D)",
                       mx, my, v, allowed ? 1 : 0, c.flags, c.filter);
        }
    }
    if (allowed) {
        // [RE 0x445E4D LABEL_140] 命中：记选中（规范化为原版编码）+ 光标换类型
        c.selected = normalizeHitId(v);
        setCursor(c, c.cursorType, 1, 10);
    } else {
        c.selected = 0;
        setCursor(c, 5, 1, 0);  // 默认指针
    }
}

void drawTargetSel(TargetSelCtx& c) {
    renderGameFrame(*c.app);
    // [RE 0x445E4D WM_USER+1] setPauseDraw(1) + rebuildPickBuffer(1) + PostMessage(0x200)：
    //   进入后以**当前鼠标位置**做一次初始判定（重写等价：首次绘制后按新命中区拾取；
    //   此前缺失会导致"不移动鼠标直接点"永远 selected=0）
    if (!c.hoverInit) {
        c.hoverInit = true;
        int mx = 0;
        int my = 0;
        c.app->mouseLogicalPos(mx, my);
        updateTargetHover(c, mx, my);
    }
}

bool targetSelHandler(const SDL_Event* event, void* user) {
    auto& c = *static_cast<TargetSelCtx*>(user);
    GameState& st = c.app->gameState();
    if (!event) {
        drawTargetSel(c);
        return true;
    }
    // [RE 0x445E4D WM_TIMER 0x113] 滚动：50ms 节流，speed +4（上限 64），
    //   v7 = (mapRotation + 2*edgeDir - 2) & 7，视口中心按 16.16 向量位移，clamp [220,2084]
    if (event->type == kModalTimerEvent) {
        if (c.scrollMode && c.edgeDir != 0) {
            const uint64_t now = SDL_GetTicks();
            if (now - c.lastScrollMs >= kScrollIntervalMs) {
                c.lastScrollMs = now;
                c.scrollSpeed = std::min(kScrollSpeedMax, c.scrollSpeed + 4);
                const int v7 = (st.mapRotation + 2 * c.edgeDir - 2) & 7;
                int vx = st.viewSmoothX + ((kScrollVecX[v7] * c.scrollSpeed) >> 16);
                int vy = st.viewSmoothY + ((kScrollVecY[v7] * c.scrollSpeed) >> 16);
                vx = std::clamp(vx, 220, 2084);
                vy = std::clamp(vy, 220, 2084);
                st.manualView = true;
                st.viewSmoothX = vx;
                st.viewSmoothY = vy;
                st.viewScrolling = false;
                renderGameFrame(*c.app);
            }
        }
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const int mx = static_cast<int>(event->motion.x);
            const int my = static_cast<int>(event->motion.y);
            if (c.scrollMode) {
                // [RE 0x445E4D 0x80 分支] 边缘判定（窗口坐标）：x==0 左 / x>=440 右 /
                //   y<=40 上 / y==479 下；回到视口内 → 停滚并立即判定
                int dir = 0;
                if (mx <= 0) {
                    dir = 2;
                } else if (mx >= 440) {
                    dir = 4;
                } else if (my <= 40) {
                    dir = 1;
                } else if (my >= 479) {
                    dir = 3;
                }
                if (dir != 0) {
                    if (c.edgeDir != dir) {
                        c.edgeDir = dir;
                        c.scrollSpeed = kScrollSpeedInit;
                        c.lastScrollMs = SDL_GetTicks() - kScrollIntervalMs;  // 立即滚一次
                    }
                    setCursor(c, kEdgeCursor[dir], 1, 0);  // 方向箭头光标
                    return true;
                }
                c.edgeDir = 0;  // [RE KillTimer] 鼠标回视口内 → 停止滚动
            }
            updateTargetHover(c, mx, my);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (c.scrollMode && c.edgeDir != 0) {
                    // [RE 0x445E4D] 滚动计时器激活中不可确认（提示音）
                    c.app->audio().playEffect(3);  // [RE g_uiSoundMusicTip]
                    return true;
                }
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
//   LBUTTONUP 确认（仅未处于延迟输入状态）；RBUTTON/MBUTTON 取消；光标默认 5、确认 41；
//   0x80 地图滚动模式（飞弹/核弹）：鼠标出视口 → 方向箭头光标 + SetTimer(50ms) 逐帧滚动，
//   回视口内 KillTimer 并立即判定；滚动中不可确认（提示音）
int selectTargetDialog(Application& app, int mode, bool allowEmptyLand) {
    GameState& st = app.gameState();
    TargetSelCtx c;
    c.app = &app;
    c.allowEmptyLand = allowEmptyLand;
    c.flags = static_cast<uint16_t>(mode & 0xFFFF);
    c.filter = static_cast<uint8_t>((mode >> 8) & 0xFF);  // [RE 0x445E4D BYTE1]
    if ((c.flags & 0x40) != 0) {
        // [RE 0x445E4D] 0x40 → 全可选 + 滚动模式（0x80 | 0x37）
        c.flags = static_cast<uint16_t>(0x80 | 0x37);
    }
    c.cursorType = (mode >> 16) & 0xFF;
    c.scrollMode = (c.flags & 0x80) != 0;
    // 保存进入前视口状态（退出恢复；滚动/非滚动统一）
    c.savedManualView = st.manualView;
    c.savedViewX = st.viewSmoothX;
    c.savedViewY = st.viewSmoothY;
    if (c.scrollMode) {
        // [RE 0x445E4D] 滚动模式初始滚动中心 = 当前玩家位置（dword_48C570/574 = 玩家坐标）
        const Player& pl = st.players[st.currentPlayer];
        st.manualView = true;
        st.viewSmoothX = pl.spriteX;
        st.viewSmoothY = pl.spriteY;
        st.viewScrolling = false;
    }
    app.cursor().select(5, 1, 0);
    const int r = runModal(app, &targetSelHandler, &c, 16);
    st.manualView = c.savedManualView;
    st.viewSmoothX = c.savedViewX;
    st.viewSmoothY = c.savedViewY;
    st.viewScrolling = false;
    app.cursor().select(41, 1, 0);
    // [DIAG 2026-09-27 三道具放置排查] 返回结果（0=取消）
    RICH4_LOGI("selectTargetDialog: mode=0x%X -> %d (RE 0x445E4D)", mode, r);
    return r;
}

} // namespace rich4
