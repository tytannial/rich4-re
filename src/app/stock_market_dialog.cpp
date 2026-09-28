#include "game/app/stock_market_dialog.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/app/event_stack.h"
#include "game/app/game_panel.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/number_input_dialog.h"
#include "game/app/stock_system.h"
#include "game/core/trace.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/application.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

// 颜色（RGB888，绘制前 rgb888To555）
constexpr uint32_t kCyan = 0x00F0F0;   // 上市公司名
constexpr uint32_t kGray = 0xF0F0F0;   // 普通文字
constexpr uint32_t kDim = 0xA0A0A0;    // 未选中按钮文字（sub_4296C1 a1=0）
constexpr uint32_t kRed = 0xFF0000;    // 涨
constexpr uint32_t kGreen = 0x00FF00;  // 跌
constexpr uint32_t kDark = 0x101010;   // 深灰
constexpr uint32_t kYellow = 0xF0F000; // 第一大股东
constexpr uint32_t kUpBg = 0xD00100;   // 上涨底色
constexpr uint32_t kDownBg = 0x00D000; // 跌停底色
constexpr uint16_t kWhite555 = 0x7FFF; // 选框/折线白（RGB555）
constexpr double kPi = 3.14159265358979323846;

// 几何
constexpr int kRowH = 32;
constexpr int kDataRow0 = 96;
constexpr int kSelX = 15;
constexpr int kSelW = 610;

// [RE 0x428EC5] 价格档位舍入（涨跌停判定）
float priceUpdate(float prev, float momentum) {
    const double raw = (static_cast<double>(momentum) + 100.0) / 100.0 * static_cast<double>(prev);
    double v = 0.0;
    if (momentum > 0.0f) {
        double tick = 0.01;
        if (raw >= 150.0) { tick = 1.0; }
        else if (raw >= 50.0) { tick = 0.5; }
        else if (raw >= 15.0) { tick = 0.1; }
        else if (raw >= 5.0) { tick = 0.05; }
        v = raw - tick;
    } else {
        double tick = 1.0;
        if (raw < 5.0) { tick = 0.01; }
        else if (raw < 15.0) { tick = 0.05; }
        else if (raw < 50.0) { tick = 0.1; }
        else if (raw < 150.0) { tick = 0.5; }
        v = raw + tick;
    }
    if (v < 1.0) v = 1.0;
    if (v > 9999.0) v = 9999.0;
    return static_cast<float>(v);
}

// [RE 0x4295EA] 0涨 1涨停 2跌 3跌停 4平（原版 return cur>=limitUp 为真=1 涨停）
int stockStatus(const GameState& st, int s) {
    const float prev = st.stocks[s][4];
    const float cur = st.stocks[s][5];
    if (cur > prev) {
        return (cur >= priceUpdate(prev, 10.0f)) ? 1 : 0;
    }
    if (cur >= prev) {
        return 4;
    }
    return (cur <= priceUpdate(prev, -10.0f)) ? 3 : 2;
}

int priceFmt(float p) {
    if (p < 15.0f) return 0;
    if (p < 150.0f) return 1;
    return 2;
}
void fmtPrice(char* out, float p) {
    switch (priceFmt(p)) {
        case 0: std::snprintf(out, 16, "%.2f", p); break;
        case 1: std::snprintf(out, 16, "%.1f", p); break;
        default: std::snprintf(out, 16, "%.0f", p); break;
    }
}
void fmtDelta(char* out, float d, int fmt) {
    switch (fmt) {
        case 0: std::snprintf(out, 16, "%+.2f", d); break;
        case 1: std::snprintf(out, 16, "%+.1f", d); break;
        default: std::snprintf(out, 16, "%+.0f", d); break;
    }
}
void fmtNum(char* out, float p, int fmt) {  // 无符号价格（走势图均值/高低）
    switch (fmt) {
        case 0: std::snprintf(out, 16, "%.2f", p); break;
        case 1: std::snprintf(out, 16, "%.1f", p); break;
        default: std::snprintf(out, 16, "%.0f", p); break;
    }
}

