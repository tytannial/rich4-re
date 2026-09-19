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

// 布局（[RE 0x44DFB4]/[RE 0x44EB39]，相对 400x400 框架）
constexpr int kCatNameX = 59;       // 目录名 x（sub_44EB39）
constexpr int kCatNameY = 73;       // 目录名首行 y
constexpr int kCatLineH = 36;       // 目录行距
constexpr int kTitleBarX = 26;      // 标题栏帧 1 偏移（sub_44DFB4）
constexpr int kTitleBarY = 58;
constexpr int kContentTitleX = 140; // 分类标题
constexpr int kContentTitleY = 57;
constexpr int kPageListX = 108;     // 页面列表 x（帧 2/3）
constexpr int kPageListY = 78;
constexpr int kPageListLineH = 34;
constexpr int kPageListTitleX = 150;
constexpr int kPageListTitleY = 94;
constexpr int kPageTitleX = 270;    // 当前页标题
constexpr int kPageTitleY = 63;
constexpr int kTextX = 232;         // 正文 x
constexpr int kTextY = 90;
constexpr int kTextLineH = 18;
constexpr int kTextMaxLines = 14;   // 原版每页最多 14 行（v1 == 13 截断）
constexpr int kPageListVisible = 8;

struct HelpState {
    Application* app = nullptr;
    UiImage* ui = nullptr;   // help.mkf[0] 界面框架
    MkfArchive* help = nullptr;
    int category = -1;       // dword_476018（-1 = 仅显示目录）
    int page = 0;            // dword_47601C
    int scroll = 0;          // dword_4761C4 列表滚动
    int originX = 0;         // dword_48C5E4
    int originY = 0;         // dword_48C5E8
    std::vector<std::string> lines; // 当前页文本行（UTF-8）
};

// 加载页面文本（help.mkf[start+page]，0x00 分隔行 → BIG5→UTF-8）
void loadPage(HelpState& state) {
    state.lines.clear();
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
        if (data[i] == 0) {
            if (i > start) {
                state.lines.push_back(big5ToUtf8(reinterpret_cast<const char*>(data + start),
                                                 static_cast<int>(i - start)));
            }
            start = i + 1;
        }
    }
}

void redrawHelp(HelpState& state) {
    Surface& surface = state.app->surface();
    if (!state.ui) {
        return;
    }
    const UiImage& ui = *state.ui;
    const int ox = state.originX;
    const int oy = state.originY;
    TextRenderer& text = state.app->text();

    // [RE 0x44DFB4] 帧 0 框架
    blitElement(surface, ui.frame(0), ox, oy, false);

    // 8 个目录项（[RE 0x44EB39] 位置 (59, 73+36i)，字号 12）
    text.setFont(12, 0x101010, 0x101010, 0, 1);
    for (int i = 0; i < 8; ++i) {
        text.drawText(surface, kCategories[i].name, ox + kCatNameX,
                      oy + kCatNameY + kCatLineH * i, 2);
    }

    if (state.category < 0) {
        return;
    }
    const HelpCategory& cat = kCategories[state.category];

    // 标题栏（帧 1）+ 分类名
    blitElement(surface, ui.frame(1), ox + kTitleBarX,
                oy + kCatLineH * state.category + kTitleBarY, false);
    text.drawText(surface, cat.name, ox + kContentTitleX, oy + kContentTitleY, 2);

    // 页面列表（帧 3 选中 / 帧 2 未选中）
    for (int i = 0; i < kPageListVisible; ++i) {
        const int index = state.scroll + i;
        if (index >= cat.pageCount) {
            break;
        }
        const int y = kPageListY + kPageListLineH * i;
        const int frame = (index == state.page) ? 3 : 2;
        if (frame < ui.frameCount()) {
            blitElement(surface, ui.frame(frame), ox + kPageListX, oy + y, false);
        }
        text.drawText(surface, kCategoryTitles[state.category][index], ox + kPageListTitleX,
                      oy + kPageListTitleY + kPageListLineH * i, 2);
    }

    // 当前页标题
    text.drawText(surface, kCategoryTitles[state.category][state.page], ox + kPageTitleX,
                  oy + kPageTitleY, 2);

    // 正文（最多 14 行，行距 18）
    for (size_t i = 0; i < state.lines.size() && i < kTextMaxLines; ++i) {
        text.drawText(surface, state.lines[i].c_str(), ox + kTextX,
                      oy + kTextY + kTextLineH * static_cast<int>(i), 0);
    }
}

// [RE 0x44E40B] helpWndProc
bool helpEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<HelpState*>(user);

    if (!event) {
        // [RE 0x44E40B] WM_USER+1：初始显示目录
        state.category = -1;
        state.page = 0;
        state.scroll = 0;
        redrawHelp(state);
        return true;
    }

    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) {
        return false;
    }
    const int rx = static_cast<int>(event->button.x) - state.originX;
    const int ry = static_cast<int>(event->button.y) - state.originY;

    if (event->button.button == SDL_BUTTON_RIGHT) {
        // 分类模式 → 返回目录；目录模式 → 退出
        if (state.category >= 0) {
            state.category = -1;
            state.page = 0;
            state.scroll = 0;
            state.lines.clear();
            redrawHelp(state);
        } else {
            state.app->events().requestExit(0);
        }
        return true;
    }

    if (event->button.button != SDL_BUTTON_LEFT) {
        return true;
    }

    // 目录项点击（左侧 8 行）
    if (rx >= 30 && rx <= 210 && ry >= kCatNameY - 18) {
        const int cat = (ry - kCatNameY + kCatLineH / 2) / kCatLineH;
        if (cat >= 0 && cat < 8) {
            state.category = cat;
            state.page = 0;
            state.scroll = 0;
            loadPage(state);
            redrawHelp(state);
            return true;
        }
    }

    // 页面列表点击（右侧 8 项）
    if (state.category >= 0 && rx >= kPageListX && rx <= kPageListX + 90 && ry >= kPageListY) {
        const int item = (ry - kPageListY) / kPageListLineH;
        if (item >= 0 && item < kPageListVisible) {
            const int index = state.scroll + item;
            if (index < kCategories[state.category].pageCount) {
                state.page = index;
                loadPage(state);
                redrawHelp(state);
            }
        }
        return true;
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
    if (uiImage.frameCount() < 4) {
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
