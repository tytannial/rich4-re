#include "game/app/settings_dialog.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {

constexpr int kControlCount = 16;
constexpr size_t kCfgSize = 72; // 16 字节设置 + 56 字节键位（sub_411F80）

// [RE 0x474B92] dword_474B92/B96/B9A/B9E 控件命中矩形 (left, top, right, bottom)
struct ControlRect {
    int left;
    int top;
    int right;
    int bottom;
};
constexpr ControlRect kControls[kControlCount] = {
    {81, 17, 128, 33},    // 0  游戏速度滑块
    {89, 81, 152, 97},    // 1  音乐音量滑块
    {89, 113, 152, 129},  // 2  音效音量滑块
    {227, 14, 327, 49},   // 3  页按钮 1
    {227, 68, 327, 103},  // 4  页按钮 2
    {227, 119, 327, 154}, // 5  页按钮 3
    {18, 226, 177, 345},  // 6  音乐列表
    {194, 314, 256, 344}, // 7  取消
    {266, 314, 328, 344}, // 8  確定
    {98, 50, 113, 65},    // 9  复选（自动存档）
    {66, 82, 81, 97},     // 10 音乐静音开关
    {66, 114, 81, 129},   // 11 音效静音开关
    {98, 146, 113, 161},  // 12 复选
    {217, 214, 324, 236}, // 13 单选 1
    {217, 246, 324, 268}, // 14 单选 2
    {217, 278, 324, 300}, // 15 单选 3
};

// [RE 0x474B38] word_474B38 标签位置 (x, y, align)
struct LabelPos {
    int x;
    int y;
    int align;
};
constexpr LabelPos kLabelPos[10] = {
    {14, 25, 5},  {14, 58, 5},   {14, 90, 5},   {14, 122, 5},  {14, 155, 5},
    {49, 202, 2}, {209, 202, 2}, {286, 226, 2}, {286, 258, 2}, {286, 290, 2},
};

// [RE 0x474A54] 设置项标签（BIG5 原文）
const char* const kLabels[10] = {
    "遊戲速度", "動畫過程", "音 樂",   "音 效",     "自動存檔",
    "音 量",   "音 量",   "日、月、", "縮小地圖", "組合畫面",
};

// [RE 0x474A9C] 音乐列表（8 首曲目）
const char* const kMusicNames[8] = {
    "1.星際總動員", "2.重回侏儸紀", "3.夢幻伊甸園", "4.打拼為將來",
    "5.椰林風情畫", "6.浪漫月世界", "7.熱情的夏天", "8.漫步星空下",
};

// [RE 0x474B2C] 键位页画布（帧 1）的页标签（原始設定 / 取消 / 確定）
const char* const kPageLabels[3] = {"原始設定", "取 消", "確 定"};

// [RE 0x463590/463599/4635A2] off_474A54 索引 12-14：页按钮文本（帧 page+10 画布）
// 位置 word_474B38[3*j] (j=12..14) = (108,31)/(108,85)/(108,136)
const char* const kPageButtonText[3] = {"日期更改", "熱鍵設定", "遊戲說明"};
struct PageButtonPos {
    int x;
    int y;
};
constexpr PageButtonPos kPageButtonPos[3] = {{108, 31}, {108, 85}, {108, 136}};

// [RE 0x474C92] word_474C92 单选 y 位置
constexpr int kRadioY[3] = {218, 250, 281};

// [RE 0x40FD49] 滑块单位帧与复选标记帧。
// 依据: 元素偏移 = 12 + 12*i；dword_48BB60+72 → 帧 5（15x16 滑块单位）;
//       dword_48BB60+120 → 帧 9（16x16 复选标记）
constexpr int kSliderFrame = 5;
constexpr int kCheckFrame = 9;

struct SettingsState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    uint8_t settings[16] = {}; // byte_48BB48 工作副本
    int hoverControl = -1;     // dword_474D74
    int dragControl = -1;      // 拖动中的滑块
    int originX = 0;           // dword_48BB74 对话框 x
    int originY = 0;           // dword_48BB78 对话框 y
    int page = 0;              // dword_48BB58 当前页
    int currentTrack = 1;      // sub_454F5B 当前曲目（1-8）
};