// [RE 0x452793] 千分位
void fmtMoney(char* out, int32_t value) {
    char tmp[24];
    bool neg = value < 0;
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
    int o = 0;
    if (neg) out[o++] = '-';
    for (int i = r - 1; i >= 0; --i) out[o++] = rev[i];
    out[o] = '\0';
}

uint32_t nameColor(const GameState& st, int s) {
    return static_cast<int>(st.stocks[s][1]) != 0 ? kCyan : kGray;
}
// 现价字色：0涨停红 1涨白(红底) 2跌绿 3跌停黑(绿底) 4平灰
uint32_t priceColor(int status) {
    switch (status) {
        case 0: return kRed;
        case 1: return kGray;
        case 2: return kGreen;
        case 3: return kDark;
        default: return kGray;
    }
}
// 涨跌/交易量字色：0/1红 2/3绿 4灰
uint32_t deltaColor(int status) {
    switch (status) {
        case 0:
        case 1: return kRed;
        case 2:
        case 3: return kGreen;
        default: return kGray;
    }
}

// [RE 0x4754C8] Rect[5]：切換/買進/賣出/上市公司資訊/EXIT
struct Btn { int l, t, r, b; };
const Btn kBtns[5] = {{16, 9, 124, 39}, {128, 9, 198, 39}, {202, 9, 272, 39},
                      {276, 8, 410, 40}, {552, 8, 623, 40}};

// [RE 0x47552C] off_47552C 行业→公司图标帧号（[mode][map][costType]；值 15/24 特殊=股票号偏移）
const uint8_t kStockIconTable[2][4][12] = {
  { {0, 11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10}, {0, 11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10},
    {0, 11, 0, 8, 5, 6, 9, 3, 0, 0, 4, 10}, {0, 11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10} },
  { {0, 11, 0, 7, 12, 6, 9, 3, 0, 0, 4, 10}, {0, 11, 23, 7, 14, 6, 9, 3, 0, 0, 4, 10},
    {15, 11, 0, 7, 13, 6, 9, 3, 0, 0, 4, 10}, {0, 11, 24, 7, 5, 6, 9, 3, 0, 0, 4, 10} },
};

struct StockPanelState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    int view = 0;
    int selStock = 0;   // 选中股票 1..12，0=无
    int hoverRow = 0;   // 悬停行 1..12
    int hoverBtn = -1;  // 悬停按钮 0..4
    int pressedBtn = -1;
    bool closed = false;
    int pickMode = 0;   // [NEW] 0=普通行情；1/2=选股模式（红/黑卡：点击行即返回）
};

// [RE 0x4296C1] 列标题 + 按钮标签（未选中→買/賣/資訊灰）
void drawHeaders(StockPanelState& s) {
    GameState& st = s.app->gameState();
    TextRenderer& t = s.app->text();
    Surface& dst = s.app->surface();
    const uint32_t act = s.selStock ? kGray : kDim;
    t.setFont(16, kGray, kDark, 3, 1);
    t.drawText(dst, s.view == 0 ? "持有股數表" : "股 價 表", 70, 24, 2);
    t.setFont(16, act, kDark, 3, 1);
    t.drawText(dst, "買進", 163, 24, 2);
    t.drawText(dst, "賣出", 237, 24, 2);
    t.drawText(dst, "上市公司資訊", 343, 24, 2);
    if (s.view == 0) {
        t.setFont(16, kGray, kDark, 3, 1);
        t.drawText(dst, "股票名稱", 76, 64, 2);
        t.drawText(dst, "成交價", 188, 64, 2);
        t.drawText(dst, "漲跌", 280, 64, 2);
        t.drawText(dst, "交易量", 368, 64, 2);
        t.drawText(dst, "持有股數", 468, 64, 2);
        t.drawText(dst, "平均成本", 572, 64, 2);
    } else {
        t.setFont(16, kGray, kDark, 3, 1);
        t.drawText(dst, "股票名稱", 71, 64, 2);
        for (int p = 0; p < st.playerCount; ++p) {
            t.drawText(dst, st.players[p].name ? st.players[p].name : "", 168 + 80 * p, 64, 2);
        }
        t.drawText(dst, "保留股份", 492, 64, 2);
        t.drawText(dst, "累積盈餘", 580, 64, 2);
    }
}

