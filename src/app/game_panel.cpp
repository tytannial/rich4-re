#include "game/app/game_loop.h"   // blockingPerf [M4-B C1]
#include "game/app/game_panel.h"
#include "game/app/ui_layout.h"

#include <cstdio>
#include <cstring>

#include "game/app/date_util.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"   // playerNameNoSpace [RE 0x452946]
#include "game/app/item_lines.h"   // playValueLine [RE 0x44F230]
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/turn_system.h"   // playEventFlc [RE 0x45144F]
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/raw_bitmap.h"
#include "game/render/text.h"

namespace rich4 {

namespace {

constexpr uint32_t kTextDark = 0x101010; // 原版 1052688
constexpr uint32_t kTextRed = 0xFF0000;  // 原版 16711680
constexpr int kMiniMapW = 200;           // 小地图尺寸
constexpr int kMiniMapH = 200;

// [RE 0x452793] 千分位金额格式化（sub_452793）
void formatMoney(char* out, int32_t value) {
    char digits[24];
    std::snprintf(digits, sizeof(digits), "%d", value);
    const int len = static_cast<int>(std::strlen(digits));
    int n = 0;
    for (int i = len; i > 0; --i) {
        if (i % 3 == 0 && n > 0) {
            out[n++] = ',';
        }
        out[n++] = digits[len - i];
    }
    out[n] = '\0';
}

// [RE 0x4520A6] 日历计算
int daysInMonth(int year, int month) {
    static const int days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4) == 0) {
        return 29;
    }
    return days[month];
}

// [RE 0x451F8C] 自 1998-01-01 起的累计天数 → 月首星期（0=周日）
int firstWeekday(int year, int month) {
    int total = 0;
    for (int y = 1998; y < year; ++y) {
        total += (y % 4) ? 365 : 366;
    }
    for (int m = 1; m < month; ++m) {
        total += daysInMonth(year, m);
    }
    return (total + 4) % 7;
}

// [RE 0x415D31] 顶部工具条（panel.mkf[1]）
// 依据: 帧 0 背景 blit 到 (0,0); 11 个按钮帧 1+i 画在 (40*i+20, 20);
//       高亮帧 12+i（dword_48BDE4 为高亮索引）
//       （原版 (u16*)dword_475118 + 6*i + 12 → 帧 i+1；+78 → 帧 i+12）
void drawTopBar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    if (state.panelTopBar.frameCount() <= 0) {
        return;
    }
    blitElementOpaque(dst, state.panelTopBar.frame(0), 0, 0);
    for (int i = 0; i < 11; ++i) {
        // 普通帧 1+i，高亮帧 12+i
        const int frame = (i == state.topBarHover) ? (12 + i) : (1 + i);
        if (frame < state.panelTopBar.frameCount()) {
            blitElement(dst, state.panelTopBar.frame(frame), 40 * i + 20, 20, false);
        }
    }
}

// [RE 0x4166F8] 玩家信息条（布局 2，440,0-640,80；panel.mkf[0] 帧 4 = 200x80）
// 依据: 0x4166F8 反编译; dword_48BE0C + 60 → 帧 4（+12 主头 + 12*4）；
//   currentPlayer==8（機器娃娃虚拟槽）→ 沿用发起玩家 槽8 bailer＝byte_498E70（0x4167F4）
void drawPlayerBar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    // [NEW M4-D] 宽屏右栏整体右移（函数内坐标保持 440 基准）
    SurfaceOriginGuard og(dst, uiPanelOffsetX(dst), 0);
    if (state.panelFrame0.frameCount() <= 4) {
        return;
    }
    // [NEW M4-D 实机] 函数内坐标保持 440 基准（原版）：宽屏右移由上方 SurfaceOriginGuard
    //   统一 +uiPanelOffsetX 完成——此处若再取 uiPanelLogicalX 会双重偏移，背景帧被画到
    //   屏外（右栏背景缺失 → 花屏/金额更新残影）
    blitElementOpaque(dst, state.panelFrame0.frame(4), 440, 0);

    const int v1 = state.currentPlayer;
    if (v1 >= state.playerCount && v1 != 8) {
        return;
    }
    // [RE 0x4167F4] 機器娃娃虚拟槽 8：面板沿用发起玩家（槽8 bailer = byte_498E70）
    const int idx = (v1 == 8) ? static_cast<int>(state.npcSlots[4].bailer) : v1;
    if (idx < 0 || idx >= state.playerCount) {
        return;
    }
    const Player& p = state.players[idx];

    // 颜色条 + 棋子图标
    dst.fillRect(529, 33, 0x6A, 4, 0);
        dst.fillRect(528, 32, 0x6A, 4, rgb888To555(p.color));
    if (state.pieceSprites[idx].frameCount() > 0) {
        blitElement(dst, state.pieceSprites[idx].frame(0), 482, 40, false);
    }
    // [RE 0x416256] 同盟对象头像：g_playerVehicle(+65) 非 0 → pieceSprites[+65-1] 帧 2 @ (524,64)
    if (p.ally && p.ally <= 9 && state.pieceSprites[p.ally - 1].frameCount() > 2) {
        blitElement(dst, state.pieceSprites[p.ally - 1].frame(2), 524, 64, false);
    }

    if (p.name) {
        app.text().setFont(20, kTextDark, 0, 2, 0);
        app.text().drawText(dst, p.name, 582, 16, 2);
    }
    app.text().setFont(12, kTextDark, 0, 2, 1);
    char buf[24];
    buf[0] = '$';
    formatMoney(buf + 1, p.cash);
    app.text().drawText(dst, buf, 634, 41, 1);
    formatMoney(buf + 1, p.bank);
    app.text().drawText(dst, buf, 634, 63, 1);
}

