#include "game/app/hotkey_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/paths.h"
#include "game/platform/input.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {

constexpr int kKeyCount = 28;
constexpr size_t kCfgSize = 72; // 16 设置 + 56 键位（sub_411F80）

// [RE 0x47EDFA] 可选按键表（78 项，VK → 名称；方向键原版为 BIG5 A1F4-A1F7）
struct VkName {
    uint8_t vk;
    const char* name;
};
constexpr VkName kKeyNames[] = {
    {0x08, "BS"},   {0x09, "TAB"},   {0x0D, "ENTER"}, {0x11, "CTRL-"}, {0x1B, "ESC"},
    {0x20, "SPACE"}, {0x21, "PG UP"}, {0x22, "PG DN"}, {0x23, "END"},  {0x24, "HOME"},
    {0x25, "←"},    {0x26, "↑"},     {0x27, "→"},     {0x28, "↓"},     {0x2D, "INS"},
    {0x30, "0"},    {0x31, "1"},     {0x32, "2"},     {0x33, "3"},     {0x34, "4"},
    {0x35, "5"},    {0x36, "6"},     {0x37, "7"},     {0x38, "8"},     {0x39, "9"},
    {0x41, "A"},    {0x42, "B"},     {0x43, "C"},     {0x44, "D"},     {0x45, "E"},
    {0x46, "F"},    {0x47, "G"},     {0x48, "H"},     {0x49, "I"},     {0x4A, "J"},
    {0x4B, "K"},    {0x4C, "L"},     {0x4D, "M"},     {0x4E, "N"},     {0x4F, "O"},
    {0x50, "P"},    {0x51, "Q"},     {0x52, "R"},     {0x53, "S"},     {0x54, "T"},
    {0x55, "U"},    {0x56, "V"},     {0x57, "W"},     {0x58, "X"},     {0x59, "Y"},
    {0x5A, "Z"},    {0x6A, "*"},     {0x6B, "+"},     {0x6D, "-"},     {0x6F, "/"},
    {0x70, "F1"},   {0x71, "F2"},    {0x72, "F3"},    {0x73, "F4"},    {0x74, "F5"},
    {0x75, "F6"},   {0x76, "F7"},    {0x77, "F8"},    {0x78, "F9"},    {0x79, "F10"},
    {0x7A, "F11"},  {0x7B, "F12"},   {0xBA, ";"},     {0xBB, "="},     {0xBC, "<"},
    {0xBD, "-"},    {0xBE, ">"},     {0xBF, "?"},     {0xC0, "~"},     {0xDB, "["},
    {0xDC, "\\"},   {0xDD, "]"},     {0xDE, "'"},
};

const char* keyName(uint8_t vk) {
    for (const VkName& entry : kKeyNames) {
        if (entry.vk == vk) {
            return entry.name;
        }
    }
    return nullptr;
}

// [RE 0x46362E] 28 项功能名（与键位一一对应；原版 settingsDialog 预画到帧 1 画布，
//               左列 x=62 / 右列 x=208，12 号白色 0xF0F0F0；0x474ABC 指针表）
const char* const kHotkeyLabels[kKeyCount] = {
    "游標上移",  "游標右移", "游標下移", "游標左移",   "確定執行", "取消指令", "切換選項",
    "切換視窗組", "是<YES>", "否<NO>",  "前進指令",   "選擇骰子數", "股市",   "交易",
    "卡片",      "道具",    "查詢",    "地圖",       "地圖向左旋轉", "地圖向右旋轉", "託管",
    "系統",      "SAVE GAME", "LOAD GAME", "輔助說明", "向上換頁", "向下換頁", "結束程式",
};

// 写 RICH4.CFG（[RE 0x411F80] fwrite 16B 设置 + 56B 键位）
bool saveCfgRaw(const std::string& path, const uint8_t cfg[kCfgSize]) {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        return false;
    }
    const size_t wrote = std::fwrite(cfg, 1, kCfgSize, fp);
    std::fclose(fp);
    return wrote == kCfgSize;
}

