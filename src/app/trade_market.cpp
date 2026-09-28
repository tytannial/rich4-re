#include "game/app/trade_market.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/app/confirm_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/number_input_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
#include "game/core/clock.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

// 颜色（RGB888）
constexpr uint32_t kTextGray = 0xF0F0F0;  // 15790320
constexpr uint32_t kTextDark = 0x101010;  // 1052688
constexpr uint32_t kBtnWhite = 0xFFFFFF;
constexpr uint32_t kBtnShadow = 0x800000;
// [RE 0x425C23 / 0x4262CC] 股票/地产列表行悬停框：调用点 push 0FFFFFFh → 白色
constexpr uint16_t kHoverWhite = rgb888To555(0xFFFFFF);

// 挂单类型（g_miscTable336 +0；旧文档 3/4 写反，见 4284be-trade-market.md）
constexpr int kTradeStock = 1;
constexpr int kTradeLand = 2;
constexpr int kTradeItem = 3;
constexpr int kTradeCard = 4;

// 主面板几何（原版 sub_427C21/sub_4249C2）
constexpr int kMainX = 22;
constexpr int kMainY = 66;
constexpr int kGridX0 = 104;  // 格子 72x72 起点
constexpr int kGridY0 = 114;
constexpr int kBidX = 464;   // 投标按钮带 (464..536, 74..114)
constexpr int kBidY = 74;
constexpr int kBidW = 72;
constexpr int kBidH = 40;
constexpr int kPanelX = 464;  // 帧17 子按钮底板 @ (464,116) 144x96
constexpr int kPanelY = 116;
constexpr int kSubX = 473;  // 子按钮 2x2（63x39 步进）
constexpr int kSubY = 125;
constexpr int kSubStepX = 63;
constexpr int kSubStepY = 39;
constexpr int kSubW = 62;
constexpr int kSubH = 38;
constexpr int kExitX = 536;  // EXIT (536..608, 74..114)
constexpr int kExitY = 74;

// 详情按钮（原版 sub_42704E：左 = 撤件/購買 x偏移16、右 = EXIT x偏移104，24 高）
constexpr int kDetailBtnL = 16;
constexpr int kDetailBtnR = 104;
constexpr int kDetailBtnW = 72;
constexpr int kDetailBtnH = 24;

// 地产挂单列表（原版 sub_424AEA/sub_42608F）
constexpr int kLandListY0 = 96;   // 行高亮起（文本 y=112+32i）
constexpr int kLandRowStep = 32;
constexpr int kLandRows = 11;
constexpr int kLandTabY = 32;
constexpr int kLandTabStep = 80;
constexpr int kLandBtnX = 512;  // 右侧 3 按钮 x 512..527
constexpr int kLandColX[5] = {147, 231, 319, 395, 471};  // word_4754B0

// 道具/卡片挂单对话框（帧3/帧4 @ (140,160)）；格子 5 列 × 3 行
constexpr int kBagX = 140;
constexpr int kBagY = 160;
constexpr int kBagCols = 5;
constexpr int kBagCellX0 = 176;  // 原版 v45 起点（图标 = x-16）
constexpr int kBagCellY0 = 208;  // 原版 v46 起点
constexpr int kBagStepX = 72;
constexpr int kBagStepY = 32;
constexpr int kBagHitY0 = 192;
constexpr int kBagExitL = 429;  // EXIT 命中 (429..499, 161..191)
constexpr int kBagExitT = 161;
constexpr int kBagExitR = 499;
constexpr int kBagExitB = 191;

// [RE 0x452793] 千分位
void fmtMoney(char* out, size_t cap, int32_t value) {
    char tmp[24];
    const bool neg = value < 0;
    unsigned u = neg ? static_cast<unsigned>(-(value + 1)) + 1u : static_cast<unsigned>(value);
    std::snprintf(tmp, sizeof(tmp), "%u", u);
    const int n = static_cast<int>(std::strlen(tmp));
    char rev[32];
    int r = 0;
    int cnt = 0;
    for (int i = n - 1; i >= 0; --i) {
        rev[r++] = tmp[i];
        if (++cnt % 3 == 0 && i > 0) rev[r++] = ',';
    }
    int w = 0;
    if (neg && w < static_cast<int>(cap) - 1) out[w++] = '-';
    for (int i = r - 1; i >= 0 && w < static_cast<int>(cap) - 1; --i) out[w++] = rev[i];
    out[w] = '\0';
}

// [RE 0x452793 + aS_43 0x463EE0 "%s元"] 详情价格格式（千分位 + 元）
void fmtMoneyYuan(char* out, size_t cap, int32_t value) {
    fmtMoney(out, cap, value);
    const size_t n = std::strlen(out);
    if (n + 4 < cap) {
        std::memcpy(out + n, "元", 3);
        out[n + 3] = '\0';
    }
}

// 日期 "%02d/%d/%d"（expireDate BYTE0=日 BYTE1=月 HIWORD=年）或「無限期」
void fmtDate(char* out, size_t cap, uint32_t v) {
    if (v == 0) {
        std::snprintf(out, cap, "%s", "無限期");  // [RE byte_463E42]
        return;
    }
    std::snprintf(out, cap, "%02d/%d/%d", static_cast<int>((v >> 16) % 100),
                  static_cast<int>((v >> 8) & 0xF), static_cast<int>(v & 0xFF));
}

// [RE 0x424502] 提示框：panel[73] 帧5（184×88）不透明 @ (227, y) + 文本居中 (320, y+44)
//   y = 42（上）/ 196（下）；字体 setTextFont(16, 0x101010, 0, 2, 1)
void drawTradeTipBox(Application& app, const UiImage& sheet, const char* text, int y) {
    Surface& dst = app.surface();
    if (sheet.frameCount() > 5) {
        blitElementOpaque(dst, sheet.frame(5), 227, y);
    }
    app.text().setFont(16, kTextDark, 0, 2, 1);
    app.text().drawText(dst, text, 320, y + 44, 4);
}

// [RE 0x424502 + 0x4528B9(1500) + 0x424620] 阻塞提示（主面板「公佈欄已滿」；下方 y=196）
struct TradeTipState {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    const char* text = nullptr;
    int y = 42;
    uint64_t startMs = 0;
};

bool tradeTipEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<TradeTipState*>(user);
    if (!event) {
        drawTradeTipBox(*s.app, *s.sheet, s.text, s.y);
        s.startMs = nowMs();
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (nowMs() - s.startMs >= 1500) {
            s.app->events().requestExit(0);
        } else {
            drawTradeTipBox(*s.app, *s.sheet, s.text, s.y);
        }
        return true;
    }
    return true;  // 原版 sub_4528B9 纯延时，期间不响应输入
}

void tradeTipBlocking(Application& app, const UiImage& sheet, const char* text, int y) {
    TradeTipState s;
    s.app = &app;
    s.sheet = &sheet;
    s.text = text;
    s.y = y;
    runModal(app, &tradeTipEvent, &s, 16);
}

// 挂单价换算（原版 byte_47FDEF/byte_47FEDF 点券价 ×100 × 物價指數）
int32_t cardTradePrice(const GameState& st, int cardId) {
    if (cardId < 1 || cardId > 30) return 0;
    return static_cast<int32_t>(kCardPrices[cardId]) * 100 * st.moneyMul;
}
int32_t itemTradePrice(const GameState& st, int itemId) {
    if (itemId < 1 || itemId > 13) return 0;
    return static_cast<int32_t>(kItemPrice[itemId - 1]) * 100 * st.moneyMul;
}

// 地产估值（原版 (priceAdd + level*priceBase)*M / (buildPrice + feeTable[0]*sub)*M）
int32_t landOrderValue(const GameState& st, int objId) {
    if (objId >= 4000) {
        const size_t i = static_cast<size_t>(objId - 4000);
        if (i >= st.corps.size()) return 0;
        const Corp& cp = st.corps[i];
        return static_cast<int32_t>(
            (cp.buildPrice + static_cast<int32_t>(cp.feeTable[0]) * cp.sub) * st.moneyMul);
    }
    const size_t i = static_cast<size_t>(objId - 2000);
    if (i >= st.estates.size()) return 0;
    const Estate& es = st.estates[i];
    return static_cast<int32_t>(
        (es.priceAdd + static_cast<int32_t>(es.priceBase) * es.level) * st.moneyMul);
}