// [RE 0x415F69] 玩家信息面板（布局 0/1，440,0-640,280）
// 依据: 0x415F69 反编译; 背景帧 = panel.mkf[0] 帧 panelTab（dword_48BE24[玩家]）；
//   物价指数 "物價指數  %d" @ (450,260) font12；
//   4 页签资金/地產/股票/其他 @ (627, 35/108/181/254) font18（当前页 0x101010 / 其他 0x404040）；
//   色条 fillRect(523,57,86,12,0)+fillRect(522,56,86,12,玩家色)、棋子 @ (482,40)、
//   同盟头像 pieceSprites[ally-1] 帧 2 @ (524,64)、玩家名 @ (564,40) font22；
//   内容 font18 数值 @ (600, 102/166/230) 右上对齐：
//     0 資金 = 現金/存款/總資產（"$"+千分位）；1 地產 = 土地(estate+corp)/連鎖店/設施；
//     2 股票 = 總市值(Σ股数×现价)/成本(Σ股数×均价)/經營權家数；3 其他 = 點券/貸款/保險期
//     （"%d天"）+ 貸款剩餘天數 @ (500,146) font12
// 差异: 事件槽回合（currentPlayer 4..7）NPC 面板分支 ✅ 2026-09-26（帧 5 + kNpcNames + bailer 棋子）；
//   currentPlayer==8（機器娃娃虚拟槽）
//   沿用发起玩家（槽8 bailer = byte_498E70，0x416118）；脏区标志 dword_475110&0xC 忽略（逐帧重绘）
void drawPlayerInfoPanel(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    // [NEW M4-D] 宽屏右栏整体右移（函数内坐标保持 440 基准）
    SurfaceOriginGuard og(dst, uiPanelOffsetX(dst), 0);
    if (state.panelFrame0.frameCount() <= 0) {
        return;
    }
    int v1 = state.currentPlayer;
    // [RE 0x416118] 機器娃娃虚拟槽 8：完整面板沿用发起玩家（槽8 bailer = byte_498E70）
    if (v1 == 8) {
        v1 = static_cast<int>(state.npcSlots[4].bailer);
    }
    // [RE 0x415FF1 else 分支] 事件槽 NPC 回合面板（cur>=playerCount）：背景帧 5
    //   （dword_48BE0C+72 = 12+12*5）+ NPC 名 dword_47ED5A[cur] @(582,40) 22 号 +
    //   bailer 棋子帧 2 @(524,64)；无页签内容/色条（与玩家分支不同）
    if (v1 >= state.playerCount && v1 >= 4 && v1 < 8) {
        const int bi = state.npcSlots[v1 - 4].bailer;
        if (state.panelFrame0.frameCount() > 5) {
            blitElementOpaque(dst, state.panelFrame0.frame(5), 440, 0);
        }
        app.text().setFont(22, kTextDark, 0, 2, 0);
        app.text().drawText(dst, kNpcNames[v1], 582, 40, 2);
        if (bi < 4 && state.pieceSprites[bi].frameCount() > 2) {
            blitElement(dst, state.pieceSprites[bi].frame(2), 524, 64, false);
        }
        return;
    }
    if (v1 < 0 || v1 >= state.playerCount) {
        return;
    }
    const Player& p = state.players[v1];
    const int tab = p.panelTab & 3;
    if (tab < state.panelFrame0.frameCount()) {
        blitElementOpaque(dst, state.panelFrame0.frame(tab), 440, 0);
    }
    char buf[32];
    // [RE 0x416165/0x41617e/0x416199] 物價指數：font12/0x101010/style2(spacing1)，格式串池 0x4638F5
    //   「物價指數  %d」@ (450,260) align0；任何 tab 均显示（layout 2 玩家条不含）
    app.text().setFont(12, kTextDark, 0, 2, 1);
    std::snprintf(buf, sizeof(buf), "物價指數  %d", state.moneyMul);
    app.text().drawText(dst, buf, 450, 260, 0);
    // 4 页签
    static const int kTabY[4] = {35, 108, 181, 254};
    for (int i = 0; i < 4; ++i) {
        app.text().setFont(18, (i == tab) ? kTextDark : 0x404040, 0, 2, 0);
        app.text().drawText(dst, kTabText[i], 627, kTabY[i], 3);
    }
    // 色条 + 棋子 + 同盟 + 名字
    dst.fillRect(523, 57, 0x56, 12, 0);
    dst.fillRect(522, 56, 0x56, 12, rgb888To555(p.color));
    if (state.pieceSprites[v1].frameCount() > 0) {
        blitElement(dst, state.pieceSprites[v1].frame(0), 482, 40, false);
    }
    if (p.ally && p.ally <= 9 && state.pieceSprites[p.ally - 1].frameCount() > 2) {
        blitElement(dst, state.pieceSprites[p.ally - 1].frame(2), 524, 64, false);
    }
    if (p.name) {
        app.text().setFont(22, kTextDark, 0, 2, 0);
        app.text().drawText(dst, p.name, 564, 40, 2);
    }
    // 页签内容（原版内容沿用最后设置的 font18）
    app.text().setFont(18, kTextDark, 0, 2, 0);
    auto drawMoney = [&](int32_t value, int y) {
        buf[0] = '$';
        formatMoney(buf + 1, value);
        app.text().drawText(dst, buf, 600, y, 1);
    };
    switch (tab) {
        case 0: // 資金
            drawMoney(p.cash, 102);
            drawMoney(p.bank, 166);
            drawMoney(playerTotalAssets(app, v1), 230);
            break;
        case 1: { // 地產：土地(estate+corp)/連鎖店/設施
            int land = 0;
            int chain = 0;
            for (size_t i = 1; i < state.estates.size(); ++i) {
                if (state.estates[i].owner == v1 + 1) {
                    ++land;
                    if (state.estates[i].type != 0) {
                        ++chain;
                    }
                }
            }
            int facility = 0;
            for (size_t i = 1; i < state.corps.size(); ++i) {
                if (state.corps[i].owner == v1 + 1) {
                    ++land;
                    if (state.corps[i].sub != 0) {
                        ++facility;
                    }
                }
            }
            std::snprintf(buf, sizeof(buf), "%d", land);
            app.text().drawText(dst, buf, 600, 102, 1);
            std::snprintf(buf, sizeof(buf), "%d", chain);
            app.text().drawText(dst, buf, 600, 166, 1);
            std::snprintf(buf, sizeof(buf), "%d", facility);
            app.text().drawText(dst, buf, 600, 230, 1);
            break;
        }
        case 2: { // 股票：總市值/成本/經營權家数
            int marketValue = 0;
            int costValue = 0;
            for (int i = 0; i < 12; ++i) {
                const int shares = state.playerShares[v1][i];
                if (shares == 0) {
                    continue;
                }
                marketValue = static_cast<int>(shares * state.stocks[i][5]) + marketValue;
                costValue = static_cast<int>(shares * state.playerAvgCost[v1][i]) + costValue;
            }
            drawMoney(marketValue, 102);
            drawMoney(costValue, 166);
            int holdCorp = 0;
            for (size_t i = 1; i < state.specPts.size(); ++i) {
                if (state.specPts[i].owner == v1 + 1) {
                    ++holdCorp;
                }
            }
            std::snprintf(buf, sizeof(buf), "%d", holdCorp);
            app.text().drawText(dst, buf, 600, 230, 1);
            break;
        }
        case 3: { // 其他：點券/貸款/保險期
            std::snprintf(buf, sizeof(buf), "%u", p.points);
            app.text().drawText(dst, buf, 600, 102, 1);
            std::snprintf(buf, sizeof(buf), "%d天", p.insuranceDays);
            app.text().drawText(dst, buf, 600, 230, 1);
            drawMoney(p.loan, 166);
            if (p.loan != 0) {
                const int left = dateDiff(state.gameDate, p.loanDate);
                if (left != 0) {
                    app.text().setFont(12, kTextDark, 0, 2, 1);
                    std::snprintf(buf, sizeof(buf), "%d天", left);
                    app.text().drawText(dst, buf, 500, 146, 0);
                }
            }
            break;
        }
        default:
            break;
    }
}

