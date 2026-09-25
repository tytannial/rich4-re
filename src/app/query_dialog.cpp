#include "game/app/query_dialog.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "game/app/event_stack.h"
#include "game/app/economy.h"
#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
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
    // 3 大页签文字（选中白/未选灰，font20 shadow+bold @ x=60 居中）
    for (int i = 0; i < 3; ++i) {
        s.app->text().setFont(20, (i == s.page) ? kTextWhite : kTextDim, kTextDark, 3, 0);
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
    // 翻页箭头（帧 7 上 / 8 下）
    if (s.sheet.frameCount() > 8) {
        blitElement(dst, s.sheet.frame(7), kPageBtnL, kPageUpT, false);
        blitElement(dst, s.sheet.frame(8), kPageBtnL, kPageDownT, false);
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

void drawLandPage(QueryPanelState& s) {
    (void)s; // 功能点 3 实现（5 子页签 + 列表）
}

void drawStockPage(QueryPanelState& s) {
    (void)s; // 功能点 4 实现（12 支持仓/市值）
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
        // 关闭（EXIT 钮）
        if (x >= kCloseL && x <= kCloseR && y >= kCloseT && y <= kCloseB) {
            s.app->audio().playEffect(1);
            s.app->events().requestExit(0);
            return true;
        }
        bool changed = false;
        // 3 大页签
        for (int i = 0; i < 3 && !changed; ++i) {
            const int ty = kBigTabY0 + kBigTabStep * i;
            if (x >= kBigTabX && x <= kBigTabX + kBigTabW && y >= ty && y <= ty + kBigTabH) {
                if (s.page != i) {
                    s.page = i;
                    changed = true;
                }
            }
        }
        if (!changed && s.page != kPageAsset) {
            // 子页签（地產清單 5 项）
            for (int i = 0; i < 5; ++i) {
                const int sx = kSubTabX + kSubTabW * i;
                if (x >= sx && x <= sx + kSubTabW && y >= kSubTabT && y <= kSubTabB) {
                    if (s.subPage != i) {
                        s.subPage = i;
                        changed = true;
                    }
                    break;
                }
            }
        }
        if (!changed) {
            // 顶部玩家条
            for (size_t k = 0; k < s.aliveList.size(); ++k) {
                const int bx = kPlayerBtnX + kPlayerBtnStep * static_cast<int>(k);
                if (x >= bx && x <= bx + 88 && y >= kPlayerBtnT && y <= kPlayerBtnB) {
                    if (s.selPlayer != s.aliveList[k]) {
                        s.selPlayer = s.aliveList[k];
                        changed = true;
                    }
                    break;
                }
            }
        }
        if (!changed && x >= kPageBtnL && x <= kPageBtnR) {
            // 翻页箭头
            if (y >= kPageUpT && y <= kPageUpB) {
                if (s.page == kPageLand) {
                    updateLandPage(s, 2);
                    changed = true;
                }
            } else if (y >= kPageDownT && y <= kPageDownB) {
                if (s.page == kPageLand) {
                    updateLandPage(s, 1);
                    changed = true;
                }
            }
        }
        if (changed) {
            s.app->audio().playEffect(1);
            refreshLandPage(s);
            drawQueryPanel(s);
        }
        return true;
    }
    return false;
}

} // namespace

void queryDialog(Application& app) {
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