// 读 RICH4.CFG 全部 72 字节（保留键位表）
bool loadCfg(const std::string& path, uint8_t out[kCfgSize]) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    const size_t got = std::fread(out, 1, kCfgSize, fp);
    std::fclose(fp);
    return got == kCfgSize;
}

// [RE 0x411F80] 写 RICH4.CFG（16 字节设置 + 56 字节键位）
bool saveCfg(const std::string& path, const uint8_t settings[16]) {
    uint8_t cfg[kCfgSize] = {};
    loadCfg(path, cfg); // 保留现有键位表
    std::memcpy(cfg, settings, 16);
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        return false;
    }
    const size_t wrote = std::fwrite(cfg, 1, kCfgSize, fp);
    std::fclose(fp);
    return wrote == kCfgSize;
}

// 命中检测（相对对话框坐标）
int hitTest(int rx, int ry) {
    for (int i = 0; i < kControlCount; ++i) {
        const ControlRect& r = kControls[i];
        if (rx >= r.left && rx < r.right && ry >= r.top && ry < r.bottom) {
            return i;
        }
    }
    return -1;
}

void redrawSettings(SettingsState& state) {
    Surface& surface = state.app->surface();
    if (!state.ui) {
        return;
    }
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;
    TextRenderer& text = state.app->text();

    // [RE 0x40FD49] 背景帧 0
    blitElement(surface, ui.frame(0), ox, oy, false);
    // [RE 0x411B53] 页标签元素（帧 page+10）叠加到背景 (168,2)
    // 原版: blitBackground(帧0, 12*(page+10)+资源+12, 168, 2)
    const int pageFrame = 10 + state.page;
    if (pageFrame < ui.frameCount()) {
        blitElement(surface, ui.frame(pageFrame), ox + 168, oy + 2, false);
    }

    // 速度滑块：帧 6，数量 settings[0]+1，16px 间隔，起点 (81,17)
    for (int i = 0; i <= state.settings[0] && i < 4; ++i) {
        blitElement(surface, ui.frame(kSliderFrame), ox + 81 + i * 16, oy + 17, false);
    }
    // [RE 0x40FD49] 動畫過程开关（settings[1]）：帧 9 在 (98,50)
    if (state.settings[1]) {
        blitElement(surface, ui.frame(kCheckFrame), ox + 98, oy + 50, false);
    }
    // 音乐音量滑块：数量 settings[2]，起点 (89,81)
    for (int i = 0; i < state.settings[2] && i < 4; ++i) {
        blitElement(surface, ui.frame(kSliderFrame), ox + 89 + i * 16, oy + 81, false);
    }
    // 音效音量滑块：数量 settings[3]，起点 (89,113)
    for (int i = 0; i < state.settings[3] && i < 4; ++i) {
        blitElement(surface, ui.frame(kSliderFrame), ox + 89 + i * 16, oy + 113, false);
    }
    // [RE 0x40FD49] 自動存檔开关（settings[4]）：帧 9 在 (98,146)
    if (state.settings[4]) {
        blitElement(surface, ui.frame(kCheckFrame), ox + 98, oy + 146, false);
    }
    // 单选 settings[5]：帧 10 在 (218, kRadioY[i])
    if (state.settings[5] < 3) {
        blitElement(surface, ui.frame(kCheckFrame), ox + 218,
                    oy + kRadioY[state.settings[5]], false);
    }

    // [RE 0x40FC57] 音乐列表：从帧 0 拷贝列表区域背景 (18,226) 159x120
    blitElementRegionOpaque(surface, ui.frame(0), ox + 18, oy + 226, 18, 226, 159, 120, false);
    // 8 项文本 + 当前曲目红色高亮（0xFF0000）
    text.setFont(12, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    for (int i = 0; i < 8; ++i) {
        const int y = oy + 233 + i * 15;
        if (i + 1 == state.currentTrack) {
            surface.fillRect(ox + 18, y - 7, 0x9F, 14, rgb888To555(0xFF0000));
        }
        text.drawText(surface, kMusicNames[i], ox + 26, y, 5);
    }

    // 设置项标签（原版 setTextFont(15, 0x101010, 0, 2, 1)）
    text.setFont(15, 0x101010, 0, kTextStyleBold, 1);
    for (int i = 0; i < 10; ++i) {
        text.drawText(surface, kLabels[i], ox + kLabelPos[i].x, oy + kLabelPos[i].y,
                      kLabelPos[i].align);
    }

    // [RE 0x4103A3] 页按钮悬停高亮：帧 6（101x36，dword_48BB60+84）
    // 原版: case 3/4/5 点击时 sub_456418(帧 6) 画到控件位置
    if (state.hoverControl >= 3 && state.hoverControl <= 5 && ui.frameCount() > 6) {
        const ControlRect& r = kControls[state.hoverControl];
        blitElement(surface, ui.frame(6), ox + r.left, oy + r.top, false);
    }

    // [RE 0x411B53] 页按钮文本（字号 20）：画在帧 (page+10) 画布上，帧 10 位于 (168,2)
    // 原版: setTextFont(20, 0x101010, 0x101010, 2, 1) + drawText(帧(page+10), 索引 3*page+12..14)
    text.setFont(20, 0x101010, 0x101010, kTextStyleBold, 1);
    for (int i = 0; i < 3; ++i) {
        text.drawText(surface, kPageButtonText[i], ox + 168 + kPageButtonPos[i].x,
                      oy + 2 + kPageButtonPos[i].y, 2);
    }

    // [RE 0x4103A3] 取消/確定悬停按下效果：帧 3/4（62x30）
    // 原版: case 7/8 点击时 sub_456418(帧 dword_474D74-4) 画到按钮位置
    if (ui.frameCount() > 4) {
        if (state.hoverControl == 7) {
            blitElement(surface, ui.frame(3), ox + 194, oy + 314, false);
        } else if (state.hoverControl == 8) {
            blitElement(surface, ui.frame(4), ox + 266, oy + 314, false);
        }
    }

    // [RE 0x411B53] 取消/確定文本（字号 20，位置 word_474B74/B7A = (224,328)/(296,328)）
    // 原版: drawText(帧0, off_474A7C/A80, 224/296, 328, 2)；帧 0 背景自带按钮图形
    text.drawText(surface, kPageLabels[1], ox + 224, oy + 328, 2);
    text.drawText(surface, kPageLabels[2], ox + 296, oy + 328, 2);
}

// 控件操作（对应 0x4103A3 的 switch (dword_474D74)）
void applyControl(SettingsState& state, int control, int rx, int ry) {
    const auto clamp = [](int v, int lo, int hi) { return std::min(std::max(v, lo), hi); };
    switch (control) {
        case 0: // 速度滑块：byte_48BB48 = (rx - 81) / 16
            state.settings[0] = static_cast<uint8_t>(clamp((rx - 81) / 16, 0, 3));
            break;
        case 1: // 音乐音量：byte_48BB4A = (rx - 89) / 16 + 1
            state.settings[2] = static_cast<uint8_t>(clamp((rx - 89) / 16 + 1, 0, 4));
            break;
        case 2: // 音效音量：byte_48BB4B = (rx - 89) / 16 + 1
            state.settings[3] = static_cast<uint8_t>(clamp((rx - 89) / 16 + 1, 0, 4));
            break;
        case 6: { // 音乐列表：行号 → 曲目
            const int index = (ry - 233) / 15;
            if (index >= 0 && index < 8) {
                state.currentTrack = index + 1;
                // TODO(RE 0x454D91): 播放选中曲目（MIDI 迁移后接入）
            }
            break;
        }
        case 7: // 取消（不保存）
            state.app->events().requestExit(0);
            break;
        case 8: // 確定：memcpy(byte_497158) + sub_411F80 写 RICH4.CFG
            saveCfg(state.app->gameDir() + "/RICH4.CFG", state.settings);
            RICH4_LOGI("settings saved (RE 0x411F80)");
            state.app->events().requestExit(1);
            break;
        case 9: // byte_48BB49 ^= 1（動畫過程开关）
            state.settings[1] ^= 1;
            break;
        case 10: // byte_48BB4A toggle 0/4（音乐静音）
            state.settings[2] = state.settings[2] ? 0 : 4;
            break;
        case 11: // byte_48BB4B toggle 0/4（音效静音）
            state.settings[3] = state.settings[3] ? 0 : 4;
            break;
        case 12: // byte_48BB4C ^= 1（自動存檔开关）
            state.settings[4] ^= 1;
            break;
        case 13:
        case 14:
        case 15: // byte_48BB4D = i - 13（单选）
            state.settings[5] = static_cast<uint8_t>(control - 13);
            break;
        case 3:
        case 4:
        case 5:
            // [RE 0x4103A3] case 3/4/5 → funcs_410935[control-3]
            // TODO(RE 0x4119E3/0x411A86/0x411A96): 日期更改/熱鍵設定/遊戲說明 子界面
            RICH4_LOGW("TODO: settings page %d (RE 0x4119E3/0x411A86/0x411A96)", control - 3);
            break;
        default:
            break;
    }
}

// [RE 0x4103A3] settingsWndProc
// 依据: 0x4103A3 反编译; WM_MOUSEMOVE(515) 查找控件矩形 dword_474B92 并处理;
//       WM_LBUTTONDOWN(513) 分发控件操作; WM_USER+1 重置悬停并复制设置
bool settingsEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<SettingsState*>(user);

    if (!event) {
        // [RE 0x4103A3] WM_USER+1(1025): dword_474D74 = -1 + 重绘
        state.hoverControl = -1;
        redrawSettings(state);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int rx = static_cast<int>(event->motion.x) - state.originX;
        const int ry = static_cast<int>(event->motion.y) - state.originY;
        if (state.dragControl >= 0) {
            applyControl(state, state.dragControl, rx, ry);
            redrawSettings(state);
            return true;
        }
        const int hit = hitTest(rx, ry);
        if (hit != state.hoverControl) {
            // [RE 0x4103A3] 悬停变化：重绘高亮（帧 6 页按钮 / 帧 3-4 按钮）
            state.hoverControl = hit;
            redrawSettings(state);
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        const int rx = static_cast<int>(event->button.x) - state.originX;
        const int ry = static_cast<int>(event->button.y) - state.originY;
        const int hit = hitTest(rx, ry);
        if (hit >= 0) {
            state.hoverControl = hit;
            applyControl(state, hit, rx, ry);
            if (hit >= 0 && hit <= 2) {
                state.dragControl = hit; // 滑块开始拖动
            }
            redrawSettings(state);
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (state.dragControl >= 0) {
            state.dragControl = -1;
            return true;
        }
    }

    return false;
}

} // namespace

int settingsDialog(Application& app, int page) {
    // [RE 0x411B53] settingsDialog
    // 依据: 0x411B53 反编译; dword_48BB58 = page; sub_450441(data.mkf, 3);
    //       对话框居中 (320 - 帧0宽/2, 240 - 帧0高/2); runModal(sub_4103A3)
    SettingsState state;
    state.app = &app;
    state.page = page;

    UiImage uiImage;
    if (auto blob = app.gameState().data.read(3)) {
        uiImage.load(std::move(*blob));
    } else {
        RICH4_LOGE("settingsDialog: Data.mkf[3] unavailable");
        return 0;
    }
    if (uiImage.frameCount() == 0) {
        return 0;
    }
    state.ui = &uiImage;

    // 对话框位置（原版 v11 = 320 - 帧0宽/2, v12 = 240 - 帧0高/2）
    state.originX = 320 - uiImage.frame(0).width / 2;
    state.originY = 240 - uiImage.frame(0).height / 2;

    // 复制当前设置（原版 WM_USER+1: memcpy(&byte_48BB48, &byte_497158, 16)）
    uint8_t cfg[kCfgSize] = {};
    if (loadCfg(app.gameDir() + "/RICH4.CFG", cfg)) {
        std::memcpy(state.settings, cfg, 16);
    }

    redrawSettings(state);
    RICH4_LOGI("settings dialog: page=%d at (%d,%d) (RE 0x411B53)", page, state.originX,
               state.originY);
    return runModal(app, &settingsEventHandler, &state);
}

} // namespace rich4