// [RE 0x416E6D] 小地图（200x200，位置由 kMiniMapY[布局] 决定）
void drawMiniMap(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    // [NEW M4-D] 宽屏右栏整体右移（函数内坐标保持 440 基准）
    SurfaceOriginGuard og(dst, uiPanelOffsetX(dst), 0);
    const int layout = state.settings[5] & 3;
    const int v1 = kMiniMapY[layout];
    if (v1 == 0) {
        return;
    }
    // [RE 0x40A4E1] 重建工作副本（底图 + 住宅用地/商業用地/行業設施點玩家颜色标记），随物件变化更新
    buildMiniMapMarks(app);
    // [DIAG] 小地图状态（每 120 帧一次）；临时关闭，排查小地图问题时改回 #if 1
#if 0
    static int s_mmLog = 0;
    if (++s_mmLog >= 120) {
        s_mmLog = 0;
        int eOwn = 0;
        int cOwn = 0;
        int sOwn = 0;
        for (size_t i = 1; i < state.estates.size(); ++i) {
            if (state.estates[i].owner) {
                ++eOwn;
            }
        }
        for (size_t i = 1; i < state.corps.size(); ++i) {
            if (state.corps[i].owner) {
                ++cOwn;
            }
        }
        for (size_t i = 1; i < state.specPts.size(); ++i) {
            if (state.specPts[i].owner) {
                ++sOwn;
            }
        }
        RICH4_LOGI("minimap: y=%d buf=%zu rawF=%d mmF=%d estates=%zu(own%d) corps=%zu(own%d) specPts=%zu(own%d)",
                   v1, state.miniMapBuffer.size(), state.miniMapRaw.frameCount(),
                   state.miniMap.frameCount(), state.estates.size(), eOwn, state.corps.size(), cOwn,
                   state.specPts.size(), sOwn);
        for (int i = 0; i < state.playerCount && i < 9; ++i) {
            RICH4_LOGI("minimap player %d: sprite=(%d,%d) pieceF=%d", i, state.players[i].spriteX,
                       state.players[i].spriteY, state.pieceSprites[i].frameCount());
        }
    }
#endif
    if (state.miniMapBuffer.size() == static_cast<size_t>(kMiniMapW) * kMiniMapH) {
        // [NEW M4-A2] 小地图工作副本按画布 scale 缩放绘制（scale=1 逐像素等价）
        blitScaled(dst, reinterpret_cast<const uint8_t*>(state.miniMapBuffer.data()),
                   kMiniMapW * 2, nullptr, 440, v1, 0, 0, kMiniMapW, kMiniMapH, false, true);
    } else if (state.miniMap.frameCount() > 0) {
        blitElementOpaque(dst, state.miniMap.frame(0), 440, v1);
    }
    // [RE 0x416E6D] 左右旋转箭头（data.mkf[517] 帧 18/19 按下、20/21 正常）
    // 原版 sub_416E6D: sub_456418(dword_48BAD8 + 252/264, 443/468, v1+3)
    //       （252 = 12+12*20 → 帧 20，264 = 12+12*21 → 帧 21）;
    //       按下态帧 18/19 见 sub_417E26 WM_LBUTTONDOWN/UP（12*(byte_48BE28+17)+12）
    if (state.estateTiles.frameCount() > 21) {
        const int leftFrame = (state.pendingAction == 1) ? 18 : 20;
        const int rightFrame = (state.pendingAction == 2) ? 19 : 21;
        blitElement(dst, state.estateTiles.frame(leftFrame), 443, v1 + 3, false);
        blitElement(dst, state.estateTiles.frame(rightFrame), 468, v1 + 3, false);
    }
    // 玩家标记（5696/65536 缩放）+ 当前玩家框
    int markX = 0;
    int markY = 0;
    for (int i = 0; i < state.playerCount && i < 9; ++i) {
        const Player& p = state.players[i];
        if (p.spriteX == 0) {
            continue;
        }
        const int v2 = ((5696 * static_cast<int>(p.spriteX)) >> 16) + 440;
        const int v3 = ((5696 * static_cast<int>(p.spriteY)) >> 16) + v1;
        // [RE 0x416E6D] 小地图标记 = g_pieceSprites[13*i]+84 = 帧 6
        //   （帧头公式 基址+12+12*帧号：84 = 12+12*6；资源共 7 帧 0..6）
        if (state.pieceSprites[i].frameCount() > 6) {
            blitElement(dst, state.pieceSprites[i].frame(6), v2, v3, false);
        }
        if (i == state.currentPlayer) {
            markX = v2;
            markY = v3;
        }
    }
    // [RE 0x416E6D] 事件槽 4..8（8=機器娃娃）：不画标记；仅自由（busy==0，原版 !byte_498DF2）
    //   且为当前玩家时用 word_498DE8/DEA（槽 pixelX/Y）记录位置供白框
    for (int j = 4; j < 9; ++j) {
        if (state.npcSlots[j - 4].busy == 0 && j == state.currentPlayer) {
            markX = ((5696 * static_cast<int>(state.npcSlots[j - 4].pixelX)) >> 16) + 440;
            markY = ((5696 * static_cast<int>(state.npcSlots[j - 4].pixelY)) >> 16) + v1;
        }
    }
    // [实机反馈] 小地图方框 = 游戏内视野大小（地图区在小地图尺度下的尺寸：
    //   5696/65536 ≈ 0.0869；native 38x38 / 宽屏随地图区宽 56x38）
    const int viewW = (5696 * uiMapLogicalWidth(dst)) >> 16;
    const int viewH = (5696 * 440) >> 16;
    if (markX && markY) {
        // [RE 0x416E6D] 当前玩家框（白色，push 0FFFFFFh）
        drawRectBorder(dst, markX - viewW / 2, markY - viewH / 2, viewW, viewH,
                       rgb888To555(0xFFFFFF));
    }
    // [RE 0x416E6D] 手动视野框（红色，push 0FF0000h；dword_48BE18 时显示视口中心）
    // 原版 sub_416E6D: if(dword_48BE18){ v5=(5696*dword_48BE1C)>>16; v6=(5696*dword_48BE20)>>16;
    //       if(v5+440!=v8||v6+v1!=v9) drawRectBorder(v5+425, v1+v6-15, 30, 30, 0xFF0000) }
    if (state.manualView) {
        const int vx = (5696 * state.viewSmoothX) >> 16;
        const int vy = (5696 * state.viewSmoothY) >> 16;
        if (vx + 440 != markX || vy + v1 != markY) {
            drawRectBorder(dst, vx + 440 - viewW / 2, v1 + vy - viewH / 2, viewW, viewH,
                           rgb888To555(0xFF0000));
        }
    }
}

