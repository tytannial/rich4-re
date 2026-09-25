// 新聞格事件（landingEvent case 2 → newsEvent 0x44B6DF）
// 依据: 0x44B6DF 主循环 + newsEventCheck 0x448BE2 36 条判定 + g_newsFuncs 0x475E24 36 效果；
//       详见 docs/reverse/functions/44b6df-news-events.md（逐条文本/判定/效果/资源）。
//
// 原版流程：抽 g_newsOrder[g_newsPos]（未触发则 ++pos 继续）→ panel.mkf[66] 画布（SMP
//   440×480，用帧 0）+ data.mkf[441+idx] 插画（388×251 RGB555 @25,44）+ 标题 @(24,8) →
//   效果函数(0) → 全屏 blt → 阻塞 2400ms → 效果函数(1) → 释放。
// 重写：单 Surface 事件驱动，进入时绘制一次；runModal 每 tick renderFrame 呈现。
// 差异: 语音 sub_44EF41 / 浮动数字 sub_44F354 → TODO(P4)；idx 7 拍卖 sub_43BDE5 → TODO(拍卖)。

#include "game/app/news_dialog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/map_objects.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/raw_bitmap.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {
namespace {

// ===== 文本/标题表（[RE 0x465424..] BIG5 原文；'#NNNN' 前缀由 TextRenderer 触发语音）=====

// 36 条文本：idx 0..35（#0149..#0184）
const char* const kNewsTexts[36] = {
    /* 00 */ "#0149獄中囚犯無罪開釋",
    /* 01 */ "#0150獄中囚犯延長刑期%d天",
    /* 02 */ "#0151住院中病患提前出院",
    /* 03 */ "#0152住院中病患延長住院%d天",
    /* 04 */ "#0153外星人攻打地球",
    /* 05 */ "#0154外星怪獸襲擊%s\n摧毀建築一棟",
    /* 06 */ "#0155%s公告地價調漲３０％",
    /* 07 */ "#0156公開拍賣%s\n公有土地一處",
    /* 08 */ "#0157公開表揚第一大地主\n%s獲得%d元獎勵",
    /* 09 */ "#0158公開補助土地最少者\n%s獲得%d元補助",
    /* 10 */ "#0159公開表揚股市第一大戶\n%s獲得%d元獎勵",
    /* 11 */ "#0160所有人繳交所得稅５％",
    /* 12 */ "#0161所有人繳交地價稅５％",
    /* 13 */ "#0162所有人繳交證交稅５％",
    /* 14 */ "#0163%s房屋鬧鬼\n地價下跌３０％",
    /* 15 */ "#0164%s一處民宅瓦斯爆炸\n房屋失火",
    /* 16 */ "#0165豪雨特報\n行人休息一回合",
    /* 17 */ "#0166交通阻塞\n汽車停止一回合",
    /* 18 */ "#0167%s強烈地震房屋倒塌",
    /* 19 */ "#0168%s山洪爆發土地流失",
    /* 20 */ "#0169超級颱風侵襲%s\n多處房屋受損",
    /* 21 */ "#0170龍捲風侵襲%s\n摧毀房屋一棟",
    /* 22 */ "#0171銀行擠兌停止放款１５天",
    /* 23 */ "#0172銀行加發１０％儲金紅利",
    /* 24 */ "#0173股市低迷不振重挫崩盤",
    /* 25 */ "#0174股市氣勢如虹全面上漲",
    /* 26 */ "#0175股市暫停交易１０天",
    /* 27 */ "#0176%s股票暫停交易１０天",
    /* 28 */ "#0177%s股票恢復上市交易",
    /* 29 */ "#0178%s違法超貸\n經營者%s坐牢５天",
    /* 30 */ "#0179%s工廠排放污水\n罰款10000元",
    /* 31 */ "#0180%s海外投資\n獲利20000元",
    /* 32 */ "#0181%s海外投資\n虧損20000元",
    /* 33 */ "#0182%s違規開發山坡地\n罰款10000元",
    /* 34 */ "#0183%s製造噪音公害\n罰款5000元",
    /* 35 */ "#0184%s獲利調高一倍",
};

// [RE 0x475ED8] 6 类标题（無責任新聞/政府公告/社會新聞/路況報導/氣象報導/財經新聞）
const char* const kNewsTitles[6] = {
    "無責任新聞", "政府公告", "社會新聞", "路況報導", "氣象報導", "財經新聞",
};

// [RE 0x475EB4] 每条事件的标题类（36B：0×6, 1×8, 2×2, 3×2, 4×4, 5×14）
const uint8_t kNewsTitleClass[36] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 3, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

// 玩家头像帧号：原版 blitElementToCanvas(panel, g_pieceSprites[13*i]+N)（N/12 = 帧号）
enum : int {
    kPieceFrameRest = 3,    // +36（豪雨/交通：休息玩家）
    kPieceFramePenalty = 4, // +48（延长刑期/住院、列表行）
    kPieceFrameRelease = 5, // +60（释放/奖励）
};

// ===== 事件上下文 =====

struct NewsCtx {
    Application* app = nullptr;
    UiImage panel;             // panel.mkf[66]（SMP 2 帧 440×480）
    std::vector<uint16_t> art; // 插画 data.mkf[441+idx]（388×251 RGB555 拷贝）
    int artW = 0;
    int artH = 0;
    int eventIdx = -1;
    int startMs = 0;
    bool phase1Done = false;
    // 原版临时全局 dword_48C59C / dword_48C5A0 的事件私有存储（phase0 填、phase1 用）
    int targetObjId = 0; // 地块 objId / estate 索引 / 玩家索引
    int targetParam = 0; // 业主+1 / 奖金金额 / 坐标打包
};

// ===== 绘制辅助 =====

// 不透明贴一块 RGB555 位图（panel 底图/插画；原版 blitBackground 无色键）
void blitOpaqueRgb(Surface& dst, const uint16_t* src, int w, int h, int x, int y) {
    UiFrameView view{};
    view.width = static_cast<uint16_t>(w);
    view.height = static_cast<uint16_t>(h);
    view.pixels = src;
    blitElementOpaque(dst, view, x, y);
}

// 事件文本（默认字体 28 号白字阴影 @ x=24）
void drawEffectText(NewsCtx& ctx, const char* text, int y, int size = 28) {
    ctx.app->text().setFont(size, 0xF0F0F0, 0x101010, 3, 0); // [RE setTextFont]
    ctx.app->text().drawText(ctx.app->surface(), text, 24, y, 0);
}

// [RE blitElementToCanvas(panel, g_pieceSprites[13*i]+N, 390, y)] 玩家头像（8bit 索引色键）
void drawPiece(NewsCtx& ctx, int player, int frame, int y) {
    const GameState& st = ctx.app->gameState();
    if (player < 0 || player >= 9) {
        return;
    }
    if (st.pieceSprites[player].frameCount() <= frame) {
        return;
    }
    blitSpriteFrame(ctx.app->surface(), st.pieceSprites[player], frame, 390, y);
}

// 底图 + 插画 + 标题（进入时绘制一次）
void drawNewsBase(NewsCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    if (ctx.panel.frameCount() > 0) {
        // [RE 0x44B814] blitElementFullscreen(backbuffer, panel+12, 0, 0)（440×480 @0,0）
        blitElementOpaque(dst, ctx.panel.frame(0), 0, 0);
    }
    if (!ctx.art.empty()) {
        // [RE 0x44B77F] blitBackground(panel, art, 25, 44)
        blitOpaqueRgb(dst, ctx.art.data(), ctx.artW, ctx.artH, 25, 44);
    }
    // [RE 0x44B7AF] drawText(panel, g_newsTitles[g_newsTitleClass[v1]], 24, 8)
    app.text().setFont(28, 0xF0F0F0, 0x101010, 3, 0); // [RE setTextFont(28, 0xF0F0F0, 0x101010, 3, 0)]
    app.text().drawText(dst, kNewsTitles[kNewsTitleClass[ctx.eventIdx]], 24, 8, 0);
}

// ===== 破坏类辅助（idx 4/5/14/15/18-21；原版 g_newsTarget 复用见研究文档 §0）=====

// 有建筑地块 objId（estate level!=0 / corp sub!=0）
std::vector<int> collectBuiltObjIds(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].level != 0) {
            out.push_back(static_cast<int>(i) + 2000);
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].sub != 0) {
            out.push_back(static_cast<int>(i) + 4000);
        }
    }
    return out;
}

