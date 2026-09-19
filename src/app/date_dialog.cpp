#include "game/app/date_dialog.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace rich4 {

namespace {

// [RE 0x458331] _dos_getdate 等价的系统日期（BYTE0=日, BYTE1=月, HIWORD=年）
uint32_t systemDate() {
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    SDL_DateTime dt{};
    SDL_TimeToDateTime(now, &dt, true);
    return static_cast<uint32_t>(dt.day) | (static_cast<uint32_t>(dt.month) << 8) |
           (static_cast<uint32_t>(dt.year) << 16);
}

// [RE 0x4119E3] 帧索引（元素偏移 = 12 + 12*i）
// 原版: dword_48BB60+36 → 帧 2（199x220 对话框）; +156 → 帧 12（月箭头 17x11）;
//       +168 → 帧 13（年箭头）; +180 → 帧 14（按钮 56x31）
constexpr int kFrameBg = 2;
constexpr int kFrameArrowH = 12;
constexpr int kFrameArrowV = 13;
constexpr int kFrameButton = 14;

struct ControlRect {
    int left;
    int top;
    int right;
    int bottom;
};

// [RE 0x474CE8] dword_474CE8/EC/F0/F4 控件矩形（8 组）
constexpr ControlRect kControls[8] = {
    {74, 21, 90, 31},     // 0 月-1
    {74, 31, 90, 41},     // 1 年-1
    {160, 21, 176, 31},   // 2 月+1
    {160, 31, 176, 41},   // 3 年+1
    {9, 180, 64, 210},    // 4 重置
    {72, 180, 127, 210},  // 5 取消
    {134, 180, 189, 210}, // 6 確定
    {15, 70, 181, 177},   // 7 日历网格
};

// [RE 0x474C94] 月份名（索引 1-12 = 一月..十二月）
const char* const kMonths[12] = {"一月", "二月", "三月", "四月", "五月", "六月",
                                 "七月", "八月", "九月", "十月", "十一月", "十二月"};

// [RE 0x474CC8] 按钮文本（重置/取消/確定）与位置 [RE 0x474CDC]
const char* const kButtons[3] = {"重 置", "取 消", "確 定"};
constexpr int kButtonTextX[3] = {38, 101, 163};
constexpr int kButtonTextY = 196;

// [RE 0x474CD4] 月份显示位置 / [RE 0x474CD8] 年份显示位置
constexpr int kMonthX = 48;
constexpr int kMonthY = 32;
constexpr int kYearX = 133;
constexpr int kYearY = 30;

// [RE 0x40FF4B] 日历网格：x = 28 + 23*星期, y = 80, 到 x==166 换行, 行距 18
constexpr int kGridX0 = 28;
constexpr int kGridY0 = 80;
constexpr int kCellW = 23;
constexpr int kCellH = 18;
constexpr int kWrapX = 166;
// [RE 0x40FF4B] 当前日期高亮色 5345644 (0x51976C, RGB888)
constexpr int kHighlightColor888 = 5345644;

// [RE 0x4520A6] 日历计算
// 依据: 0x4520A6 中 v6 = (天数+4)%7（1998-01-01 为周四）; 2 月闰年 29 天（年%4）
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

struct DateDialogState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    uint32_t date = 0;        // dword_48BB84 工作值
    uint32_t initialDate = 0; // dword_48BB5C 当前日期（重置用）
    int originX = 0;          // dword_48BB7C
    int originY = 0;          // bErase（原版变量名）
    int pressed = -1;         // dword_474D78 按下中的控件（-1 无）
};

int hitTest(int rx, int ry) {
    for (int i = 0; i < 8; ++i) {
        const ControlRect& r = kControls[i];
        if (rx >= r.left && rx < r.right && ry >= r.top && ry < r.bottom) {
            return i;
        }
    }
    return -1;
}