// [RE 0x4297F7] 数据列
void drawRows(StockPanelState& s) {
    GameState& st = s.app->gameState();
    TextRenderer& t = s.app->text();
    Surface& dst = s.app->surface();
    const int cur = st.currentPlayer;
    const int gn = st.gameMode * 4 + st.mapIndex;
    char buf[24];
    if (s.view == 0) {
        for (int i = 0; i < 12; ++i) {
            const int y = kDataRow0 + kRowH * i;
            const int status = stockStatus(st, i);
            if (status == 1) {
                dst.fillRect(144, y - 10, 89, 20, rgb888To555(kUpBg));
            } else if (status == 3) {
                dst.fillRect(144, y - 10, 89, 20, rgb888To555(kDownBg));
            }
            t.setFont(16, nameColor(st, i), kDark, 3, 1);
            t.drawText(dst, kStockNames[gn][i], 76, y, 2);
            // 现价（有底色时白/黑字 style2，否则红/绿/灰 style3）
            const int pstyle = (status == 1 || status == 3) ? 2 : 3;
            const uint32_t pbg = (status == 1 || status == 3) ? 0 : kDark;
            t.setFont(16, priceColor(status), pbg, pstyle, 1);
            fmtPrice(buf, st.stocks[i][5]);
            t.drawText(dst, buf, 225, y, 6);
            // 涨跌
            t.setFont(16, deltaColor(status), kDark, 3, 1);
            fmtDelta(buf, st.stocks[i][5] - st.stocks[i][4], priceFmt(st.stocks[i][5]));
            t.drawText(dst, buf, 305, y, 6);
            // 停牌 / 交易量
            if (st.stockHalted[i]) {
                t.setFont(16, kGray, kDark, 3, 1);
                t.drawText(dst, "暫停交易", 368, y, 2);
            } else {
                std::snprintf(buf, sizeof(buf), "%d", st.stockVolume[i]);
                t.drawText(dst, buf, 401, y, 6);
            }
            // 持股 / 均价
            if (cur >= 0 && st.playerShares[cur][i] != 0) {
                t.setFont(16, kGray, kDark, 3, 1);
                fmtMoney(buf, st.playerShares[cur][i]);
                t.drawText(dst, buf, 504, y, 6);
                std::snprintf(buf, sizeof(buf), "%.2f", st.playerAvgCost[cur][i]);
                t.drawText(dst, buf, 609, y, 6);
            }
        }
    } else {
        for (int i = 0; i < 12; ++i) {
            const int y = kDataRow0 + kRowH * i;
            const int si = static_cast<int>(st.stocks[i][1]);
            const bool listed = si != 0;
            t.setFont(16, listed ? kCyan : kGray, kDark, 3, 1);
            t.drawText(dst, kStockNames[gn][i], 71, y, 2);
            for (int p = 0; p < st.playerCount; ++p) {
                const int x = 192 + 80 * p;
                const bool owner = listed && si > 0 && si < static_cast<int>(st.specPts.size()) &&
                                   st.specPts[si].owner == static_cast<uint8_t>(p + 1);
                if (owner) {
                    dst.fillRect(x - 56, y - 10, 64, 20, kWhite555);
                    t.setFont(16, kYellow, 0, 2, 1);
                } else {
                    t.setFont(16, listed ? kCyan : kGray, kDark, 3, 1);
                }
                fmtMoney(buf, st.playerShares[p][i]);
                t.drawText(dst, buf, x, y, 6);
            }
            if (listed && si > 0 && si < static_cast<int>(st.specPts.size())) {
                t.setFont(16, kCyan, kDark, 3, 1);
                fmtMoney(buf, st.specPts[si].sharesLeft);
                t.drawText(dst, buf, 520, y, 6);
                fmtMoney(buf, st.specPts[si].fund);
                t.drawText(dst, buf, 609, y, 6);
            }
        }
    }
    // 余额
    if (cur >= 0) {
        t.setFont(16, kGray, kDark, 3, 1);
        buf[0] = '$';
        fmtMoney(buf + 1, st.players[cur].bank);
        t.drawText(dst, buf, 540, 24, 6);
    }
}

