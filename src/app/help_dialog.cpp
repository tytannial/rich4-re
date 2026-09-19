#include "game/app/help_dialog.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

#include <string>
#include <vector>

namespace rich4 {

namespace {

// [RE 0x4761B4] 8 个分类（名称 / 起始页 dword_4761BC / 页数 dword_4761C0）
struct HelpCategory {
    const char* name;
    int startPage;
    int pageCount;
};
constexpr HelpCategory kCategories[8] = {
    {"操作說明", 1, 1},   {"遊戲畫面", 2, 6},   {"遊戲指令", 8, 12},  {"房地產", 20, 3},
    {"特殊地點", 23, 16}, {"特殊人物", 39, 18}, {"卡片", 57, 30},     {"道具", 87, 13},
};

// [RE 0x4761B8] 各分类页面标题（off_4761B8[5*cat][page]）
const char* const kCat0Titles[] = {"遊戲操作"};
const char* const kCat1Titles[] = {"日、月曆", "地產資料", "其他資料",
                                   "物價指數", "股票資料", "資金資料"};
const char* const kCat2Titles[] = {"LOAD",   "SAVE", "卡片", "交易", "地圖", "系統",
                                   "股市",   "前進", "查詢", "託管", "道具", "說明"};
const char* const kCat3Titles[] = {"公司企業", "住宅用地", "商業用地"};
const char* const kCat4Titles[] = {"七彩氣球", "公園",     "卡片",     "企鵝挖寶", "百貨公司",
                                   "命運",     "得十點",   "得三十點", "得五十點", "喜從天降",
                                   "新聞",     "監獄",     "銀行",     "樂透",     "醫院",
                                   "魔法屋"};
const char* const kCat5Titles[] = {"乞丐",   "土地公", "大衰神", "大財神", "大福神", "大窮神",
                                   "小衰神", "小財神", "小偷",   "小福神", "小窮神", "天使",
                                   "死神",   "流氓",   "強盜",   "惡犬",   "惡魔",   "間諜"};
const char* const kCat6Titles[] = {
    "天使卡", "冬眠卡", "同盟卡", "免費卡", "免罪卡", "均貧卡", "均富卡", "改建卡",
    "怪獸卡", "拍賣卡", "拆除卡", "查封卡", "查稅卡", "紅卡",   "烏龜卡", "送神符",
    "停留卡", "陷害卡", "復仇卡", "惡魔卡", "換地卡", "換屋卡", "黑卡",   "嫁禍卡",
    "搶奪卡", "夢遊卡", "漲價卡", "請神符", "購地卡", "轉向卡"};
const char* const kCat7Titles[] = {"工程車", "地雷",     "汽車",     "定時炸彈", "飛彈",
                                   "時光機", "核子飛彈", "傳送機",   "路障",     "遙控骰子",
                                   "機車",   "機器工人", "機器娃娃"};

const char* const* const kCategoryTitles[8] = {
    kCat0Titles, kCat1Titles, kCat2Titles, kCat3Titles,
    kCat4Titles, kCat5Titles, kCat6Titles, kCat7Titles,
};

// 布局（[RE 0x44DFB4] 绘制 / [RE 0x44E40B] 控件表 dword_476254 与 dword_476274 等，
//       均相对 400x400 框架；帧号 = 原版偏移/12 - 1）
constexpr int kCatHitX1 = 26;       // 目录项命中区 (26..91, 58+36i..90+36i)
constexpr int kCatHitX2 = 92;
constexpr int kCatHitY = 58;
constexpr int kCatLineH = 36;
constexpr int kCatNameX = 59;       // 目录名中心
constexpr int kCatNameY = 73;
constexpr int kListHitX1 = 108;     // 页面列表命中区 (108..192, 78+34j..110+34j)
constexpr int kListHitX2 = 193;
constexpr int kListY = 78;
constexpr int kListLineH = 34;
constexpr int kListNameX = 150;     // 页面标题中心（15 号）
constexpr int kListNameY = 94;
constexpr int kScrollX = 170;       // 滚动箭头帧 4/5
constexpr int kScrollUpY = 43;
constexpr int kScrollDownY = 59;
constexpr int kMoreLeftX = 322;     // 继续阅读按钮帧 8/9
constexpr int kMoreRightX = 343;
constexpr int kMoreY = 48;
constexpr int kContentTitleX = 140; // 分类名（内容区）
constexpr int kContentTitleY = 57;
constexpr int kPageTitleX = 270;    // 当前页标题（15 号）
constexpr int kPageTitleY = 63;
constexpr int kTextX = 232;         // 正文
constexpr int kTextY = 90;
constexpr int kTextLineH = 18;
constexpr int kTextMaxLines = 14;

struct HelpLine {
    bool isBreak = false; // '@' 分页符行（原版 *(BYTE*)==64）
    std::string text;
};

struct HelpState {
    Application* app = nullptr;
    UiImage* ui = nullptr;      // help.mkf[0] 界面框架
    MkfArchive* help = nullptr;
    int category = 0;           // dword_476018（原版初始 0，打开即显示首分类）
    int page = 0;               // dword_47601C
    int scroll = 0;             // dword_4761C4[5*cat]
    int originX = 0;            // dword_48C5E4
    int originY = 0;            // dword_48C5E8
    std::vector<HelpLine> lines;
    int pageMark = 0;           // dword_4762B8（0 或 '@' 行索引；绘制起点 = 其后一行）
    bool hasMore = false;       // dword_48C5EC（正文被 '@' 或 14 行截断）
    int pressed = -1;           // dword_48C5FC 按下的控件（2=上滚 3=下滚 4=继续左 5=继续右）
};

// 加载页面文本（help.mkf[start+page]，0x00 分隔行，'@' 行 = 分页符）
void loadPage(HelpState& state) {
    state.lines.clear();
    state.pageMark = 0;
    state.hasMore = false;
    if (!state.help || state.category < 0) {
        return;
    }
    const HelpCategory& cat = kCategories[state.category];
    auto blob = state.help->read(static_cast<size_t>(cat.startPage + state.page));
    if (!blob) {
        return;
    }
    const uint8_t* data = blob->data();
    size_t start = 0;
    for (size_t i = 0; i < blob->size(); ++i) {
        if (data[i] != 0) {
            continue;
        }
        if (i > start) {
            HelpLine line;
            if (data[start] == '@') {
                line.isBreak = true;
            } else {
                line.text = big5ToUtf8(reinterpret_cast<const char*>(data + start),
                                       static_cast<int>(i - start));
            }
            state.lines.push_back(std::move(line));
        }
        start = i + 1;
    }
}

// 绘制正文：从 pageMark（0 或 '@' 行）之后起最多 14 行，遇 '@' 行停止
// （[RE 0x44DFB4] 主路径 / [RE 0x44DD9F] 翻屏路径共用；原版绘制循环不更新
//   dword_4762B8；updateMore=false 时保持 dword_48C5EC 不变——翻屏不重算截断标志）
void drawPageText(HelpState& state, Surface& surface, bool updateMore) {
    TextRenderer& text = state.app->text();
    text.setFont(12, 0x101010, 0x101010, 0, 1);
    if (updateMore) {
        state.hasMore = false;
    }
    const int count = static_cast<int>(state.lines.size());
    int v = state.pageMark;
    if (v < count && state.lines[v].isBreak) {
        ++v; // 跳过 '@' 分页符行
    }
    int drawn = 0;
    while (v < count) {
        if (state.lines[v].isBreak) {
            if (updateMore) {
                state.hasMore = true;
            }
            break;
        }
        text.drawText(surface, state.lines[v].text.c_str(), state.originX + kTextX,
                      state.originY + kTextY + kTextLineH * drawn, 0);
        ++v;
        ++drawn;
        if (drawn == kTextMaxLines) {
            if (updateMore) {
                state.hasMore = v < count;
            }
            break;
        }
    }
}

void redrawHelp(HelpState& state, bool updateMore = true) {
    Surface& surface = state.app->surface();
    if (!state.ui) {
        return;
    }
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;
    TextRenderer& text = state.app->text();

    // [RE 0x44DFB4] 帧 0 框架（全屏，原版 blitElementFullscreen = 不透明 blit）
    blitElementOpaque(surface, ui.frame(0), ox, oy);

    // [RE 0x44EB39] 8 个目录名（原版初始化时画到帧 0 画布）
    text.setFont(12, 0x101010, 0x101010, 0, 1);
    for (int i = 0; i < 8; ++i) {
        text.drawText(surface, kCategories[i].name, ox + kCatNameX, oy + kCatNameY + kCatLineH * i, 2);
    }

    if (state.category < 0) {
        return;
    }
    const HelpCategory& cat = kCategories[state.category];

    // 帧 1 选中高亮条 (26, 58+36*cat) + 选中分类名重画（目录位置被高亮条覆盖）
    blitElementOpaque(surface, ui.frame(1), ox + kCatHitX1,
                      oy + kCatLineH * state.category + kCatHitY);
    text.setFont(12, 0x101010, 0x101010, 0, 1);
    text.drawText(surface, cat.name, ox + kCatNameX, oy + kCatNameY + kCatLineH * state.category, 2);
    text.drawText(surface, cat.name, ox + kContentTitleX, oy + kContentTitleY, 2);

    // 滚动箭头（帧 4/5 正常，帧 6/7 按下；pageCount > 8 时）
    if (cat.pageCount > 8) {
        const int upFrame = (state.pressed == 2) ? 6 : 4;
        const int downFrame = (state.pressed == 3) ? 7 : 5;
        blitElementOpaque(surface, ui.frame(upFrame), ox + kScrollX, oy + kScrollUpY);
        blitElementOpaque(surface, ui.frame(downFrame), ox + kScrollX, oy + kScrollDownY);
    }

    // 正文（14 行上限）
    drawPageText(state, surface, updateMore);

    // 继续阅读按钮（帧 8/9 正常，帧 10/11 按下）
    if (state.hasMore) {
        const int leftFrame = (state.pressed == 4) ? 10 : 8;
        const int rightFrame = (state.pressed == 5) ? 11 : 9;
        blitElementOpaque(surface, ui.frame(leftFrame), ox + kMoreLeftX, oy + kMoreY);
        blitElementOpaque(surface, ui.frame(rightFrame), ox + kMoreRightX, oy + kMoreY);
    }

    // 页面列表（帧 3 选中 / 帧 2 未选中）+ 标题（15 号粗体）
    for (int i = 0; i < 8; ++i) {
        const int index = state.scroll + i;
        if (index >= cat.pageCount) {
            break;
        }
        const int frame = (index == state.page) ? 3 : 2;
        blitElementOpaque(surface, ui.frame(frame), ox + kListHitX1, oy + kListY + kListLineH * i);
    }
    text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 0);
    for (int i = 0; i < 8; ++i) {
        const int index = state.scroll + i;
        if (index >= cat.pageCount) {
            break;
        }
        text.drawText(surface, kCategoryTitles[state.category][index], ox + kListNameX,
                      oy + kListNameY + kListLineH * i, 2);
    }

