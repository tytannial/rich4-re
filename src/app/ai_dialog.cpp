#include "game/app/ai_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <algorithm>
#include <cstdint>

namespace rich4 {

namespace {

// [RE 0x41E345] 对话框画布（panel.mkf[77] 帧 0，435x355 背景 + 预渲染文本）blit 到 (102,62)
constexpr int kDialogX = 102;
constexpr int kDialogY = 62;
constexpr int kCanvasFrame = 0;
constexpr int kSelectFrame = 1;    // 选中高亮框
constexpr int kPlayerBoxFrame = 2; // 玩家框（未选中槽）
constexpr int kCheckFrame = 3;     // 勾选框
constexpr int kAvatarBase = 6;     // 帧 6+charIndex 头像

// 玩家槽（屏幕坐标）：命中 x∈(110,226)、y∈(70+83i, +83)
constexpr int kSlotX = 110;
constexpr int kSlotRight = 226;
constexpr int kSlotY = 70;
constexpr int kSlotStep = 83;
constexpr int kSlotH = 83;
constexpr int kPortraitX = 182; // 头像/棋子 x
constexpr int kPortraitDy = 40;
constexpr int kHostX = 121; // 托管勾选框
constexpr int kHostDy = 36;

// 选项勾选框（屏幕坐标）
constexpr int kOptionX = 288;
constexpr int kCardY = 107;
constexpr int kItemY = 139;
constexpr int kPersonaY[3] = {195, 227, 259}; // 乖寶寶/普通人/大老奸

// 比例条：背景取自画布 (208,265) 80x57 → (310,327)；10 格 7x22，步进 8
constexpr int kRatioX = 310;
constexpr int kRatioY = 327;
constexpr int kRatioSrcX = 208;
constexpr int kRatioSrcY = 265;
constexpr int kRatioW = 80;
constexpr int kRatioH = 57;
constexpr int kBarX = 311;
constexpr int kBarStep = 8;
constexpr int kCellW = 7;
constexpr int kCellH = 22;
constexpr int kCashBarY = 328;
constexpr int kStockBarY = 361;
constexpr uint32_t kBarColor = 0xFF0000;

// [RE 0x4752AE] word_4752AE 控件矩形表（屏幕坐标，ctrl 2..14；
//   word_4752BE[4*i] 即该项 i+2，命中与按下效果共用）
struct ControlRect {
    int left;
    int top;
    int right;
    int bottom;
};
constexpr ControlRect kControls[13] = {
    {287, 106, 383, 124}, // 2  使用卡片
    {287, 137, 383, 155}, // 3  使用道具
    {287, 194, 383, 212}, // 4  乖寶寶
    {287, 226, 383, 244}, // 5  普通人
    {287, 259, 383, 277}, // 6  大老奸
    {298, 328, 309, 350}, // 7  现金 -
    {392, 328, 403, 350}, // 8  现金 +
    {298, 361, 309, 383}, // 9  股票 -
    {392, 361, 403, 383}, // 10 股票 +
    {479, 159, 519, 215}, // 11 確定
    {479, 247, 519, 303}, // 12 取消
    {310, 327, 390, 351}, // 13 现金滑块轨道
    {310, 360, 390, 384}, // 14 股票滑块轨道
};

// [RE 0x48BE34] byte_48BE34..39 槽数组字段（每槽 6 字节）
enum SlotField {
    kSlotId = 0,       // 玩家 ID + 1（0 = 空槽）
    kSlotAlive = 1,    // alive 副本（bit2 = 托管）
    kSlotCardItem = 2, // 卡片/道具开关（bit0/bit1）
    kSlotPersona = 3,  // 个性（0/1/2）
    kSlotCash = 4,     // 现金比例（0-100，步进 10）
    kSlotStock = 5,    // 股票比例（0-100，步进 10）
};

struct AiDialogState {
    Application* app = nullptr;
    UiImage* ui = nullptr;
    uint8_t slots[4][6] = {}; // byte_48BE34 槽数组
    int slotCount = 0;
    int selected = 0; // dword_48BE4C 选中槽索引
    int pressed = 0;  // byte_48BE54 按下控件
    int dragControl = 0;
};

int clampInt(int v, int lo, int hi) {
    return std::min(std::max(v, lo), hi);
}

// [RE 0x41DA61] aiDialogDrawRatio（比例条：背景 + 10 格红条）
// 依据: 0x41DA61 反编译; 画布 (208,265) 80x57 → (310,327); 现金格 y=328、股票格 y=361,
//       格 x = 311+8i 7x22, 填充格数 = 比例/10（0xFF0000）
void drawRatioBars(AiDialogState& st) {
    Surface& dst = st.app->surface();
    const UiImage& ui = *st.ui;
    if (ui.frameCount() <= kCanvasFrame || st.slotCount <= 0) {
        return;
    }
    blitElementRegionOpaque(dst, ui.frame(kCanvasFrame), kRatioX, kRatioY, kRatioSrcX, kRatioSrcY,
                            kRatioW, kRatioH, false);
    const uint8_t* s = st.slots[st.selected];
    for (int i = 0; i < 10; ++i) {
        const int x = kBarX + i * kBarStep;
        if (s[kSlotCash] > i * 10) {
            dst.fillRect(x, kCashBarY, kCellW, kCellH, rgb888To555(kBarColor));
        }
        if (s[kSlotStock] > i * 10) {
            dst.fillRect(x, kStockBarY, kCellW, kCellH, rgb888To555(kBarColor));
        }
    }
}

// [RE 0x41DB91] aiDialogRedraw（整框重绘）
// 依据: 0x41DB91 反编译; blit 画布 → 玩家槽（选中：高亮框帧1 + 棋子帧0 覆盖头像帧6+charIndex；
//       托管：勾选框帧3）→ 选项勾选（卡片/道具/个性）→ sub_41DA61 比例条
void drawAiDialog(AiDialogState& st) {
    Application& app = *st.app;
    GameState& state = app.gameState();
    Surface& dst = app.surface();
    const UiImage& ui = *st.ui;
    if (ui.frameCount() <= kCanvasFrame) {
        return;
    }
    blitElementOpaque(dst, ui.frame(kCanvasFrame), kDialogX, kDialogY);
    // [RE 0x41E345] 文本（原版 drawText 预渲染到画布帧 0：20/16 号浅字深底+阴影粗体）
    TextRenderer& text = app.text();
    text.setFont(20, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    text.drawText(dst, "託管AI", kDialogX + 173, kDialogY + 26, 2);
    text.drawText(dst, "個  性", kDialogX + 173, kDialogY + 114, 2);
    text.drawText(dst, "資金運用比例", kDialogX + 249, kDialogY + 246, 2);
    text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    text.drawText(dst, "使用卡片", kDialogX + 244, kDialogY + 53, 2);
    text.drawText(dst, "使用道具", kDialogX + 244, kDialogY + 84, 2);
    text.drawText(dst, "乖寶寶", kDialogX + 244, kDialogY + 141, 2);
    text.drawText(dst, "普通人", kDialogX + 244, kDialogY + 173, 2);
    text.drawText(dst, "大老奸", kDialogX + 244, kDialogY + 206, 2);
    text.drawText(dst, "現金", kDialogX + 191, kDialogY + 277, 6);
    text.drawText(dst, "存款", kDialogX + 305, kDialogY + 277, 5);
    text.drawText(dst, "股票", kDialogX + 191, kDialogY + 310, 6);
    text.drawText(dst, "資金", kDialogX + 305, kDialogY + 310, 5);
    text.setFont(20, 0x101010, 0, kTextStyleBold, 1);
    text.drawText(dst, "確定", kDialogX + 397, kDialogY + 125, 3);
    text.drawText(dst, "取消", kDialogX + 397, kDialogY + 213, 3);
    for (int i = 0; i < st.slotCount; ++i) {
        const int y = kSlotY + i * kSlotStep;
        const uint8_t* s = st.slots[i];
        const int playerId = static_cast<int>(s[kSlotId]) - 1;
        if (playerId < 0 || playerId >= state.playerCount) {
            continue;
        }
        const int charIndex = state.players[playerId].charIndex;
        if (i == st.selected) {
            // 选中槽：高亮框 + 棋子（覆盖画布预渲染的头像；棋子按玩家索引）
            if (ui.frameCount() > kSelectFrame) {
                blitElement(dst, ui.frame(kSelectFrame), kSlotX, y, false);
            }
            if (playerId < 9 && state.pieceSprites[playerId].frameCount() > 0) {
                blitElement(dst, state.pieceSprites[playerId].frame(0), kPortraitX, y + kPortraitDy,
                            false);
            }
        } else {
            if (ui.frameCount() > kPlayerBoxFrame) {
                blitElement(dst, ui.frame(kPlayerBoxFrame), kSlotX, y, false);
            }
            if (ui.frameCount() > kAvatarBase + charIndex) {
                blitElement(dst, ui.frame(kAvatarBase + charIndex), kPortraitX, y + kPortraitDy,
                            false);
            }
        }
        // 托管勾选框（alive bit2）
        if ((s[kSlotAlive] & 4) != 0 && ui.frameCount() > kCheckFrame) {
            blitElement(dst, ui.frame(kCheckFrame), kHostX, y + kHostDy, false);
        }
    }
    if (st.slotCount > 0 && ui.frameCount() > kCheckFrame) {
        const uint8_t* s = st.slots[st.selected];
        if ((s[kSlotCardItem] & 1) != 0) {
            blitElement(dst, ui.frame(kCheckFrame), kOptionX, kCardY, false);
        }
        if ((s[kSlotCardItem] & 2) != 0) {
            blitElement(dst, ui.frame(kCheckFrame), kOptionX, kItemY, false);
        }
        const int persona = s[kSlotPersona];
        if (persona >= 0 && persona < 3) {
            blitElement(dst, ui.frame(kCheckFrame), kOptionX, kPersonaY[persona], false);
        }
    }
    drawRatioBars(st);
    // [RE 0x41DDA9] 按下效果：±按钮画布帧 4/5 到 (rect+1)；確定/取消 -12 变暗 40x56
    if (st.pressed >= 7 && st.pressed <= 10) {
        const ControlRect& r = kControls[st.pressed - 2];
        const int frame = 4 + ((st.pressed - 7) & 1);
        if (ui.frameCount() > frame) {
            blitElement(dst, ui.frame(frame), r.left + 1, r.top + 1, false);
        }
    } else if (st.pressed == 11) {
        scaleSurfaceChannels(dst, 479, 159, 40, 56, kChannelDim);
    } else if (st.pressed == 12) {
        scaleSurfaceChannels(dst, 479, 247, 40, 56, kChannelDim);
    }
}

// 玩家槽命中（0x41DDA9 WM_LBUTTONDOWN 中 x∈(110,226)、y∈(70+83i, +83)）
int hitPlayerSlot(const AiDialogState& st, int x, int y) {
    if (x <= kSlotX || x >= kSlotRight) {
        return -1;
    }
    for (int i = 0; i < st.slotCount; ++i) {
        const int y0 = kSlotY + i * kSlotStep;
        if (y > y0 && y < y0 + kSlotH) {
            return i;
        }
    }
    return -1;
}

// 控件命中（返回 ctrl 2..14；0 = 未命中）
int hitControl(int x, int y) {
    for (int i = 0; i < 13; ++i) {
        const ControlRect& r = kControls[i];
        if (x > r.left && x < r.right && y > r.top && y < r.bottom) {
            return i + 2;
        }
    }
    return 0;
}

// [RE 0x41DDA9] 滑块拖动（LABEL_18）：值 = 10 * ((x - 轨道left) / 8)
// 说明: 0x41DE44; 原版无上下限（字节回绕），重写 clamp 0..100
void dragSlider(AiDialogState& st, int control, int x) {
    const int value = clampInt(10 * ((x - kBarX) / 8), 0, 100);
    const int field = (control == 13) ? kSlotCash : kSlotStock;
    st.slots[st.selected][field] = static_cast<uint8_t>(value);
}

// [RE 0x41E25F] 確定写回（槽 → g_playerAlive/byte_496B7E/BF/B81/B82）
void applyAiConfig(AiDialogState& st) {
    GameState& state = st.app->gameState();
    for (int i = 0; i < st.slotCount; ++i) {
        const uint8_t* s = st.slots[i];
        const int id = static_cast<int>(s[kSlotId]) - 1;
        if (id < 0 || id >= state.playerCount) {
            continue;
        }
        Player& p = state.players[id];
        p.alive = s[kSlotAlive];
        p.aiCardItem = s[kSlotCardItem];
        p.aiPersonality = s[kSlotPersona];
        p.aiCashPct = s[kSlotCash];
        p.aiStockPct = s[kSlotStock];
    }
    RICH4_LOGI("ai dialog: config applied (RE 0x41E345)");
}

// [RE 0x41DDA9] WM_LBUTTONUP 动作分发（case 2..12）
// 说明: 2/3 ^=1/^=2、4/5/6 设个性、7-10 ±10（0..100）、11 写回+关闭、12 直接关闭（不写回）
void handleControlUp(AiDialogState& st, int control) {
    uint8_t* s = st.slots[st.selected];
    switch (control) {
        case 2:
            s[kSlotCardItem] ^= 1;
            break;
        case 3:
            s[kSlotCardItem] ^= 2;
            break;
        case 4:
            s[kSlotPersona] = 0;
            break;
        case 5:
            s[kSlotPersona] = 1;
            break;
        case 6:
            s[kSlotPersona] = 2;
            break;
        case 7:
            s[kSlotCash] = static_cast<uint8_t>(std::max(0, s[kSlotCash] - 10));
            break;
        case 8:
            s[kSlotCash] = static_cast<uint8_t>(std::min(100, s[kSlotCash] + 10));
            break;
        case 9:
            s[kSlotStock] = static_cast<uint8_t>(std::max(0, s[kSlotStock] - 10));
            break;
        case 10:
            s[kSlotStock] = static_cast<uint8_t>(std::min(100, s[kSlotStock] + 10));
            break;
        case 11:
            applyAiConfig(st);
            st.app->events().requestExit(0);
            return;
        case 12:
            st.app->events().requestExit(0);
            return;
        default:
            break;
    }
    drawAiDialog(st);
}

// [RE 0x41DDA9] aiDialogWndProc
// 依据: 0x41DDA9 反编译; WM_LBUTTONDOWN 玩家槽（再点选中槽 → alive^=4 托管）/控件；
//       WM_MOUSEMOVE 拖动滑块；WM_LBUTTONUP 动作；WM_RBUTTONUP/WM_KEYDOWN 取消关闭
bool aiDialogEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<AiDialogState*>(user);
    if (!event) {
        drawAiDialog(st);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        st.app->events().requestExit(0);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            st.app->events().requestExit(0);
            return true;
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        const int x = static_cast<int>(event->button.x);
        const int y = static_cast<int>(event->button.y);
        const int slot = hitPlayerSlot(st, x, y);
        if (slot >= 0) {
            if (slot == st.selected) {
                // 再次点击当前选中玩家 → 切换托管（byte_48BE35 ^= 4）
                st.slots[slot][kSlotAlive] ^= 4;
            }
            st.selected = slot;
            drawAiDialog(st);
            return true;
        }
        const int control = hitControl(x, y);
        if (control == 13 || control == 14) {
            st.dragControl = control;
            dragSlider(st, control, x);
            drawAiDialog(st);
            return true;
        }
        if (control != 0) {
            st.pressed = control;
            drawAiDialog(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (st.dragControl != 0) {
            dragSlider(st, st.dragControl, static_cast<int>(event->motion.x));
            drawAiDialog(st);
            return true;
        }
        return false;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        if (st.dragControl != 0) {
            st.dragControl = 0;
            drawAiDialog(st);
            return true;
        }
        const int control = st.pressed;
        st.pressed = 0;
        if (control != 0) {
            handleControlUp(st, control);
        }
        return true;
    }
    return false;
}

} // namespace

void aiDialog(Application& app) {
    // [RE 0x41E345] aiDialog（託管AI 配置面板）
    // 依据: 0x41E345 反编译; panel.mkf[77] 帧 0 画布 + 槽数组 byte_48BE34 只列入
    //       alive&1 的人类玩家（最多 4），默认选中当前玩家；runModal(sub_41DDA9)
    UiImage ui;
    if (auto blob = app.gameState().panel.read(77)) {
        ui.load(std::move(*blob));
    } else {
        RICH4_LOGE("aiDialog: panel.mkf[77] unavailable");
        return;
    }
    GameState& state = app.gameState();
    AiDialogState st;
    st.app = &app;
    st.ui = &ui;
    for (int i = 0; i < state.playerCount && st.slotCount < 4; ++i) {
        const Player& p = state.players[i];
        if ((p.alive & 1) == 0) {
            continue;
        }
        uint8_t* s = st.slots[st.slotCount];
        s[kSlotId] = static_cast<uint8_t>(i + 1);
        s[kSlotAlive] = p.alive;
        s[kSlotCardItem] = p.aiCardItem;
        s[kSlotPersona] = p.aiPersonality;
        s[kSlotCash] = p.aiCashPct;
        s[kSlotStock] = p.aiStockPct;
        if (i == state.currentPlayer) {
            st.selected = st.slotCount;
        }
        ++st.slotCount;
    }
    RICH4_LOGI("ai dialog (RE 0x41E345): %d human slots, selected=%d", st.slotCount, st.selected);
    runModal(app, &aiDialogEventHandler, &st);
}

} // namespace rich4
