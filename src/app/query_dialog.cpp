#include "game/app/query_dialog.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "game/app/event_stack.h"
#include "game/app/economy.h"
#include "game/app/map_tables.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

constexpr uint32_t kTextDark = 0x101010;  // 原版 1052688
constexpr uint32_t kTextDim = 0xC0C0C0;   // 原版 12632256（未选中页签）
constexpr uint32_t kTextWhite = 0xFFFFFF; // 选中页签

// 命中区（原版 sub_423CF3 WM_LBUTTONUP/KEYDOWN 常量）
constexpr int kCloseL = 492;   // 0x1EC
constexpr int kCloseT = 9;     // 0x9
constexpr int kCloseR = 602;   // 0x25A
constexpr int kCloseB = 38;    // 0x26
constexpr int kBigTabX = 12;   // 12..109
constexpr int kBigTabW = 97;
constexpr int kBigTabY0 = 282;
constexpr int kBigTabStep = 64;
constexpr int kBigTabH = 40;
constexpr int kPlayerBtnX = 16;
constexpr int kPlayerBtnStep = 88;
constexpr int kPlayerBtnT = 14; // 0xE
constexpr int kPlayerBtnB = 47; // 0x2F
constexpr int kSubTabX = 120;
constexpr int kSubTabW = 75;
constexpr int kSubTabT = 64;   // 0x40
constexpr int kSubTabB = 97;   // 0x61
constexpr int kPageBtnL = 593; // 0x251
constexpr int kPageBtnR = 623; // 0x26F
constexpr int kPageUpT = 369;  // 0x171
constexpr int kPageUpB = 399;  // 0x18F
constexpr int kPageDownT = 417; // 0x1A1
constexpr int kPageDownB = 447; // 0x1BF

// 页签/列表页（dword_4753FC）
constexpr int kPageAsset = 0; // 資產清單
constexpr int kPageLand = 1;  // 地產清單
constexpr int kPageStock = 2; // 股票清單

// 地產清單列表几何（sub_4225A3）
constexpr int kListRowY0 = 144;
constexpr int kListRowStep = 32;
constexpr int kListRows = 10;

struct QueryPanelState {
    Application* app = nullptr;
    UiImage sheet;                  // panel.mkf[9]（25 帧）
    UiImage icons;                  // panel.mkf[74]（13 帧）
    int page = kPageAsset;          // dword_4753FC
    int subPage = 0;                // dword_475400（地產清單 5 子页签）
    int selPlayer = 0;              // dword_48C27C（当前查看的玩家）
    int pageStart = 0;              // dword_475404
    int pageShown = 0;              // dword_475408
    std::vector<uint16_t> assetIds; // word_48BE70（collectOwnedAssetIds 结果）
    std::vector<uint8_t> aliveList; // byte_48C278 存活玩家（0xFF 终止语义 → 用 size）
    // [RE 0x48C284] byte_48C284 按下记录：1=EXIT / 2..4=3 大页签 / 5=上翻 / 6=下翻；
    //   原版 DOWN 时记下并画按下态，UP 时按此分发（子页签/玩家条为 DOWN 立即切换，不记录）
    int pressed = 0;
};

// [RE 0x423B3B] collectOwnedAssetIds：按子页签筛选玩家地产/公司（2000+/4000+）
// 依据: 0x423B3B 反编译; mode 0=estate+corp 1=estate 2=corp 3=有建筑普通地产
//       （level≠0 && type==0） 4=有建筑连锁店（level≠0 && type≠0）
int collectOwnedAssetIds(const GameState& st, int player, int mode,
                         std::vector<uint16_t>& out) {
    out.clear();
    const uint8_t owner = static_cast<uint8_t>(player + 1);
    auto pushEstates = [&](bool needLevel, bool wantChain) {
        for (size_t i = 1; i < st.estates.size(); ++i) {
            const Estate& es = st.estates[i];
            if (es.owner != owner) {
                continue;
            }
            if (needLevel && (es.level == 0 || (es.type != 0) != wantChain)) {
                continue;
            }
            out.push_back(static_cast<uint16_t>(i + 2000));
        }
    };
    auto pushCorps = [&]() {
        for (size_t i = 1; i < st.corps.size(); ++i) {
            const Corp& cp = st.corps[i];
            if (cp.owner != owner) {
                continue;
            }
            out.push_back(static_cast<uint16_t>(i + 4000));
        }
    };
    switch (mode) {
        case 0:
            pushEstates(false, false);
            pushCorps();
            break;
        case 1:
            pushEstates(false, false);
            break;
        case 2:
            pushCorps();
            break;
        case 3:
            pushEstates(true, false);
            break;
        case 4:
            pushEstates(true, true);
            break;
        default:
            break;
    }
    return static_cast<int>(out.size());
}