void redrawDateDialog(DateDialogState& state) {
    Surface& surface = state.app->surface();
    if (!state.ui) {
        return;
    }
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;
    const int day = static_cast<int>(state.date & 0xFF);
    const int month = static_cast<int>((state.date >> 8) & 0xFF);
    const int year = static_cast<int>((state.date >> 16) & 0xFFFF);
    TextRenderer& text = state.app->text();

    // [RE 0x40FF4B] 背景帧 2（原版 blitElementFullscreen = 不透明 blit，0 像素写入）
    blitElementOpaque(surface, ui.frame(kFrameBg), ox, oy);

    // 月份名 + 年份（原版 setTextFont(15, 0x101010, 0x101010, 2, 0)）
    text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 0);
    if (month >= 1 && month <= 12) {
        text.drawText(surface, kMonths[month - 1], ox + kMonthX, oy + kMonthY, 2);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", year);
    text.drawText(surface, buf, ox + kYearX, oy + kYearY, 2);

    // 日历网格（1..天数，7 列）
    const int count = daysInMonth(year, month);
    int x = kGridX0 + kCellW * firstWeekday(year, month);
    int y = kGridY0;
    for (int d = 1; d <= count; ++d) {
        std::snprintf(buf, sizeof(buf), "%d", d);
        if (d == day) {
            // 高亮当前日（原版 fillRect(v5-10, v6-6, 0x14, 16, 5345644) + 白字）
            surface.fillRect(ox + x - 10, oy + y - 6, 0x14, 16, rgb888To555(kHighlightColor888));
            text.setFont(15, 0xFFFFFF, 0x101010, kTextStyleBold, 0);
            text.drawText(surface, buf, ox + x, oy + y, 2);
            text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 0);
        } else {
            text.drawText(surface, buf, ox + x, oy + y, 2);
        }
        if (x == kWrapX) {
            x = kGridX0;
            y += kCellH;
        } else {
            x += kCellW;
        }
    }

    // 按钮文本（帧 2 背景自带按钮图形，原版 sub_4119E3 绘制一次）
    for (int i = 0; i < 3; ++i) {
        text.drawText(surface, kButtons[i], ox + kButtonTextX[i], oy + kButtonTextY, 2);
    }
}

// 控件操作（对应 0x410AC3 WM_LBUTTONUP 的 switch (dword_474D78)）
void applyDateControl(DateDialogState& state, int control, int rx, int ry) {
    int day = static_cast<int>(state.date & 0xFF);
    int month = static_cast<int>((state.date >> 8) & 0xFF);
    int year = static_cast<int>((state.date >> 16) & 0xFFFF);
    switch (control) {
        case 0: // 月-1（1-12 回绕）
            if (--month == 0) {
                month = 12;
            }
            break;
        case 1: // 月+1（左列下，原版 case 1）
            if (++month == 13) {
                month = 1;
            }
            break;
        case 2: // 年-1（右列上，原版 case 2，最小 1998 = 0x7CE）
            if (year > 1998) {
                --year;
            }
            break;
        case 3: // 年+1
            ++year;
            break;
        case 4: // 重置为当前日期
            state.date = state.initialDate;
            return;
        case 5: // 取消
            state.app->events().requestExit(0);
            return;
        case 6: // 確定
            state.app->events().requestExit(static_cast<int>(state.date));
            return;
        case 7: { // 日历点击（±10 x, ±8 y）
            const int count = daysInMonth(year, month);
            int x = kGridX0 + kCellW * firstWeekday(year, month);
            int y = kGridY0;
            for (int d = 1; d <= count; ++d) {
                if (rx >= x - 10 && rx < x + 10 && ry >= y - 8 && ry < y + 8) {
                    day = d;
                    break;
                }
                if (x == kWrapX) {
                    x = kGridX0;
                    y += kCellH;
                } else {
                    x += kCellW;
                }
            }
            break;
        }
        default:
            return;
    }
    state.date = static_cast<uint32_t>(day) | (static_cast<uint32_t>(month) << 8) |
                 (static_cast<uint32_t>(year) << 16);
}