// 选框（白）+ 按钮悬停/按下下沉
void drawSelection(StockPanelState& s) {
    Surface& dst = s.app->surface();
    if (s.hoverRow >= 1 && s.hoverRow <= 12) {
        drawRectBorder(dst, kSelX, kRowH * s.hoverRow + 48, kSelW, kRowH, kWhite555);
    }
    if (s.selStock >= 1 && s.selStock <= 12 && s.selStock != s.hoverRow) {
        drawRectBorder(dst, kSelX, kRowH * s.selStock + 48, kSelW, kRowH, kWhite555);
    }
    if (s.pressedBtn >= 0 && s.pressedBtn < 5) {
        // [RE 0x451B9E] 仅按住时下沉；大按钮用 2px/kChannelThird（更夸张）
        const Btn& b = kBtns[s.pressedBtn];
        pressDown(dst, b.l, b.t, b.r - b.l, b.b - b.t, 2, kChannelThird);
    }
}

void drawClosed(StockPanelState& s) {
    TextRenderer& t = s.app->text();
    Surface& dst = s.app->surface();
    t.setFont(72, kDark, 0, 2, 1);
    t.drawText(dst, "本日休市", 324, 244, 2);
    t.setFont(72, kGray, 0, 2, 1);
    t.drawText(dst, "本日休市", 320, 240, 2);
}

void renderStockPanel(StockPanelState& s) {
    Surface& dst = s.app->surface();
    blitElementOpaque(dst, s.ui->frame(s.view), 0, 0);
    drawHeaders(s);
    if (s.closed) {
        drawClosed(s);
        return;
    }
    drawRows(s);
    drawSelection(s);
}