// [RE 0x4225A3] 列表分页（dir: 0=切换页/子页重置, 1=下页, 2=上页）
void updateLandPage(QueryPanelState& s, int dir) {
    const int count = static_cast<int>(s.assetIds.size());
    if (dir == 0) {
        s.pageStart = 0;
    } else if (dir == 1) {
        if (s.pageStart + 11 > count) {
            return;
        }
        s.pageStart += 10;
    } else if (dir == 2) {
        if (s.pageStart == 0) {
            return;
        }
        s.pageStart -= 10;
    }
    s.pageShown = (s.pageStart + kListRows <= count) ? kListRows : count - s.pageStart;
    if (s.pageShown < 0) {
        s.pageShown = 0;
    }
}

// 重选页签/子页签后的列表刷新（对应 sub_423070 内 `if (dword_4753FC==1) sub_4225A3(...,0)`）
void refreshLandPage(QueryPanelState& s) {
    if (s.page != kPageLand) {
        return;
    }
    collectOwnedAssetIds(s.app->gameState(), s.selPlayer, s.subPage, s.assetIds);
    updateLandPage(s, 0);
}

// 内容区绘制（功能点 2/3/4 逐页实现）
void drawAssetPage(QueryPanelState& s);
void drawLandPage(QueryPanelState& s);
void drawStockPage(QueryPanelState& s);

// [RE 0x423070] 页内容 + [RE 0x422443] 静态层（重写合并为每次全量绘制）
void drawQueryPanel(QueryPanelState& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    const int sel = s.selPlayer;
    if (s.sheet.frameCount() <= 5 || sel < 0 || sel >= 8) {
        return;
    }
    // 页背景（帧 0/1/2 全屏 640x480）
    blitElementOpaque(dst, s.sheet.frame(s.page), 0, 0);
    // EXIT 钮（帧 5 @ 实参 (547,23)，落点 = 实参 − offset(29,9)）
    blitElement(dst, s.sheet.frame(5), 547, 23, false);
    // 选中玩家棋子头像（g_pieceSprites[13*sel] +12 = 帧 0）
    if (st.pieceSprites[sel].frameCount() > 0) {
        blitElement(dst, st.pieceSprites[sel].frame(0), 60, 100, false);
    }
    // 携带神明图标（12 + 12*frame @ (60,188)）+ 剩余天数（%d @ (60,234)）
    const uint8_t slot = st.players[sel].cellTableIdx;
    if (slot != 0 && slot < 19) {
        const int frame = kGodIconFrame[slot];
        if (frame > 0 && frame < s.sheet.frameCount()) {
            blitElement(dst, s.sheet.frame(frame), 60, 188, false);
        }
        if (slot <= 46) {
            const uint8_t life = st.cellTable[static_cast<size_t>(24 * (slot - 1)) + 4];
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", life);
            s.app->text().setFont(16, kTextDark, 0, 2, 0);
            s.app->text().drawText(dst, buf, 60, 234, 2);
        }
    }
    // 3 大页签文字（选中/按下白，其余灰；font20 shadow+bold @ x=60 居中）
    // 按下态底 = 帧 12 @ (12, 282+64i)（原版 DOWN 分支；UP 切换后由 page 态取代）
    for (int i = 0; i < 3; ++i) {
        const bool active = (i == s.page) || (s.pressed == i + 2);
        if (s.pressed == i + 2 && s.sheet.frameCount() > 12) {
            blitElement(dst, s.sheet.frame(12), kBigTabX, 282 + 64 * i, false);
        }
        s.app->text().setFont(20, active ? kTextWhite : kTextDim, kTextDark, 3, 0);
        s.app->text().drawText(dst, kQueryPageNames[i], 60, 302 + 64 * i, 2);
    }
    // 顶部玩家条（按钮帧 3 选中 / 4 未选 @ (16+88k, 14/15) + 名字 @ (+44, 30)）
    s.app->text().setFont(20, kTextDark, 0, 2, 0);
    for (size_t k = 0; k < s.aliveList.size(); ++k) {
        const int pi = s.aliveList[k];
        const int bx = kPlayerBtnX + kPlayerBtnStep * static_cast<int>(k);
        const int bf = (pi == sel) ? 3 : 4;
        if (bf < s.sheet.frameCount()) {
            blitElement(dst, s.sheet.frame(bf), bx, (pi == sel) ? 14 : 15, false);
        }
        if (st.players[pi].name) {
            s.app->text().drawText(dst, st.players[pi].name, bx + 44, 30, 2);
        }
    }
    // 翻页按钮：正常态仅地產清單页背景帧（panel.mkf[9] 帧 1）自带；
    // 按下态 = 帧 7（上）/8（下）（原版 DOWN 分支 blit，UP 后整页重绘恢复）
    if (s.pressed == 5 && s.sheet.frameCount() > 7) {
        blitElement(dst, s.sheet.frame(7), kPageBtnL, kPageUpT, false);
    }
    if (s.pressed == 6 && s.sheet.frameCount() > 8) {
        blitElement(dst, s.sheet.frame(8), kPageBtnL, kPageDownT, false);
    }
    // EXIT 按下态（原版 DOWN：帧 0 的 (492,9) 110x29 恢复 + 帧 6 @ (547,23)）
    if (s.pressed == 1 && s.sheet.frameCount() > 6) {
        blitElementRegionOpaque(dst, s.sheet.frame(0), kCloseL, kCloseT, kCloseL, kCloseT,
                                kCloseR - kCloseL, kCloseB - kCloseT, false);
        blitElement(dst, s.sheet.frame(6), 547, 23, false);
    }
    // 页内容
    switch (s.page) {
        case kPageAsset:
            drawAssetPage(s);
            break;
        case kPageLand:
            drawLandPage(s);
            break;
        case kPageStock:
            drawStockPage(s);
            break;
        default:
            break;
    }
}