// [RE 0x4521F0] 节日查询：组 base=4*gameMode+mapIndex，遍历 24 条，命中返回索引(0..23)否则 -1
// 依据: 0x4521F0; type 0=固定(月,日)、1=浮动(查 kFloatHoliday 农历表)、2=第N星期X
int findHoliday(const GameState& st, uint32_t date) {
    const int base = 4 * st.gameMode + st.mapIndex;
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    const int v2 = (month << 8) | day;
    const int32_t days = daysSince1998(date);
    for (int i = 0; i < 24; ++i) {
        const uint8_t* h = kHoliday[base][i];
        const int type = h[1];
        int a1 = -1;
        int vcmp = v2;
        if (type == 0) {
            a1 = (h[2] << 8) | h[3];
        } else if (type == 1) {
            // [RE 0x452285] 浮动（农历）节日：查农历表；越界（表止 ~2021）不命中
            if (days < 0 || days >= kFloatHolidayLen) {
                continue;
            }
            vcmp = kFloatHoliday[days];
            a1 = (h[2] << 8) | h[3];
        } else if (type == 2) {
            if (h[2] != month) {
                continue;
            }
            const int firstWd = firstWeekday(year, month);
            const int totalDays = daysInMonth(year, month);
            int wd = h[4];
            if (wd < firstWd) {
                wd = 7;
            }
            const int d = 7 * (h[3] - 1) + wd - firstWd + 1;
            if (d > totalDays) {
                continue;
            }
            a1 = (month << 8) | d;
        } else {
            continue;
        }
        if (vcmp == a1 && h[0] < 0x80) {
            return i;
        }
    }
    return -1;
}