// 全部地块 objId（estate + corp）
std::vector<int> collectAllObjIds(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        out.push_back(static_cast<int>(i) + 2000);
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        out.push_back(static_cast<int>(i) + 4000);
    }
    return out;
}

// 有建筑 estate 索引（idx 15 专用：原版只收集 estate，不含 corp）
std::vector<int> collectBuiltEstateIndices(const GameState& st) {
    std::vector<int> out;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].level != 0) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

int pickRandom(const std::vector<int>& list) {
    return list.empty() ? 0 : list[static_cast<size_t>(std::rand()) % list.size()];
}

std::string estateNameUtf8(const Estate& es) {
    return big5ToUtf8(es.name, sizeof(es.name));
}

std::string corpNameUtf8(const Corp& cp) {
    return big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
}

std::string objNameUtf8(const GameState& st, int objId) {
    if (objId > 2000 && objId < 4000 && objId - 2000 < static_cast<int>(st.estates.size())) {
        return estateNameUtf8(st.estates[objId - 2000]);
    }
    if (objId > 4000 && objId < 6000 && objId - 4000 < static_cast<int>(st.corps.size())) {
        return corpNameUtf8(st.corps[objId - 4000]);
    }
    return {};
}

int objOwner(const GameState& st, int objId) {
    if (objId > 2000 && objId < 4000 && objId - 2000 < static_cast<int>(st.estates.size())) {
        return st.estates[objId - 2000].owner;
    }
    if (objId > 4000 && objId < 6000 && objId - 4000 < static_cast<int>(st.corps.size())) {
        return st.corps[objId - 4000].owner;
    }
    return 0;
}