// [RE 0x452793] 千分位金额（sub_452793）
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
        if (++cnt % 3 == 0 && i > 0) {
            rev[r++] = ',';
        }
    }
    int o = 0;
    if (neg) {
        out[o++] = '-';
    }
    for (int i = r - 1; i >= 0; --i) {
        out[o++] = rev[i];
    }
    out[o] = '\0';
}

// [RE 0x423070] 資產清單页：12 字段（标签画在背景帧 0 上）+ 道具 13 格 + 卡片 15 槽
// 依据: 0x423070 反编译 page==0 分支（330/602/250 右对齐，字体 28；道具 panel[74] 图标 +
//       数量，卡片 kCardNames[id]）；0x422443 画 12 字段标签（x=142/430 两列 + 第二排 142）
void drawAssetPage(QueryPanelState& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    TextRenderer& t = s.app->text();
    const int sel = s.selPlayer;
    const Player& p = st.players[sel];
    char buf[32];

    // 12 字段标签（sub_422443：off_475418）
    t.setFont(20, kTextDark, 0, 2, 0);
    for (int i = 0; i < 4; ++i) {
        t.drawText(dst, kQueryFieldNames[i], 142, 88 + 48 * i, 2);
    }
    for (int i = 4; i < 8; ++i) {
        t.drawText(dst, kQueryFieldNames[i], 430, 88 + 48 * (i - 4), 2);
    }
    for (int i = 8; i < 12; ++i) {
        t.drawText(dst, kQueryFieldNames[i], 142, 296 + 48 * (i - 8), 2);
    }

    // 第一排数值（font 28 深灰，右对齐）
    t.setFont(28, kTextDark, 0, 2, 0);
    fmtMoney(buf, p.cash);
    t.drawText(dst, buf, 330, 88, 6);
    fmtMoney(buf, p.bank);
    t.drawText(dst, buf, 330, 136, 6);
    fmtMoney(buf, p.loan);
    t.drawText(dst, buf, 330, 184, 6);
    fmtMoney(buf, playerTotalAssets(*s.app, sel));
    t.drawText(dst, buf, 330, 232, 6);
    // 持股市值 Σ(股数 × 现价)（逐项浮点累加后截断，同原版 sub_423070）
    int stockValue = 0;
    for (int i = 0; i < 12; ++i) {
        stockValue = static_cast<int>(st.playerShares[sel][i] * st.stocks[i][5]) + stockValue;
    }
    fmtMoney(buf, stockValue);
    t.drawText(dst, buf, 602, 88, 6);
    std::snprintf(buf, sizeof(buf), "%u", p.points); // itoa10(u16)
    t.drawText(dst, buf, 602, 136, 6);
    std::snprintf(buf, sizeof(buf), "%d", p.insuranceDays);
    t.drawText(dst, buf, 602, 184, 6);
    int holdCorp = 0; // 控股企業（specPt owner 计数）
    for (size_t i = 1; i < st.specPts.size(); ++i) {
        if (st.specPts[i].owner == sel + 1) {
            ++holdCorp;
        }
    }
    std::snprintf(buf, sizeof(buf), "%d", holdCorp);
    t.drawText(dst, buf, 602, 232, 6);
    // 第二排：土地 / 連鎖店 / 房 屋 / 設 施
    int land = 0;
    int chain = 0;
    int house = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (es.owner != sel + 1) {
            continue;
        }
        ++land;
        if (es.type != 0) {
            ++chain;
        } else if (es.level != 0) {
            ++house;
        }
    }
    int facility = 0;
    for (size_t i = 1; i < st.corps.size(); ++i) {
        if (st.corps[i].owner == sel + 1 && st.corps[i].sub != 0) {
            ++facility;
        }
    }
    std::snprintf(buf, sizeof(buf), "%d", land);
    t.drawText(dst, buf, 250, 296, 6);
    std::snprintf(buf, sizeof(buf), "%d", chain);
    t.drawText(dst, buf, 250, 344, 6);
    std::snprintf(buf, sizeof(buf), "%d", house);
    t.drawText(dst, buf, 250, 392, 6);
    std::snprintf(buf, sizeof(buf), "%d", facility);
    t.drawText(dst, buf, 250, 440, 6);

    // 道具 13 格（panel[74] 帧 id-1 @ (i-16, y)，数量 @ (i+30, y) 右对齐，白字）
    t.setFont(16, kTextWhite, kTextDark, 3, 0);
    int ix = 300;
    int iy = 281;
    for (int i = 0; i < 13; ++i) {
        const uint8_t n = st.itemStock[15 * sel + i];
        if (n == 0) {
            continue;
        }
        if (i < s.icons.frameCount()) {
            blitElement(dst, s.icons.frame(i), ix - 16, iy, false);
        }
        std::snprintf(buf, sizeof(buf), "%d", n);
        t.drawText(dst, buf, ix + 30, iy, 6);
        ix += 72;
        if (ix > 588) {
            ix = 300;
            iy += 32;
        }
    }
    // 卡片 15 槽（kCardNames[id] @ (i, y) 居中；深灰）
    t.setFont(16, kTextDark, 0, 2, 1);
    ix = 300;
    iy = 385;
    for (int i = 0; i < 15; ++i) {
        const uint8_t id = st.cardState60[15 * sel + i];
        if (id == 0) {
            continue;
        }
        t.drawText(dst, kCardNames[id], ix, iy, 2);
        ix += 72;
        if (ix > 588) {
            ix = 300;
            iy += 32;
        }
    }
}