// [RE 0x4523D5] 特殊日期判断：星期日 或 命中节日且 flag 非0 → 红字
bool isSpecialDate(const GameState& st, uint32_t date) {
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    const int wd = (firstWeekday(year, month) + day - 1) % 7;
    if (wd == 0) {
        return true; // 星期日
    }
    const int idx = findHoliday(st, date);
    if (idx >= 0) {
        const int base = 4 * st.gameMode + st.mapIndex;
        if (kHoliday[base][idx][0] != 0) {
            return true;
        }
    }
    return false;
}

// [RE 0x4169BC] 节日专属背景：data.mkf[kCalendarRes[base]+idx] 是 RAW 200x200 插画，
//   缓存到 holidayBgData（原版 dword_48BDCC/dword_48BDD0）
void drawHolidayBackground(Application& app, int holiday) {
    GameState& st = app.gameState();
    const int base = 4 * st.gameMode + st.mapIndex;
    const int resIdx = kCalendarRes[base] + holiday;
    if (st.holidayBgIdx != resIdx) {
        auto blob = st.data.read(static_cast<size_t>(resIdx));
        st.holidayBgData = blob ? std::move(*blob) : std::vector<uint8_t>{};
        st.holidayBgIdx = resIdx;
    }
    const RawBitmap raw = decodeRawBitmap(st.holidayBgData);
    if (!raw.valid()) {
        return;
    }
    Surface& dst = app.surface();
    // [NEW M4-D] 宽屏右栏整体右移（函数内坐标保持 440 基准）
    SurfaceOriginGuard og(dst, uiPanelOffsetX(dst), 0);
    // [NEW M4-A2] 节日插画按画布 scale 缩放绘制（不透明；scale=1 时逐像素等价）
    blitScaled(dst, reinterpret_cast<const uint8_t*>(raw.pixels), raw.width * 2, nullptr, 440,
               280, 0, 0, raw.width, raw.height, false, true);
}