// [RE 0x41D476] refreshGameUi(x, y, 2)：视口对准目标并全量重绘（收租/破坏共用）
void focusView(Application& app, int x, int y) {
    GameState& st = app.gameState();
    st.manualView = true;
    st.viewSmoothX = x;
    st.viewSmoothY = y;
    st.viewScrolling = false;
    renderGameFrame(app);
    app.renderFrame();
}

// [RE 0x41D546] 视口复位（还原主循环居中玩家）
void resetView(Application& app) {
    GameState& st = app.gameState();
    st.manualView = false;
    renderGameFrame(app);
    app.renderFrame();
}

// [RE 0x4528B9] 阻塞延时（音频续喂；阶段 1 内 300/500ms 停顿）
void newsWait(Application& app, int ms) {
    for (int t = 0; t < ms; t += 5) {
        app.audio().update();
        SDL_Delay(5);
    }
}

// [RE 0x40DFFA forceHotelCheckout] 地产/设施拆除：住宿中玩家状态 BYTE0=0x80（强制退房）
void forceHotelCheckout(GameState& st) {
    for (int i = 0; i < st.playerCount; ++i) {
        if (st.players[i].alive != 0 && (st.players[i].stateFlags & 0xFFu) != 0) {
            st.players[i].stateFlags = (st.players[i].stateFlags & 0xFFFFFF00u) | 0x80u;
        }
    }
}

// [RE 0x451985] 同名（同路段）estate 高亮闪烁：填列表 → highlightFrame=0 → 阻塞播放
// 注：原版调用点直接 highlightBlink（无 >1 判定），单块也播
void blinkHighlight(Application& app) {
    GameState& st = app.gameState();
    if (st.highlightEstates.empty()) {
        return;
    }
    st.highlightFrame = 0;
    playHighlightBlink(app);
}

// 破坏类事件阶段 1 公共：视口对准 + 输出对象坐标（expireAssets 范围中心）
void focusObjId(Application& app, int objId, int& outX, int& outY) {
    getObjectPosition(app.gameState(), objId, outX, outY);
    focusView(app, outX, outY);
}

// [RE 0x44913D] idx 4：外星人攻打地球
void evt04_alienAttack(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        drawEffectText(ctx, kNewsTexts[4], 310);
        return;
    }
    const int objId = pickRandom(collectBuiltObjIds(st));
    if (objId == 0) {
        return;
    }
    int x = 0;
    int y = 0;
    focusObjId(app, objId, x, y);
    // [RE 0x44922D] expireAssets(100, 38, 1, -1)：半径 100、estate+corp+事件槽、归公
    expireAssets(app, x, y, 100, 38, 1, -1);
    playEventFlc(app, 531, 0, 40, 86, false, 0x18); // [RE 0x44925B flags 0x180001]
    for (int j = 0; j < st.playerCount; ++j) {
        if ((st.players[j].alive & 0x40) != 0) {
            hospitalizePlayer(app, j, 3); // [RE 0x449285] 消失中玩家住院 3 天
        }
    }
    resetView(app); // [RE 0x449290] sub_41D546
}