// [RE 0x4225A3] 地產清單页：5 子页签 + 5 列列表（10 行/页）
// 依据: 0x4225A3 反编译; 子页签底 panel[9] 帧 11 @ (120+75k,64)、文字 off_4753D4 @ (+37,80)；
//   列表行 y=144+32*row；列 x = 168（地點 align2）/ 264（開發狀況）/ 394（價 格 align6）/
//   490（收 費 align6）/ 540（租 期 align2）；estate 字体 16/0x101010、corp 16/0x101110；
//   價格 = M×(priceAdd+priceBase×level) / M×(buildPrice+feeTable[0]×sub)；
//   收費 estate 普通 = estateRouteRent(地主, 同街)，連鎖行 = 查看玩家連鎖總租；
//   corp = M×feeTable[sub]；租期 = "%02d/%d/%d"（expireDate 年%100/月/日）或「無限期」
void drawLandPage(QueryPanelState& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    TextRenderer& t = s.app->text();
    const int sel = s.selPlayer;
    char buf[32];

    // 5 子页签
    for (int i = 0; i < 5; ++i) {
        const int sx = kSubTabX + kSubTabW * i;
        if (i == s.subPage && s.sheet.frameCount() > 11) {
            blitElement(dst, s.sheet.frame(11), sx, kSubTabT, false);
        }
        t.setFont(20, kTextDark, 0, 2, 0);
        t.drawText(dst, kQuerySubPageNames[i], sx + 37, kSubTabT + 16, 2);
    }
    // 列头（sub_422443 画在帧 1 上：off_4753E8 @ x=168/264/356/448/540 y=112）
    static const int kColX[5] = {168, 264, 356, 448, 540};
    for (int i = 0; i < 5; ++i) {
        t.setFont(20, kTextDark, 0, 2, 0);
        t.drawText(dst, kQueryLandCols[i], kColX[i], 112, 2);
    }

    // 查看玩家的連鎖店總租（原版 v3 = estateRouteRent(dword_48C27C + 1, 0)）
    int chainCount = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].owner == sel + 1 && st.estates[i].type != 0) {
            ++chainCount;
        }
    }
    const int32_t chainRent = chainCount * 2000 * st.moneyMul;

    // 行文字色（sub_4225A3）：普通/住宅 0x101010（黑）；商業用地/設施 0x1010F0（蓝）；
    //   子页签 2（商業區）整页蓝；子页签 0（全部）按列表序——首个 corp 行起后续行全部转蓝
    //   （原版 v32 标志只切一次，estate 分支不再改回）
    uint32_t rowColor = (s.subPage == 2) ? 0x1010F0 : kTextDark;
    for (int row = 0; row < s.pageShown; ++row) {
        const int idx = s.pageStart + row;
        if (idx < 0 || idx >= static_cast<int>(s.assetIds.size())) {
            break;
        }
        const uint16_t id = s.assetIds[idx];
        const int y = kListRowY0 + kListRowStep * row;
        if (id >= 4000) {
            rowColor = 0x1010F0; // [RE 0x4225A3] 蓝（1052912 = 0x1010F0）
            const size_t ci = id - 4000;
            if (ci >= st.corps.size()) {
                continue;
            }
            const Corp& cp = st.corps[ci];
            const std::string name = big5ToUtf8(reinterpret_cast<const char*>(cp.pad4),
                                                sizeof(cp.pad4));
            t.setFont(16, rowColor, 0, 2, 1);
            t.drawText(dst, name.c_str(), kColX[0], y, 2);
            if (cp.sub != 0 && cp.type <= 4) {
                t.drawText(dst, kBuildingNames[6 + cp.type], kColX[1], y, 2);
            } else {
                t.drawText(dst, kBuildingNames[0], kColX[1], y, 2);
            }
            std::snprintf(buf, sizeof(buf), "%d",
                          st.moneyMul * (cp.buildPrice + cp.feeTable[0] * cp.sub));
            t.drawText(dst, buf, kColX[2] + 38, y, 6);
            const int fee = (cp.type != 0 && cp.sub != 0 && cp.sub < 6)
                                ? st.moneyMul * cp.feeTable[cp.sub]
                                : 0;
            std::snprintf(buf, sizeof(buf), "%d", fee);
            t.drawText(dst, buf, kColX[3] + 42, y, 6);
            if (cp.expireDate != 0) {
                std::snprintf(buf, sizeof(buf), "%02d/%d/%d",
                              static_cast<int>((cp.expireDate >> 16) % 100),
                              static_cast<int>((cp.expireDate >> 8) & 0xF),
                              static_cast<int>(cp.expireDate & 0xFF));
                t.drawText(dst, buf, kColX[4], y, 2);
            } else {
                t.drawText(dst, "無限期", kColX[4], y, 2);
            }
        } else {
            // 住宅用地产行
            const size_t ei = id - 2000;
            if (ei >= st.estates.size()) {
                continue;
            }
            const Estate& es = st.estates[ei];
            const std::string name = big5ToUtf8(es.name, sizeof(es.name));
            t.setFont(16, rowColor, 0, 2, 1);
            t.drawText(dst, name.c_str(), kColX[0], y, 2);
            int32_t fee = 0;
            if (es.type != 0) {
                t.drawText(dst, "連鎖店", kColX[1], y, 2);
                fee = chainRent;
            } else {
                const int lv = es.level < 17 ? es.level : 0;
                t.drawText(dst, kBuildingNames[lv], kColX[1], y, 2);
                fee = estateRouteRent(st, es.owner, es);
            }
            std::snprintf(buf, sizeof(buf), "%d",
                          st.moneyMul * (es.priceAdd + es.priceBase * es.level));
            t.drawText(dst, buf, kColX[2] + 38, y, 6);
            std::snprintf(buf, sizeof(buf), "%d", fee);
            t.drawText(dst, buf, kColX[3] + 42, y, 6);
            if (es.expireDate != 0) {
                std::snprintf(buf, sizeof(buf), "%02d/%d/%d",
                              static_cast<int>((es.expireDate >> 16) % 100),
                              static_cast<int>((es.expireDate >> 8) & 0xF),
                              static_cast<int>(es.expireDate & 0xFF));
                t.drawText(dst, buf, kColX[4], y, 2);
            } else {
                t.drawText(dst, "無限期", kColX[4], y, 2);
            }
        }
    }
}

