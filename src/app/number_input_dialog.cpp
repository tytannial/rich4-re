#include "game/app/number_input_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/render/blit.h"
#include "game/render/cursor.h"
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
constexpr int kPanelInitX = 256; // [RE 0x452C02] word_48CAB8/CAB6 初始位置
constexpr int kPanelInitY = 144;
constexpr int kSfxKeyPress = 7;  // [RE 0x48234A] g_effectSlots[0]（按键音效）
constexpr int kSfxSlider = 9;    // [RE 0x482352] 滑块音效
constexpr int kSfxCancel = 4;    // [RE 0x482332] g_uiSoundCancel
constexpr int kCursorPanel = 27; // [RE 0x452C02] cursorSelect(27) 面板交互光标
constexpr int kCursorArrow = 41; // 默认箭头

struct NumberInputCtx {
    Application* app = nullptr;
    UiImage panel;
    const uint8_t* indexMap = nullptr; // panel.mkf[22] 索引图（128x192，值 = 按键 ID）
    size_t indexMapSize = 0;
    int maxValue = 0;
    char text[16] = "0";
    int panelX = kPanelInitX;
    int panelY = kPanelInitY;
    uint8_t pressedKey = 0; // 按下态按键 ID（0=无；绘制帧 keyId 作按下动画）
    int dragOffX = 0;       // 面板拖动抓取偏移
    int dragOffY = 0;
    std::vector<uint16_t> snapshot; // [PORT] 进入时背景快照（叠加显示，不重绘场景）
    bool captured = false;
};

// [RE 0x456512] SPR 索引+调色板绘制（不透明，含索引 0；对齐原版 sub_456512）
// [NEW M4-A2] 走 blitScaled：scale=1 时逐像素等价，scale>1 时按画布缩放（源区域语义一致）
void blitSprRegion(Surface& dst, const UiImage& spr, int frame, int dstX, int dstY, int srcX,
                   int srcY, int w, int h) {
    const uint8_t* bytes = spr.frameBytes(frame);
    const uint16_t* pal = spr.palette();
    if (bytes == nullptr || pal == nullptr || frame < 0 || frame >= spr.frameCount()) {
        return;
    }
    const UiFrameView& view = spr.frame(frame);
    blitScaled(dst, bytes, view.width, pal, dstX, dstY, srcX, srcY, w, h, false, true);
}

// [RE 0x45297E] 数字显示 + 滑块 + 按下态
void drawNumber(NumberInputCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    const int x = ctx.panelX;
    const int y = ctx.panelY;
    // 数字（帧 16..25 = '0'..'9'，从右到左每字符 12px，最多 9 位）
    int px = x + 107;
    const int py = y + 11;
    const int len = static_cast<int>(std::strlen(ctx.text));
    int drawn = 0;
    for (int i = len - 1; i >= 0; --i) {
        const uint8_t ch = static_cast<uint8_t>(ctx.text[i]);
        if (ch >= 32) {
            const int frame = ch - 32;
            if (frame >= 0 && frame < ctx.panel.frameCount()) {
                const UiFrameView& v = ctx.panel.frame(static_cast<size_t>(frame));
                blitSprRegion(dst, ctx.panel, frame, px, py, 0, 0, v.width, v.height);
            }
        }
        px -= 12;
        if (++drawn >= 9) {
            break;
        }
    }
    // 滑块轨道（帧 1，108x12）+ 已选比例：用帧 0 背景擦除轨道左段
    if (ctx.panel.frameCount() > 1) {
        const UiFrameView& track = ctx.panel.frame(1);
        blitSprRegion(dst, ctx.panel, 1, x + 10, y + 42, 0, 0, track.width, track.height);
    }
    const int value = std::atoi(ctx.text);
    if (value > 0 && ctx.maxValue > 0) {
        int step = static_cast<int>(static_cast<float>(value) /
                                    static_cast<float>(ctx.maxValue) * kSliderDivisor);
        step = step < 0 ? 0 : (step > 33 ? 33 : step);
        const int w = kSliderSteps[step];
        if (w > 0) {
            blitSprRegion(dst, ctx.panel, 0, x + 10, y + 42, 10, 42, w, 12);
        }
    }
    // [RE 0x452C02] 按下态：画帧 keyId @ 按键位置（kKeyRect + 面板偏移）
    if (ctx.pressedKey >= 2 && ctx.pressedKey <= 16 &&
        ctx.pressedKey < static_cast<uint8_t>(ctx.panel.frameCount())) {
        const uint8_t* r = kKeyRect[ctx.pressedKey];
        const UiFrameView& v = ctx.panel.frame(ctx.pressedKey);
        blitSprRegion(dst, ctx.panel, ctx.pressedKey, x + r[0], y + r[1], 0, 0, v.width,
                      v.height);
    }
}