// [RE 0x4492A0] idx 5：外星怪獸襲擊%s 摧毀建築一棟
void evt05_monsterDestroy(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectBuiltObjIds(st));
        ctx.targetObjId = objId;
        ctx.targetParam = objOwner(st, objId);
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[5], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    if (ctx.targetObjId == 0) {
        return;
    }
    int x = 0;
    int y = 0;
    focusObjId(app, ctx.targetObjId, x, y);
    demolishAtObjId(app, ctx.targetObjId, 1); // [RE 0x44944F]
    playEventFlc(app, 539, 0, 40, 84, false, 0x20); // [RE 0x44947D flags 0x200001]
    // [TODO P4] 业主台词 sub_44EF41(targetParam-1, 2, off_480856[char*27+(rand&1)])
    resetView(app);
}

// [RE 0x44A220] idx 14：%s房屋鬧鬼 地價下跌３０％
void evt14_haunted(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectAllObjIds(st));
        ctx.targetObjId = objId;
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[14], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    const int objId = ctx.targetObjId;
    if (objId > 2000 && objId < 4000) {
        Estate& es = st.estates[objId - 2000];
        focusView(app, es.x, es.y);
        st.highlightEstates.clear();
        for (size_t i = 1; i < st.estates.size(); ++i) {
            Estate& e = st.estates[i];
            if (std::strncmp(e.name, es.name, sizeof(es.name)) == 0) {
                st.highlightEstates.push_back(static_cast<int>(i));
                // [RE dbl_46561C = 0.7] 同路段每块 +28 ×0.7
                e.priceAdd = static_cast<uint16_t>(static_cast<int>(e.priceAdd * 0.7));
            }
        }
        blinkHighlight(app);
        resetView(app);
    } else if (objId > 4000 && objId < 6000) {
        Corp& cp = st.corps[objId - 4000];
        focusView(app, cp.x, cp.y);
        // [RE 0x44A6B6 共享段] +34 buildPrice ×0.7
        cp.buildPrice = static_cast<uint16_t>(static_cast<int>(cp.buildPrice * 0.7));
        // [差异] 原版此分支 markPickBuffer(corp) 单块高亮：重写高亮机制仅支持 estate（跳过）
        resetView(app);
    }
}

// [RE 0x44A453] idx 15：%s一處民宅瓦斯爆炸 房屋失火
void evt15_gasExplosion(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int idx = pickRandom(collectBuiltEstateIndices(st));
        ctx.targetObjId = idx;
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[15],
                      idx ? estateNameUtf8(st.estates[idx]).c_str() : "");
        drawEffectText(ctx, text, 310);
        return;
    }
    const int idx = ctx.targetObjId;
    if (idx <= 0 || idx >= static_cast<int>(st.estates.size())) {
        return;
    }
    const Estate& es = st.estates[idx];
    focusView(app, es.x, es.y);
    demolishAtObjId(app, idx + 2000, 0); // [RE 0x44A543] 降一级
    playEventFlc(app, 527, 0, 40, 87, false, 5); // [RE 0x44A571 flags 0x50001]
    newsWait(app, 300);                          // [RE 0x44A587]
    // [TODO P4] 业主台词 sub_44EF41(owner-1, 2, off_480856[...])
    resetView(app);
}

// [RE 0x44A6E0] idx 18：%s強烈地震房屋倒塌
void evt18_earthquake(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectAllObjIds(st));
        ctx.targetObjId = objId;
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[18], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    const int objId = ctx.targetObjId;
    if (objId > 2000 && objId < 4000) {
        Estate& es = st.estates[objId - 2000];
        focusView(app, es.x, es.y);
        st.highlightEstates.clear();
        for (size_t i = 1; i < st.estates.size(); ++i) {
            Estate& e = st.estates[i];
            if (std::strncmp(e.name, es.name, sizeof(es.name)) != 0) {
                continue;
            }
            st.highlightEstates.push_back(static_cast<int>(i));
            if (e.level != 0) {
                --e.level; // [RE 0x44A84E] 同路段每块降一级
                if (e.type != 0) {
                    e.level = 0;
                    e.type = 0;
                }
            }
        }
        blinkHighlight(app);
        resetView(app);
        newsWait(app, 500); // [RE 0x44A906]
    } else if (objId > 4000 && objId < 6000) {
        Corp& cp = st.corps[objId - 4000];
        focusView(app, cp.x, cp.y);
        if (cp.sub != 0) {
            --cp.sub; // [RE 0x44A8E6]
            if (cp.sub == 0) {
                cp.type = 0;
                forceHotelCheckout(st); // [RE 0x44A8EE sub_40DFFA]
            }
        }
        // [差异] 原版单块 corp 高亮：重写机制仅支持 estate（跳过）
        resetView(app);
        newsWait(app, 500);
    }
}