// [RE 0x423070] 股票清單页：12 支股票名/持有股数/市值
// 依据: 0x423070 反编译 page==2 分支; 列头 off_475448 @ x=204/332/476 y=68；
//   行 y=100+32*i；股票名 kStockNames[4*mode+map] @ 204 align2、
//   持有張數 @ 384 align6、總市價 = 股数×现价 @ 536 align6；字体 16 深灰
void drawStockPage(QueryPanelState& s) {
    GameState& st = s.app->gameState();
    Surface& dst = s.app->surface();
    TextRenderer& t = s.app->text();
    const int sel = s.selPlayer;
    const int gn = st.gameMode * 4 + st.mapIndex;
    char buf[32];

    static const int kStockColX[3] = {204, 332, 476};
    t.setFont(20, kTextDark, 0, 2, 0);
    for (int j = 0; j < 3; ++j) {
        t.drawText(dst, kQueryStockCols[j], kStockColX[j], 68, 2);
    }
    t.setFont(16, kTextDark, 0, 2, 0);
    for (int i = 0; i < 12; ++i) {
        const int y = 100 + 32 * i;
        t.drawText(dst, kStockNames[gn][i], kStockColX[0], y, 2);
        fmtMoney(buf, st.playerShares[sel][i]);
        t.drawText(dst, buf, kStockColX[1] + 52, y, 6);
        const int value = static_cast<int>(st.playerShares[sel][i] * st.stocks[i][5]);
        fmtMoney(buf, value);
        t.drawText(dst, buf, kStockColX[2] + 60, y, 6);
    }
}