    // 当前页标题
    text.drawText(surface, kCategoryTitles[state.category][state.page], ox + kPageTitleX,
                  oy + kPageTitleY, 2);
}

// [RE 0x44DD9F] 继续阅读翻屏：a1=1 下一屏 / a1=0 上一屏
// 依据: dword_4762B8 为屏起点标记（0 或 '@' 行）；前进/回退最多 14 行（遇 '@' 停），
//       结果写回 dword_4762B8；绘制时若为 '@' 行则从其后一行开始
void scrollScreen(HelpState& state, bool forward) {
    const int count = static_cast<int>(state.lines.size());
    if (forward) {
        int v = state.pageMark;
        if (v < count && state.lines[v].isBreak) {
            ++v; // 跳过 '@' 分页符行
        }
        if (v >= count) {
            return; // 已到末尾（原版 v2 == size 直接返回）
        }
        int v1 = 0;
        while (v < count) {
            ++v;
            if (v >= count || state.lines[v].isBreak || v1 == kTextMaxLines - 1) {
                break;
            }
            ++v1;
        }
        state.pageMark = v;
    } else {
        if (state.pageMark == 0) {
            return; // 已在首屏（原版 !v2 直接返回）
        }
        int v = state.pageMark;
        int v1 = 0;
        while (v > 0) {
            --v;
            if (state.lines[v].isBreak || v1 == kTextMaxLines - 1) {
                break;
            }
            ++v1;
        }
        state.pageMark = v;
    }
    redrawHelp(state, false);
}

// [RE 0x44E40B] helpWndProc
bool helpEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<HelpState*>(user);