void drawAll(NumberInputCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    // [PORT] 叠加显示：首次捕获调用者画面，之后每帧恢复（原版子模态保留父画面，
    //   不再 renderGameFrame 重绘地图，避免股市面板被擦除）
    if (!ctx.captured) {
        ctx.snapshot.assign(dst.pixels(), dst.pixels() + dst.width() * dst.height());
        ctx.captured = true;
    } else {
        std::memcpy(dst.pixels(), ctx.snapshot.data(), ctx.snapshot.size() * sizeof(uint16_t));
    }
    if (ctx.panel.frameCount() > 0) {
        const UiFrameView& bg = ctx.panel.frame(0);
        blitSprRegion(dst, ctx.panel, 0, ctx.panelX, ctx.panelY, 0, 0, bg.width, bg.height);
    }
    drawNumber(ctx);
}

// 面板坐标 → 输入值（滑块档位比例；rel 为轨道内相对 x）
void sliderValueFromRel(NumberInputCtx& ctx, int rel) {
    int step = 0;
    if (rel > 0) {
        while (step < 33 && rel > kSliderSteps[step]) {
            ++step;
        }
    }
    const int v = static_cast<int>(static_cast<float>(ctx.maxValue) *
                                   (static_cast<float>(step) / kSliderDivisor));
    std::snprintf(ctx.text, sizeof(ctx.text), "%d", v);
    if (v > ctx.maxValue) {
        std::snprintf(ctx.text, sizeof(ctx.text), "%d", ctx.maxValue);
    }
}