// ===== 走勢圖 [RE 0x429D65] =====
void drawLine(Surface& dst, int x0, int y0, int x1, int y1, uint16_t color) {
    uint16_t* px = dst.pixels();
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < Surface::kWidth && y0 >= 0 && y0 < Surface::kHeight) {
            px[y0 * Surface::kWidth + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

float histAt(const GameState& st, int stock, int idx) {
    const int32_t bits = st.stockHistory[stock][((idx % 144) + 144) % 144];
    if ((bits & 0x7FFFFFFF) == 0) return 0.0f;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct ChartState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    int stock = 0;
};

void renderChart(ChartState& c) {
    GameState& st = c.app->gameState();
    TextRenderer& t = c.app->text();
    Surface& dst = c.app->surface();
    blitElementOpaque(dst, c.ui->frame(2), 26, 52);  // 帧2 = 587x375 框图
    const int s = c.stock;
    const int gn = st.gameMode * 4 + st.mapIndex;
    const float price = st.stocks[s][5];
    const float prev = st.stocks[s][4];
    const float delta = price - prev;
    const int fmt = priceFmt(price);
    char buf[24];
    // [RE 0x42B58F canvas+36] 走势图标签（原版预渲染到帧2，此处每帧绘制，全屏坐标）
    t.setFont(16, kGray, kDark, 3, 1);
    t.drawText(dst, "本月盈餘", 212, 123, 6);
    t.drawText(dst, "平均盈餘", 212, 162, 6);
    t.drawText(dst, "經 營 者", 212, 203, 6);
    t.drawText(dst, "成交價", 395, 123, 6);
    t.drawText(dst, "漲  跌", 395, 162, 6);
    t.drawText(dst, "週均價", 395, 203, 6);
    t.drawText(dst, "交易量", 531, 123, 6);
    t.drawText(dst, "漲跌幅", 531, 162, 6);
    t.drawText(dst, "月均價", 531, 203, 6);
    t.drawText(dst, "歷史高價", 531, 243, 6);
    t.drawText(dst, "歷史低價", 531, 283, 6);
    t.setFont(12, kGray, kDark, 3, 1);
    t.drawText(dst, "半年內股價走勢線圖", 236, 248, 2);
    t.drawText(dst, "持股比例", 502, 318, 2);
    t.setFont(20, kGray, kDark, 3, 1);
    t.drawText(dst, kStockNames[gn][s], 320, 82, 2);
    // 左栏公司資訊
    const int si = static_cast<int>(st.stocks[s][1]);
    if (si > 0 && si < static_cast<int>(st.specPts.size())) {
        const SpecPt& sp = st.specPts[si];
        // [RE 0x429D65] 公司实景图标 blit panel.mkf[75] 帧 v5 @ (50,107)；仅 80×112 帧才画
        int ind = sp.costType;
        if (ind < 0) ind = 0;
        if (ind > 11) ind = 11;
        int v5 = kStockIconTable[st.gameMode & 1][st.mapIndex & 3][ind];
        if (v5 == 15) v5 = sp.stockNo - 3 + 15;
        if (v5 == 24) v5 = sp.stockNo - 2 + 24;
        if (v5 >= 0 && v5 < c.ui->frameCount() && c.ui->frame(v5).width == 80 &&
            c.ui->frame(v5).height == 112) {
            blitElementOpaque(dst, c.ui->frame(v5), 50, 107);
        }
        t.setFont(16, kGray, kDark, 3, 1);
        fmtMoney(buf, sp.fund);
        t.drawText(dst, buf, 309, 123, 6);
        fmtMoney(buf, st.stockDivPeriod ? sp.fundPaid / st.stockDivPeriod : sp.fundPaid);
        t.drawText(dst, buf, 309, 162, 6);
        if (sp.owner && sp.owner <= 4) {
            t.drawText(dst, st.players[sp.owner - 1].name ? st.players[sp.owner - 1].name : "", 269,
                       203, 2);
        }
    }
    // 中/右栏统计
    t.setFont(16, kGray, kDark, 3, 1);
    fmtPrice(buf, price);
    t.drawText(dst, buf, 453, 123, 6);
    t.setFont(16, deltaColor(stockStatus(st, s)), kDark, 3, 1);
    fmtDelta(buf, delta, fmt);
    t.drawText(dst, buf, 453, 162, 6);
    t.setFont(16, kGray, kDark, 3, 1);
    std::snprintf(buf, sizeof(buf), "%d", st.stockVolume[s]);
    t.drawText(dst, buf, 589, 123, 6);
    std::snprintf(buf, sizeof(buf), "%.2f", prev > 0.0f ? delta / prev * 100.0 : 0.0);
    t.drawText(dst, buf, 589, 162, 6);
    auto avg = [&](int n) {
        double sum = 0.0;
        int cnt = 0;
        int idx = st.turnCounter;
        for (int k = 0; k < n; ++k) {
            idx = (idx - 1 + 144) % 144;
            const float v = histAt(st, s, idx);
            if (v != 0.0f) { sum += v; ++cnt; }
        }
        return cnt ? sum / cnt : static_cast<double>(price);
    };
    fmtNum(buf, static_cast<float>(avg(6)), fmt);
    t.drawText(dst, buf, 453, 203, 6);
    fmtNum(buf, static_cast<float>(avg(24)), fmt);
    t.drawText(dst, buf, 589, 203, 6);
    float hi = 0.0f, lo = 10000.0f;
    for (int k = 0; k < 144; ++k) {
        const float v = histAt(st, s, k);
        if (v != 0.0f) {
            if (v > hi) hi = v;
            if (v < lo) lo = v;
        }
    }
    if (lo > hi) { lo = hi = price; }
    fmtNum(buf, hi, fmt);
    t.drawText(dst, buf, 589, 243, 6);
    fmtNum(buf, lo, fmt);
    t.drawText(dst, buf, 589, 283, 6);
    // 饼图（持股比例）：立体圆柱 = 底椭圆(蓝+黑框) 先画，顶椭圆(扇形+黑框) 覆盖其上半
    // [RE 0x429D65] GDI 顺序：Ellipse(414,343,591,401) 底 → Ellipse/Pie(414,336,591,394) 顶；
    //   CreatePen(0,1,0) 黑描边。底露出的下半 = 圆柱侧壁。
    const int32_t mine = (st.currentPlayer >= 0) ? st.playerShares[st.currentPlayer][s] : 0;
    const double frac = (mine > 0) ? std::min(1.0, mine / 10000.0) : 0.0;
    const uint16_t cRed = rgb888To555(0xD00000);
    const uint16_t cBlue = rgb888To555(0x0000D0);
    const uint16_t kBlack = 0x0000;
    const int cx = 502, cy = 365, rx = 88, ry = 29;
    uint16_t* px = dst.pixels();
    auto ellipseOutline = [&](int ecx, int ecy) {
        for (int i = 0; i <= 720; ++i) {
            const double a = i * (2 * kPi / 720.0);
            const int ex = ecx + static_cast<int>(std::lround(rx * std::cos(a)));
            const int ey = ecy + static_cast<int>(std::lround(ry * std::sin(a)));
            if (ex >= 0 && ex < Surface::kWidth && ey >= 0 && ey < Surface::kHeight) {
                px[ey * Surface::kWidth + ex] = kBlack;
            }
        }
    };
    // 底椭圆（圆柱壁）：填充蓝 + 黑轮廓
    for (int y = cy - ry + 7; y <= cy + ry + 7; ++y) {
        const int ey = y - 7;
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const double nx = (x - cx) / static_cast<double>(rx);
            const double ny = (ey - cy) / static_cast<double>(ry);
            if (nx * nx + ny * ny <= 1.0 && y >= 0 && y < Surface::kHeight && x >= 0 &&
                x < Surface::kWidth) {
                px[y * Surface::kWidth + x] = cBlue;
            }
        }
    }
    ellipseOutline(cx, cy + 7);
    // 顶椭圆（扇形）：填充 + 黑轮廓（覆盖底上半 → 露出侧壁）
    for (int y = cy - ry; y <= cy + ry; ++y) {
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const double nx = (x - cx) / static_cast<double>(rx);
            const double ny = (y - cy) / static_cast<double>(ry);
            if (nx * nx + ny * ny > 1.0) continue;
            double ang = std::atan2(ny, nx) + kPi / 2;
            while (ang < 0) ang += 2 * kPi;
            while (ang >= 2 * kPi) ang -= 2 * kPi;
            uint16_t col;
            if (frac <= 0.0) {
                col = cBlue;  // 无持股 → 全蓝（原版 if 分支）
            } else {
                col = (ang < frac * 2 * kPi) ? cBlue : cRed;  // 持股扇=蓝，其余=红
            }
            px[y * Surface::kWidth + x] = col;
        }
    }
    ellipseOutline(cx, cy);
    if (frac > 0.0 && frac < 1.0) {  // 持股分界半径（顶→角度点）
        const double a = kPi / 2 - frac * 2 * kPi;
        const int ex = cx + static_cast<int>(std::lround(rx * std::cos(a)));
        const int ey = cy - static_cast<int>(std::lround(ry * std::sin(a)));
        drawLine(dst, cx, cy - ry, ex, ey, kBlack);
    }
    drawLine(dst, 414, 365, 414, 372, kBlack);  // 左刻度短线
    drawLine(dst, 590, 365, 590, 372, kBlack);  // 右刻度短线
    // 折线（144 期，白）[RE 0x429D65] scale: (hi-lo)/mid<=0.3 ? 109/(mid*0.6) : 109/(hi-lo)
    const double mid = (hi + lo) * 0.5;
    const double span = hi - lo;
    double scale = 0.0;
    if (mid > 0.0 && span / mid <= 0.3) {
        scale = 109.0 / (mid * 0.6);
    } else if (span > 0.0) {
        scale = 109.0 / span;
    }
    int lastX = -1, lastY = -1, seq = 0;
    int idx = st.turnCounter;
    if (histAt(st, s, idx) == 0.0f) idx = 0;
    for (int k = 0; k < 144; ++k) {
        const float v = histAt(st, s, idx + k);
        if (v == 0.0f) continue;
        const int x = 92 + 2 * seq;
        int y = static_cast<int>(324.0 - (v - mid) * scale);
        if (y < 60) y = 60;
        if (y > 420) y = 420;
        if (lastX >= 0) drawLine(dst, lastX, lastY, x, y, kWhite555);
        lastX = x; lastY = y; ++seq;
    }
    // [RE 0x429D65] 折线框左侧 hi/lo 标尺
    t.setFont(12, kGray, kDark, 1, 1);
    const int hy = 324 - static_cast<int>((hi - mid) * scale);
    const int ly = 324 - static_cast<int>((lo - mid) * scale);
    fmtNum(buf, hi, fmt);
    t.drawText(dst, buf, 88, hy - 8, 6);
    fmtNum(buf, lo, fmt);
    t.drawText(dst, buf, 88, ly + 8, 6);
}