// [RE 0x410AC3] WM_LBUTTONDOWN 按下态：箭头帧 12/13 画在控件矩形左上；
//               按钮帧 14 + 文本（位置 +1,+1）
void drawPressedControl(DateDialogState& state, int control) {
    Surface& surface = state.app->surface();
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;
    const ControlRect& r = kControls[control];
    if (control <= 3) {
        const int frame = (control == 0 || control == 2) ? kFrameArrowH : kFrameArrowV;
        blitElementOpaque(surface, ui.frame(frame), ox + r.left, oy + r.top);
    } else if (control >= 4 && control <= 6) {
        blitElementOpaque(surface, ui.frame(kFrameButton), ox + r.left, oy + r.top);
        TextRenderer& text = state.app->text();
        text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 0);
        text.drawText(surface, kButtons[control - 4], ox + kButtonTextX[control - 4] + 1,
                      oy + kButtonTextY + 1, 2);
    }
}

// [RE 0x410AC3] dateDialogWndProc
bool dateDialogEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<DateDialogState*>(user);

    if (!event) {
        // [RE 0x410AC3] WM_USER+1(1025)：重置工作值 + 居中 + 绘制
        state.date = state.initialDate;
        state.pressed = -1;
        redrawDateDialog(state);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int rx = static_cast<int>(event->button.x) - state.originX;
        const int ry = static_cast<int>(event->button.y) - state.originY;
        if (event->button.button == SDL_BUTTON_LEFT) {
            const int hit = hitTest(rx, ry);
            if (hit >= 0) {
                state.app->audio().playEffect(1); // [RE 0x410C42] unk_482322 点击音效
            }
            if (hit == 7) {
                // [RE 0x410AC3] case 7：日历点击（Down 立即执行）
                applyDateControl(state, hit, rx, ry);
                redrawDateDialog(state);
            } else if (hit >= 0) {
                // [RE 0x410AC3] 其余控件：Down 画按下态，Up 执行动作
                state.pressed = hit;
                drawPressedControl(state, hit);
            }
            return true;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        // [RE 0x410AC3] WM_RBUTTONUP(0x205)：取消（postModalExit(-1)）
        state.app->events().requestExit(0);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x410AC3] WM_LBUTTONUP(0x202)：执行按下控件的动作
        const int rx = static_cast<int>(event->button.x) - state.originX;
        const int ry = static_cast<int>(event->button.y) - state.originY;
        const int pressed = state.pressed;
        state.pressed = -1;
        if (pressed >= 0 && pressed != 7) {
            applyDateControl(state, pressed, rx, ry);
            redrawDateDialog(state);
        }
        return true;
    }

    return false;
}

} // namespace

uint32_t dateDialog(Application& app, uint32_t currentDate) {
    // [RE 0x4119E3] 依据: _dos_getdate 取当前日期 → dword_48BB5C（重置目标）;
    // dword_48BB84 = dword_48BB50（上次确认日期，工作值）
    DateDialogState state;
    state.app = &app;
    state.initialDate = systemDate();
    state.date = currentDate != 0 ? currentDate : state.initialDate;

    UiImage uiImage;
    if (auto blob = app.gameState().data.read(3)) {
        uiImage.load(std::move(*blob));
    } else {
        RICH4_LOGE("dateDialog: Data.mkf[3] unavailable");
        return 0;
    }
    state.ui = &uiImage;
    if (uiImage.frameCount() <= kFrameButton) {
        return 0;
    }

    // 对话框居中（原版 v11 = 320 - 帧2宽/2, v12 = 240 - 帧2高/2）
    state.originX = 320 - uiImage.frame(kFrameBg).width / 2;
    state.originY = 240 - uiImage.frame(kFrameBg).height / 2;

    redrawDateDialog(state);
    RICH4_LOGI("date dialog at (%d,%d) date=%u-%u-%u (RE 0x4119E3)", state.originX, state.originY,
               (currentDate >> 16) & 0xFFFF, (currentDate >> 8) & 0xFF, currentDate & 0xFF);
    // [RE 0x4018E7] runModal(sub_410AC3)
    return static_cast<uint32_t>(runModal(app, &dateDialogEventHandler, &state));
}

} // namespace rich4
