#include "game/app/roulette_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

#include "game/app/game_loop.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x475D0C] byte_475D0C 转盘主题表（12 格；0xFF = 无效格，指针不停在该格）
constexpr uint8_t kRouletteTable[4][12] = {
    {1, 0xFF, 0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 0xFF, 2, 0xFF},       // 0 出國天數
    {0xFF, 0xFF, 1, 0xFF, 0xFF, 4, 0xFF, 3, 0xFF, 0xFF, 2, 0xFF}, // 1 旅館天數
    {1, 0xFF, 6, 0xFF, 5, 0xFF, 4, 0xFF, 3, 0xFF, 2, 0xFF},       // 2 購物倍數
    {5, 0xFF, 3, 0xFF, 30, 0xFF, 20, 0xFF, 15, 0xFF, 10, 0xFF},   // 3 投保天數
};

// [RE 0x475CF8] off_475CF8 主题提示文本（%s = 地主名/对象名）
const char* const kRouletteText[4] = {
    "%s\n\n送您出國旅遊...",
    "%s的旅館\n\n請進來休息...",
    "%s的購物中心\n\n您的消費倍數為...",
    "%s\n\n您的投保天數為...",
};

constexpr int kFrameMs = 40; // [RE 0x43F7C6] 主循环帧间隔 36~40ms

} // namespace

// [RE 0x44090E] roulettePrompt：转盘 UI（完整还原）
// 依据: 0x44090E 反编译; 详见头文件注释（panel.mkf[68+theme] 14 帧 + sub_43F7C6 动画）
// 迁移: 原版阻塞消息泵 → 逐帧 renderGameFrame + 转盘 UI 叠加 + 分段喂音频（同 playHighlightBlink）
int roulettePrompt(Application& app, int theme, const char* arg) {
    GameState& st = app.gameState();
    const int t = theme & 3;
    UiImage ui;
    if (auto blob = st.panel.read(static_cast<size_t>(68 + t))) {
        ui.load(std::move(*blob));
    }
    if (ui.frameCount() < 14) {
        RICH4_LOGW("roulettePrompt: panel.mkf[%d] unavailable (frames=%d)", 68 + t,
                   ui.frameCount());
        return 0;
    }
    const uint8_t* table = kRouletteTable[t];
    char text[192];
    std::snprintf(text, sizeof(text), kRouletteText[t], arg ? arg : "");
    RICH4_LOGI("roulette prompt: theme=%d arg=%s (RE 0x44090E)", t, arg ? arg : "");

    Surface& dst = app.surface();
    int cell = std::rand() % 12; // [RE 0x43F7C6] dword_48C50C = rand()%12
    // [RE 0x43F7C6] 点击响应仅限人类玩家（alive==1 且非夢遊；AI/夢遊走自动节奏）
    const int cp = st.currentPlayer;
    const bool humanCtl =
        cp >= 0 && cp < 4 && st.players[cp].alive == 1 && st.players[cp].state37 == 0;
    // [RE 0x43F127] 绘制：帧 2+格 @(220,320)；center 时画帧 1（中心）@(265,230)
    auto draw = [&](bool center) {
        renderGameFrame(app); // 地图背景
        const UiImage& tip = st.estateTiles; // [RE 0x48BAD8] data.mkf[517] 帧 5 提示框
        if (tip.frameCount() > 5) {
            blitElement(dst, tip.frame(5), 220, 140, false);
        }
        app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        app.text().drawText(dst, text, 220, 140, 4);
        blitElement(dst, ui.frame(2 + cell), 220, 320, false);
        blitElement(dst, ui.frame(center ? 1 : 0), 265, 230, false);
        app.renderFrame();
    };
    // [PORT] 音频续喂（kTargetQueued=1024≈23ms；帧间隔 40ms 需分段）
    auto waitAudio = [&app](int ms) {
        for (int i = 0; i < ms; i += 5) {
            app.audio().update();
            SDL_Delay(5);
        }
    };
    app.audio().playEffect(52); // [RE 0x475D4C] 转盘音效

    // 阶段 1：滚动（原版：人类无限滚动直到点击、AI 40 帧自动；重写保留 40 帧自动 + 点击提前）
    for (int f = 0; f < 40; ++f) {
        cell = (cell + 1) % 12; // [RE 0x43F127] 指针格 +1，12 格循环
        draw(false);
        app.pumpEvents();
        if (humanCtl && app.consumeUiClick()) {
            RICH4_LOGI("roulette: early stop at scroll frame %d (RE 0x43F7C6)", f);
            break;
        }
        waitAudio(kFrameMs);
    }
    // 阶段 2：停止过渡（画中心帧 1）
    draw(true);
    app.pumpEvents();
    waitAudio(kFrameMs);
    // 阶段 3：减速步进（v16 间隔 1..5，每 3 步 +1；落在有效格且间隔达 5 → 停）
    int interval = 1;
    int stepsLeft = 3;
    for (;;) {
        for (int k = 0; k < interval; ++k) {
            draw(true);
            app.pumpEvents();
            waitAudio(kFrameMs);
        }
        cell = (cell + 1) % 12;
        if (--stepsLeft == 0) {
            stepsLeft = 3;
            if (interval < 5) {
                ++interval;
            }
        }
        if (interval >= 5 && table[cell] != 0xFF) {
            break;
        }
    }
    // 阶段 5：停留 40 帧（原版 v3 == 40 → 结束；点击立即结束，先清减速期残留点击）
    (void)app.consumeUiClick();
    for (int f = 0; f < 40; ++f) {
        draw(true);
        app.pumpEvents();
        if (humanCtl && app.consumeUiClick()) {
            RICH4_LOGI("roulette: early stop at dwell frame %d (RE 0x43F7C6)", f);
            break;
        }
        waitAudio(kFrameMs);
    }
    const int value = table[cell];
    RICH4_LOGI("roulette result: cell=%d value=%d (RE 0x43F7C6)", cell, value);
    return value;
}

} // namespace rich4
