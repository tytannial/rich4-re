#include "game/app/game_panel.h"

#include <cstdio>
#include <cstring>

#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
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
// 依据: 0x4166F8 反编译; dword_48BE0C + 60 → 帧 4（+12 主头 + 12*4）
void drawPlayerBar(Application& app) {
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    if (state.panelFrame0.frameCount() <= 4) {
        return;
    }
    blitElementOpaque(dst, state.panelFrame0.frame(4), 440, 0);

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
    // [RE 0x40A4E1] 优先用叠加标记后的工作副本（dword_48BADC）
    if (state.miniMapBuffer.size() == static_cast<size_t>(kMiniMapW) * kMiniMapH) {
        uint16_t* d = dst.pixels() + static_cast<size_t>(v1) * Surface::kWidth + 440;
        for (int row = 0; row < kMiniMapH; ++row) {
            std::memcpy(d, &state.miniMapBuffer[static_cast<size_t>(row) * kMiniMapW],
                        kMiniMapW * sizeof(uint16_t));
            d += Surface::kWidth;
        }
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
        if (state.pieceSprites[i].frameCount() > 7) {
            blitElement(dst, state.pieceSprites[i].frame(7), v2, v3, false);
        }
        if (i == state.currentPlayer) {
            markX = v2;
            markY = v3;
        }
    }
    if (markX && markY) {
        // [RE 0x416E6D] 当前玩家框（白色，push 0FFFFFFh）
        drawRectBorder(dst, markX - 15, markY - 15, 30, 30, rgb888To555(0xFFFFFF));
    }
    // [RE 0x416E6D] 手动视野框（红色，push 0FF0000h；dword_48BE18 时显示视口中心）
    // 原版 sub_416E6D: if(dword_48BE18){ v5=(5696*dword_48BE1C)>>16; v6=(5696*dword_48BE20)>>16;
    //       if(v5+440!=v8||v6+v1!=v9) drawRectBorder(v5+425, v1+v6-15, 30, 30, 0xFF0000) }
    if (state.manualView) {
        const int vx = (5696 * state.viewSmoothX) >> 16;
        const int vy = (5696 * state.viewSmoothY) >> 16;
        if (vx + 440 != markX || vy + v1 != markY) {
            drawRectBorder(dst, vx + 425, v1 + vy - 15, 30, 30, rgb888To555(0xFF0000));
        }
    }
}

// [RE 0x4521F0] 节日查询：组 base=4*gameMode+mapIndex，遍历 24 条，命中返回索引(0..23)否则 -1
// 依据: 0x4521F0; type 0=固定(月,日)、2=第N个星期X；1=浮动(需 dword_47639C 表)
// 差异: type1 浮动节日暂未实现（跳过）
int findHoliday(const GameState& st, uint32_t date) {
    const int base = 4 * st.gameMode + st.mapIndex;
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    const int v2 = (month << 8) | day;
    for (int i = 0; i < 24; ++i) {
        const uint8_t* h = kHoliday[base][i];
        const int type = h[1];
        int a1 = -1;
        if (type == 0) {
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
            continue; // type1 浮动，暂不支持
        }
        if (v2 == a1 && h[0] < 0x80) {
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
    uint16_t* d = dst.pixels();
    for (int row = 0; row < raw.height; ++row) {
        const int py = 280 + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < raw.width; ++col) {
            const int px = 440 + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            d[static_cast<size_t>(py) * Surface::kWidth + px] = raw.pixels[row * raw.width + col];
        }
    }
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
        app.text().drawText(dst, kMonthName[curWd], 454, 352, 3);
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
// 差异: 位置 qword_475284 原版动态确定，当前用 advancePanelX/Y
void drawAdvancePanel(Application& app) {
    GameState& state = app.gameState();
    if (!state.gamePlayerControl || state.panelFrame7.frameCount() <= 0) {
        return;
    }
    Surface& dst = app.surface();
    const Player& p = state.players[state.currentPlayer];
    const int x = state.advancePanelX;
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
    drawAdvancePanel(app);
}

} // namespace rich4