bool chartHandler(const SDL_Event* event, void* user) {
    ChartState& c = *static_cast<ChartState*>(user);
    if (!event) {
        renderChart(c);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_KEY_DOWN) {
        c.app->events().requestExit(0);
    }
    return true;
}

void stockChartDialog(Application& app, UiImage& ui, int stock) {
    ChartState c;
    c.app = &app;
    c.ui = &ui;
    c.stock = stock;
    runModal(app, &chartHandler, &c);
}

// [RE 0x42AAFF] 買進
void doBuy(StockPanelState& s) {
    GameState& st = s.app->gameState();
    const int stock = s.selStock - 1;
    if (stock < 0 || stock >= 12 || st.stockHalted[stock]) return;
    if (stockStatus(st, stock) == 1) {
        showMessage(*s.app, "漲停無法買進！", 1500);
        return;
    }
    const float price = st.stocks[stock][5];
    if (price <= 0.0f) return;
    int maxBuy = static_cast<int>(st.players[st.currentPlayer].bank / price);
    if (maxBuy > st.stockVolume[stock]) maxBuy = st.stockVolume[stock];
    if (maxBuy <= 0) return;
    const int count = numberInputDialog(*s.app, maxBuy);
    if (count > 0) buyStock(*s.app, st.currentPlayer, stock, count, true);
}

