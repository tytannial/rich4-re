#include "game/app/roulette_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
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

constexpr int kFrameMs = 40;        // [RE 0x43F7C6] 主循环帧间隔 36~40ms
constexpr int kScrollFrames = 40;   // AI 自动滚动帧数（原版 v3 >= 0x28）
constexpr int kDwellFrames = 40;    // 停留帧数（原版 v3 == 40 → 结束）

struct RouletteCtx {
    Application* app = nullptr;
    UiImage ui;
    const uint8_t* table = nullptr;
    char text[192] = {};
    int cell = 0;
    int phase = 1;      // 1 滚动 / 2 停止过渡 / 3 减速 / 5 停留 / 6 结束
    int frame = 0;      // 当前阶段帧计数
    int interval = 1;   // 减速间隔（1..5）
    int stepsLeft = 3;  // 减速每 3 步 +1
    bool humanCtl = false;
    int value = 0;
};

// [RE 0x43F127] 绘制：帧 2+格 @(220,320)；phase>=2 时画帧 1（天使）@(265,230) 否则帧 0（底盘）
void rouletteDraw(RouletteCtx& rs) {
    Application& app = *rs.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    renderGameFrame(app); // 地图背景
    const UiImage& tip = st.estateTiles; // [RE 0x48BAD8] data.mkf[517] 帧 5 提示框
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(5), 220, 140, false);
    }
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, rs.text, 220, 140, 4);
    blitElement(dst, rs.ui.frame(2 + rs.cell), 220, 320, false);
    blitElement(dst, rs.ui.frame(rs.phase >= 2 ? 1 : 0), 265, 230, false);
}

// 模态事件处理器：吞掉所有事件（覆盖层优先，不再与场景地图交互）
// [RE 0x43F7C6] 点击（人类玩家）→ 滚动阶段提前停止 / 停留阶段立即结束
bool rouletteHandler(const SDL_Event* event, void* user) {
    RouletteCtx& rs = *static_cast<RouletteCtx*>(user);
    Application& app = *rs.app;
    if (event == nullptr) {
        rouletteDraw(rs); // 模态进入
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        rs.humanCtl) {
        if (rs.phase == 1) {
            rs.phase = 2;
            rs.frame = 0;
            RICH4_LOGI("roulette: click stop at scroll (RE 0x43F7C6)");
        } else if (rs.phase == 5) {
            rs.phase = 6;
            RICH4_LOGI("roulette: click stop at dwell (RE 0x43F7C6)");
        }
        return true;
    }
    if (event->type != kModalTimerEvent) {
        return true; // 模态：吞掉其他事件
    }
    switch (rs.phase) {
        case 1: // 滚动（原版：人类无限滚动直到点击、AI 40 帧自动）
            rs.cell = (rs.cell + 1) % 12;
            ++rs.frame;
            if (!rs.humanCtl && rs.frame >= kScrollFrames) {
                rs.phase = 2;
                rs.frame = 0;
            }
            break;
        case 2: // 停止过渡：画中心帧 1（天使出现）+ UI 点击音效（原版状态2）
            app.audio().playEffect(1);
            rs.phase = 3;
            rs.frame = 0;
            rs.interval = 1;
            rs.stepsLeft = 3;
            break;
        case 3: // 减速步进（间隔 1..5，每 3 步 +1；落在有效格且间隔达 5 → 停）
            if (++rs.frame >= rs.interval) {
                rs.frame = 0;
                rs.cell = (rs.cell + 1) % 12;
                if (--rs.stepsLeft == 0) {
                    rs.stepsLeft = 3;
                    if (rs.interval < 5) {
                        ++rs.interval;
                    }
                }
                if (rs.interval >= 5 && rs.table[rs.cell] != 0xFF) {
                    rs.phase = 5;
                    rs.frame = 0;
                }
            }
            break;
        case 5: // 停留（原版 v3 == 40 → 结束；点击可立即结束）
            if (++rs.frame >= kDwellFrames) {
                rs.phase = 6;
            }
            break;
        default:
            break;
    }
    rouletteDraw(rs);
    if (rs.phase >= 6) {
        // [NEW] 定点注入生效点=最终停靠格（人类点击路径下指针从初始 cell 继续演化，
        //   仅覆盖起始值无法控制结果；未注入时零 rand 消耗，默认行为与序列不变）。
        //   docs/testing.md §1.2
        if (dbg::hasInject(dbg::SlotRoulette)) {
            rs.cell = dbg::roll(dbg::SlotRoulette, 12);
            if (rs.table[rs.cell] == 0xFF) {
                // 注入落在无效格：回退到第一个有效格（不可用 cell0——theme1(旅館) 的 cell0=0xFF）
                while (rs.table[rs.cell] == 0xFF) {
                    rs.cell = (rs.cell + 1) % 12;
                }
            }
        }
        rs.value = rs.table[rs.cell];
        app.events().requestExit(rs.value);
    }
    return true;
}

} // namespace

// [RE 0x44090E] roulettePrompt：转盘 UI（完整还原 + 模态事件栈）
// 依据: 0x44090E 反编译; panel.mkf[(theme&3)+68] 14 帧（0 底盘/1 天使/2..13 十二格）+
//       data.mkf[517] 帧 5 提示框 @(220,140) + off_475CF8 文本 + sub_43F7C6 动画状态机
// 迁移: 原版阻塞消息泵 + 离屏表面 → runModal 模态事件栈（覆盖层优先，事件不再传给场景）+
//       逐帧 renderGameFrame 重绘（离屏表面恢复背景的效果等价）
int roulettePrompt(Application& app, int theme, const char* arg) {
    GameState& st = app.gameState();
    const int t = theme & 3;
    RouletteCtx rs;
    rs.app = &app;
    if (auto blob = st.panel.read(static_cast<size_t>(68 + t))) {
        rs.ui.load(std::move(*blob));
    }
    if (rs.ui.frameCount() < 14) {
        RICH4_LOGW("roulettePrompt: panel.mkf[%d] unavailable (frames=%d)", 68 + t,
                   rs.ui.frameCount());
        return 0;
    }
    rs.table = kRouletteTable[t];
    std::snprintf(rs.text, sizeof(rs.text), kRouletteText[t], arg ? arg : "");
    // [NEW] 有注入时起始格取 0（不消费），最终停靠格在出口按注入值覆写（见 rouletteHandler）
    rs.cell = dbg::hasInject(dbg::SlotRoulette) ? 0 : dbg::roll(dbg::SlotRoulette, 12); // [RE 0x43F7C6] dword_48C50C = rand()%12
    const int cp = st.currentPlayer;
    rs.humanCtl = cp >= 0 && cp < 4 && st.players[cp].alive == 1 && st.players[cp].state37 == 0;
    RICH4_LOGI("roulette prompt: theme=%d arg=%s human=%d (RE 0x44090E)", t, arg ? arg : "",
               rs.humanCtl ? 1 : 0);
    app.audio().playEffect(52); // [RE 0x475D4C] 转盘音效
    const int value = runModal(app, rouletteHandler, &rs, kFrameMs);
    RICH4_LOGI("roulette result: cell=%d value=%d (RE 0x43F7C6)", rs.cell, value);
    trace::logf("roulette theme=%d cell=%d value=%d", t, rs.cell, value);
    return value;
}

} // namespace rich4