// [RE 0x44A91E] idx 19：%s山洪爆發土地流失
void evt19_flood(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectAllObjIds(st));
        ctx.targetObjId = objId;
        ctx.targetParam = objOwner(st, objId);
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[19], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    if (ctx.targetObjId == 0) {
        return;
    }
    int x = 0;
    int y = 0;
    focusObjId(app, ctx.targetObjId, x, y);
    demolishAtObjId(app, ctx.targetObjId, 1); // [RE 0x44AAAF] 夷平归公
    st.highlightEstates.clear();
    if (ctx.targetObjId < 4000) {
        st.highlightEstates.push_back(ctx.targetObjId - 2000);
    }
    blinkHighlight(app);
    resetView(app);
    newsWait(app, 300); // [RE 0x44AACA]
    // [TODO P4] 业主台词 sub_44EF41(targetParam-1, 2, ...)
}

// [RE 0x44AB2C] idx 20：超級颱風侵襲%s 多處房屋受損
void evt20_typhoon(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectAllObjIds(st));
        ctx.targetObjId = objId;
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[20], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    if (ctx.targetObjId == 0) {
        return;
    }
    int x = 0;
    int y = 0;
    focusObjId(app, ctx.targetObjId, x, y);
    // [RE 0x44AC43] expireAssets(100, 6, 0, -1)：半径 100、estate+corp、降级
    expireAssets(app, x, y, 100, 6, 0, -1);
    playEventFlc(app, 534, 0, 40, 89, false, 8); // [RE 0x44AC71 flags 0x80001]
    newsWait(app, 500);                          // [RE 0x44AC87]
    resetView(app);
}

// [RE 0x44AC99] idx 21：龍捲風侵襲%s 摧毀房屋一棟
void evt21_tornado(NewsCtx& ctx, int phase) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    if (phase == 0) {
        const int objId = pickRandom(collectAllObjIds(st));
        ctx.targetObjId = objId;
        ctx.targetParam = objOwner(st, objId);
        char text[192];
        std::snprintf(text, sizeof(text), kNewsTexts[21], objNameUtf8(st, objId).c_str());
        drawEffectText(ctx, text, 310);
        return;
    }
    if (ctx.targetObjId == 0) {
        return;
    }
    int x = 0;
    int y = 0;
    focusObjId(app, ctx.targetObjId, x, y);
    demolishAtObjId(app, ctx.targetObjId, 0); // [RE 0x44ADFE] 降一级
    playEventFlc(app, 535, 0, 40, 88, false, 8); // [RE 0x44AE2C flags 0x80001]
    newsWait(app, 300);                          // [RE 0x44AE42]
    // [TODO P4] 业主台词 sub_44EF41(targetParam-1, 2, ...)
    resetView(app);
}

// ===== newsEventCheck 触发判定（0x448BE2，36 条；详见研究文档 §1）=====

bool anyJailed(const GameState& st) {
    for (int i = 0; i < 8; ++i) {
        if (st.jailFlags[i] != 0) {
            return true;
        }
    }
    return false;
}

bool anyHospitalized(const GameState& st) {
    for (int i = 0; i < 8; ++i) {
        if (st.hospitalFlags[i] != 0) {
            return true;
        }
    }
    return false;
}

bool anyBuilt(const GameState& st) {
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].level != 0) {
            return true;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].sub != 0) {
            return true;
        }
    }
    return false;
}

// owner==0 存在
bool anyUnowned(const GameState& st) {
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].owner == 0) {
            return true;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].owner == 0) {
            return true;
        }
    }
    return false;
}

// owner!=0 存在
bool anyOwned(const GameState& st) {
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].owner != 0) {
            return true;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].owner != 0) {
            return true;
        }
    }
    return false;
}

bool anyPlayerShares(const GameState& st) {
    const int n = std::min(st.playerCount, 4);
    for (int p = 0; p < n; ++p) {
        if (st.players[p].alive == 0) {
            continue;
        }
        for (int s = 0; s < 12; ++s) {
            if (st.playerShares[p][s] != 0) {
                return true;
            }
        }
    }
    return false;
}