// [RE 0x4169BC] 日历（440,280-640,480；panel.mkf[2]）
// 依据: 0x4169BC 反编译; byte_497164 二选一：
//   !=0 月历网格（背景帧 kMonthFrame[月-1]+4，1..N 数字，节日红字 sub_4523D5，当日画框）
//    ==0 大数字（背景帧 kMonthFrame[月-1]；sub_4521F0 命中节日→专属背景 kCalendarRes[base]+idx；
//         装饰帧 + 星期名 off_47511C[wd] at 454,352 + 大号「日」at 500,376）
//   两模式共有 LABEL_28：年 at 580,288（24号）、月 at 500,328（28号）
void drawCalendar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    // [NEW M4-D] 宽屏右栏整体右移（函数内坐标保持 440 基准）
    SurfaceOriginGuard og(dst, uiPanelOffsetX(dst), 0);
    const uint32_t date = state.gameDate;
    const int year = static_cast<int>(date >> 16);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    if (month < 1 || month > 12) {
        return;
    }
    const int firstWd = firstWeekday(year, month);
    const int totalDays = daysInMonth(year, month);
    const uint32_t ym = date & 0xFFFFFF00u;
    char text[8];

    if (state.calendarMode != 0) {
        // —— 月历网格模式 ——
        const int frame = kMonthFrame[month - 1] + 4;
        if (state.panelFrame2.frameCount() > frame) {
            blitElementOpaque(dst, state.panelFrame2.frame(frame), 440, 280);
        }
        int col = 470 + 23 * firstWd;
        int row = 378;
        for (int d = 1; d <= totalDays; ++d) {
            std::snprintf(text, sizeof(text), "%d", d);
            if (d == day) {
                drawRectBorder(dst, col - 10, row - 6, 0x14, 14, 0xF800);
            }
            const bool special = isSpecialDate(state, ym | static_cast<uint32_t>(d));
            app.text().setFont(12, special ? kTextRed : kTextDark, 0, 2, 1);
            app.text().drawText(dst, text, col, row, 2);
            if (col == 608) {
                col = 447;
                row += 14;
            }
            col += 23;
        }
    } else {
        // —— 大数字日历模式 ——
        const int holiday = findHoliday(state, date); // [RE 0x4521F0] 节日索引，-1=无
        int v6 = 0;
        if (holiday < 0) {
            const int frame = kMonthFrame[month - 1];
            if (state.panelFrame2.frameCount() > frame) {
                blitElementOpaque(dst, state.panelFrame2.frame(frame), 440, 280);
            }
        } else {
            drawHolidayBackground(app, holiday); // 节日专属背景（kCalendarRes[base]+idx）
            v6 = 1;
        }
        if (state.panelFrame2.frameCount() > 11) {
            blitElement(dst, state.panelFrame2.frame(8), 462, 300, false);
            blitElement(dst, state.panelFrame2.frame(11), 492, 301, false);
        }
        const int curWd = (firstWd + day - 1) % 7;
        if (v6) {
            app.text().setFont(16, kTextRed, kTextDark, 6, 1);
        } else {
            app.text().setFont(16, kTextDark, 0xFFFFFF, 6, 1);
        }
        app.text().drawText(dst, kWeekdayNames[curWd], 454, 352, 3);
        if (v6) {
            app.text().setFont(60, kTextRed, kTextDark, 6, 1);
        } else {
            app.text().setFont(60, kTextDark, 0xFFFFFF, 6, 1);
        }
        std::snprintf(text, sizeof(text), "%d", day);
        app.text().drawText(dst, text, 500, 376, 2);
    }
    // LABEL_28：年 / 月（两模式共有）
    app.text().setFont(24, kTextDark, 0xFFFFFF, 6, 1);
    std::snprintf(text, sizeof(text), "%d", year);
    app.text().drawText(dst, text, 580, 288, 0);
    app.text().setFont(28, kTextDark, 0xFFFFFF, 6, 1);
    std::snprintf(text, sizeof(text), "%d", month);
    app.text().drawText(dst, text, 500, 328, 2);
}