struct HotkeyDialogState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    uint16_t working[kKeyCount] = {}; // word_48BB10（与 word_48BB0E[1..28] 重叠）
    int editing = -1;                 // dword_48BBA6 - 1（捕获中键位，-1 无）
    uint16_t savedValue = 0;          // word_48BB8C（捕获前原值，右键取消恢复）
    int originX = 0;                  // dword_48BB8E
    int originY = 0;                  // dword_48BB92
    bool blink = false;               // dword_48BBAA（WM_TIMER 250ms 翻转的闪烁态）
    int pressed = -1;                 // dword_48BB9E：按住中的控件（0..27 列表项 / 100..102 按钮 / -1 无）
};

// 点击区（原版 sub_411122 WM_LBUTTONDOWN 的坐标范围，相对对话框）
bool hitLeftColumn(int rx, int ry, int& row) {
    if (rx >= 105 && rx <= 160 && ry >= 25 && ry <= 264) {
        row = (ry - 25) / 16;
        return true;
    }
    return false;
}

bool hitRightColumn(int rx, int ry, int& row) {
    if (rx >= 257 && rx <= 312 && ry >= 25 && ry <= 264) {
        row = (ry - 25) / 16;
        return true;
    }
    return false;
}

void redrawHotkeys(HotkeyDialogState& state) {
    Surface& surface = state.app->surface();
    if (!state.ui) {
        return;
    }
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;

    // [RE 0x410158] 帧 1 背景
    blitElement(surface, ui.frame(1), ox, oy, false);

    TextRenderer& text = state.app->text();

    // [RE 0x411B53] 28 项功能名（原版 settingsDialog 初始化时预画到帧 1 画布）
    text.setFont(12, 0xF0F0F0, 0x101010, kTextStyleShadow, 0);
    for (int i = 0; i < kKeyCount; ++i) {
        const bool left = i < 15;
        text.drawText(surface, kHotkeyLabels[i], ox + (left ? 62 : 208),
                      oy + 33 + 16 * (left ? i : i - 15), 2);
    }

    for (int i = 0; i < kKeyCount; ++i) {
        const bool left = i < 15;
        const int x = ox + (left ? 132 : 284);
        const int y = oy + 33 + 16 * (left ? i : i - 15);
        // [RE 0x410158] 颜色：键位 0-7 青色 0x00F0F0，8-27 黄色 0xF0F000（style 1 阴影）
        const uint32_t color = (i < 8) ? 61680u : 15790080u;
        // [RE 0x411122] 编辑中闪烁框 53x13（WM_TIMER 250ms 翻转 dword_48BBAA，blink 控制）
        if (i == state.editing && state.blink) {
            surface.fillRect(x - 26, y - 7, 53, 13, rgb888To555(0xF0F0F0));
        }
        char buf[32] = {};
        const uint16_t value = state.working[i];
        const uint8_t mod = static_cast<uint8_t>(value >> 8);
        const uint8_t key = static_cast<uint8_t>(value);
        if (mod == 0x11) {
            std::strcat(buf, "CTRL-");
        }
        if (key != 0) {
            const char* name = keyName(key);
            if (name) {
                std::strcat(buf, name);
            }
        }
        if (buf[0] != '\0') {
            text.setFont(12, color, 0x101010, kTextStyleShadow, 0);
            text.drawText(surface, buf, x, y, 2);
        }
        // [RE 0x451B9E] 按住列表项（行矩形 55×16）：整体下沉（含已绘数值）+ 顶/左阴影
        if (i == state.pressed) {
            pressDown(surface, x - 27, y - 8, 55, 16, 1, kChannelHalf);
        }
    }

    // 按钮文本（原版 sub_411B53 画到帧 1 画布：(52/165/278, 296)）
    text.setFont(12, 0x101010, 0x101010, kTextStyleBold, 0);
    text.drawText(surface, "原始設定", ox + 52, oy + 296, 2);
    text.drawText(surface, "取 消", ox + 165, oy + 296, 2);
    text.drawText(surface, "確 定", ox + 278, oy + 296, 2);

    // [RE 0x451B9E] 按住按钮（原始設定/取消/確定，矩形 70×30）：内容下沉 + 顶/左阴影
    if (state.pressed >= 100 && state.pressed <= 102) {
        static const int kBtnRect[3][4] = {
            {17, 281, 70, 30}, {130, 281, 70, 30}, {242, 281, 70, 30}};
        const int bi = state.pressed - 100;
        pressDown(surface, ox + kBtnRect[bi][0], oy + kBtnRect[bi][1], kBtnRect[bi][2],
                  kBtnRect[bi][3], 1, kChannelHalf);
    }
}