// [RE 0x42AAFF] 賣出
void doSell(StockPanelState& s) {
    GameState& st = s.app->gameState();
    const int stock = s.selStock - 1;
    if (stock < 0 || stock >= 12) return;
    const int32_t held = st.playerShares[st.currentPlayer][stock];
    if (held == 0 || st.stockHalted[stock]) return;
    if (stockStatus(st, stock) == 3) {
        showMessage(*s.app, "跌停無法賣出！", 1500);
        return;
    }
    const int count = numberInputDialog(*s.app, held);
    if (count > 0) sellStock(*s.app, st.currentPlayer, stock, count, true);
}

int hitButton(int x, int y) {
    for (int b = 0; b < 5; ++b) {
        if (x >= kBtns[b].l && x < kBtns[b].r && y >= kBtns[b].t && y < kBtns[b].b) return b;
    }
    return -1;
}
int hitRow(int x, int y) {
    if (x >= 16 && x < 625 && y >= 80 && y < 464) return (y - 80) / kRowH + 1;
    return 0;
}

bool stockPanelHandler(const SDL_Event* event, void* user) {
    StockPanelState& s = *static_cast<StockPanelState*>(user);
    if (!event) {
        renderStockPanel(s);
        return true;
    }
    if (s.closed) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_KEY_DOWN) {
            s.app->events().requestExit(0);
        }
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const int x = static_cast<int>(event->motion.x);
            const int y = static_cast<int>(event->motion.y);
            const int row = hitRow(x, y);
            const int btn = hitButton(x, y);
            if (row != s.hoverRow || btn != s.hoverBtn) {
                s.hoverRow = row;
                s.hoverBtn = btn;
                renderStockPanel(s);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_RIGHT) {
                s.app->events().requestExit(0);
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) return true;
            const int btn = hitButton(static_cast<int>(event->button.x),
                                      static_cast<int>(event->button.y));
            if (btn != s.pressedBtn) {
                s.pressedBtn = btn;
                renderStockPanel(s);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button != SDL_BUTTON_LEFT) return true;
            const int x = static_cast<int>(event->button.x);
            const int y = static_cast<int>(event->button.y);
            const int btn = hitButton(x, y);
            s.pressedBtn = -1;
            if (s.pickMode != 0) {  // [NEW/RE stockPanelMain] 选股：点行返回 1..12，EXIT 取消
                if (btn == 4) {
                    s.app->events().requestExit(0);
                    return true;
                }
                const int row = hitRow(x, y);
                if (row >= 1 && row <= 12) {
                    s.app->events().requestExit(row);
                }
                renderStockPanel(s);
                return true;
            }
            if (btn >= 0) {
                switch (btn) {
                    case 0: s.view ^= 1; break;
                    case 1: doBuy(s); break;
                    case 2: doSell(s); break;
                    case 3:
                        if (s.selStock >= 1) stockChartDialog(*s.app, *s.ui, s.selStock - 1);
                        break;
                    case 4: s.app->events().requestExit(0); break;
                    default: break;
                }
                renderStockPanel(s);
                return true;
            }
            const int row = hitRow(x, y);
            if (row >= 1 && row <= 12) {
                if (s.selStock == row) {
                    stockChartDialog(*s.app, *s.ui, row - 1);  // 二次点击→走势图
                } else {
                    s.selStock = row;
                }
            }
            renderStockPanel(s);
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (event->key.key == SDLK_ESCAPE) {
                s.app->events().requestExit(0);
            }
            return true;
        }
        default:
            return true;
    }
}

} // namespace

