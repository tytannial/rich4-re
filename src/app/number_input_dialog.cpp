#include "game/app/number_input_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x47E6D8] byte_47E6D8 按键矩形表（17 项 × 4：x/y/w/h）
constexpr uint8_t kKeyRect[17][4] = {
    {16, 11, 228, 7},  {17, 11, 228, 7},  {8, 63, 58, 25},   {64, 63, 57, 25},
    {8, 95, 33, 17},   {48, 95, 33, 17},  {88, 95, 33, 17},  {8, 119, 33, 17},
    {48, 119, 33, 17}, {88, 119, 33, 17}, {8, 143, 33, 17},  {48, 143, 33, 17},
    {88, 143, 33, 17}, {8, 167, 33, 17},  {48, 167, 33, 17}, {88, 167, 33, 17},
    {48, 48, 48, 55},
};

// [RE 0x47E714] byte_47E714 按键字符表（ID 4..15：清除/0/退格/7/8/9/4/5/6/1/2/3）
constexpr char kKeyChar[17] = {'X', 0, '!', 0, '0', '0', '0', '7', '8',
                               '9', '4', '5', '6', '1', '2', '3', 0};

// [RE 0x47E725] byte_47E725 滑块档位阈值（34 档，像素位置）
constexpr uint8_t kSliderSteps[34] = {3,  6,  9,  12, 15, 19, 22, 25, 28,  31, 34, 38,
                                      41, 44, 47, 50, 53, 57, 60, 63, 66,  69, 73, 76,
                                      79, 82, 85, 88, 92, 95, 98, 101, 104, 107};

constexpr float kSliderDivisor = 33.0f; // [RE 0x466218/0x46621C] 档位除数
constexpr int kPanelW = 128;
constexpr int kPanelH = 192;
constexpr int kPanelX = 256; // 原版默认位置 word_48CAB8/CAB6（拖动简化：固定）
constexpr int kPanelY = 144;

struct NumberInputCtx {
    Application* app = nullptr;
    UiImage panel;
    const uint8_t* indexMap = nullptr; // panel.mkf[22] 索引图（128x192，值 = 按键 ID）
    size_t indexMapSize = 0;
    int maxValue = 0;
    char text[16] = "0";
};

// [RE 0x45297E] 数字显示 + 滑块
void drawNumber(NumberInputCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    const int x = kPanelX;
    const int y = kPanelY;
    int px = x + 107;
    const int py = y + 11;
    const int len = static_cast<int>(std::strlen(ctx.text));
    int drawn = 0;
    for (int i = len - 1; i >= 0; --i) {
        const uint8_t ch = static_cast<uint8_t>(ctx.text[i]);
        if (ch >= 32) {
            const int frame = ch - 32;
            if (frame >= 0 && frame < ctx.panel.frameCount()) {
                blitElementOpaque(dst, ctx.panel.frame(static_cast<size_t>(frame)), px, py);
            }
        }
        px -= 12;
        if (++drawn >= 9) {
            break;
        }
    }
    // 滑块按钮（帧 1）+ 位置条
    if (ctx.panel.frameCount() > 1) {
        blitElementOpaque(dst, ctx.panel.frame(1), x + 10, y + 42);
    }
    const int value = std::atoi(ctx.text);
    if (value > 0 && ctx.maxValue > 0) {
        int step = static_cast<int>(static_cast<float>(value) /
                                    static_cast<float>(ctx.maxValue) * kSliderDivisor);
        if (step < 0) {
            step = 0;
        }
        if (step > 33) {
            step = 33;
        }
        // [RE 0x45297E] 原版：blitOpaqueToBackbuffer 从离屏背景恢复 (x+10,y+42) 处 w×12
        //   （效果 = 擦除轨道左段表示已选比例）；重写用帧 0 背景对应区域等价覆盖
        const int w = kSliderSteps[step];
        if (w > 0) {
            blitElementRegionOpaque(dst, ctx.panel.frame(0), x + 10, y + 42, 10, 42, w, 12, false);
        }
    }
}

void drawAll(NumberInputCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    renderGameFrame(app);
    if (ctx.panel.frameCount() > 0) {
        blitElementOpaque(dst, ctx.panel.frame(0), kPanelX, kPanelY);
    }
    drawNumber(ctx);
}