bool numberInputHandler(const SDL_Event* event, void* user) {
    NumberInputCtx& ctx = *static_cast<NumberInputCtx*>(user);
    Application& app = *ctx.app;
    auto clampText = [&]() {
        if (ctx.text[0] == '\0') {
            std::snprintf(ctx.text, sizeof(ctx.text), "0");
        }
        // [PORT] 用 strtoll：Windows long=32bit / Linux long=64bit，超大输入两平台行为不一致
        const long long v = std::strtoll(ctx.text, nullptr, 10);
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
            app.audio().playEffect(kSfxKeyPress);
        } else if (key == SDLK_BACKSPACE) {
            const size_t len = std::strlen(ctx.text);
            if (len > 1) {
                ctx.text[len - 1] = '\0';
            } else {
                std::snprintf(ctx.text, sizeof(ctx.text), "0");
            }
            app.audio().playEffect(kSfxKeyPress);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            clampText();
            app.audio().playEffect(kSfxKeyPress);
            app.events().requestExit(std::atoi(ctx.text));
            return true;
        } else if (key == SDLK_ESCAPE) {
            app.audio().playEffect(kSfxCancel);
            app.events().requestExit(-1);
            return true;
        }
        drawAll(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (ctx.pressedKey == 0) {
            return true;
        }
        int mx = 0;
        int my = 0;
        app.mouseLogicalPos(mx, my);
        if (ctx.pressedKey == 1) {
            // [RE 0x452C02] 拖动面板（byte_48CAC2==1 时 WM_MOUSEMOVE 更新 word_48CAB8/CAB6）
            ctx.panelX = mx - ctx.dragOffX;
            ctx.panelY = my - ctx.dragOffY;
            drawAll(ctx);
        } else if (ctx.pressedKey == 16) {
            // [RE 0x452C02] 拖动滑块（byte_48CAC2==16 时 WM_MOUSEMOVE 实时更新值）
            const int rel = (mx - ctx.panelX) - 10;
            sliderValueFromRel(ctx, rel);
            app.audio().playEffect(kSfxSlider);
            drawAll(ctx);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        // [RE 0x453544] 右键取消（postModalExit -1）
        app.audio().playEffect(kSfxCancel);
        app.events().requestExit(-1);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        ctx.pressedKey = 0; // 释放清除按下态
        drawAll(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        int mx = 0;
        int my = 0;
        app.mouseLogicalPos(mx, my);
        const int rx = mx - ctx.panelX;
        const int ry = my - ctx.panelY;
        if (rx < 0 || rx >= kPanelW || ry < 0 || ry >= kPanelH || ctx.indexMap == nullptr ||
            ctx.indexMapSize < static_cast<size_t>(kPanelW * kPanelH)) {
            return true;
        }
        const uint8_t key = ctx.indexMap[ry * kPanelW + rx];
        ctx.pressedKey = key; // 按下态动画
        // 音效：按键（2..15）/ 滑块（16）
        if (key == 16) {
            app.audio().playEffect(kSfxSlider);
        } else if (key >= 2 && key <= 15) {
            app.audio().playEffect(kSfxKeyPress);
        }
        switch (key) {
            case 1: // 拖动区：记录抓取偏移
                ctx.dragOffX = mx - ctx.panelX;
                ctx.dragOffY = my - ctx.panelY;
                break;
            case 2: // 最大
                std::snprintf(ctx.text, sizeof(ctx.text), "%d", ctx.maxValue);
                break;
            case 3: // 确定
                clampText();
                drawAll(ctx);
                app.events().requestExit(std::atoi(ctx.text));
                return true;
            case 4: // 清除
                std::snprintf(ctx.text, sizeof(ctx.text), "0");
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
                    std::snprintf(ctx.text, sizeof(ctx.text), "0");
                }
                break;
            }
            case 16: { // 滑块：点击位置 → 值（拖动在 MOUSE_MOTION 处理）
                sliderValueFromRel(ctx, rx - 10);
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
//   上限 clamp（超限 → 上限）、"最大"= 上限、"清除"归 0、退格、滑块按档位比例；
//   0x452C02: 按键/滑块音效（g_effectSlots[0]/dword_482352）、按下态画帧 keyId、
//   byte_48CAC2==1 拖动面板（word_48CAB8/CAB6）、byte_48CAC2==16 拖动滑块实时更新、
//   进入 cursorSelect(27)/退出 cursorSelect(41)
// 迁移: panel[21] 为 SPR（8bit 索引 + RGB555 调色板），按原版 sub_456512 实现 blitSprRegion
// 差异: 背景保存恢复（saveBackground/allocUiElement）改为逐帧全量重绘（等价）
int numberInputDialog(Application& app, int maxValue) {
    // [NEW] named region 一次性登记（kKeyRect 初始位同源常量；语义名映射见 testing.md：
    //   numkey.cancel/ok/clear/back/slider + numkey.0..9）
    {
        static bool s_reg = false;
        if (!s_reg) {
            s_reg = true;
            debug::registerRegion("numkey.cancel", kPanelInitX + kKeyRect[2][0],
                                  kPanelInitY + kKeyRect[2][1], kKeyRect[2][2], kKeyRect[2][3]);
            debug::registerRegion("numkey.ok", kPanelInitX + kKeyRect[3][0],
                                  kPanelInitY + kKeyRect[3][1], kKeyRect[3][2], kKeyRect[3][3]);
            debug::registerRegion("numkey.clear", kPanelInitX + kKeyRect[4][0],
                                  kPanelInitY + kKeyRect[4][1], kKeyRect[4][2], kKeyRect[4][3]);
            debug::registerRegion("numkey.back", kPanelInitX + kKeyRect[6][0],
                                  kPanelInitY + kKeyRect[6][1], kKeyRect[6][2], kKeyRect[6][3]);
            debug::registerRegion("numkey.slider", kPanelInitX + kKeyRect[16][0],
                                  kPanelInitY + kKeyRect[16][1], kKeyRect[16][2], kKeyRect[16][3]);
            // kKeyChar 数字位：id 5='0' 13='1' 14='2' 15='3' 10='4' 11='5' 12='6' 7='7' 8='8' 9='9'
            static const int kDigitKeyId[10] = {5, 13, 14, 15, 10, 11, 12, 7, 8, 9};
            for (int d = 0; d < 10; ++d) {
                const int id = kDigitKeyId[d];
                char nm[16];
                std::snprintf(nm, sizeof(nm), "numkey.%d", d);
                debug::registerRegion(nm, kPanelInitX + kKeyRect[id][0],
                                      kPanelInitY + kKeyRect[id][1], kKeyRect[id][2],
                                      kKeyRect[id][3]);
            }
        }
    }

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
    app.cursor().select(kCursorPanel, 1, 0); // [RE 0x452C02] 面板交互光标
    const int result = runModal(app, numberInputHandler, &ctx);
    app.cursor().select(kCursorArrow, 1, 0); // 恢复默认箭头
    RICH4_LOGI("number input result: %d", result);
    return result;
}

} // namespace rich4