// 按键捕获（对应 0x411122 WM_KEYDOWN 0x100）
void handleKeyCapture(HotkeyDialogState& state, SDL_Keycode key) {
    const uint8_t vk = sdlKeycodeToVk(key);
    if (vk == 0) {
        return;
    }
    const int index = state.editing;
    if (index < 0 || index >= kKeyCount) {
        return;
    }
    if (vk == 0x11) {
        // [RE 0x411122] CTRL 键：word_48BB0E[idx] = 4352 (0x1100)，继续等待主键
        state.working[index] = 0x1100;
        return;
    }
    // [RE 0x411122] 冲突检测：候选值 = VK | 当前编辑值，与其余键位比较
    const uint16_t candidate = static_cast<uint16_t>(state.working[index] | vk);
    for (int j = 0; j < kKeyCount; ++j) {
        if (j != index && state.working[j] == candidate) {
            // 原版冲突时仅重绘并保持编辑状态（不退出捕获）
            RICH4_LOGW("hotkey conflict: %s used by key %d", keyName(vk) ? keyName(vk) : "?", j);
            return;
        }
    }
    state.working[index] = candidate;
    state.editing = -1;
    RICH4_LOGI("hotkey %d set to 0x%04X (RE 0x411122)", index, candidate);
}

// 按钮（原版坐标范围，相对对话框）
enum class ButtonHit { None, Reset, Cancel, Confirm };

ButtonHit hitButton(int rx, int ry) {
    if (ry < 281 || ry > 311) {
        return ButtonHit::None;
    }
    if (rx >= 17 && rx <= 87) {
        return ButtonHit::Reset;
    }
    if (rx >= 130 && rx <= 200) {
        return ButtonHit::Cancel;
    }
    if (rx >= 242 && rx <= 312) {
        return ButtonHit::Confirm;
    }
    return ButtonHit::None;
}