bool anyPedestrian(const GameState& st) {
    const int n = std::min(st.playerCount, 4);
    for (int p = 0; p < n; ++p) {
        if (st.players[p].alive != 0 && st.players[p].travel == 0) {
            return true;
        }
    }
    return false;
}

bool anyVehicleUser(const GameState& st) {
    const int n = std::min(st.playerCount, 4);
    for (int p = 0; p < n; ++p) {
        if (st.players[p].alive != 0 && st.players[p].travel != 0) {
            return true;
        }
    }
    return false;
}

// byte_496986[36*股] != 0（有停牌股）
bool anyStockHalted(const GameState& st) {
    for (int i = 0; i < 12; ++i) {
        if (st.stockHalted[i] != 0) {
            return true;
        }
    }
    return false;
}

// [RE sub_40D73F] 玩家存活且 stateFlags==0（无任何状态）
bool playerNormal(const GameState& st, int p) {
    return p >= 0 && p < 4 && st.players[p].alive != 0 && st.players[p].stateFlags == 0;
}

// specPt owner 非 0 且该玩家正常
bool anyActiveSpecPtOwner(const GameState& st) {
    for (size_t i = 1; i < st.specPts.size(); ++i) {
        const uint8_t owner = st.specPts[i].owner;
        if (owner != 0 && playerNormal(st, static_cast<int>(owner) - 1)) {
            return true;
        }
    }
    return false;
}

// specPt fund > 10000
bool anyRichSpecPt(const GameState& st) {
    for (size_t i = 1; i < st.specPts.size(); ++i) {
        if (st.specPts[i].fund > 10000) {
            return true;
        }
    }
    return false;
}

bool newsEventCheck(const GameState& st, int idx) {
    switch (idx) {
        case 0:
        case 1:
            return anyJailed(st);
        case 2:
        case 3:
            return anyHospitalized(st);
        case 4:
        case 5:
        case 15:
            return anyBuilt(st);
        case 6:
        case 11:
        case 14:
            return true;
        case 7:
            return anyUnowned(st);
        case 8:
        case 9:
        case 12:
            return anyOwned(st);
        case 10:
        case 13:
            return anyPlayerShares(st);
        case 16:
            return anyPedestrian(st);
        case 17:
            return anyVehicleUser(st);
        case 28:
            return anyStockHalted(st);
        case 29:
            return anyActiveSpecPtOwner(st);
        case 35:
            return anyRichSpecPt(st);
        default:
            // 18..27（17 除外）与 30..34 无条件
            return idx >= 18;
    }
}

// ===== 效果函数（g_newsFuncs 0x475E24；phase 0 = 显示阶段，1 = 2400ms 后）=====

using NewsPhaseFn = void (*)(NewsCtx& ctx, int phase);

// 桩文本：展开 %s/%d 为零值（未实现效果的中间提交可安全显示）
std::string stubText(const char* src) {
    std::string out;
    for (const char* p = src; *p != '\0';) {
        if (p[0] == '%' && (p[1] == 's' || p[1] == 'd')) {
            if (p[1] == 'd') {
                out += '0';
            }
            p += 2;
        } else {
            out += *p++;
        }
    }
    return out;
}

// [RE 0x448ECA] idx 0：獄中囚犯無罪開釋
void evt00_jailFree(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    drawEffectText(ctx, kNewsTexts[0], 310);
    int y = 328;
    for (int i = 0; i < 4; ++i) {
        if (st.jailFlags[i] == 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFrameRelease, y);
        // [RE 0x448F31] stateFlags BYTE2 = 0x80（刑满标记，下回合结束在押）
        st.players[i].stateFlags = (st.players[i].stateFlags & 0xFF00FFFFu) | 0x00800000u;
        st.jailFlags[i] = 0;
        y += 42;
    }
}

// [RE 0x448F45] idx 1：獄中囚犯延長刑期%d天（3 天）
void evt01_jailExtend(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    char text[128];
    std::snprintf(text, sizeof(text), kNewsTexts[1], 3);
    drawEffectText(ctx, text, 310);
    int y = 328;
    for (int i = 0; i < 4; ++i) {
        if (st.jailFlags[i] == 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFramePenalty, y);
        // [RE 0x448FEA] BYTE2 = (3 + 旧) & 0x7F
        const uint32_t sf = st.players[i].stateFlags;
        const uint8_t days = static_cast<uint8_t>((sf >> 16) & 0xFF);
        const uint8_t next = static_cast<uint8_t>((3 + days) & 0x7F);
        st.players[i].stateFlags = (sf & 0xFF00FFFFu) | (static_cast<uint32_t>(next) << 16);
        y += 42;
    }
}