bool numberInputHandler(const SDL_Event* event, void* user) {
    NumberInputCtx& ctx = *static_cast<NumberInputCtx*>(user);
    Application& app = *ctx.app;
    auto clampText = [&]() {
        if (ctx.text[0] == '\0') {
            std::strcpy(ctx.text, "0");
        }
        const long v = std::strtol(ctx.text, nullptr, 10);
        if (v > ctx.maxValue) {
            std::snprintf(ctx.text, sizeof(ctx.text), "%d", ctx.maxValue);
        }
    };
    if (event == nullptr) {
        drawAll(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode key = event->key.key;
        if (key >= SDLK_0 && key <= SDLK_9) {
            const char c = static_cast<char>('0' + (key - SDLK_0));
            const size_t len = std::strlen(ctx.text);
            if (len < 9) {
                if (len == 1 && ctx.text[0] == '0') {
                    ctx.text[0] = c;
                    ctx.text[1] = '\0';
                } else {
                    ctx.text[len] = c;
                    ctx.text[len + 1] = '\0';
                }
            }
            clampText();
        } else if (key == SDLK_BACKSPACE) {
            const size_t len = std::strlen(ctx.text);
            if (len > 1) {
                ctx.text[len - 1] = '\0';
            } else {
                std::strcpy(ctx.text, "0");
            }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            clampText();
            app.events().requestExit(std::atoi(ctx.text));
        } else if (key == SDLK_ESCAPE) {
            app.events().requestExit(-1);
        }
        drawAll(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        int mx = 0;
        int my = 0;
        app.mouseLogicalPos(mx, my);
        const int rx = mx - kPanelX;
        const int ry = my - kPanelY;
        if (rx < 0 || rx >= kPanelW || ry < 0 || ry >= kPanelH || ctx.indexMap == nullptr ||
            ctx.indexMapSize < static_cast<size_t>(kPanelW * kPanelH)) {
            return true;
        }
        const uint8_t key = ctx.indexMap[ry * kPanelW + rx];
        RICH4_LOGI("number input click: rx=%d ry=%d key=%u", rx, ry, key);
        switch (key) {
            case 1:
                break; // 拖动区（简化：固定位置）
            case 2: // 最大
                std::snprintf(ctx.text, sizeof(ctx.text), "%d", ctx.maxValue);
                break;
            case 3: // 确定
                clampText();
                app.events().requestExit(std::atoi(ctx.text));
                return true;
            case 4: // 清除
                std::strcpy(ctx.text, "0");
                break;
            case 5: // 数字 0
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15: {
                const char c = kKeyChar[key];
                const size_t len = std::strlen(ctx.text);
                if (len < 9) {
                    if (len == 1 && ctx.text[0] == '0' && c != '0') {
                        ctx.text[0] = c;
                        ctx.text[1] = '\0';
                    } else {
                        ctx.text[len] = c;
                        ctx.text[len + 1] = '\0';
                    }
                }
                clampText();
                break;
            }
            case 6: { // 退格
                const size_t len = std::strlen(ctx.text);
                if (len > 1) {
                    ctx.text[len - 1] = '\0';
                } else {
                    std::strcpy(ctx.text, "0");
                }
                break;
            }
            case 16: { // 滑块：位置 → 档位 → 值 = 上限 × 档位/33
                const int rel = rx - 10;
                int step = 0;
                while (step < 33 && rel > kSliderSteps[step]) {
                    ++step;
                }
                const int v = static_cast<int>(static_cast<float>(ctx.maxValue) *
                                               (static_cast<float>(step) / kSliderDivisor));
                std::snprintf(ctx.text, sizeof(ctx.text), "%d", v);
                clampText();
                break;
            }
            default:
                break;
        }
        drawAll(ctx);
        return true;
    }
    return true; // 模态：吞掉其他事件
}

} // namespace

// [RE 0x453544] numberInputDialog
// 依据: 0x453544 反编译（panel[21] 背景/滑块/按键 + panel[22] 索引图 + runModal(sub_452C02)）;
//   上限 clamp（超限 → 上限，LABEL_71）、"最大"= 上限、"清除"归 0、退格、滑块按档位比例
// 差异: 拖动面板（word_48CAB8/CAB6 更新 + 背景保存恢复）简化为固定位置 (256,144)；
//       滑块高亮条用纯色占位（原版 blitOpaqueToBackbuffer 恢复背景 + 档位条）
int numberInputDialog(Application& app, int maxValue) {
    GameState& st = app.gameState();
    NumberInputCtx ctx;
    ctx.app = &app;
    ctx.maxValue = maxValue > 0 ? maxValue : 0;
    if (auto blob = st.panel.read(21)) {
        ctx.panel.load(std::move(*blob));
    }
    if (ctx.panel.frameCount() < 26) {
        RICH4_LOGW("numberInputDialog: panel.mkf[21] unavailable (frames=%d)",
                   ctx.panel.frameCount());
        return -1;
    }
    auto mapBlob = st.panel.read(22);
    if (mapBlob) {
        ctx.indexMap = mapBlob->data();
        ctx.indexMapSize = mapBlob->size();
    }
    std::snprintf(ctx.text, sizeof(ctx.text), "0");
    RICH4_LOGI("number input dialog: max=%d (RE 0x453544)", ctx.maxValue);
    const int result = runModal(app, numberInputHandler, &ctx);
    RICH4_LOGI("number input result: %d", result);
    return result;
}

} // namespace rich4