// [RE 0x411122] hotkeyWndProc
bool hotkeyEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<HotkeyDialogState*>(user);

    if (!event) {
        // [RE 0x411122] WM_USER+1(1025)：复制当前键位 + 居中 + 绘制
        std::memcpy(state.working, state.app->input().bindings(),
                    kKeyCount * sizeof(uint16_t));
        state.editing = -1;
        state.blink = false;
        redrawHotkeys(state);
        return true;
    }

    if (event->type == kModalTimerEvent) {
        // [RE 0x411122] WM_TIMER(0x113)：编辑中翻转 dword_48BBAA → 待修改格反白闪烁
        if (state.editing >= 0) {
            state.blink = !state.blink;
            redrawHotkeys(state);
        }
        return true;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && state.editing >= 0) {
        handleKeyCapture(state, event->key.key);
        redrawHotkeys(state);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x411122] WM_LBUTTONDOWN：命中列表项/按钮 → 记 dword_48BB9E + 音效 + highlightRect 按下态
        const int rx = static_cast<int>(event->button.x) - state.originX;
        const int ry = static_cast<int>(event->button.y) - state.originY;

        int row = -1;
        int index = -1;
        if (hitLeftColumn(rx, ry, row)) {
            index = row;
        } else if (hitRightColumn(rx, ry, row)) {
            index = row + 15;
        }
        if (index >= 0) {
            if (state.editing >= 0) {
                // [RE 0x411122] 捕获中（dword_48BBA6 非零）否决列表命中 → 禁止切换编辑目标
                return true;
            }
            state.pressed = index;
            state.app->audio().playEffect(1); // unk_482322 列表点击音效
            redrawHotkeys(state);
            return true;
        }

        const ButtonHit b = hitButton(rx, ry);
        if (b != ButtonHit::None) {
            state.pressed = 100 + static_cast<int>(b) - 1; // Reset=100/Cancel=101/Confirm=102
            const int sfx = (b == ButtonHit::Reset) ? 1 : (b == ButtonHit::Cancel) ? 4 : 2;
            state.app->audio().playEffect(sfx); // 482322/482332/48232A
            redrawHotkeys(state);
            return true;
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x411122] WM_LBUTTONUP：sub_451D4E 恢复按下态，再依 dword_48BB9E 执行动作
        const int pressed = state.pressed;
        state.pressed = -1;
        if (pressed < 0) {
            return true;
        }
        if (pressed < kKeyCount) {
            // 列表项：仅键位 8-27 可编辑（原版 dword_48BB9E > 8）；固定项闪一下即回
            if (pressed >= 8) {
                state.editing = pressed;
                state.blink = false;
                state.savedValue = state.working[pressed]; // word_48BB8C
                state.working[pressed] = 0;
                RICH4_LOGI("hotkey capture started: index %d", pressed);
            }
            redrawHotkeys(state);
            return true;
        }
        switch (pressed) {
            case 100: { // 原始設定：恢复默认键位（unk_47EDC2）
                state.app->input().setDefaultBindings();
                std::memcpy(state.working, state.app->input().bindings(),
                            kKeyCount * sizeof(uint16_t));
                state.editing = -1;
                redrawHotkeys(state);
                return true;
            }
            case 101: // 取消：不保存退出
                state.app->events().requestExit(0);
                return true;
            case 102: { // 確定：写回键位 + saveConfig（RE 0x411F80）
                state.app->input().setBindings(state.working);
                uint8_t cfg[kCfgSize] = {};
                std::memcpy(cfg, state.app->gameState().settings, 16);
                // byte_497158+8..11 与 dword_497160（游戏日期）重叠，随设置区一并存盘
                std::memcpy(cfg + 8, &state.app->gameState().gameDate, 4);
                std::memcpy(cfg + 16, state.working, kKeyCount * sizeof(uint16_t));
                saveCfgRaw(writableDataFile(state.app->gameDir(), "RICH4.CFG"), cfg);
                RICH4_LOGI("hotkeys saved (RE 0x411F80)");
                state.app->events().requestExit(1);
                return true;
            }
            default:
                return true;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        if (state.editing >= 0) {
            // [RE 0x411122] WM_RBUTTONUP(0x205)：捕获中恢复原值（word_48BB8C）并取消捕获
            state.working[state.editing] = state.savedValue;
            state.editing = -1;
            redrawHotkeys(state);
        } else {
            state.app->events().requestExit(0);
        }
        return true;
    }

    return false;
}

} // namespace

void hotkeyDialog(Application& app) {
    // [RE 0x411A86] 依据: runModal(sub_411122)；帧 1 居中 (320-宽/2, 240-高/2)
    HotkeyDialogState state;
    state.app = &app;

    UiImage uiImage;
    if (auto blob = app.gameState().data.read(3)) {
        uiImage.load(std::move(*blob));
    } else {
        RICH4_LOGE("hotkeyDialog: Data.mkf[3] unavailable");
        return;
    }
    if (uiImage.frameCount() < 2) {
        return;
    }
    state.ui = &uiImage;
    state.originX = 320 - uiImage.frame(1).width / 2;
    state.originY = 240 - uiImage.frame(1).height / 2;

    RICH4_LOGI("hotkey dialog at (%d,%d) (RE 0x411A86)", state.originX, state.originY);
    // [RE 0x4018E7] runModal(sub_411122)；[RE 0x411122] SetTimer(0xFA=250ms) 驱动编辑闪烁
    runModal(app, &hotkeyEventHandler, &state, 250);
}

} // namespace rich4