// [RE 0x449006] idx 2：住院中病患提前出院
void evt02_hospitalFree(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    drawEffectText(ctx, kNewsTexts[2], 310);
    int y = 328;
    for (int i = 0; i < 4; ++i) {
        if (st.hospitalFlags[i] == 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFrameRelease, y);
        // [RE 0x44906D] stateFlags BYTE3 = 0x80
        st.players[i].stateFlags = (st.players[i].stateFlags & 0x00FFFFFFu) | 0x80000000u;
        st.hospitalFlags[i] = 0;
        y += 42;
    }
}

// [RE 0x449081] idx 3：住院中病患延長住院%d天（3 天）
void evt03_hospitalExtend(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    char text[128];
    std::snprintf(text, sizeof(text), kNewsTexts[3], 3);
    drawEffectText(ctx, text, 310);
    int y = 328;
    for (int i = 0; i < 4; ++i) {
        if (st.hospitalFlags[i] == 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFramePenalty, y);
        // [RE 0x44912A] BYTE3 = (3 + 旧) & 0x7F
        const uint32_t sf = st.players[i].stateFlags;
        const uint8_t days = static_cast<uint8_t>((sf >> 24) & 0xFF);
        const uint8_t next = static_cast<uint8_t>((3 + days) & 0x7F);
        st.players[i].stateFlags = (sf & 0x00FFFFFFu) | (static_cast<uint32_t>(next) << 24);
        y += 42;
    }
}

// [RE 0x44A5D6] idx 16：豪雨特報 行人休息一回合（travel==0 → skipMove=1）
void evt16_rainRest(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    drawEffectText(ctx, kNewsTexts[16], 310);
    int y = 346 + 12;
    for (int i = 0; i < st.playerCount; ++i) {
        if (st.players[i].alive == 0 || st.players[i].travel != 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFrameRest, y);
        st.players[i].skipMove = 1; // [RE 0x44A64A] byte_496BA0 = 1
        y += 32;
    }
}

// [RE 0x44A657] idx 17：交通阻塞 汽車停止一回合（travel!=0 → skipMove=1）
void evt17_trafficStop(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    drawEffectText(ctx, kNewsTexts[17], 310);
    int y = 346 + 12;
    for (int i = 0; i < st.playerCount; ++i) {
        if (st.players[i].alive == 0 || st.players[i].travel == 0) {
            continue;
        }
        drawPiece(ctx, i, kPieceFrameRest, y);
        st.players[i].skipMove = 1; // [RE 0x44A6D3] byte_496BA0 = 1
        y += 32;
    }
}

// [RE 0x44AE89] idx 22：銀行擠兌停止放款１５天（bankFinanceFlags=15）
void evt22_bankRun(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    GameState& st = ctx.app->gameState();
    drawEffectText(ctx, kNewsTexts[22], 310);
    for (int i = 0; i < st.playerCount; ++i) {
        if (st.players[i].alive != 0) {
            st.players[i].bankFinanceFlags = 15; // [RE 0x44AED2] byte_496BA4 = 15
        }
    }
}

// 未实现事件桩（提交 3/4/5 逐个替换；显示展开后的文本便于实机核对）
void evtStub(NewsCtx& ctx, int phase) {
    if (phase != 0) {
        return;
    }
    const std::string text = stubText(kNewsTexts[ctx.eventIdx]);
    drawEffectText(ctx, text.c_str(), 310);
    RICH4_LOGI("news: event %d effect TODO (RE 0x475E24)", ctx.eventIdx);
}

