#include "game/app/dividend.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/ui_layout.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x4755A8] 分红音效表 {61, -1}（audioPlayEffect(dword_4755A8, 0)）
constexpr int kSfxDividend = 61;
// [RE 0x42B3EB] panel[76] 帧 0 落点 (24,24)（帧内坐标 → 屏幕 = +24）
constexpr int kFrameX = 24;
constexpr int kFrameY = 24;
constexpr int kFrameW = 592;
constexpr int kFrameH = 432;
constexpr uint32_t kTextBlack = 0x101010; // [RE 0x101010]
constexpr uint32_t kTitleWhite = 0xF0F0F0; // [RE 0x101010/0xF0F0F0]

// [RE 0x452793] 千分位（分红表数字无 '$' 前缀）
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

struct DividendCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    int alive = 0;
    int order[4] = {};   // 存活玩家号（列序 = 存活序）
    int32_t total[4] = {}; // v17 每人分红合计
    std::vector<uint16_t> bg; // 确认界面画面快照（帧 0 区域含文字）
    int counter = 0;      // dword_48C2F2（1s tick 计数，3 次后自动退出）
};

// [RE 0x42BA97] 分红表绘制（黑底 + panel[76] 帧 0 + 全部文字；帧内坐标 +24）
void drawDividendTable(DividendCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int ox = kFrameX;
    const int oy = kFrameY;
    dst.clear(); // [RE 0x42B3EB] sub_456F60(0, 614400) 黑底
    blitElementOpaque(dst, ui.sheet->frame(0), ox, oy);

    app.text().setFont(28, kTitleWhite, kTextBlack, 3, 1); // [RE 0x42BA97] setTextFont(28, ...)
    app.text().drawText(dst, "上市公司分紅", ox + 296, oy + 25, 2);
    app.text().setFont(12, kTextBlack, 0, 2, 1);
    app.text().drawText(dst, "人名", ox + 104, oy + 82, 6);
    app.text().drawText(dst, "公司", ox + 18, oy + 94, 5);
    app.text().setFont(16, kTextBlack, 0, 2, 1);

    int col = 0;
    for (int i = 0; i < ui.alive; ++i) {
        const Player& pl = st.players[ui.order[i]];
        app.text().drawText(dst, pl.name ? pl.name : "", ox + 160 + col * 98, oy + 88, 2);
        ++col;
    }
    app.text().drawText(dst, "本月盈餘", ox + 542, oy + 88, 2);
    app.text().drawText(dst, "紅  利", ox + 62, oy + 404, 2);

    for (int i = 0; i < ui.alive; ++i) {
        ui.total[i] = 0;
    }
    int y = oy + 116;
    for (int s = 0; s < 12; ++s) {
        // [RE 0x42BA97] 上市公司判定 = word_496984[18*s] 非 0（重写 stocks[s][1] = specPt 索引）
        const int specIdx = static_cast<int>(st.stocks[s][1]);
        if (specIdx <= 0 || specIdx >= static_cast<int>(st.specPts.size())) {
            continue;
        }
        SpecPt& sp = st.specPts[specIdx];
        const std::string label =
            big5ToUtf8(reinterpret_cast<const char*>(sp.pad4), sizeof(sp.pad4));
        app.text().drawText(dst, label.c_str(), ox + 62, y, 2);
        // 总持股（存活玩家）
        int64_t totalShares = 0;
        for (int i = 0; i < ui.alive; ++i) {
            totalShares += st.playerShares[ui.order[i]][s];
        }
        col = 0;
        for (int i = 0; i < ui.alive; ++i) {
            const int32_t shares = st.playerShares[ui.order[i]][s];
            // [RE 0x42BA97] 原版比例先存 **float**（`*(float*)&chText[...] = shares/(double)total`），
            //   再 `(double)fund * (float)ratio` → int 截断；重写须同样截成 float 才能逐分对齐
            const float ratio =
                shares != 0
                    ? static_cast<float>(static_cast<double>(shares) /
                                         static_cast<double>(totalShares))
                    : 0.0f; // 总持股 0 → 比例 0（原版 total 非 0 才除）
            // [HELP 20] 公司若有虧損，負債由股東們負擔：fund 为负 → 负分红按持股比例
            //   从股东扣款（结算见函数尾：银行 → 现金 → 破产 sub_40CD87）
            const int32_t dividend =
                static_cast<int32_t>(static_cast<double>(sp.fund) * ratio);
            char buf[32];
            formatMoney(buf, dividend);
            app.text().drawText(dst, buf, ox + 198 + col * 98, y, 6);
            ui.total[i] += dividend;
            ++col;
        }
        char buf[32];
        formatMoney(buf, sp.fund); // 公司累积盈余（分配前）
        app.text().drawText(dst, buf, ox + 572, y, 6);
        y += 24;
        if (totalShares != 0) {
            sp.fund = 0; // [RE 0x42BA97] 分配后清零
        }
    }
    col = 0;
    for (int i = 0; i < ui.alive; ++i) {
        char buf[32];
        formatMoney(buf, ui.total[i]);
        app.text().drawText(dst, buf, ox + 198 + col * 98, oy + 404, 6);
        ++col;
    }
}