bool tradeSlotInvalid(const GameState& st, int player, const TradeSlot& t) {
    switch (t.type) {
        case kTradeStock: {
            const int s = t.objId;
            if (s < 0 || s >= 12) return true;
            return t.count > st.playerShares[player][s];
        }
        case kTradeLand: {
            if (t.objId >= 4000) {
                const size_t i = static_cast<size_t>(t.objId - 4000);
                if (i >= st.corps.size()) return true;
                const Corp& cp = st.corps[i];
                return cp.owner != player + 1 || cp.type != t.snapType || cp.sub != t.snapLevel;
            }
            const size_t i = static_cast<size_t>(t.objId - 2000);
            if (i >= st.estates.size()) return true;
            const Estate& es = st.estates[i];
            return es.owner != player + 1 || es.type != t.snapType || es.level != t.snapLevel;
        }
        case kTradeItem:
            if (t.objId < 1 || t.objId > 13) return true;
            return st.itemStock[15 * player + t.objId - 1] == 0;
        case kTradeCard:
            return !cardBagHas(st, player, t.objId);
        default:
            return false;  // type 0 = 空槽，保留
    }
}

// [RE 0x423B3B] collectOwnedAssetIds（query_dialog 同源；本档独立副本，避免改动已验证代码）
int collectOwnedAssets(const GameState& st, int player, int mode,
                       std::vector<uint16_t>& out) {
    out.clear();
    const uint8_t owner = static_cast<uint8_t>(player + 1);
    auto pushEstates = [&](bool needLevel, bool wantChain) {
        for (size_t i = 1; i < st.estates.size(); ++i) {
            const Estate& es = st.estates[i];
            if (es.owner != owner) continue;
            if (needLevel && (es.level == 0 || (es.type != 0) != wantChain)) continue;
            out.push_back(static_cast<uint16_t>(i + 2000));
        }
    };
    auto pushCorps = [&]() {
        for (size_t i = 1; i < st.corps.size(); ++i) {
            if (st.corps[i].owner == owner) {
                out.push_back(static_cast<uint16_t>(i + 4000));
            }
        }
    };
    switch (mode) {
        case 0: pushEstates(false, false); pushCorps(); break;
        case 1: pushEstates(false, false); break;
        case 2: pushCorps(); break;
        case 3: pushEstates(true, false); break;
        case 4: pushEstates(true, true); break;
        default: break;
    }
    return static_cast<int>(out.size());
}

// ===== 股票挂单对话框 [RE 0x4258C1] =====
// 前置声明（定义在 drawTradeMain 附近）：子对话框内数字框取消后整层重绘
void redrawSubLayer(Application& app, const UiImage& sheet);

struct StockOrderUi {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    const UiImage* mainSheet = nullptr;  // 主面板 panel[73]（整层重绘用）
    std::vector<uint8_t> rows;  // 行 → 0-based 股票号
    int hoverRow = 0;           // 列表行悬停白框（原版 WM_MOUSEMOVE 有）
    bool exitDown = false;      // EXIT 按下态（原版 DBLCLK 画帧18；重写单击 = DOWN 画、UP 退出）
};

void drawStockOrder(StockOrderUi& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    const UiImage& sh = *s.sheet;
    TextRenderer& t = s.app->text();
    // 帧1（336x416）@ (152,32)；列头文字原版预绘于帧内 (48,16)/(144,16)/(250,16)
    blitElementOpaque(dst, sh.frame(1), 152, 32);
    t.setFont(16, kTextGray, kTextDark, 3, 1);
    t.drawText(dst, "股票名稱", 200, 48, 2);
    t.drawText(dst, "持有張數", 296, 48, 2);
    t.drawText(dst, "總 市 價", 402, 48, 2);
    const int gn = st.gameMode * 4 + st.mapIndex;
    const int cur = st.currentPlayer;
    char buf[32];
    int y = 80;
    for (size_t i = 0; i < s.rows.size(); ++i) {
        const int idx = s.rows[i];
        t.drawText(dst, kStockNames[gn][idx], 200, y, 2);
        const int n = st.playerShares[cur][idx];
        std::snprintf(buf, sizeof(buf), "%d", n);
        t.drawText(dst, buf, 332, y, 6);
        fmtMoney(buf, sizeof(buf), static_cast<int32_t>(n * st.stocks[idx][5]));
        t.drawText(dst, buf, 478, y, 6);
        y += 32;
    }
    if (sh.frameCount() > 18 && s.exitDown) {
        // [RE 0x4258C1] EXIT 按下态帧18（底图 EXIT 图案在帧1 内 (311,6)）
        blitElementOpaque(dst, sh.frame(18), 463, 38);
    }
    if (s.hoverRow > 0) {
        drawRectBorder(dst, 152, 64 + 32 * (s.hoverRow - 1), 336, 32, kHoverWhite);
    }
}