void stockMarketDialog(Application& app) {
    // [NEW] named region：0x4754C8 Rect[5] 行情/持股/賣出·買入切换/所有公司資訊/EXIT
    //   （绝对坐标同源，脚本 stock.btn.<0..4>）
    {
        static bool s_reg = false;
        if (!s_reg) {
            s_reg = true;
            for (int i = 0; i < 5; ++i) {
                char nm[16];
                std::snprintf(nm, sizeof(nm), "stock.btn.%d", i);
                debug::registerRegion(nm, kBtns[i].l, kBtns[i].t, kBtns[i].r - kBtns[i].l,
                                      kBtns[i].b - kBtns[i].t);
            }
        }
    }

    trace::logf("dialog open name=stock_market");
    GameState& st = app.gameState();
    UiImage ui;
    if (auto blob = st.panel.read(75)) {
        ui.load(std::move(*blob));
    }
    if (ui.frameCount() < 3) {
        return;
    }
    StockPanelState s;
    s.app = &app;
    s.ui = &ui;
    s.view = 0;
    s.selStock = 0;
    s.hoverRow = 0;
    s.hoverBtn = -1;
    s.pressedBtn = -1;
    s.closed = (st.stockMarketClosed != 0) || stockIsHoliday(st);
    runModal(app, &stockPanelHandler, &s);
}

// [RE stockPanelMain] stockPickDialog：选股模态（红/黑卡）
int stockPickDialog(Application& app, int mode) {
    GameState& st = app.gameState();
    UiImage ui;
    if (auto blob = st.panel.read(75)) {
        ui.load(std::move(*blob));
    }
    if (ui.frameCount() < 3) {
        return 0;
    }
    // [NEW] named region：選股行 1..12（hitRow 同源 kRowH*r+48；stockPickDialog 選股鏈腳本化）
    {
        static bool s_regRows = false;
        if (!s_regRows) {
            s_regRows = true;
            for (int r = 1; r <= 12; ++r) {
                char nm[16];
                std::snprintf(nm, sizeof(nm), "stock.row.%d", r);
                debug::registerRegion(nm, kSelX, kRowH * r + 48, kSelW, kRowH);
            }
        }
    }
    StockPanelState s;
    s.app = &app;
    s.ui = &ui;
    s.view = 1;  // 股价表（选公司）
    s.selStock = 0;
    s.hoverRow = 0;
    s.hoverBtn = -1;
    s.pressedBtn = -1;
    s.pickMode = mode;
    s.closed = false;  // 选股不受休市限制
    return runModal(app, &stockPanelHandler, &s);
}

} // namespace rich4