// [RE 0x423CF3] sub_423CF3 模态窗口过程
bool queryEventHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<QueryPanelState*>(user);
    if (!event) {
        drawQueryPanel(s); // WM_USER+1 初始化/重绘
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            s.app->events().requestExit(0);
            return true;
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x423CF3] WM_RBUTTONUP → setPauseDraw(0) + postModalExit(0)
            s.app->events().requestExit(0);
            return true;
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        // 注意：原版 sub_423CF3 **全函数无音效调用**——本面板不播点击音
        // [RE 0x423CF3] WM_LBUTTONDOWN：记 byte_48C284 + 画按下态（UP 时执行）
        if (x >= kCloseL && x <= kCloseR && y >= kCloseT && y <= kCloseB) {
            s.pressed = 1; // EXIT
            drawQueryPanel(s);
            return true;
        }
        for (int i = 0; i < 3; ++i) {
            const int ty = kBigTabY0 + kBigTabStep * i;
            if (x >= kBigTabX && x <= kBigTabX + kBigTabW && y >= ty && y <= ty + kBigTabH) {
                s.pressed = 2 + i;
                drawQueryPanel(s);
                return true;
            }
        }
        // 子页签：原版 DOWN 立即切换（sub_423070 + sub_4225A3(dir=0) 回首页），不记 byte_48C284
        //   （原版外层条件 dword_4753FC == 1，仅地產清單页）
        if (s.page == kPageLand) {
            for (int i = 0; i < 5; ++i) {
                const int sx = kSubTabX + kSubTabW * i;
                if (x >= sx && x <= sx + kSubTabW && y >= kSubTabT && y <= kSubTabB) {
                    if (s.subPage != i) {
                        s.subPage = i;
                        refreshLandPage(s);
                        drawQueryPanel(s);
                    }
                    return true;
                }
            }
        }
        // 顶部玩家条：原版 DOWN 立即切换（byte_48C284=7 但 UP 分支无 case 7）
        for (size_t k = 0; k < s.aliveList.size(); ++k) {
            const int bx = kPlayerBtnX + kPlayerBtnStep * static_cast<int>(k);
            if (x >= bx && x <= bx + 88 && y >= kPlayerBtnT && y <= kPlayerBtnB) {
                if (s.selPlayer != s.aliveList[k]) {
                    s.selPlayer = s.aliveList[k];
                    refreshLandPage(s);
                    drawQueryPanel(s);
                }
                return true;
            }
        }
        // 翻页箭头（仅地產清單页；DOWN 画帧 7/8 按下态，UP 翻页）
        if (s.page == kPageLand && x >= kPageBtnL && x <= kPageBtnR) {
            if (y >= kPageUpT && y <= kPageUpB) {
                s.pressed = 5;
                drawQueryPanel(s);
                return true;
            }
            if (y >= kPageDownT && y <= kPageDownB) {
                s.pressed = 6;
                drawQueryPanel(s);
                return true;
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        // [RE 0x423CF3] WM_LBUTTONUP：按 byte_48C284 分发（松开触发）
        const int pressed = s.pressed;
        s.pressed = 0;
        switch (pressed) {
            case 1: // EXIT：setPauseDraw(0) + postModalExit(0)
                s.app->events().requestExit(0);
                return true;
            case 2:
            case 3:
            case 4: { // 切页签：dword_4753FC = byte_48C284-2；page==1 时 sub_4225A3(...,0)
                const int page = pressed - 2;
                if (s.page != page) {
                    s.page = page;
                    refreshLandPage(s);
                }
                drawQueryPanel(s);
                return true;
            }
            case 5: // 上一页（sub_4225A3(dir=2)）
                updateLandPage(s, 2);
                drawQueryPanel(s);
                return true;
            case 6: // 下一页（sub_4225A3(dir=1)）
                updateLandPage(s, 1);
                drawQueryPanel(s);
                return true;
            default:
                break;
        }
        drawQueryPanel(s);
        return true;
    }
    return false;
}

} // namespace