bool stockOrderEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<StockOrderUi*>(user);
    if (!event) {
        drawStockOrder(s);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 列表行悬停黄框（原版 WM_MOUSEMOVE 仅处理列表行；EXIT 无 hover——见 DOWN 分支）
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int row = 0;
        if (x > 152 && x < 152 + 336 && y > 64 && y < 64 + 32 * static_cast<int>(s.rows.size())) {
            row = (y - 64) / 32 + 1;
        }
        if (row != s.hoverRow) {
            s.hoverRow = row;
            drawStockOrder(s);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        // EXIT 按下态（原版 WM_LBUTTONDBLCLK 画帧18；重写单击 = DOWN 画、UP 退出）
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        if (x >= 463 && x <= 484 && y >= 38 && y <= 59 && !s.exitDown) {
            s.exitDown = true;
            drawStockOrder(s);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        // 用 UP 坐标重算命中（不依赖 MOUSEMOVE；原版 UP 按 byte_48C2B8 分发）
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        s.exitDown = false;
        int row = 0;
        if (x > 152 && x < 152 + 336 && y > 64 &&
            y < 64 + 32 * static_cast<int>(s.rows.size())) {
            row = (y - 64) / 32 + 1;
        }
        // 原版 EXIT 需 WM_LBUTTONDBLCLK 置 99 后下次 UP 生效（双击）；重写单击退出（差异）
        if (x >= 463 && x <= 484 && y >= 38 && y <= 59) {
            s.exitDown = false;
            s.app->events().requestExit(0);
            return true;
        }
        if (row <= 0 || row > static_cast<int>(s.rows.size())) {
            s.exitDown = false;
            s.hoverRow = 0;
            drawStockOrder(s);
            return true;
        }
        GameState& st = s.app->gameState();
        const int stock = s.rows[static_cast<size_t>(row - 1)];
        // [RE 0x4258C1] 张数（提示框 byte_463EA0 + 数字框）→ 价格（提示框 byte_463EB3）→ queue
        const int maxCount = st.playerShares[st.currentPlayer][stock];
        drawTradeTipBox(*s.app, *s.sheet, "請輸入欲賣出的張數", 42);  // [RE byte_463EA0]
        const int n = numberInputDialog(*s.app, maxCount);
        if (n <= 0) {
            redrawSubLayer(*s.app, *s.mainSheet);  // 提示框 y=42 超出子对话框区域 → 整层重绘
            drawStockOrder(s);
            return true;
        }
        const int32_t value = static_cast<int32_t>(n * st.stocks[stock][5]);
        char tip[64];
        std::snprintf(tip, sizeof(tip), "請輸入欲拍賣的價格\n\n（市價：%d元）", value);
        drawTradeTipBox(*s.app, *s.sheet, tip, 42);  // [RE byte_463EB3]
        const int price = numberInputDialog(*s.app, 10 * value);
        if (price > 0) {
            queueTradeOrder(st, st.currentPlayer, kTradeStock, stock, price, n);
            s.app->events().requestExit(0);
            return true;
        }
        redrawSubLayer(*s.app, *s.mainSheet);
        drawStockOrder(s);
        return true;
    }
    return false;
}

// ===== 地产挂单对话框 [RE 0x42608F + 0x424AEA] =====
struct LandOrderUi {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    const UiImage* mainSheet = nullptr;  // 主面板 panel[73]（整层重绘用）
    int tab = 0;         // dword_4754C2（5 子页签）
    int pageStart = 0;   // dword_4754BA
    int pageShown = 0;   // dword_4754BE
    std::vector<uint16_t> assets;  // word_48BE6E
    int hoverRow = 0;      // 列表行悬停黄框（原版 WM_MOUSEMOVE 有）
    int pressedBtn = 0;    // 右侧按钮按下态（原版 byte_48C2BA，DOWN 判定 + highlightRect）
};

void landUpdatePage(LandOrderUi& s) {
    const int total = static_cast<int>(s.assets.size());
    if (s.pageStart > total) s.pageStart = 0;
    s.pageShown = std::min(kLandRows, total - s.pageStart);
    if (s.pageShown < 0) s.pageShown = 0;
}

void drawLandOrder(LandOrderUi& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    const UiImage& sh = *s.sheet;
    TextRenderer& t = s.app->text();
    blitElementOpaque(dst, sh.frame(2), 112, 32);
    // 当前页签高亮帧19 @ (112+80*tab, 32)
    if (sh.frameCount() > 19) {
        blitElementOpaque(dst, sh.frame(19), 112 + kLandTabStep * s.tab, kLandTabY);
    }
    // 页签文字（深灰 font20，对齐原版预绘 (40+80i,16) → 屏幕 (152+80i,48)）
    t.setFont(20, kTextDark, 0, 2, 1);
    for (int i = 0; i < 5; ++i) {
        t.drawText(dst, kQuerySubPageNames[i], 152 + kLandTabStep * i, 48, 2);
    }
    // 列头（白 font20；原版预绘帧内 (word_4754B0-112, 48) → 屏幕 y=48+32=80）
    t.setFont(20, kTextGray, 0, 2, 1);
    for (int i = 0; i < 5; ++i) {
        t.drawText(dst, kQueryLandCols[i], kLandColX[i], 80, 2);
    }
    // 列表（白 font16）：普通/住宅与商業用地行色（原版 estate 0xF0F0F0 / corp 同）
    t.setFont(16, kTextGray, kTextDark, 3, 1);
    const int cur = st.currentPlayer;
    // 当前玩家连锁店总租（原版 v4 = estateRouteRent(cur+1, 0)）
    int chainCount = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].owner == cur + 1 && st.estates[i].type != 0) ++chainCount;
    }
    const int32_t chainRent = chainCount * 2000 * st.moneyMul;
    char buf[48];
    for (int row = 0; row < s.pageShown; ++row) {
        const int idx = s.pageStart + row;
        const uint16_t id = s.assets[static_cast<size_t>(idx)];
        const int y = 112 + kLandRowStep * row;
        if (id >= 4000) {
            const size_t ci = id - 4000;
            if (ci >= st.corps.size()) continue;
            const Corp& cp = st.corps[ci];
            const std::string name =
                big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
            t.drawText(dst, name.c_str(), kLandColX[0], y, 2);
            if (cp.sub != 0 && cp.type <= 4) {
                t.drawText(dst, kBuildingNames[6 + cp.type], kLandColX[1], y, 2);
            } else {
                t.drawText(dst, kBuildingNames[0], kLandColX[1], y, 2);
            }
            std::snprintf(buf, sizeof(buf), "%d",
                          st.moneyMul * (cp.buildPrice + cp.feeTable[0] * cp.sub));
            t.drawText(dst, buf, kLandColX[2] + 33, y, 6);
            const int fee = (cp.type != 0 && cp.sub != 0 && cp.sub < 6)
                                ? st.moneyMul * cp.feeTable[cp.sub]
                                : 0;
            std::snprintf(buf, sizeof(buf), "%d", fee);
            t.drawText(dst, buf, kLandColX[3] + 29, y, 6);
            fmtDate(buf, sizeof(buf), cp.expireDate);
            t.drawText(dst, buf, kLandColX[4], y, 2);
        } else {
            const size_t ei = id - 2000;
            if (ei >= st.estates.size()) continue;
            const Estate& es = st.estates[ei];
            const std::string name = big5ToUtf8(es.name, sizeof(es.name));
            t.drawText(dst, name.c_str(), kLandColX[0], y, 2);
            int32_t fee = 0;
            if (es.type != 0) {
                t.drawText(dst, "連鎖店", kLandColX[1], y, 2);  // [RE byte_463E30]
                fee = chainRent;
            } else {
                const int lv = es.level < 6 ? es.level : 0;
                t.drawText(dst, kBuildingNames[lv], kLandColX[1], y, 2);
                fee = estateRouteRent(st, es.owner, es);
            }
            std::snprintf(buf, sizeof(buf), "%d",
                          st.moneyMul * (es.priceAdd + es.priceBase * es.level));
            t.drawText(dst, buf, kLandColX[2] + 33, y, 6);
            std::snprintf(buf, sizeof(buf), "%d", fee);
            t.drawText(dst, buf, kLandColX[3] + 29, y, 6);
            fmtDate(buf, sizeof(buf), es.expireDate);
            t.drawText(dst, buf, kLandColX[4], y, 2);
        }
    }
    // 行悬停（黄框）
    if (s.hoverRow > 0) {
        drawRectBorder(dst, 112, kLandListY0 + kLandRowStep * (s.hoverRow - 1), 416,
                       kLandRowStep, kHoverWhite);
    }
    // 右侧 3 按钮：仅按下态显示下沉（原版 WM_LBUTTONDOWN 判定 + highlightRect，无 hover）
    if (s.pressedBtn > 0 && s.pressedBtn <= 3) {
        static const int kBtnY[3] = {33, 65, 97};
        static const int kBtnH[3] = {17, 30, 30};
        pressDown(dst, kLandBtnX, kBtnY[s.pressedBtn - 1], 15, kBtnH[s.pressedBtn - 1], 1,
                  kChannelHalf);
    }
}