// [RE 0x417191] 前进面板（panel.mkf[7] = dword_48BE04）
// 依据: 0x417191 反编译;
//   背景帧 = dword_48BDD4 + v1（v1 = byte_496BA0?2 : byte_496BA1?4 : 0）
//   GO/骰子图标（帧 6..11）:
//     travel 0/3: 帧 6（byte_496BA0）或 7，(x+8/y+26) 或 (x+7/y+26)
//     travel 1: 2 个骰子（帧 2i+6 未选 / 2i+7 选中），(x+8/y+19i+16) / (x+7/y+19i+16)
//     travel 2: 3 个骰子，(x+8/y+16j+9) / (x+7/y+16j+9)
// 位置 = qword_475284 **静态常量 (180,120)**（2026-10-05 IDA 复核：18 处 xref 全为读取，
//   无任何写入点；beginPlayerTurn 仅读它 SetCursorPos(+46,+34) 把鼠标移到 GO 中心——
//   重写不劫持系统光标）；宽屏经 uiMapCenterShiftX 随地图区中心平移（绘制/命中同源）
// [M4-B C1] 阻塞演出期禁止绘制（原版前进面板是一次性增量层 0x417191，演出期消息泵
//   不派发 → 既不会重画也不会残留；重写逐帧全量重绘必须显式收口，见 blockingPerf）
void drawAdvancePanel(Application& app) {
    GameState& state = app.gameState();
    if (!state.gamePlayerControl || state.panelFrame7.frameCount() <= 0 || blockingPerf(app)) {
        return;
    }
    // [RE 0x417191] 状态效果中（住宿/出國/監獄/醫院/冬眠/夢遊）不显示前进面板：
    //   原版状态中玩家回合被跳过（checkPlayerAction 返回 0），不会进入 enablePlayerControl
    if (state.currentPlayer >= 0 && state.currentPlayer < 9) {
        const Player& cp = state.players[state.currentPlayer];
        if (cp.stateFlags != 0 || cp.byte54 != 0 || cp.state37 != 0 || (cp.alive & 0x30) != 0) {
            return;
        }
    }
    trace::logf("go panel draw cur=%d", state.currentPlayer);
    Surface& dst = app.surface();
    const Player& p = state.players[state.currentPlayer];
    // [NEW M4-D 实机] 宽屏下前进面板随地图区中心平移（native shift=0；用户要求居中）
    const int x = state.advancePanelX + uiMapCenterShiftX(dst);
    const int y = state.advancePanelY;
    int v1 = 0;
    if (p.skipMove) {
        v1 = 2;
    }
    if (p.fixedStep) {
        v1 = 4;
    }
    const int bg = v1 + state.advancePanelBlink;
    if (bg >= 0 && bg < state.panelFrame7.frameCount()) {
        blitElement(dst, state.panelFrame7.frame(bg), x, y, false);
    }
    auto blitFrame = [&](int frame, int px, int py) {
        if (frame >= 0 && frame < state.panelFrame7.frameCount()) {
            blitElement(dst, state.panelFrame7.frame(frame), px, py, false);
        }
    };
    switch (p.travel & 3) {
        case 0:
        case 3:
            if (p.skipMove) {
                blitFrame(6, x + 8, y + 26);
            } else {
                blitFrame(7, x + 7, y + 26);
            }
            break;
        case 1:
            for (int i = 0; i < 2; ++i) {
                if (i > static_cast<int>(p.diceCount) - 1 || p.skipMove) {
                    blitFrame(2 * i + 6, x + 8, y + 19 * i + 16);
                } else {
                    blitFrame(2 * i + 7, x + 7, y + 19 * i + 16);
                }
            }
            break;
        case 2:
            for (int j = 0; j < 3; ++j) {
                if (j > static_cast<int>(p.diceCount) - 1 || p.skipMove) {
                    blitFrame(2 * j + 6, x + 8, y + 16 * j + 9);
                } else {
                    blitFrame(2 * j + 7, x + 7, y + 16 * j + 9);
                }
            }
            break;
        default:
            break;
    }
}

} // namespace