void queryDialog(Application& app) {
    trace::logf("dialog open name=query");
    // [RE 0x424492] 工具条 case 6 → 查詢面板（panel.mkf[9] + [74]）
    QueryPanelState st;
    st.app = &app;
    GameState& state = app.gameState();
    if (auto blob = state.panel.read(9)) {
        st.sheet.load(std::move(*blob));
    } else {
        RICH4_LOGE("queryDialog: panel.mkf[9] unavailable (RE 0x424492)");
        return;
    }
    if (auto blob = state.panel.read(74)) {
        st.icons.load(std::move(*blob));
    } else {
        RICH4_LOGW("queryDialog: panel.mkf[74] unavailable (RE 0x424492)");
    }
    // [RE 0x422443] 存活玩家列表（byte_48C278）+ 默认选中当前玩家（dword_48C27C）
    for (int i = 0; i < state.playerCount; ++i) {
        if (state.players[i].alive != 0) {
            st.aliveList.push_back(static_cast<uint8_t>(i));
        }
    }
    st.selPlayer = state.currentPlayer;
    if (st.selPlayer < 0 || st.selPlayer >= 8) {
        st.selPlayer = st.aliveList.empty() ? 0 : st.aliveList[0];
    }
    refreshLandPage(st);
    RICH4_LOGI("query dialog (RE 0x424492): page=%d sel=%d alive=%zu", st.page, st.selPlayer,
               st.aliveList.size());
    runModal(app, &queryEventHandler, &st);
}

} // namespace rich4
