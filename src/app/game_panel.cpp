#include "game/app/game_panel.h"

#include <cstdio>
#include <cstring>

#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"

namespace rich4 {

namespace {

constexpr uint32_t kTextDark = 0x101010; // 原版 1052688
constexpr uint32_t kTextRed = 0xFF0000;  // 原版 16711680

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
// 依据: 帧 0 背景 blit 到 (0,0); 11 个按钮帧 2+i 画在 (40*i+20, 20);
//       按下/高亮帧 13+i（dword_48BDE4 为高亮索引，阶段 1 恒 -1）
void drawTopBar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    if (state.panelTopBar.frameCount() <= 0) {
        return;
    }
    blitElementOpaque(dst, state.panelTopBar.frame(0), 0, 0);
    for (int i = 0; i < 11; ++i) {
        // 高亮帧 13+i（原版 dword_48BDE4+13），普通帧 2+i
        const int frame = (i == state.topBarHover) ? (13 + i) : (2 + i);
        if (frame < state.panelTopBar.frameCount()) {
            blitElement(dst, state.panelTopBar.frame(frame), 40 * i + 20, 20, false);
        }
    }
}

// [RE 0x4166F8] 玩家信息条（布局 2，440,0-640,80；panel.mkf[0] 帧 5）
void drawPlayerBar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    if (state.panelFrame0.frameCount() <= 5) {
        return;
    }
    blitElementOpaque(dst, state.panelFrame0.frame(5), 440, 0);

    const int v1 = state.currentPlayer;
    if (v1 >= state.playerCount && v1 != 8) {
        return;
    }
    const int idx = (v1 == 8) ? 0 : v1;
    const Player& p = state.players[idx];

    // 颜色条 + 棋子图标
    dst.fillRect(529, 33, 0x6A, 4, 0);
    dst.fillRect(528, 32, 0x6A, 4, p.color);
    if (state.pieceSprites[idx].frameCount() > 0) {
        blitElement(dst, state.pieceSprites[idx].frame(0), 482, 40, false);
    }
    if (p.vehicle && p.vehicle <= 9 && state.pieceSprites[p.vehicle - 1].frameCount() > 2) {
        blitElement(dst, state.pieceSprites[p.vehicle - 1].frame(2), 524, 64, false);
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

// [RE 0x415F69] 玩家信息面板（布局 0/1，440,0-640,280）— 阶段 1 简化
// 差异: 4 页签（資金/地產/股票/其他）切换与地产/股票统计阶段 2 接入
void drawPlayerInfoPanel(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    if (state.panelFrame0.frameCount() <= 0) {
        return;
    }
    blitElementOpaque(dst, state.panelFrame0.frame(0), 440, 0);
    const int v1 = state.currentPlayer;
    if (v1 >= state.playerCount) {
        return;
    }
    const Player& p = state.players[v1];
    if (state.pieceSprites[v1].frameCount() > 0) {
        blitElement(dst, state.pieceSprites[v1].frame(0), 482, 40, false);
    }
    if (p.name) {
        app.text().setFont(22, kTextDark, 0, 2, 0);
        app.text().drawText(dst, p.name, 564, 40, 2);
    }
    app.text().setFont(16, kTextDark, 0, 2, 0);
    for (int i = 0; i < 4; ++i) {
        app.text().drawText(dst, kTabText[i], 627, 20 + 15 + i * 15, 3);
    }
    app.text().setFont(12, kTextDark, 0, 2, 1);
    char buf[24];
    buf[0] = '$';
    formatMoney(buf + 1, p.cash);
    app.text().drawText(dst, buf, 600, 102, 1);
    formatMoney(buf + 1, p.bank);
    app.text().drawText(dst, buf, 600, 166, 1);
    formatMoney(buf + 1, p.cash + p.bank);
    app.text().drawText(dst, buf, 600, 230, 1);
}

// [RE 0x416E6D] 小地图（200x200，位置由 kMiniMapY[布局] 决定）
void drawMiniMap(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    const int layout = state.settings[5] & 3;
    const int v1 = kMiniMapY[layout];
    if (v1 == 0) {
        return;
    }
    if (state.miniMap.frameCount() > 0) {
        blitElementOpaque(dst, state.miniMap.frame(0), 440, v1);
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
        if (state.pieceSprites[i].frameCount() > 7) {
            blitElement(dst, state.pieceSprites[i].frame(7), v2, v3, false);
        }
        if (i == state.currentPlayer) {
            markX = v2;
            markY = v3;
        }
    }
    if (markX && markY) {
        drawRectBorder(dst, markX - 15, markY - 15, 30, 30, 0x7FE0); // 黄色 RGB555
    }
}

// [RE 0x4169BC] 日历（440,280-640,480；panel.mkf[2]）
void drawCalendar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    const uint32_t date = state.gameDate;
    const int year = static_cast<int>(date >> 16);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    if (month < 1 || month > 12) {
        return;
    }
    const int frame = kMonthFrame[month - 1] + 4;
    if (state.panelFrame2.frameCount() > frame) {
        blitElementOpaque(dst, state.panelFrame2.frame(frame), 440, 280);
    }
    // 日历网格（列宽 23，行高 14，起点由月首星期决定）
    const int weekdays = firstWeekday(year, month);
    const int totalDays = daysInMonth(year, month);
    int col = 470 + 23 * weekdays;
    int row = 378;
    for (int d = 1; d <= totalDays; ++d) {
        char text[8];
        std::snprintf(text, sizeof(text), "%d", d);
        if (d == day) {
            drawRectBorder(dst, col - 10, row - 6, 0x14, 14, 0);
        }
        app.text().setFont(12, kTextDark, 0, 2, 1);
        app.text().drawText(dst, text, col, row, 2);
        if (col == 608) {
            col = 447;
            row += 14;
        }
        col += 23;
    }
    // 年 / 月 / 日
    app.text().setFont(24, kTextDark, 0xFFFFFF, 6, 1);
    char text[8];
    std::snprintf(text, sizeof(text), "%d", year);
    app.text().drawText(dst, text, 580, 288, 0);
    app.text().setFont(28, kTextDark, 0xFFFFFF, 6, 1);
    std::snprintf(text, sizeof(text), "%d", month);
    app.text().drawText(dst, text, 500, 328, 2);
    app.text().setFont(60, kTextRed, 0x101010, 6, 1);
    std::snprintf(text, sizeof(text), "%d", day);
    app.text().drawText(dst, text, 500, 376, 2);
}

} // namespace

void renderGamePanel(Application& app) {
    // [RE 0x417E26] WM_PAINT 分支：顶部工具条 + 按 byte_49715D 布局
    const int layout = app.gameState().settings[5] & 3;
    drawTopBar(app);
    if (layout == 0) {
        drawPlayerInfoPanel(app);
        drawCalendar(app);
    } else if (layout == 1) {
        drawPlayerInfoPanel(app);
        drawMiniMap(app);
    } else {
        drawPlayerBar(app);
        drawMiniMap(app);
        drawCalendar(app);
    }
}

} // namespace rich4