    if (!event) {
        // [RE 0x44E40B] WM_USER+1：居中 + 初始显示首个分类
        state.category = 0;
        state.page = 0;
        state.scroll = 0;
        state.pressed = -1;
        loadPage(state);
        redrawHelp(state);
        return true;
    }

    // [RE 0x44E40B] WM_LBUTTONUP(0x202)：执行按下控件（滚动/继续阅读）
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        const int pressed = state.pressed;
        state.pressed = -1;
        if (pressed < 0) {
            return true;
        }
        const HelpCategory& cat = kCategories[state.category];
        if ((pressed == 2 || pressed == 3) && cat.pageCount > 8) {
            if (pressed == 2 && state.scroll > 0) {
                // [RE 0x44E40B] case 2：向上滚动一屏（page 同步上移）
                if (state.scroll <= 8) {
                    state.page -= state.scroll;
                    state.scroll = 0;
                } else {
                    state.page -= 8;
                    state.scroll -= 8;
                }
                loadPage(state);
            } else if (pressed == 3) {
                // [RE 0x44E40B] case 3：向下滚动一屏（page 同步下移）
                const int maxScroll = cat.pageCount - 8;
                if (state.scroll < maxScroll) {
                    if (state.scroll + 8 >= maxScroll) {
                        state.page += maxScroll - state.scroll;
                        state.scroll = maxScroll;
                    } else {
                        state.page += 8;
                        state.scroll += 8;
                    }
                    loadPage(state);
                }
            }
            redrawHelp(state);
            return true;
        }
        if (pressed == 4) {
            // [RE 0x44E40B] case 4 → sub_44DD9F(0)：上一屏
            scrollScreen(state, false);
            return true;
        }
        if (pressed == 5) {
            // [RE 0x44E40B] case 5 → sub_44DD9F(1)：下一屏
            scrollScreen(state, true);
            return true;
        }
        return true;
    }

    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) {
        return false;
    }
    const int rx = static_cast<int>(event->button.x) - state.originX;
    const int ry = static_cast<int>(event->button.y) - state.originY;

    if (event->button.button == SDL_BUTTON_RIGHT) {
        // [RE 0x44E40B] WM_RBUTTONDOWN：退出
        state.app->events().requestExit(0);
        return true;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return true;
    }

    const HelpCategory& cat = kCategories[state.category];

    // 目录项（命中 (26..91, 58+36i..90+36i)）
    if (rx >= kCatHitX1 && rx < kCatHitX2 && ry >= kCatHitY &&
        ry < kCatHitY + kCatLineH * 8) {
        const int catIndex = (ry - kCatHitY) / kCatLineH;
        state.category = catIndex;
        state.page = 0;
        state.scroll = 0;
        loadPage(state);
        redrawHelp(state);
        return true;
    }

    // 页面列表（命中 (108..192, 78+34j..110+34j)，仅可见项）
    if (rx >= kListHitX1 && rx < kListHitX2 && ry >= kListY && ry < kListY + kListLineH * 8) {
        const int visible = cat.pageCount - state.scroll;
        const int item = (ry - kListY) / kListLineH;
        if (item >= 0 && item < visible && item < 8) {
            state.page = state.scroll + item;
            loadPage(state);
            redrawHelp(state);
        }
        return true;
    }

    // 滚动箭头：Down 画按下态（帧 6/7），Up 执行（pageCount > 8 时有效）
    if (cat.pageCount > 8 && rx >= kScrollX && rx < kScrollX + 22) {
        if (ry >= kScrollUpY && ry < kScrollUpY + 15) {
            state.pressed = 2;
            redrawHelp(state);
            return true;
        }
        if (ry >= kScrollDownY && ry < kScrollDownY + 15) {
            state.pressed = 3;
            redrawHelp(state);
            return true;
        }
    }

    // 继续阅读按钮：Down 画按下态（帧 10/11），Up 执行
    if (state.hasMore && ry >= kMoreY && ry < kMoreY + 32) {
        if (rx >= kMoreLeftX && rx < kMoreRightX) {
            state.pressed = 4;
            redrawHelp(state);
            return true;
        }
        if (rx >= kMoreRightX && rx < kMoreRightX + 23) {
            state.pressed = 5;
            redrawHelp(state);
            return true;
        }
    }

    return true;
}

} // namespace

void helpDialog(Application& app) {
    // [RE 0x44EB39] 依据: sub_4502FE("help.mkf") + sub_450441(handle, 0) 框架;
    //       runModal(sub_44E40B, -1 | (-1 << 16))
    HelpState state;
    state.app = &app;

    MkfArchive help;
    if (!help.load(app.gameDir() + "/Help.mkf")) {
        RICH4_LOGE("helpDialog: Help.mkf unavailable");
        return;
    }
    state.help = &help;

    UiImage uiImage;
    if (auto blob = help.read(0)) {
        uiImage.load(std::move(*blob));
    }
    if (uiImage.frameCount() < 12) {
        RICH4_LOGE("helpDialog: help.mkf[0] invalid");
        return;
    }
    state.ui = &uiImage;
    state.originX = 320 - uiImage.frame(0).width / 2;
    state.originY = 240 - uiImage.frame(0).height / 2;

    RICH4_LOGI("help dialog at (%d,%d) (RE 0x44EB39)", state.originX, state.originY);
    runModal(app, &helpEventHandler, &state);
}

} // namespace rich4