bool landOrderEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<LandOrderUi*>(user);
    GameState& st = s.app->gameState();
    if (!event) {
        drawLandOrder(s);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 列表行悬停黄框（原版 WM_MOUSEMOVE 仅处理列表行；按钮无 hover）
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int row = 0;
        if (x > 112 && x < 528 && y > kLandListY0 &&
            y < kLandListY0 + kLandRowStep * s.pageShown) {
            const int r = (y - kLandListY0) / kLandRowStep;
            if (r < s.pageShown) row = r + 1;
        }
        if (row != s.hoverRow) {
            s.hoverRow = row;
            drawLandOrder(s);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        // 页签：DOWN 立即切换（sub_424AEA(tab,0) 重收集）
        for (int i = 0; i < 5; ++i) {
            const int sx = 112 + kLandTabStep * i;
            if (x >= sx && x <= sx + kLandTabStep && y >= kLandTabY && y <= kLandTabY + 32) {
                if (s.tab != i) {
                    s.tab = i;
                    collectOwnedAssets(st, st.currentPlayer, s.tab, s.assets);
                    s.pageStart = 0;
                    landUpdatePage(s);
                    drawLandOrder(s);
                }
                return true;
            }
        }
        if (s.pressedBtn > 0) {
            s.pressedBtn = 0;
            drawLandOrder(s);
            return true;
        }
        // 右侧按钮：DOWN 坐标判定（原版 WM_LBUTTONDOWN → byte_48C2BA + highlightRect）
        int btn = 0;
        if (x >= kLandBtnX && x <= kLandBtnX + 15) {
            if (y > 32 && y < 62) btn = 1;        // EXIT
            else if (y > 64 && y < 96) btn = 2;   // 上一页
            else if (y > 96 && y < 128) btn = 3;  // 下一页
        }
        if (btn > 0) {
            s.pressedBtn = btn;
            drawLandOrder(s);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        // 按钮（原版 byte_48C2BA 分发：1=退出 2=上一页 3=下一页）
        if (s.pressedBtn == 1) {
            s.pressedBtn = 0;
            s.app->events().requestExit(0);
            return true;
        }
        if (s.pressedBtn == 2) {
            s.pressedBtn = 0;
            if (s.pageStart > 0) {
                s.pageStart -= 11;
                if (s.pageStart < 0) s.pageStart = 0;
                landUpdatePage(s);
            }
            drawLandOrder(s);
            return true;
        }
        if (s.pressedBtn == 3) {
            s.pressedBtn = 0;
            if (s.pageStart + 11 < static_cast<int>(s.assets.size())) {
                s.pageStart += 11;
                landUpdatePage(s);
            }
            drawLandOrder(s);
            return true;
        }
        // 列表行（UP 坐标重算，不依赖 MOUSEMOVE）：提示框 + 价格输入 → queueTradeOrder(type=2)
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        int row = 0;
        if (x > 112 && x < 528 && y > kLandListY0 &&
            y < kLandListY0 + kLandRowStep * s.pageShown) {
            const int r = (y - kLandListY0) / kLandRowStep;
            if (r < s.pageShown) row = r + 1;
        }
        if (row > 0) {
            const uint16_t id = s.assets[static_cast<size_t>(s.pageStart + row - 1)];
            const int32_t value = landOrderValue(st, id);
            char tip[64];
            std::snprintf(tip, sizeof(tip), "請輸入欲拍賣的價格\n\n（市價：%d元）", value);
            drawTradeTipBox(*s.app, *s.sheet, tip, 42);  // [RE byte_463EB3]
            const int price = numberInputDialog(*s.app, 10 * value);
            if (price > 0) {
                queueTradeOrder(st, st.currentPlayer, kTradeLand, id, price, 0);
                s.app->events().requestExit(0);
                return true;
            }
            redrawSubLayer(*s.app, *s.mainSheet);
            drawLandOrder(s);
            return true;
        }
        drawLandOrder(s);
        return true;
    }
    return false;
}

// ===== 道具挂单对话框 [RE 0x4267A4] =====
struct ItemOrderUi {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    const UiImage* icons = nullptr;
    const UiImage* mainSheet = nullptr;  // 主面板 panel[73]（整层重绘用）
    std::vector<uint8_t> ids;  // 可见道具 id（1..13）
    // 原版无 hover：命中在 DBLCLK（highlightRect 下沉）；重写单击 = DOWN 记录 + 绘制按下态
    int pressedCell = -1;
    bool pressedExit = false;
};

void drawItemOrder(ItemOrderUi& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    // [RE 0x4267A4] sub_456418 色键（非不透明）；帧3 顶部整行为透明，露出主面板木纹
    blitElement(dst, s.sheet->frame(3), kBagX, kBagY, false);
    TextRenderer& t = s.app->text();
    t.setFont(16, kBtnWhite, kTextDark, 3, 0);
    char buf[24];
    for (size_t i = 0; i < s.ids.size(); ++i) {
        const int id = s.ids[i];
        const int col = static_cast<int>(i) % kBagCols;
        const int row = static_cast<int>(i) / kBagCols;
        const int cx = kBagCellX0 + kBagStepX * col;
        const int cy = kBagCellY0 + kBagStepY * row;
        if (s.icons && id - 1 < s.icons->frameCount()) {
            blitElement(dst, s.icons->frame(id - 1), cx - 16, cy, false);  // 色键
        }
        std::snprintf(buf, sizeof(buf), "×%d", st.itemStock[15 * st.currentPlayer + id - 1]);
        t.drawText(dst, buf, cx + 30, cy, 6);
    }
    // 按下态（原版 highlightRect：格子 (141+72c, 193+32r, 70, 30) / EXIT 命中区）
    if (s.pressedCell >= 0 && s.pressedCell < static_cast<int>(s.ids.size())) {
        const int col = s.pressedCell % kBagCols;
        const int row = s.pressedCell / kBagCols;
        pressDown(dst, 141 + kBagStepX * col, 193 + kBagStepY * row, 70, 30, 1, kChannelHalf);
    }
    if (s.pressedExit) {
        pressDown(dst, kBagExitL, kBagExitT, kBagExitR - kBagExitL, kBagExitB - kBagExitT, 1,
                  kChannelHalf);
    }
}

bool itemOrderEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<ItemOrderUi*>(user);
    if (!event) {
        drawItemOrder(s);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 原版本对话框无 WM_MOUSEMOVE 处理（无 hover 效果）
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        // 按下态（原版 DBLCLK 命中 → highlightRect；重写单击 = DOWN 记录）
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        if (x >= kBagExitL && x <= kBagExitR && y >= kBagExitT && y <= kBagExitB) {
            s.pressedExit = true;
            s.pressedCell = -1;
            drawItemOrder(s);
            return true;
        }
        if (x > kBagX && x < kBagX + kBagCols * kBagStepX && y > kBagHitY0 &&
            y < kBagHitY0 + 3 * kBagStepY) {
            const int col = (x - kBagX) / kBagStepX;
            const int row = (y - kBagHitY0) / kBagStepY;
            if (col < kBagCols && row < 3) {
                const int idx = row * kBagCols + col;
                if (idx < static_cast<int>(s.ids.size())) {
                    s.pressedCell = idx;
                    s.pressedExit = false;
                    drawItemOrder(s);
                }
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        // 原版道具/卡片命中在 WM_LBUTTONDBLCLK（需双击）；重写单击（差异）
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        s.pressedCell = -1;
        s.pressedExit = false;
        if (x >= kBagExitL && x <= kBagExitR && y >= kBagExitT && y <= kBagExitB) {
            s.app->events().requestExit(0);
            return true;
        }
        int cell = -1;
        if (x > kBagX && x < kBagX + kBagCols * kBagStepX && y > kBagHitY0 &&
            y < kBagHitY0 + 3 * kBagStepY) {
            const int col = (x - kBagX) / kBagStepX;
            const int row = (y - kBagHitY0) / kBagStepY;
            if (col < kBagCols && row < 3) {
                const int idx = row * kBagCols + col;
                if (idx < static_cast<int>(s.ids.size())) cell = idx;
            }
        }
        if (cell >= 0) {
            GameState& st = s.app->gameState();
            const int itemId = s.ids[static_cast<size_t>(cell)];
            const int32_t value = itemTradePrice(st, itemId);
            char tip[64];
            std::snprintf(tip, sizeof(tip), "請輸入欲拍賣的價格\n\n（市價：%d元）", value);
            drawTradeTipBox(*s.app, *s.sheet, tip, 42);  // [RE byte_463EB3]
            const int price = numberInputDialog(*s.app, 10 * value);
            if (price > 0) {
                queueTradeOrder(st, st.currentPlayer, kTradeItem, itemId, price, 0);
                s.app->events().requestExit(0);
                return true;
            }
            redrawSubLayer(*s.app, *s.mainSheet);
            drawItemOrder(s);
            return true;
        }
        drawItemOrder(s);
        return true;
    }
    return false;
}

// ===== 卡片挂单对话框 [RE 0x426C2E] =====
struct CardOrderUi {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    const UiImage* mainSheet = nullptr;  // 主面板 panel[73]（整层重绘用）
    // 原版无 hover：命中在 DBLCLK（highlightRect）；重写单击 = DOWN 记录 + 按下态
    int pressedSlot = -1;  // 0..14
    bool pressedExit = false;
};

void drawCardOrder(CardOrderUi& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    // [RE 0x426C2E] sub_456418 色键；帧4 顶部整行透明
    blitElement(dst, s.sheet->frame(4), kBagX, kBagY, false);
    TextRenderer& t = s.app->text();
    t.setFont(16, kBtnWhite, kTextDark, 3, 0);
    for (int slot = 0; slot < 15; ++slot) {
        const uint8_t id = st.cardState60[15 * st.currentPlayer + slot];
        if (id == 0) continue;
        const int col = slot % 5;
        const int row = slot / 5;
        t.drawText(dst, kCardNames[id], kBagCellX0 + kBagStepX * col, kBagCellY0 + kBagStepY * row,
                   2);
    }
    // 按下态（原版 highlightRect：格子 (141+72c, 193+32r, 70, 30) / EXIT 命中区）
    if (s.pressedSlot >= 0) {
        const int col = s.pressedSlot % 5;
        const int row = s.pressedSlot / 5;
        pressDown(dst, 141 + kBagStepX * col, 193 + kBagStepY * row, 70, 30, 1, kChannelHalf);
    }
    if (s.pressedExit) {
        pressDown(dst, kBagExitL, kBagExitT, kBagExitR - kBagExitL, kBagExitB - kBagExitT, 1,
                  kChannelHalf);
    }
}

bool cardOrderEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<CardOrderUi*>(user);
    if (!event) {
        drawCardOrder(s);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 原版本对话框无 WM_MOUSEMOVE 处理（无 hover 效果）
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        if (x >= kBagExitL && x <= kBagExitR && y >= kBagExitT && y <= kBagExitB) {
            s.pressedExit = true;
            s.pressedSlot = -1;
            drawCardOrder(s);
            return true;
        }
        if (x > kBagX && x < kBagX + 5 * kBagStepX && y > kBagHitY0 &&
            y < kBagHitY0 + 3 * kBagStepY) {
            const int col = (x - kBagX) / kBagStepX;
            const int row = (y - kBagHitY0) / kBagStepY;
            const int idx = row * 5 + col;
            if (col < 5 && row < 3 && idx < 15 &&
                s.app->gameState().cardState60[15 * s.app->gameState().currentPlayer + idx] != 0) {
                s.pressedSlot = idx;
                s.pressedExit = false;
                drawCardOrder(s);
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        s.pressedSlot = -1;
        s.pressedExit = false;
        if (x >= kBagExitL && x <= kBagExitR && y >= kBagExitT && y <= kBagExitB) {
            s.app->events().requestExit(0);
            return true;
        }
        GameState& st = s.app->gameState();
        int slot = -1;
        if (x > kBagX && x < kBagX + 5 * kBagStepX && y > kBagHitY0 &&
            y < kBagHitY0 + 3 * kBagStepY) {
            const int col = (x - kBagX) / kBagStepX;
            const int row = (y - kBagHitY0) / kBagStepY;
            const int idx = row * 5 + col;
            if (col < 5 && row < 3 && idx < 15 &&
                st.cardState60[15 * st.currentPlayer + idx] != 0) {
                slot = idx;
            }
        }
        if (slot >= 0) {
            const int card = st.cardState60[15 * st.currentPlayer + slot];
            const int32_t value = cardTradePrice(st, card);
            char tip[64];
            std::snprintf(tip, sizeof(tip), "請輸入欲拍賣的價格\n\n（市價：%d元）", value);
            drawTradeTipBox(*s.app, *s.sheet, tip, 42);  // [RE byte_463EB3]
            const int price = numberInputDialog(*s.app, 10 * value);
            if (price > 0) {
                queueTradeOrder(st, st.currentPlayer, kTradeCard, card, price, 0);
                s.app->events().requestExit(0);
                return true;
            }
            redrawSubLayer(*s.app, *s.mainSheet);
            drawCardOrder(s);
            return true;
        }
        drawCardOrder(s);
        return true;
    }
    return false;
}

// ===== 详情对话框 [RE 0x42704E] =====
struct TradeDetailUi {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int seller = 0;
    int slot = 0;
    int hlBtn = 0;  // 1=撤件/購買 2=EXIT
};

// [RE 0x4754A8] 详情背景帧：股票=7 地产=8 道具/卡片=6
int detailFrameFor(int type) {
    if (type == kTradeStock) return 7;
    if (type == kTradeLand) return 8;
    return 6;
}

void drawTradeDetail(TradeDetailUi& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    const UiImage& sh = *s.sheet;
    const TradeSlot t = st.tradeSlots[s.seller][s.slot];
    if (t.type == 0) return;
    const int frame = detailFrameFor(t.type);
    if (frame >= sh.frameCount()) return;
    const UiFrameView& bg = sh.frame(frame);
    const int c4 = (640 - bg.width) / 2;
    const int c6 = (480 - bg.height) / 2;
    blitElementOpaque(dst, bg, c4, c6);
    // 物品图标（帧 = 4*颜色 + type + 8 @ (c4+112, c6+6)；原版 blitElementFullscreen 不透明）
    const int iconFrame = 4 * st.players[s.seller].byte20 + t.type + 8;
    if (iconFrame >= 0 && iconFrame < sh.frameCount()) {
        blitElementOpaque(dst, sh.frame(iconFrame), c4 + 112, c6 + 6);
    }
    TextRenderer& tr = s.app->text();
    char buf[48];
    // 玩家名（0xF0F0F0/0x101010 align2 @ (+58,+40)）
    tr.setFont(16, kTextGray, kTextDark, 3, 1);
    if (st.players[s.seller].name) {
        tr.drawText(dst, st.players[s.seller].name, c4 + 58, c6 + 40, 2);
    }
    // 标签（原版预绘于帧内 x=31：類型/市價/賣價/張數/地點/等級）
    switch (t.type) {
        case kTradeStock: {
            tr.drawText(dst, "類型：", c4 + 31, c6 + 92, 2);
            tr.drawText(dst, "張數：", c4 + 31, c6 + 124, 2);
            tr.drawText(dst, "市價：", c4 + 31, c6 + 156, 2);
            tr.drawText(dst, "賣價：", c4 + 31, c6 + 188, 2);
            const int gn = st.gameMode * 4 + st.mapIndex;
            if (t.objId < 12) {
                tr.drawText(dst, kStockNames[gn][t.objId], c4 + 120, c6 + 92, 2);
            }
            std::snprintf(buf, sizeof(buf), "%d", t.count);
            tr.drawText(dst, buf, c4 + 178, c6 + 124, 6);
            fmtMoneyYuan(buf, sizeof(buf),
                         static_cast<int32_t>(t.count * (t.objId < 12 ? st.stocks[t.objId][5] : 0.0f)));
            tr.drawText(dst, buf, c4 + 178, c6 + 156, 6);
            fmtMoneyYuan(buf, sizeof(buf), t.price);
            tr.drawText(dst, buf, c4 + 178, c6 + 188, 6);
            break;
        }
        case kTradeLand: {
            tr.drawText(dst, "類型：", c4 + 31, c6 + 92, 2);
            tr.drawText(dst, "地點：", c4 + 31, c6 + 124, 2);
            tr.drawText(dst, "等級：", c4 + 31, c6 + 156, 2);
            tr.drawText(dst, "市價：", c4 + 31, c6 + 188, 2);
            tr.drawText(dst, "賣價：", c4 + 31, c6 + 220, 2);
            if (t.objId >= 4000) {
                const size_t ci = t.objId - 4000;
                if (ci < st.corps.size()) {
                    const Corp& cp = st.corps[ci];
                    if (cp.sub != 0 && cp.type <= 4) {
                        tr.drawText(dst, kBuildingNames[6 + cp.type], c4 + 120, c6 + 92, 2);
                    } else {
                        tr.drawText(dst, "商業用地", c4 + 120, c6 + 92, 2);  // [RE byte_463EEE]
                    }
                    const std::string name =
                        big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
                    tr.drawText(dst, name.c_str(), c4 + 120, c6 + 124, 2);
                    const int lv = cp.sub < 6 ? cp.sub : 0;
                    tr.drawText(dst, kBuildingNames[11 + lv], c4 + 120, c6 + 156, 2);
                }
            } else {
                const size_t ei = t.objId - 2000;
                if (ei < st.estates.size()) {
                    const Estate& es = st.estates[ei];
                    tr.drawText(dst, "住宅用地", c4 + 120, c6 + 92, 2);  // [RE byte_463EE5]
                    const std::string name = big5ToUtf8(es.name, sizeof(es.name));
                    tr.drawText(dst, name.c_str(), c4 + 120, c6 + 124, 2);
                    if (es.type != 0) {
                        tr.drawText(dst, "連鎖店", c4 + 120, c6 + 156, 2);  // [RE byte_463E30]
                    } else {
                        const int lv = es.level < 6 ? es.level : 0;
                        tr.drawText(dst, kBuildingNames[lv], c4 + 120, c6 + 156, 2);
                    }
                }
            }
            fmtMoneyYuan(buf, sizeof(buf), landOrderValue(st, t.objId));
            tr.drawText(dst, buf, c4 + 178, c6 + 188, 6);
            fmtMoneyYuan(buf, sizeof(buf), t.price);
            tr.drawText(dst, buf, c4 + 178, c6 + 220, 6);
            break;
        }
        default: {  // 道具/卡片（背景帧6）
            tr.drawText(dst, "類型：", c4 + 31, c6 + 92, 2);
            tr.drawText(dst, "市價：", c4 + 31, c6 + 122, 2);
            tr.drawText(dst, "賣價：", c4 + 31, c6 + 152, 2);
            const char* name = nullptr;
            int32_t value = 0;
            if (t.type == kTradeItem) {
                if (t.objId >= 1 && t.objId <= 13) name = kItemBagNames[t.objId];
                value = itemTradePrice(st, t.objId);
            } else {
                if (t.objId >= 1 && t.objId <= 30) name = kCardNames[t.objId];
                value = cardTradePrice(st, t.objId);
            }
            if (name) tr.drawText(dst, name, c4 + 120, c6 + 92, 2);
            fmtMoneyYuan(buf, sizeof(buf), value);
            tr.drawText(dst, buf, c4 + 178, c6 + 122, 6);
            fmtMoneyYuan(buf, sizeof(buf), t.price);
            tr.drawText(dst, buf, c4 + 178, c6 + 152, 6);
            break;
        }
    }
    // 按钮（左 = 撤件/購買、右 = EXIT；原版 word_48C2C8 = 203/236/265）
    const int btnY = (t.type == kTradeStock) ? 236 : (t.type == kTradeLand) ? 265 : 203;
    tr.setFont(16, kBtnWhite, kBtnShadow, 3, 1);
    tr.drawText(dst, (s.seller == st.currentPlayer) ? "撤 件" : "購 買", c4 + 52, c6 + btnY, 2);
    tr.drawText(dst, "EXIT", c4 + 140, c6 + btnY, 2);
    if (s.hlBtn == 1) {
        pressDown(dst, c4 + kDetailBtnL, c6 + btnY - 12, kDetailBtnW, kDetailBtnH, 1, kChannelHalf);
    } else if (s.hlBtn == 2) {
        pressDown(dst, c4 + kDetailBtnR, c6 + btnY - 12, kDetailBtnW, kDetailBtnH, 1, kChannelHalf);
    }
}

bool tradeDetailEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<TradeDetailUi*>(user);
    GameState& st = s.app->gameState();
    if (!event) {
        drawTradeDetail(s);
        return true;
    }
    const TradeSlot t = st.tradeSlots[s.seller][s.slot];
    const int frame = detailFrameFor(t.type);
    if (frame >= s.sheet->frameCount()) {
        s.app->events().requestExit(0);
        return true;
    }
    const UiFrameView& bg = s.sheet->frame(frame);
    const int c4 = (640 - bg.width) / 2;
    const int c6 = (480 - bg.height) / 2;
    const int btnY = (t.type == kTradeStock) ? 236 : (t.type == kTradeLand) ? 265 : 203;
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 原版无 hover：按钮高亮在 WM_LBUTTONDOWN（highlightRect）——见 DOWN 分支
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        // [RE 0x42704E] WM_LBUTTONDOWN：命中按钮 → byte_48C2C1 + highlightRect（按下下沉）
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        int hl = 0;
        if (y > c6 + btnY - 12 && y < c6 + btnY + 12) {
            if (x > c4 + kDetailBtnL && x < c4 + kDetailBtnL + kDetailBtnW) hl = 1;
            else if (x > c4 + kDetailBtnR && x < c4 + kDetailBtnR + kDetailBtnW) hl = 2;
        }
        if (hl != 0 && hl != s.hlBtn) {
            s.hlBtn = hl;
            drawTradeDetail(s);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        // [RE 0x42704E] 左按钮（hlBtn==1）：自己 → 撤件；他人 → 确认框 + 成交。
        //   成交失败/取消 → 停留；其余（含右按钮/空白）→ 退出
        const int ux = static_cast<int>(event->button.x);
        const int uy = static_cast<int>(event->button.y);
        s.hlBtn = 0;
        int hl = 0;
        if (uy > c6 + btnY - 12 && uy < c6 + btnY + 12) {
            if (ux > c4 + kDetailBtnL && ux < c4 + kDetailBtnL + kDetailBtnW) hl = 1;
            else if (ux > c4 + kDetailBtnR && ux < c4 + kDetailBtnR + kDetailBtnW) hl = 2;
        }
        if (hl == 1) {
            if (s.seller == st.currentPlayer) {
                removeTradeOrder(st, st.currentPlayer, s.slot);
                s.app->events().requestExit(0);
                return true;
            }
            if (confirmDialog(*s.app)) {
                if (executeTrade(*s.app, s.seller, s.slot, s.sheet) == 1) {
                    removeTradeOrder(st, s.seller, s.slot);
                    s.app->events().requestExit(0);
                    return true;
                }
            }
            drawTradeDetail(s);
            return true;
        }
        s.app->events().requestExit(0);
        return true;
    }
    return false;
}

// ===== 主面板 [RE 0x427C21] =====
struct TradeUi {
    Application* app = nullptr;
    UiImage sheet;  // panel.mkf[73]
    UiImage icons;  // panel.mkf[74]
    // 原版交互：DOWN 记 byte_48C2CA（0=无 1=投标 2=EXIT >=100 格子），按住可滑子按钮，
    //   UP 执行；byte_48C2CB = 投标子按钮 1..4（股票/地产/道具/卡片）
    int pressed = 0;
    int subSel = 0;
};

// 主面板静态内容（帧0 + 玩家棋子头像 + 挂单格子；不含按下高亮/子按钮底板）。
// 供主模态绘制与子对话框"整层重绘"（场景 → 主面板 → 子对话框）复用。
void drawTradeMainBase(Application& app, const UiImage& sheet) {
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    if (sheet.frameCount() <= 0) return;
    blitElementOpaque(dst, sheet.frame(0), kMainX, kMainY);
    // 玩家棋子头像（原版预绘到帧0 @ (44,36+72i) → 屏幕 (66,102+72i)）
    for (int p = 0; p < st.playerCount; ++p) {
        if (st.pieceSprites[p].frameCount() > 0) {
            blitElement(dst, st.pieceSprites[p].frame(0), 66, 150 + 72 * p, false);
        }
    }
    // 挂单格子（帧 = 4*颜色索引 + type + 8；原版 blitElementFullscreen 不透明）
    for (int p = 0; p < st.playerCount; ++p) {
        for (int c = 0; c < 7; ++c) {
            const TradeSlot& t = st.tradeSlots[p][c];
            if (t.type == 0) continue;
            const int frame = 4 * st.players[p].byte20 + t.type + 8;
            if (frame >= 0 && frame < sheet.frameCount()) {
                blitElementOpaque(dst, sheet.frame(frame), kGridX0 + 72 * c, kGridY0 + 72 * p);
            }
        }
    }
}

// 子对话框整层重绘（场景 + 主面板；调用方随后重画子对话框自身）。
// 数字框取消后提示框 (227,42..130) 超出子对话框区域、子对话框本身也大于主面板 —— 必须从场景重来
void redrawSubLayer(Application& app, const UiImage& sheet) {
    renderGameFrame(app);
    drawTradeMainBase(app, sheet);
}

void drawTradeMain(TradeUi& s) {
    Surface& dst = s.app->surface();
    const UiImage& sh = s.sheet;
    drawTradeMainBase(*s.app, sh);
    // 子按钮底板帧17：原版按住投标区（byte_48C2CA==1）时才贴出 2×2 子菜单
    if (s.pressed == 1 && sh.frameCount() > 17) {
        blitElementOpaque(dst, sh.frame(17), kPanelX, kPanelY);
    }
    // 按下高亮（原版 scaleElementChannels -12 → kChannelDim）
    if (s.pressed == 1) {
        scaleSurfaceChannels(dst, kBidX, kBidY, kBidW, kBidH, kChannelDim);
        if (s.subSel >= 1 && s.subSel <= 4) {
            const int v8 = s.subSel - 1;
            scaleSurfaceChannels(dst, kSubX + kSubStepX * (v8 / 2), kSubY + kSubStepY * (v8 % 2),
                                 kSubW, kSubH, kChannelDim);
        }
    } else if (s.pressed == 2) {
        scaleSurfaceChannels(dst, kExitX, kExitY, 72, kBidH, kChannelDim);
    } else if (s.pressed >= 100) {
        const int grid = s.pressed - 100;
        scaleSurfaceChannels(dst, kGridX0 + 72 * (grid % 7), kGridY0 + 72 * (grid / 7), 72, 72,
                             kChannelDim);
    }
}

// 公佈欄完整重绘（场景 + 主面板）：子对话框/详情/提示框退出后调用。
// 帧1/2 大于主面板（上探工具栏区 32..66、下探地图区 414..448）、提示框 y=42 超面板顶，
// 只画主面板会残留 → 先 renderGameFrame 恢复场景，再画主面板。
void redrawTradeMainFull(TradeUi& s) {
    renderGameFrame(*s.app);
    drawTradeMain(s);
}

bool tradeMainEvent(const SDL_Event* event, void* user) {
    auto& s = *static_cast<TradeUi*>(user);
    GameState& st = s.app->gameState();
    if (!event) {
        drawTradeMain(s);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // 按住投标区滑动选择子按钮（原版 WM_MOUSEMOVE 仅 byte_48C2CA==1 时处理）
        if (s.pressed == 1) {
            const int x = static_cast<int>(event->motion.x);
            const int y = static_cast<int>(event->motion.y);
            int sub = 0;
            if (x > kSubX && x < kSubX + 2 * kSubStepX && y > kSubY && y < kSubY + 2 * kSubStepY) {
                const int v8 = ((y - kSubY) / kSubStepY) + 2 * ((x - kSubX) / kSubStepX);
                if (v8 >= 0 && v8 < 4) sub = v8 + 1;
            }
            if (sub != s.subSel) {
                s.subSel = sub;
                drawTradeMain(s);
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            s.app->events().requestExit(0);
            return true;
        }
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        if (x > kBidX && x < kBidX + kBidW && y > kBidY && y < kBidY + kBidH) {
            // 投标区 (464..536, 74..114)
            if (st.tradeSlots[st.currentPlayer][6].type != 0) {
                // [RE 0x424502(byte_463F03, 1) + 0x4528B9(1500)] 下方提示框 1500ms
                tradeTipBlocking(*s.app, s.sheet, "公佈欄已滿\n\n請先撤件！", 196);
                redrawTradeMainFull(s);  // 提示框 y=42 超出主面板顶（66），需恢复场景
                return true;
            }
            s.pressed = 1;
            s.subSel = 0;
            drawTradeMain(s);
            return true;
        }
        if (x > kExitX && x < 608 && y > kExitY && y < kExitY + kBidH) {
            s.pressed = 2;
            drawTradeMain(s);
            return true;
        }
        if (x > kGridX0 && x < 608 && y > kGridY0 && y < 72 * st.playerCount + kGridY0) {
            const int col = (x - kGridX0) / 72;
            const int row = (y - kGridY0) / 72;
            if (col >= 0 && col < 7 && row >= 0 && row < st.playerCount &&
                st.tradeSlots[row][col].type != 0) {
                s.pressed = 100 + row * 7 + col;
                drawTradeMain(s);
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) return false;
        const int pressed = s.pressed;
        s.pressed = 0;
        if (pressed == 1) {
            // 投标（原版 byte_48C2CB 1..4 → 1039..1042 子对话框）
            if (st.tradeSlots[st.currentPlayer][6].type != 0) {
                tradeTipBlocking(*s.app, s.sheet, "公佈欄已滿\n\n請先撤件！", 196);
            } else if (s.subSel == 1) {
                StockOrderUi sub;
                sub.app = s.app;
                sub.sheet = &s.sheet;
                sub.mainSheet = &s.sheet;
                for (int i = 0; i < 12; ++i) {
                    if (st.playerShares[st.currentPlayer][i] != 0) {
                        sub.rows.push_back(static_cast<uint8_t>(i));
                    }
                }
                runModal(*s.app, &stockOrderEvent, &sub);
            } else if (s.subSel == 2) {
                LandOrderUi sub;
                sub.app = s.app;
                sub.sheet = &s.sheet;
                sub.mainSheet = &s.sheet;
                collectOwnedAssets(st, st.currentPlayer, 0, sub.assets);
                landUpdatePage(sub);
                runModal(*s.app, &landOrderEvent, &sub);
            } else if (s.subSel == 3) {
                ItemOrderUi sub;
                sub.app = s.app;
                sub.sheet = &s.sheet;
                sub.mainSheet = &s.sheet;
                sub.icons = &s.icons;
                for (int i = 0; i < 13; ++i) {
                    if (st.itemStock[15 * st.currentPlayer + i] != 0) {
                        sub.ids.push_back(static_cast<uint8_t>(i + 1));
                    }
                }
                RICH4_LOGI("trade: item order dialog (%zu items) (RE 0x4267A4)", sub.ids.size());
                runModal(*s.app, &itemOrderEvent, &sub);
            } else if (s.subSel == 4) {
                CardOrderUi sub;
                sub.app = s.app;
                sub.sheet = &s.sheet;
                sub.mainSheet = &s.sheet;
                runModal(*s.app, &cardOrderEvent, &sub);
            }
            s.subSel = 0;
            redrawTradeMainFull(s);  // 子对话框大于主面板（上探工具栏/下探地图）→ 恢复场景
            return true;
        }
        if (pressed == 2) {
            s.app->events().requestExit(0);
            return true;
        }
        if (pressed >= 100) {
            const int grid = pressed - 100;
            const int row = grid / 7;
            const int col = grid % 7;
            if (st.tradeSlots[row][col].type != 0) {
                TradeDetailUi det;
                det.app = s.app;
                det.sheet = &s.sheet;
                det.seller = row;
                det.slot = col;
                runModal(*s.app, &tradeDetailEvent, &det);
            }
            redrawTradeMainFull(s);  // 详情/嵌套确认框退出后恢复场景
            return true;
        }
        drawTradeMain(s);
        return true;
    }
    return false;
}

} // namespace

// ===== 数据层（原版 0x42483E/0x4246C5/0x4247D5/0x428475/0x4255DA）=====

// [RE 0x42483E] clearInvalidTradeOrders
// 依据: 0x42483E 反编译; 每玩家 7 槽逐项校验（移除后原地重检，等价前移后继续）
void clearInvalidTradeOrders(GameState& st) {
    for (int p = 0; p < st.playerCount && p < 8; ++p) {
        int s = 0;
        while (s < 7) {
            if (tradeSlotInvalid(st, p, st.tradeSlots[p][s])) {
                removeTradeOrder(st, p, s);
            } else {
                ++s;
            }
        }
    }
}

// [RE 0x4246C5] queueTradeOrder
// 依据: 0x4246C5 反编译; 找空槽或同 (type,objId) 槽覆盖；满 7 槽 return 不写；
//       type==2 记录 estate（+24/+26）或 corp（+24/+26）快照到 +10/+11
void queueTradeOrder(GameState& st, int player, int type, int objId, int price, int count) {
    if (player < 0 || player >= 8) return;
    int slot = 0;
    while (slot < 7) {
        const TradeSlot& t = st.tradeSlots[player][slot];
        if (t.type == 0) break;
        if (t.type == type && t.objId == objId) break;
        ++slot;
    }
    if (slot >= 7) {
        RICH4_LOGI("queueTradeOrder: p%d board full (RE 0x4246C5)", player);
        return;
    }
    TradeSlot& t = st.tradeSlots[player][slot];
    t.type = static_cast<uint8_t>(type);
    t.age = 0;
    t.objId = static_cast<uint16_t>(objId);
    t.price = price;
    t.count = (type == kTradeStock) ? static_cast<uint16_t>(count) : 0;
    t.snapType = 0;
    t.snapLevel = 0;
    if (type == kTradeLand) {
        if (objId >= 4000) {
            const size_t i = static_cast<size_t>(objId - 4000);
            if (i < st.corps.size()) {
                t.snapType = st.corps[i].type;
                t.snapLevel = st.corps[i].sub;
            }
        } else {
            const size_t i = static_cast<size_t>(objId - 2000);
            if (i < st.estates.size()) {
                t.snapType = st.estates[i].type;
                t.snapLevel = st.estates[i].level;
            }
        }
    }
    RICH4_LOGI("queueTradeOrder: p%d slot%d type=%d obj=%d price=%d count=%d (RE 0x4246C5)",
               player, slot, type, objId, price, count);
}

// [RE 0x4247D5] removeTradeOrder
// 依据: 0x4247D5 反编译; memcpy 12*(6-slot) 前移 + 末槽 12 字节清零
void removeTradeOrder(GameState& st, int player, int slot) {
    if (player < 0 || player >= 8 || slot < 0 || slot >= 7) return;
    for (int i = slot; i < 6; ++i) {
        st.tradeSlots[player][i] = st.tradeSlots[player][i + 1];
    }
    st.tradeSlots[player][6] = TradeSlot{};
}

// [RE 0x428475] ageTradeOrders
// 依据: 0x428475 反编译; 存活玩家每个非空挂单 +1 字节年龄（原版无读取点）
void ageTradeOrders(GameState& st) {
    for (int p = 0; p < st.playerCount && p < 8; ++p) {
        if (st.players[p].alive == 0) continue;
        for (int s = 0; s < 7; ++s) {
            if (st.tradeSlots[p][s].type != 0) {
                ++st.tradeSlots[p][s].age;
            }
        }
    }
}

// [RE 0x4255DA] executeTrade
// 依据: 0x4255DA 反编译; 现金检查（人类提示 0x463E50）→ 按类型转移：
//   case1 股票（持股/均价加权 + updateSpecPtControl 经营権提示 0x463E5F）/
//   case2 地产（owner 改）+ rebuildMiniMap(0) / case3 道具（库存 <9 转移，满 0x463E72）/
//   case4 卡片（卡数 <15 转移，满 0x463E89）→ transferMoney(cur, seller, price, 0)
int executeTrade(Application& app, int seller, int slot, const UiImage* sheet) {
    GameState& st = app.gameState();
    const int buyer = st.currentPlayer;
    if (seller < 0 || seller >= 8 || slot < 0 || slot >= 7 || buyer < 0 || buyer >= 8) {
        return 0;
    }
    const TradeSlot t = st.tradeSlots[seller][slot];
    if (t.type == 0) {
        return 1;
    }
    const bool human = st.players[buyer].alive == 1;
    if (st.players[buyer].cash < t.price) {
        if (human && sheet) {
            tradeTipBlocking(app, *sheet, "您的現金不足！", 196);  // [RE byte_463E50 @ y=196]
        }
        return 0;
    }
    switch (t.type) {
        case kTradeStock: {
            const int stock = t.objId;
            if (stock < 0 || stock >= 12) return 0;
            const float oldCost =
                static_cast<float>(st.playerShares[buyer][stock]) * st.playerAvgCost[buyer][stock];
            st.playerShares[buyer][stock] += t.count;
            st.playerShares[seller][stock] -= t.count;
            if (st.playerShares[seller][stock] <= 0) {
                st.playerShares[seller][stock] = 0;
                st.playerAvgCost[seller][stock] = 0.0f;
            }
            st.playerAvgCost[buyer][stock] =
                (static_cast<float>(t.price) + static_cast<float>(static_cast<int>(oldCost))) /
                static_cast<float>(st.playerShares[buyer][stock]);
            if (updateSpecPtControl(app, buyer, stock) == 1 && st.players[buyer].alive == 1) {
                if (sheet) {
                    tradeTipBlocking(app, *sheet, "恭喜您獲得經營權！", 196);  // [RE byte_463E5F]
                }
            }
            break;
        }
        case kTradeLand: {
            if (t.objId >= 4000) {
                const size_t i = static_cast<size_t>(t.objId - 4000);
                if (i >= st.corps.size()) return 0;
                st.corps[i].owner = static_cast<uint8_t>(buyer + 1);
            } else {
                const size_t i = static_cast<size_t>(t.objId - 2000);
                if (i >= st.estates.size()) return 0;
                st.estates[i].owner = static_cast<uint8_t>(buyer + 1);
            }
            buildMiniMapMarks(app);  // [RE rebuildMiniMap(0)]
            break;
        }
        case kTradeItem: {
            const int itemId = t.objId;
            if (itemId >= 1 && itemId <= 13 && st.itemStock[15 * buyer + itemId - 1] < 9) {
                takePlayerItem(st, seller, itemId);  // [RE takePlayerCard 0x445AA2]
                givePlayerItem(st, buyer, itemId);   // [RE givePlayerCard 0x445A4D]
            } else {
                if (human && sheet) {
                    tradeTipBlocking(app, *sheet, "道具欄已滿\n\n無法購買！", 196);  // [RE byte_463E72]
                }
                return 0;
            }
            break;
        }
        case kTradeCard: {
            if (cardBagCount(st, buyer) < 15) {
                cardBagRemove(st, seller, t.objId);  // [RE sub_441343]
                giveCardToBag(st, buyer, t.objId);   // [RE sub_4412E4]
            } else {
                if (human && sheet) {
                    tradeTipBlocking(app, *sheet, "卡片欄已滿\n\n無法購買！", 196);  // [RE byte_463E89]
                }
                return 0;
            }
            break;
        }
        default:
            return 1;
    }
    transferMoney(app, buyer, seller, t.price, 0);  // [RE 0x41D2C6]
    RICH4_LOGI("executeTrade: buyer=%d seller=%d slot=%d type=%d obj=%d price=%d (RE 0x4255DA)",
               buyer, seller, slot, t.type, t.objId, t.price);
    return 1;
}

// [RE 0x4284BE AI 分支] tradeAiTurn
// 依据: 0x4284BE 反汇编 else 分支（0x42887D..0x428CAA）：
//   清理 → 1/15 挂卖（卡数>12 收集重复卡；否则道具库存≥3 或 kItemAiPersona-性格==2）→
//   1/3 卡片/道具改价为原价 → 1/4 扫他人挂单（股票单价<现价 / 地产报价<估值/3 且余额>2×报价）
void tradeAiTurn(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 8) return;
    clearInvalidTradeOrders(st);  // [RE 0x4284BE 开头 sub_42483E]

    // [RE 0x428881] 1/15：挂卖
    if (dbg::roll(dbg::SlotAiTrade, 15) == 0) {
        bool sold = false;
        const int cardCount = cardBagCount(st, cur);
        if (cardCount > 12) {
            // 收集有重复的卡（原版扫槽 0..count-1 两两比较；值为 0 的空槽跳过）
            std::vector<uint8_t> dup;
            for (int i = 0; i < cardCount && i < 15; ++i) {
                const uint8_t a = st.cardState60[15 * cur + i];
                if (a == 0) continue;
                for (int j = 0; j < cardCount && j < 15; ++j) {
                    if (i == j) continue;
                    if (st.cardState60[15 * cur + j] == a) {
                        dup.push_back(a);
                        break;
                    }
                }
            }
            if (!dup.empty()) {
                const int card = dup[static_cast<size_t>(dbg::roll(dbg::SlotAiTrade, static_cast<int>(dup.size())))];
                if (st.tradeSlots[cur][6].type != 0) {
                    removeTradeOrder(st, cur, 0);  // [RE 0x42891B] 满槽先腾位
                }
                queueTradeOrder(st, cur, kTradeCard, card, cardTradePrice(st, card), 0);
                sold = true;
            }
        }
        if (!sold) {
            // [RE 0x428958] 道具：库存≥3 或 kItemAiPersona[id]-性格==2
            const uint8_t persona = st.players[cur].aiPersonality;
            std::vector<uint8_t> cand;
            for (int id = 0; id < 13; ++id) {
                const uint8_t have = st.itemStock[15 * cur + id];
                if (have == 0) continue;
                const int diff = static_cast<int>(kItemAiPersona[id + 1]) - persona;
                if (have >= 3 || diff == 2) {
                    cand.push_back(static_cast<uint8_t>(id + 1));
                }
            }
            if (!cand.empty()) {
                const int itemId = cand[static_cast<size_t>(dbg::roll(dbg::SlotAiTrade, static_cast<int>(cand.size())))];
                if (st.tradeSlots[cur][6].type != 0) {
                    removeTradeOrder(st, cur, 0);
                }
                queueTradeOrder(st, cur, kTradeItem, itemId, itemTradePrice(st, itemId), 0);
            }
        }
    }

    // [RE 0x428A37] 1/3：自己卡片/道具挂单改价为原价
    if (dbg::roll(dbg::SlotAiTrade, 3) == 0) {
        for (int s = 0; s < 7; ++s) {
            TradeSlot& t = st.tradeSlots[cur][s];
            if (t.type == kTradeItem) {
                t.price = itemTradePrice(st, t.objId);
            } else if (t.type == kTradeCard) {
                t.price = cardTradePrice(st, t.objId);
            }
        }
    }

    // [RE 0x428AE8] 1/4：低价买入他人挂单（每次最多成交一笔）
    if (dbg::roll(dbg::SlotAiTrade, 4) == 0) {
        bool bought = false;
        for (int p = 0; p < st.playerCount && p < 8 && !bought; ++p) {
            if (p == cur || st.players[p].alive == 0) continue;
            for (int s = 0; s < 7 && !bought; ++s) {
                const TradeSlot& t = st.tradeSlots[p][s];
                if (t.type == kTradeStock) {
                    const int stock = t.objId;
                    if (stock < 0 || stock >= 12 || t.count == 0) continue;
                    const int unit = t.price / t.count;  // 原版 fdiv 取整
                    if (unit < static_cast<int>(st.stocks[stock][5])) {  // 单价 < 现价
                        if (executeTrade(app, p, s) == 1) {
                            removeTradeOrder(st, p, s);
                            bought = true;
                        }
                    }
                } else if (t.type == kTradeLand) {
                    const int32_t value = landOrderValue(st, t.objId);
                    if (static_cast<int64_t>(3) * value <= t.price) continue;  // 报价 ≥ 估值/3
                    if (static_cast<int64_t>(2) * t.price >= st.players[cur].cash) {
                        continue;  // 余额不足以付 2 倍报价
                    }
                    if (executeTrade(app, p, s) == 1) {
                        removeTradeOrder(st, p, s);
                        bought = true;
                    }
                }
            }
        }
    }
}

// [RE 0x4284BE 人类分支] tradeMarketDialog
// 依据: 0x4284BE 反编译; 清理 → 载入 panel.mkf[73]/[74] → runModal(主面板)
void tradeMarketDialog(Application& app) {
    GameState& st = app.gameState();
    clearInvalidTradeOrders(st);  // [RE 0x4284BE 开头 sub_42483E]
    TradeUi s;
    s.app = &app;
    if (auto blob = st.panel.read(73)) {
        s.sheet.load(std::move(*blob));
    } else {
        RICH4_LOGE("tradeMarket: panel.mkf[73] unavailable (RE 0x4284BE)");
        return;
    }
    if (auto blob = st.panel.read(74)) {
        s.icons.load(std::move(*blob));
    } else {
        RICH4_LOGW("tradeMarket: panel.mkf[74] unavailable (RE 0x4284BE)");
    }
    RICH4_LOGI("trade market open (RE 0x4284BE)");
    trace::logf("dialog open name=trade_market");
    runModal(app, &tradeMainEvent, &s);
}

} // namespace rich4