// [RE 0x475E24] g_newsFuncs 36 项（顺序与 kNewsTexts 对齐）
const NewsPhaseFn kNewsFuncs[36] = {
    /* 00 */ evt00_jailFree,
    /* 01 */ evt01_jailExtend,
    /* 02 */ evt02_hospitalFree,
    /* 03 */ evt03_hospitalExtend,
    /* 04 */ evt04_alienAttack,
    /* 05 */ evt05_monsterDestroy,
    /* 06 */ evtStub,
    /* 07 */ evtStub,
    /* 08 */ evtStub,
    /* 09 */ evtStub,
    /* 10 */ evtStub,
    /* 11 */ evtStub,
    /* 12 */ evtStub,
    /* 13 */ evtStub,
    /* 14 */ evt14_haunted,
    /* 15 */ evt15_gasExplosion,
    /* 16 */ evt16_rainRest,
    /* 17 */ evt17_trafficStop,
    /* 18 */ evt18_earthquake,
    /* 19 */ evt19_flood,
    /* 20 */ evt20_typhoon,
    /* 21 */ evt21_tornado,
    /* 22 */ evt22_bankRun,
    /* 23 */ evtStub,
    /* 24 */ evtStub,
    /* 25 */ evtStub,
    /* 26 */ evtStub,
    /* 27 */ evtStub,
    /* 28 */ evtStub,
    /* 29 */ evtStub,
    /* 30 */ evtStub,
    /* 31 */ evtStub,
    /* 32 */ evtStub,
    /* 33 */ evtStub,
    /* 34 */ evtStub,
    /* 35 */ evtStub,
};

// ===== 模态流程 =====

constexpr int kNewsShowMs = 2400; // [RE 0x44B867] sub_4544F6(2400)

bool newsHandler(const SDL_Event* event, void* user) {
    NewsCtx& ctx = *static_cast<NewsCtx*>(user);
    Application& app = *ctx.app;
    if (event == nullptr) {
        // 模态进入：绘制面板/插画/标题 + 效果阶段 0
        ctx.startMs = static_cast<int>(SDL_GetTicks());
        drawNewsBase(ctx);
        kNewsFuncs[ctx.eventIdx](ctx, 0);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (!ctx.phase1Done && SDL_GetTicks() - ctx.startMs >= kNewsShowMs) {
            ctx.phase1Done = true;
            kNewsFuncs[ctx.eventIdx](ctx, 1); // [RE 0x44B875] funcs[v5](1)
            app.events().requestExit(0);
        }
        return true;
    }
    return true; // [RE 0x4544F6(2400)] 阻塞期间不响应输入
}

void newsRun(Application& app, int idx) {
    GameState& st = app.gameState();
    NewsCtx ctx;
    ctx.app = &app;
    ctx.eventIdx = idx;
    // panel.mkf[66]（SMP 2 帧 440×480，帧 0 = 新闻底图）
    if (auto blob = st.panel.read(66)) {
        ctx.panel.load(std::move(*blob));
    } else {
        RICH4_LOGW("news: panel.mkf[66] unavailable (RE 0x44B6FB)");
    }
    // data.mkf[441+idx]（388×251 RGB555 无头位图）
    if (auto blob = st.data.read(static_cast<size_t>(441 + idx))) {
        const RawBitmap raw = decodeRawBitmap(*blob);
        if (raw.valid()) {
            ctx.artW = raw.width;
            ctx.artH = raw.height;
            ctx.art.assign(raw.pixels, raw.pixels + static_cast<size_t>(raw.width) * raw.height);
        } else {
            RICH4_LOGW("news: data.mkf[%d] raw bitmap invalid (RE 0x44B768)", 441 + idx);
        }
    } else {
        RICH4_LOGW("news: data.mkf[%d] unavailable (RE 0x44B768)", 441 + idx);
    }
    RICH4_LOGI("news event: idx %d text=%s (RE 0x44B6DF)", idx, kNewsTexts[idx]);
    runModal(app, &newsHandler, &ctx, 16);
}

} // namespace

void newsEvent(Application& app) {
    GameState& st = app.gameState();
    // [RE 0x44B724] 抽取：取 g_newsOrder[g_newsPos] → check → ++pos（无论是否触发）→
    //   未触发继续；触发即显示。正常数据必有可触发事件。
    int idx = -1;
    for (int guard = 0; guard < 36; ++guard) {
        const int v = st.newsOrder[st.newsPos];
        const bool fire = newsEventCheck(st, v);
        if (++st.newsPos == 36) {
            st.newsPos = 0;
        }
        if (fire) {
            idx = v;
            break;
        }
    }
    if (idx < 0) {
        // [NEW] 防御：36 条全不可触发（原版此处死循环）→ 强制显示当前条
        idx = st.newsOrder[st.newsPos];
        RICH4_LOGW("news: no fireable event in 36 draws, forcing idx %d (RE 0x44B6DF)", idx);
    }
    newsRun(app, idx);
}

void newsDebugFire(Application& app, int idx) {
    if (idx < 0) {
        newsEvent(app);
        return;
    }
    newsRun(app, std::max(0, std::min(35, idx)));
}

} // namespace rich4