// [RE 0x452444] 每日节日处理（advanceDay 0x41D07B 调用；见 452444-holiday-system.md）：
//   命中节日(当前覆盖 type0/2)且效果 flags≠0 → 节日音乐(flags&4，无 musicTimer 时
//   playSceneMusic 不压栈) + 全屏节日插画 FLC(flags&1，data.mkf[res] @(0,40) 透明)。
//   人人发卡(flags&8)链需节日赠卡文案表 unk_4661C4.. 留后续。
void holidayDaily(Application& app) {
    GameState& st = app.gameState();
    const int holiday = findHoliday(st, st.gameDate);
    if (holiday < 0) {
        return;
    }
    const int base = 4 * st.gameMode + st.mapIndex;
    const uint8_t* h = kHoliday[base][holiday];
    const int eflags = h[5];
    if (eflags == 0) {
        return;
    }
    RICH4_LOGI("holidayDaily: base=%d holiday=%d flags=0x%x (RE 0x452444)", base, holiday, eflags);
    trace::logf("holiday base=%d idx=%d flags=0x%x", base, holiday, eflags);
    // [RE 0x4525C7] 节日音乐：g_musicTimer 非0 期间 playSceneMusic 整体被禁（Audio 内部
    //   门控 2026-09-29），此处 ==0 判定与原版 if(!g_musicTimer) 双保险等价
    if ((eflags & 4) != 0 && app.audio().switchDays() == 0) {
        const int musicIdx = h[10] | (h[11] << 8);
        if (musicIdx != 0) {
            app.audio().playSceneMusic(musicIdx, /*saveCurrent=*/false); // 原版 |0x8000 不压栈
            app.audio().setSwitchDays(0x33); // [RE 0x4525DE] 51（原版按后续 word 判 51/17）
        }
    }
    if ((eflags & 1) != 0) {
        const int flcRes = h[6] | (h[7] << 8);
        const int flcSound = h[8] | (h[9] << 8);
        if (flcRes != 0 && st.data.read(static_cast<size_t>(flcRes))) {
            playEventFlc(app, flcRes, 0, 40, flcSound);
        }
    }
    // [RE 0x45263F] 人人发卡（flags&8）：每存活玩家从赠卡池抽一张 + 展示，文案按地图组 4*mode+map
    if ((eflags & 8) != 0) {
        const int v13 = 4 * st.gameMode + st.mapIndex;
        const char* msg = (v13 == 4) ? "銀河系和平日\n\n%s得到%s！"   // [RE 0x4661C4]
                        : (v13 == 5) ? "恐龍蛋節\n\n%s得到%s！"        // [RE 0x4661DD]
                        : (v13 == 6) ? "除夕\n\n%s得到%s！"            // [RE 0x4661F2]
                                     : "聖誕節\n\n%s得到%s！";         // [RE 0x466203]
        for (int i = 0; i < st.playerCount && i < 9; ++i) {
            if (st.players[i].alive == 0) {
                continue;
            }
            const int c = drawFreeCard(st, i); // [RE sub_441E12] 赠卡池抽卡入包（0=池空）
            if (c == 0) {
                continue;
            }
            char text[192];
            std::snprintf(text, sizeof text, msg, playerNameNoSpace(st, i).c_str(), kCardNames[c]);
            showCardGet(app, c, text);             // [RE sub_441F73]
            playValueLine(app, i, kCardPrices[c]); // [RE sub_44F230]
        }
    }
}

// [RE 0x4523D5] 股市休市日封装（isSpecialDate 为匿名 ns，此处导出供 stockTick/面板复用）
bool stockIsHoliday(const GameState& st) {
    return isSpecialDate(st, st.gameDate);
}

// [RE 0x41D433] refreshPlayerPanelFor：资金变化后立即重绘指定玩家面板
// 依据: 0x41D433 反编译; `a1 <= 7 && g_modalDepth <= 1` 才执行——事件/消息模态内
//   （depth>=2）跳过，等相位1/退出后的显式重绘（防把场景画到事件面板上）；
//   临时切 g_currentPlayer 到目标玩家 → layout 2 用玩家条（0x4166F8）/
//   其他布局用完整面板（0x415F69）→ 还原。transferMoney/addMoney 尾调用。
void refreshPlayerPanelFor(Application& app, int player) {
    GameState& st = app.gameState();
    if (player < 0 || player > 7 || app.events().depth() > 1) {
        return;
    }
    const int saved = st.currentPlayer;
    st.currentPlayer = player;
    if ((st.settings[5] & 3) == 2) {
        drawPlayerBar(app);
    } else {
        drawPlayerInfoPanel(app);
    }
    st.currentPlayer = saved;
}

void renderGamePanel(Application& app, uint8_t dirty) {
    // [RE 0x417E26] WM_PAINT 分支：顶部工具条 + 按 byte_49715D 布局
    // [NEW M4-C2] 区域脏区：仅重画 dirty 指定的区域（kDirtyAll = 原全量行为）
    const int layout = app.gameState().settings[5] & 3;
    if (dirty & kDirtyTopBar) {
        drawTopBar(app);
    }
    // 玩家条：layout 2 用信息条，其余用完整信息面板（原分支等价）
    if (dirty & kDirtyPlayerBar) {
        if (layout == 2) {
            drawPlayerBar(app);
        } else {
            drawPlayerInfoPanel(app);
        }
    }
    if ((dirty & kDirtyMiniMap) && layout != 0) {
        drawMiniMap(app);
    }
    if ((dirty & kDirtyCalendar) && layout != 1) {
        drawCalendar(app);
    }
    // 前进面板（GO）绘于地图区之上：随地图场景层重画
    if (dirty & kDirtyMap) {
        drawAdvancePanel(app);
    }
}

} // namespace rich4
