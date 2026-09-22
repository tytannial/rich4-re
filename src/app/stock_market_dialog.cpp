#include "game/app/stock_market_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/map_tables.h"
#include "game/app/number_input_dialog.h"
#include "game/app/stock_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"
#include "game/render/text.h"

namespace rich4 {

namespace {

constexpr int kPanelX = 40;
constexpr int kPanelY = 28;
constexpr int kPanelW = 560;
constexpr int kPanelH = 424;
constexpr int kRowH = 28;
constexpr int kRowTop = 84;

struct MarketCtx {
    Application* app = nullptr;
    int selected = 0; // 选中股票 0..11
};

void drawMarket(MarketCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    Surface& dst = app.surface();
    renderGameFrame(app);
    // 面板（黑底 + 白色上下边）
    dst.fillRect(kPanelX, kPanelY, kPanelW, kPanelH, 0x0000);
    dst.fillRect(kPanelX, kPanelY, kPanelW, 2, 0xFFFF);
    dst.fillRect(kPanelX, kPanelY + kPanelH - 2, kPanelW, 2, 0xFFFF);
    char buf[200];
    app.text().setFont(16, 0xFFFF, 0x0000, kTextStyleShadow | kTextStyleBold, 1);
    std::snprintf(buf, sizeof(buf), "股票市場   %s   現金:%d   銀行:%d   總資產:%d",
                  st.players[p].name ? st.players[p].name : "", st.players[p].cash,
                  st.players[p].bank, playerTotalAssets(app, p));
    app.text().drawText(dst, buf, kPanelX + 12, kPanelY + 8, 2);
    app.text().setFont(16, 0xC0C0C0, 0x0000, kTextStyleShadow, 1);
    std::snprintf(buf, sizeof(buf), "%-4s %-10s %8s %8s %8s %9s %9s", "  ", "名稱", "持股",
                  "均價", "現價", "漲跌", "市值");
    app.text().drawText(dst, buf, kPanelX + 12, kPanelY + 34, 2);
    for (int i = 0; i < 12; ++i) {
        const float price = st.stocks[i][5];
        const float prev = st.stocks[i][4];
        const double chg = prev > 0.0f ? (price - prev) / prev * 100.0 : 0.0;
        const int32_t shares = st.playerShares[p][i];
        const int32_t value = static_cast<int32_t>(static_cast<double>(shares) * price);
        std::snprintf(buf, sizeof(buf), "%s %-10s %8d %8.2f %8.2f %+8.2f%% %9d",
                      (i == ctx.selected) ? ">" : " ", kStockNames[i], shares,
                      st.playerAvgCost[p][i], price, chg, value);
        app.text().drawText(dst, buf, kPanelX + 12, kRowTop + kRowH * i,
                            (i == ctx.selected) ? 0x0FF0 : 0xF0F0F0);
    }
    app.text().setFont(16, 0xFFFF, 0x0000, kTextStyleShadow, 1);
    std::snprintf(buf, sizeof(buf), "銀行可售:%u    ↑↓ 選擇   B 買入   S 賣出   ESC 離開",
                  st.stockBankShares[ctx.selected]);
    app.text().drawText(dst, buf, kPanelX + 12, kPanelY + kPanelH - 26, 2);
    app.renderFrame();
}

bool marketHandler(const SDL_Event* event, void* user) {
    MarketCtx& ctx = *static_cast<MarketCtx*>(user);
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (event == nullptr) {
        drawMarket(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode key = event->key.key;
        if (key == SDLK_UP) {
            ctx.selected = (ctx.selected + 11) % 12;
        } else if (key == SDLK_DOWN) {
            ctx.selected = (ctx.selected + 1) % 12;
        } else if (key == SDLK_ESCAPE) {
            app.events().requestExit(0);
            return true;
        } else if (key == SDLK_B) {
            // [RE 0x42AAFF case 2] 买入：上限 = min(银行/现价, 银行可售)
            const int32_t price = static_cast<int32_t>(st.stocks[ctx.selected][5]);
            if (price <= 0) {
                return true;
            }
            int32_t maxBuy = st.players[p].bank / price;
            if (maxBuy > st.stockBankShares[ctx.selected]) {
                maxBuy = st.stockBankShares[ctx.selected];
            }
            if (maxBuy <= 0) {
                RICH4_LOGI("stock market: buy rejected (bank=%d price=%d avail=%u)",
                           st.players[p].bank, price, st.stockBankShares[ctx.selected]);
                return true;
            }
            const int count = numberInputDialog(app, static_cast<int>(maxBuy));
            if (count > 0) {
                buyStock(app, p, ctx.selected, count, true);
            }
        } else if (key == SDLK_S) {
            // [RE 0x42AAFF case 3] 卖出：上限 = 持股
            const int32_t held = st.playerShares[p][ctx.selected];
            if (held <= 0) {
                return true;
            }
            const int count = numberInputDialog(app, static_cast<int>(held));
            if (count > 0) {
                sellStock(app, p, ctx.selected, count, true);
            }
        }
        drawMarket(ctx);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        int mx = 0;
        int my = 0;
        app.mouseLogicalPos(mx, my);
        const int row = (my - (kPanelY + kRowTop - kPanelY)) / kRowH;
        const int rel = my - kRowTop;
        if (mx >= kPanelX && mx < kPanelX + kPanelW && rel >= 0 && rel < 12 * kRowH) {
            ctx.selected = rel / kRowH;
            drawMarket(ctx);
        }
        (void)row;
        return true;
    }
    return true; // 模态：吞掉其他事件
}

} // namespace

// [RE 0x42B58F] stockMarketDialog（入口）+ [RE 0x42AAFF] 主对话框
// 依据: 0x42B58F 加载资源 + runModal(sub_42AAFF); 0x42AAFF 列表/买(case2: sub_428D2A a4=1)/
//   卖(case3: sub_428E23 a4=1)/走势(case4: sub_429D65)/退出(case5)
// 差异: 原版 panel 背景 + 列头排序 + 选中行详情 + K 线走势图简化为文本列表
void stockMarketDialog(Application& app) {
    MarketCtx ctx;
    ctx.app = &app;
    ctx.selected = 0;
    RICH4_LOGI("stock market dialog open (RE 0x42B58F)");
    runModal(app, marketHandler, &ctx);
}

} // namespace rich4