// [RE 0x42B3EB] 确认界面（0x401 黑屏 + 帧 0 + 音效 61；1s×3 自动退 / 点击退）
bool dividendConfirmHandler(const SDL_Event* event, void* user) {
    DividendCtx& ui = *static_cast<DividendCtx*>(user);
    Application& app = *ui.app;
    if (!event) { // 0x401
        Surface& dst = app.surface();
        // [NEW M4-D 实机] restoreRegion 内部经 deviceX 自动应用绘制原点（=模态 base）——
        //   此处传 640 基准坐标即可；此前手动 +base 在 origin=base 下双重偏移
        dst.clear();
        restoreRegion(dst, ui.bg, kFrameX, kFrameY, kFrameW, kFrameH);
        app.audio().playEffect(kSfxDividend); // [RE 0x42B4CE] audioPlayEffect(dword_4755A8, 0)
        ui.counter = 0;
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: { // 0x113（SetTimer 1000ms）
        if (++ui.counter >= 3) {
            app.events().requestExit(0); // [RE 0x42B518] PostMessage(0x202) → 退出
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_KEY_DOWN: { // 0x201/0x202/0x205
        app.events().requestExit(0);
        return true;
    }
    default:
        return true;
    }
}

} // namespace

// [RE 0x42BA97] 15 号股东大会分红（含 [HELP 20] 公司亏损由股东分担：负 fund → 负分红）
// 依据: 0x42BA97 反编译；逐存活玩家 × 12 家上市公司：比例 = 持股/总持股（存 float）、
//   分红 = (double)fund × ratio → int 截断（正负同式）；总持股≠0 → fund 清零；
//   runModal(sub_42B3EB) 后统一入银行（不足现金补 → 破产 sub_40CD87，其 a1 参数未被使用）。
void dividendMeeting(Application& app) {
    GameState& st = app.gameState();
    if (st.playerCount <= 0) {
        return;
    }
    auto blob = st.panel.read(76); // [RE 0x42BAB2] panel.mkf[76]
    if (!blob) {
        RICH4_LOGW("dividend: panel.mkf[76] unavailable (RE 0x42BA97)");
        return;
    }
    UiImage sheet;
    if (!sheet.load(std::move(*blob)) || sheet.frameCount() < 1) {
        RICH4_LOGW("dividend: panel.mkf[76] empty (RE 0x42BA97)");
        return;
    }
    DividendCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (st.players[i].alive != 0) {
            ui.order[ui.alive++] = i;
        }
    }
    if (ui.alive == 0) {
        return;
    }
    // [NEW M4-D 实机] 预绘制包 640 基准 origin（与模态内 dispatchModalAware 同域居中）；
    //   快照/恢复坐标同步 +base——否则宽屏下表格贴左、确认黑屏后恢复错位
    const int base = uiModalBaseX(app.surface());
    {
        SurfaceOriginGuard og(app.surface(), base, 0);
        drawDividendTable(ui);
    }
    saveRegion(ui.bg, app.surface(), kFrameX + base, kFrameY, kFrameW, kFrameH);
    runModal(app, &dividendConfirmHandler, &ui, 1000); // [RE 0x42B3EB] SetTimer 1000ms
    // [RE 0x42BA97 尾] 分红入银行（不足由现金补，再不足淘汰）
    for (int i = 0; i < ui.alive; ++i) {
        Player& pl = st.players[ui.order[i]];
        const int32_t total = ui.total[i];
        pl.bank += total;
        if (pl.bank < 0) {
            pl.cash += pl.bank;
            pl.bank = 0;
            if (pl.cash < 0) {
                pl.cash = 0;
                eliminatePlayer(app, ui.order[i]);
            }
        }
    }
    RICH4_LOGI("dividend: 15th meeting done alive=%d (RE 0x42BA97)", ui.alive);
}

} // namespace rich4
