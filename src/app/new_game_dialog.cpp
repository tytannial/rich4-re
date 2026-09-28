#include "game/app/new_game_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace rich4 {

namespace {

// 头像选择区（12 格 6x2，格 72x72）
constexpr int kAvatarCount = 12;
constexpr int kAvatarCols = 6;
constexpr int kAvatarCellSize = 72;

// 画布/面板在屏幕上的位置（原版 blitElementFullscreen(..., 4, 10) / (445, 10)）
constexpr int kAvatarCanvasX = 4;
constexpr int kAvatarCanvasY = 10;
constexpr int kPanelCanvasX = 445;
constexpr int kPanelCanvasY = 10;

// 头像区命中检测（原版 8..440, 15..159）
constexpr int kAvatarHitLeft = 8;
constexpr int kAvatarHitTop = 15;
constexpr int kAvatarHitRight = 440;
constexpr int kAvatarHitBottom = 159;

// 底部玩家头像 y（原版 sub_45663E(..., 440)）
constexpr int kPlayerSlotY = 440;

// 面板选项值 x 与 6 行文本 y（原版 sub_404504: drawText(..., 155, 228+36i, align 6)）
constexpr int kOptionValueX = 155;
constexpr int kOptionRowY[6] = {228, 264, 300, 336, 372, 408};

// 列表项行高（原版 23）
constexpr int kListItemHeight = 23;

// [RE 0x40482C] 高亮色 11141120 = 0xAA0000
constexpr uint16_t kListHighlight = rgb888To555(0xAA0000);

// [RE 0x48A3AC] g_newGameState
enum DialogPhase {
    kPhaseSelect = 0,
    kPhaseFillAi = 1,
    kPhaseEnter = 2,
};

// 玩家槽（对应原版 0x48A35C 起 12 字节/槽的交错结构: charId/frame/anim）
struct PlayerSlot {
    int charId = -1;
    bool isAi = false;
    int animFrame = 0;
    UiImage anim;
};

struct DialogState {
    Application* app = nullptr;
    GameState* game = nullptr;
    NewGameConfig* config = nullptr;
    bool restore = false; // [RE 0x48A404] dword_48A404 快速重开模式

    UiImage jumpUi;  // JUMP.MKF[8]（0=头像区 1/21=面板 2/3/4=按下态 5/6/7=列表 9=标记 10=AI图标）
    UiImage avatars; // Data.mkf[2]（12 帧 72x72 头像）

    std::vector<uint8_t> mapRaw;   // 当前地图预览原图（614400）
    std::vector<uint16_t> mapDark; // 暗化 -16 后的滚动源

    int charState[kAvatarCount] = {}; // [RE 0x4990F4] 0=未选 1=人类 2=AI
    PlayerSlot slots[4];

    int phase = kPhaseSelect; // [RE 0x48A3AC] g_newGameState
    int selectedCount = 0;    // [RE 0x48A40D] g_selectedCount
    int pressedCtrl = -1;     // [RE 0x48A40E] g_pressedCtrl
    int openList = -1;        // [RE 0x48A40F] g_openList
    int listHover = -1;       // [RE 0x48A40C] g_listHover
    int avatarHover = -1;     // [RE 0x48A410] g_avatarHover

    int scrollOffset = 0; // [RE 0x48A3C8] dword_48A3C8 地图滚动偏移（0..1279）
    int fillTick = 0;     // [RE 0x48A3CC] dword_48A3CC 状态 1 计数器
    int restoreTick = 0;  // [RE 0x48A408] dword_48A408 快速重开自动確定计数

    // 状态 2 过渡参数（[RE 0x48A3FC/48A400/48A3E4/48A3E8/48A3EC]）
    int transSpeed = 0;
    int transAccel = 0;
    int transMapX = 0;
    int transPanelX = 0;
    int transSlotX[4] = {};

    std::vector<uint16_t> grayBuf; // AI 头像灰度化临时缓冲
};

// ---- 绘制 ----

void drawPlayerSlots(DialogState& st, const int* slotX) {
    Surface& s = st.app->surface();
    const int count = st.selectedCount < 4 ? st.selectedCount : 4;
    for (int i = 0; i < count; ++i) {
        if (st.slots[i].anim.frameCount() <= 0) {
            continue;
        }
        blitSpriteFrame(s, st.slots[i].anim, st.slots[i].animFrame, slotX[i], kPlayerSlotY);
    }
}

// [RE 0x40423C] 上轮通关遗留 AI（charState=2）：原版 grayscaleImage(头像帧) + 帧9 红X
// 画入头像帧/帧0背景；重写不修改只读资源：灰度副本画到格子后叠加红X（offset 25,26 → 格子内 11,11）
void drawUsedAiAvatar(DialogState& st, int charId, int x, int y) {
    const UiFrameView& f = st.avatars.frame(charId);
    const size_t pixels = static_cast<size_t>(f.width) * f.height;
    if (st.grayBuf.size() < pixels) {
        st.grayBuf.resize(pixels);
    }
    std::memcpy(st.grayBuf.data(), f.pixels, pixels * sizeof(uint16_t));
    grayscaleImage(st.grayBuf.data(), pixels);
    UiFrameView gray;
    gray.width = f.width;
    gray.height = f.height;
    gray.pixels = st.grayBuf.data();
    blitElementOpaque(st.app->surface(), gray, x, y);
    blitElement(st.app->surface(), st.jumpUi.frame(9), x + 36, y + 36, false);
}

// [RE 0x40423C] redrawAvatarGrid
void drawAvatarGrid(DialogState& st, int baseX, int baseY) {
    Surface& s = st.app->surface();
    blitElementOpaque(s, st.jumpUi.frame(0), baseX, baseY);
    for (int i = 0; i < kAvatarCount; ++i) {
        const int cellX = baseX + kAvatarCellSize * (i % kAvatarCols) + 4;
        const int cellY = baseY + kAvatarCellSize * (i / kAvatarCols) + 5;
        if (st.charState[i] == 1) {
            // 已选（人类点击或随机补齐的 AI，外观相同：头像 + 变暗 -16）
            blitElementOpaque(s, st.avatars.frame(i), cellX, cellY);
            // [RE 0x4552E7] 已选头像变暗 -16
            scaleSurfaceChannels(s, cellX, cellY, kAvatarCellSize, kAvatarCellSize, kChannelHalf);
        } else if (st.charState[i] == 2) {
            // [RE 0x40423C] 上轮通关遗留 AI：灰度 + 红X，不可选
            drawUsedAiAvatar(st, i, cellX, cellY);
        } else if (i != st.avatarHover) {
            blitElementOpaque(s, st.avatars.frame(i), cellX, cellY);
        }
    }
    // 悬停未选角色：头像斜移 (4,-4) + 名字条（暗化 -20 + 居中文本）
    const int h = st.avatarHover;
    if (h >= 0 && h < kAvatarCount && st.charState[h] == 0) {
        const int v6 = kAvatarCellSize * (h % kAvatarCols);
        const int v7 = kAvatarCellSize * (h / kAvatarCols);
        blitElementOpaque(s, st.avatars.frame(h), baseX + v6 + 8, baseY + v7 + 1);
        const int nameY = (h >= kAvatarCols) ? (v7 + 5 - 24) : (v7 + 5 + 68);
        scaleSurfaceChannels(s, baseX + v6 + 9, baseY + nameY, 70, 20, kChannelThird);
        TextRenderer& text = st.app->text();
        text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        text.drawText(s, kCharNames[h], baseX + v6 + 44, baseY + nameY + 10, 2);
    }
}

const char* optionValueText(const DialogState& st, int row, char* buf, size_t size) {
    const NewGameConfig& c = *st.config;
    switch (row) {
        case 0:
            return kPlayerCountText[c.playerCountIndex];
        case 1:
            std::snprintf(buf, size, "%u", kStartMoney[c.startMoneyIndex]);
            return buf;
        case 2:
            return kTravelModeText[c.travelMode];
        case 3:
            return kLandPermText[c.landPermIndex];
        case 4:
            return kGameTimeText[c.gameTimeIndex];
        case 5: {
            const uint32_t total = kWinMoneyMul[c.winCondIndex] * kStartMoney[c.startMoneyIndex];
            if (total != 0) {
                std::snprintf(buf, size, "%u", total);
                return buf;
            }
            return kInfiniteText;
        }
        default:
            return "";
    }
}

// [RE 0x404504] redrawNewGamePanel
void drawPanel(DialogState& st, int baseX, int baseY) {
    Surface& s = st.app->surface();
    const UiImage& ui = st.jumpUi;
    // 背景帧 (20*mode+1)：模式 0 = 帧 1，模式 1（时空之旅）= 帧 21
    const int panelFrame = 20 * st.game->gameMode + 1;
    blitElementOpaque(s, ui.frame(panelFrame), baseX, baseY);
    // 地图选中标记（帧 8 红勾 画在 (150, kMapMarkY[map])，对应面板帧预渲染的复选框）
    blitElement(s, ui.frame(8), baseX + 150, baseY + kMapMarkY[st.config->mapIndex], false);
    TextRenderer& text = st.app->text();
    // 6 行标签（15 号白字阴影粗体, align 5）
    text.setFont(15, 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
    for (int i = 0; i < 6; ++i) {
        text.drawText(s, kPanelLabels[i], baseX + 8, baseY + kOptionRowY[i], 5);
    }
    // 6 行值（15 号黑字粗体, align 6）
    text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 1);
    for (int i = 0; i < 6; ++i) {
        char buf[64];
        text.drawText(s, optionValueText(st, i, buf, sizeof(buf)), baseX + kOptionValueX,
                      baseY + kOptionRowY[i], 6);
    }
}

// [RE 0x404E44] LButtonDown case 1/2/3-8 按下态（帧 2=確定 / 3=取消 / 4=选项按钮）
void drawPressedOverlay(DialogState& st) {
    const int ctrl = st.pressedCtrl;
    if (ctrl < 1 || ctrl > 8) {
        return;
    }
    const int frame = (ctrl <= 2) ? (ctrl + 1) : 4;
    const uint16_t* r = kCtrlRects[ctrl];
    blitElementOpaque(st.app->surface(), st.jumpUi.frame(frame), r[0], r[1]);
}

int listItemCount(int list) {
    return (list == 0 || list == 2) ? 3 : 6;
}

const char* listItemText(const DialogState& st, int list, int item, char* buf, size_t size) {
    const NewGameConfig& c = *st.config;
    switch (list) {
        case 0:
            return kPlayerCountText[item];
        case 1:
            std::snprintf(buf, size, "%u", kStartMoney[item]);
            return buf;
        case 2:
            return kTravelModeText[item];
        case 3:
            return kLandPermText[item];
        case 4:
            return kGameTimeText[item];
        case 5: {
            const uint32_t total = kWinMoneyMul[item] * kStartMoney[c.startMoneyIndex];
            if (total != 0) {
                std::snprintf(buf, size, "%u", total);
                return buf;
            }
            return kInfiniteText;
        }
        default:
            return "";
    }
}

// [RE 0x40482C] drawOptionList
void drawOptionList(DialogState& st) {
    if (st.openList < 0 || st.openList > 5) {
        return;
    }
    Surface& s = st.app->surface();
    const int list = st.openList;
    const uint16_t* r = kListRects[list];
    const int left = r[0];
    const int top = r[1];
    const int right = r[2];
    blitElementOpaque(s, st.jumpUi.frame(kListBgFrame[list]), left, top);
    TextRenderer& text = st.app->text();
    const int count = listItemCount(list);
    for (int i = 0; i < count; ++i) {
        const int itemY = top + 2 + kListItemHeight * i;
        if (i == st.listHover) {
            // [RE 0x4561BE] 高亮条 0xAA0000, 20px 高
            s.fillRect(left + 2, itemY, right - left - 3, 20, kListHighlight);
            text.setFont(15, 0xFFFFFF, 0x101010, kTextStyleBold, 1);
        } else {
            text.setFont(15, 0x101010, 0x101010, kTextStyleBold, 1);
        }
        char buf[64];
        text.drawText(s, listItemText(st, list, i, buf, sizeof(buf)), right - 2, itemY + 11, 6);
    }
}

void redrawAll(DialogState& st) {
    Surface& s = st.app->surface();
    if (!st.mapDark.empty()) {
        blitScrolledMap(s, reinterpret_cast<const uint8_t*>(st.mapDark.data()), st.scrollOffset);
    }
    int slotX[4];
    for (int i = 0; i < 4; ++i) {
        slotX[i] = static_cast<int>(kPlayerSlotX[st.config->playerCountIndex][i]);
    }
    // 状态 0/1 绘制顺序（原版 WM_TIMER）：地图 → 底部头像 → 头像区 → 面板
    drawPlayerSlots(st, slotX);
    drawAvatarGrid(st, kAvatarCanvasX, kAvatarCanvasY);
    drawPanel(st, kPanelCanvasX, kPanelCanvasY);
    drawPressedOverlay(st);
    if (st.openList >= 0) {
        drawOptionList(st);
    }
}

// [RE 0x404E44] 状态 2：地图画布左移 / 面板右移 / 头像右飞（原版 WM_PAINT 顺序）
void redrawTransition(DialogState& st) {
    Surface& s = st.app->surface();
    if (!st.mapRaw.empty()) {
        blitScrolledMap(s, st.mapRaw.data(), st.scrollOffset);
    }
    drawAvatarGrid(st, st.transMapX, kAvatarCanvasY);
    drawPanel(st, st.transPanelX, kPanelCanvasY);
    drawPlayerSlots(st, st.transSlotX);
}

// ---- 逻辑 ----

bool loadMapPreview(DialogState& st) {
    // [RE 0x406DE7] 依据: sub_450441(JUMP.MKF, mapIndex + 4*mode) → g_mapPreviewRaw;
    //   convertImageChannels(g_mapPreview, raw, 614400, -16) 暗化（滚动源）
    const int index = st.config->mapIndex + 4 * st.game->gameMode;
    auto blob = st.game->jump.read(static_cast<size_t>(index));
    if (!blob || blob->size() < 614400) {
        RICH4_LOGE("newGameDialog: Jump.mkf[%d] map preview invalid", index);
        return false;
    }
    st.mapRaw = std::move(*blob);
    const size_t pixels = 614400 / 2;
    st.mapDark.resize(pixels);
    convertImageChannels(st.mapDark.data(), reinterpret_cast<const uint16_t*>(st.mapRaw.data()),
                         pixels, kChannelHalf);
    return true;
}

bool selectCharacter(DialogState& st, int slot, int charId) {
    // [RE 0x404D0A] 依据: 加载 JUMP.MKF[g_newGameOptions[2](行进方式) + 3*charId + 9] SPR;
    //   g_playerCharId[slot]=charId, frame=0
    const int index = st.config->travelMode + 3 * charId + 9;
    auto blob = st.game->jump.read(static_cast<size_t>(index));
    if (!blob) {
        RICH4_LOGE("newGameDialog: character anim Jump.mkf[%d] unavailable", index);
        return false;
    }
    st.slots[slot].anim.load(std::move(*blob));
    st.slots[slot].charId = charId;
    st.slots[slot].animFrame = 0;
    return true;
}

void deselectCharacter(DialogState& st, int charId) {
    // [RE 0x404D82] 依据: 找到槽后清空（释放动画），后续槽前移；g_charState[charId] = 0
    bool moved = false;
    for (int i = 0; i < 4; ++i) {
        if (st.slots[i].charId == charId) {
            st.slots[i] = PlayerSlot{};
            moved = true;
        } else if (moved) {
            st.slots[i - 1] = std::move(st.slots[i]);
            st.slots[i] = PlayerSlot{};
        }
    }
    st.charState[charId] = 0;
}

void advanceSlotAnim(DialogState& st) {
    for (int i = 0; i < st.selectedCount && i < 4; ++i) {
        const int count = st.slots[i].anim.frameCount();
        if (count > 0) {
            st.slots[i].animFrame = (st.slots[i].animFrame + 1) % count;
        }
    }
}

// [RE 0x404E44] LButtonDown 列表分支（LABEL_62）：应用下拉列表选择
void applyListSelection(DialogState& st) {
    const int list = st.openList;
    if (list < 0) {
        return;
    }
    if (st.listHover < 0) {
        st.openList = -1;
        st.listHover = -1;
        redrawAll(st);
        return;
    }
    int* option = nullptr;
    switch (list) {
        case 0:
            option = &st.config->playerCountIndex;
            break;
        case 1:
            option = &st.config->startMoneyIndex;
            break;
        case 2:
            option = &st.config->travelMode;
            break;
        case 3:
            option = &st.config->landPermIndex;
            break;
        case 4:
            option = &st.config->gameTimeIndex;
            break;
        case 5:
            option = &st.config->winCondIndex;
            break;
        default:
            break;
    }
    if (option && *option != st.listHover) {
        *option = st.listHover;
        if (list == 0) {
            // 玩家数减少：移除超出槽位的角色（原版从槽 3 递减到 playerCount）
            for (int n = 3; n >= st.config->playerCount(); --n) {
                if (st.slots[n].charId >= 0) {
                    deselectCharacter(st, st.slots[n].charId);
                    --st.selectedCount;
                }
            }
        } else if (list == 2) {
            // 行进方式变化：重新加载所有已选角色动画（角色 ID 不变）
            for (int m = 0; m < 4; ++m) {
                if (st.slots[m].charId >= 0) {
                    const int charId = st.slots[m].charId;
                    selectCharacter(st, m, charId);
                }
            }
        }
    }
    st.openList = -1;
    st.listHover = -1;
    redrawAll(st);
}

// [RE 0x404E44] LButtonDown case 0：头像选中/取消
void toggleAvatar(DialogState& st) {
    const int idx = st.avatarHover;
    if (idx < 0 || idx >= kAvatarCount) {
        return;
    }
    const int cs = st.charState[idx];
    if (cs != 0 || st.selectedCount >= st.config->playerCount()) {
        if (cs != 1) {
            return; // AI 角色不可取消
        }
        deselectCharacter(st, idx);
        --st.selectedCount;
        st.app->audio().playEffect(1); // [RE 0x4542CE] unk_482322
    } else {
        if (!selectCharacter(st, st.selectedCount, idx)) {
            return;
        }
        st.charState[idx] = 1;
        ++st.selectedCount;
        st.app->audio().playEffect(1); // [RE 0x4542CE] unk_482322
    }
    redrawAll(st);
}

// [RE 0x404E44] LButtonDown case 9-12：切换地图（重读预览 + 暗化）
void switchMap(DialogState& st, int mapIndex) {
    if (mapIndex == st.config->mapIndex) {
        return;
    }
    st.config->mapIndex = mapIndex;
    st.app->audio().playEffect(1); // [RE 0x4542CE] unk_482322
    loadMapPreview(st);
    redrawAll(st);
}

// [RE 0x404E44] LButtonUp：確定/取消/展开列表
void handleLeftButtonUp(DialogState& st) {
    if (st.pressedCtrl == -1) {
        return;
    }
    const int ctrl = st.pressedCtrl;
    if (ctrl == 1) {
        // 確定：已选 > 0 时进入随机补齐（状态 1）
        st.pressedCtrl = -1;
        if (st.selectedCount > 0) {
            st.phase = kPhaseFillAi;
            st.fillTick = 0;
            st.app->audio().playEffect(1); // [RE 0x4542CE] unk_482322
        }
        redrawAll(st);
        return;
    }
    if (ctrl == 2) {
        // 取消：退出模态（返回 0）
        st.pressedCtrl = -1;
        st.app->events().requestExit(0);
        return;
    }
    // 选项按钮 3-8：展开下拉列表
    st.openList = ctrl - 3;
    st.pressedCtrl = -1;
    redrawAll(st);
}

// [RE 0x404E44] 状态 1：随机补齐一个 AI 角色（charState=1，与人类相同外观；AI 标志单独记录）
void fillOneAi(DialogState& st) {
    int candidates[kAvatarCount];
    int n = 0;
    for (int i = 0; i < kAvatarCount; ++i) {
        if (st.charState[i] == 0) {
            candidates[n++] = i;
        }
    }
    if (n <= 0) {
        return;
    }
    const int pick = candidates[dbg::roll(dbg::SlotSpawn, n)];
    if (!selectCharacter(st, st.selectedCount, pick)) {
        return;
    }
    st.charState[pick] = 1; // [RE 0x404E44] 状态 1（不是 2；2 专用于上轮遗留）
    st.slots[st.selectedCount].isAi = true;
    ++st.selectedCount;
    st.app->audio().playEffect(1); // [RE 0x4542CE] unk_482322
    RICH4_LOGI("newGameDialog: fill AI slot=%d char=%d count=%d (RE 0x404E44)",
               st.selectedCount - 1, pick, st.selectedCount);
}

// [RE 0x404E44] 状态 1 完成：SetTimer(50ms) + 状态 2 + 过渡参数初始化
void beginEnterPhase(DialogState& st) {
    st.phase = kPhaseEnter;
    st.transSpeed = 6;    // [RE 0x48A3FC] dword_48A3FC = 6
    st.transAccel = 4;    // [RE 0x48A400] dword_48A400 = 4
    st.transMapX = 10;    // [RE 0x48A3E4] dword_48A3E4 = 10
    st.transPanelX = 445; // [RE 0x48A3E8] dword_48A3E8 = 445
    for (int i = 0; i < 4; ++i) {
        st.transSlotX[i] = static_cast<int>(kPlayerSlotX[st.config->playerCountIndex][i]);
    }
    st.app->events().setTimer(50);
    st.app->audio().playEffect(5); // [RE 0x4542CE] unk_46CCD0
}

void handleTimer(DialogState& st) {
    if (st.phase == kPhaseSelect) {
        // [RE 0x404E44] 状态 0 WM_TIMER：滚动 +4（1280 环绕）+ 底部头像动画帧
        st.scrollOffset += 4;
        if (st.scrollOffset >= 1280) {
            st.scrollOffset = 0;
        }
        advanceSlotAnim(st);
        if (st.restore && ++st.restoreTick == 10) {
            // 快速重开：10 tick 后模拟確定按钮抬起（原版 PostMessage WM_LBUTTONUP）
            st.pressedCtrl = 1;
            handleLeftButtonUp(st);
            return;
        }
        redrawAll(st);
        return;
    }
    if (st.phase == kPhaseFillAi) {
        st.scrollOffset += 4;
        if (st.scrollOffset >= 1280) {
            st.scrollOffset = 0;
        }
        advanceSlotAnim(st);
        if (++st.fillTick >= 10) {
            st.fillTick = 0;
            if (st.selectedCount >= st.config->playerCount()) {
                beginEnterPhase(st);
                redrawAll(st);
                return;
            }
            fillOneAi(st);
        }
        redrawAll(st);
        return;
    }
    // 状态 2（50ms）：速度/加速度推进（原版 WM_TIMER）
    st.transSpeed += 2; // dword_48A3FC += dword_48A3AC(状态值 2)
    if (st.transAccel < 30) {
        ++st.transAccel;
    }
    st.transMapX -= st.transSpeed;
    st.transPanelX += st.transSpeed;
    for (int i = 0; i < 4; ++i) {
        st.transSlotX[i] += st.transAccel;
    }
    redrawTransition(st);
    // [RE 0x404E44] 状态 2 过渡动画完成后进入游戏（原版动画结束 postModalExit(1)）
    if (st.transMapX <= -640) {
        st.app->events().requestExit(1);
    }
}

void handleSelectEvent(DialogState& st, const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: {
            if (st.restore) {
                return; // [RE 0x48A404] 快速重开忽略鼠标
            }
            const int x = static_cast<int>(event.motion.x);
            const int y = static_cast<int>(event.motion.y);
            bool dirty = false;
            if (x < kAvatarHitLeft || x >= kAvatarHitRight || y < kAvatarHitTop ||
                y >= kAvatarHitBottom) {
                if (st.avatarHover != -1) {
                    st.avatarHover = -1;
                    dirty = true;
                }
            } else {
                const int idx = (x - kAvatarHitLeft) / kAvatarCellSize +
                                kAvatarCols * ((y - kAvatarHitTop) / kAvatarCellSize);
                if (idx != st.avatarHover) {
                    st.avatarHover = idx;
                    if (st.charState[idx] == 0) {
                        st.app->audio().playEffect(0); // [RE 0x4542CE] unk_48231A
                        dirty = true;
                    }
                }
            }
            if (st.openList >= 0) {
                const uint16_t* r = kListRects[st.openList];
                if (x < r[0] || y < r[1] || x >= r[2] || y >= r[3]) {
                    if (st.listHover != -1) {
                        st.listHover = -1;
                        dirty = true;
                    }
                } else {
                    const int item = (y - r[1]) / kListItemHeight;
                    if (item != st.listHover) {
                        st.listHover = item;
                        dirty = true;
                    }
                }
            }
            if (dirty) {
                redrawAll(st);
            }
            return;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT || st.restore) {
                return;
            }
            const int x = static_cast<int>(event.button.x);
            const int y = static_cast<int>(event.button.y);
            if (st.openList >= 0) {
                applyListSelection(st);
                return;
            }
            int hit = -1;
            for (int i = 0; i < 13; ++i) {
                const uint16_t* r = kCtrlRects[i];
                if (x >= r[0] && y >= r[1] && x < r[2] && y < r[3]) {
                    hit = i;
                    break;
                }
            }
            if (hit == 0) {
                toggleAvatar(st);
            } else if (hit >= 1 && hit <= 8) {
                st.pressedCtrl = hit;
                // [RE 0x404E44] 按下音效 = sub_4542CE(&dword_48231A + 8*hit + 8, 0)：
                //   控件1(確定) → dword_48232A = Effect.mkf[2]（確定音效）
                //   控件2(取消) → dword_482332 = Effect.mkf[4]（取消音效）
                //   控件3-8(选项) → dword_48231A = Effect.mkf[0]（选项音效）
                st.app->audio().playEffect(hit == 1 ? 2 : (hit == 2 ? 4 : 0));
                redrawAll(st);
            } else if (hit >= 9) {
                switchMap(st, hit - 9);
            }
            return;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                handleLeftButtonUp(st);
                return;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
                // [RE 0x404E44] WM_RBUTTONUP(0x205): 关列表或取消退出
                if (st.restore) {
                    return;
                }
                if (st.openList == -1) {
                    st.app->events().requestExit(0);
                } else {
                    st.openList = -1;
                    st.listHover = -1;
                    redrawAll(st);
                }
                return;
            }
            return;
        }
        default:
            return;
    }
}

void handleEnterEvent(DialogState& st, const SDL_Event& event) {
    // [RE 0x404E44] 状态 2：WM_KEYDOWN / WM_LBUTTONUP / WM_RBUTTONUP → postModalExit(1)
    if (event.type == SDL_EVENT_KEY_DOWN) {
        st.app->events().requestExit(1);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
        (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
        st.app->events().requestExit(1);
    }
}

// [RE 0x404E44] newGameDialog 窗口过程（handler）
bool dialogEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<DialogState*>(user);
    if (!event) {
        // [RE 0x404E44] WM_USER+1：可用角色不足时下调玩家数 + 重置交互状态 + 100ms 定时器
        int freeChars = 0;
        for (int i = 0; i < kAvatarCount; ++i) {
            if (st.charState[i] == 0) {
                ++freeChars;
            }
        }
        if (freeChars < st.config->playerCountIndex + 1) {
            st.config->playerCountIndex = freeChars - 1;
            if (st.config->playerCountIndex < 0) {
                st.config->playerCountIndex = 0;
            }
        }
        st.selectedCount = st.restore ? 1 : 0;
        st.avatarHover = -1;
        st.openList = -1;
        st.listHover = -1;
        st.pressedCtrl = -1;
        st.app->events().setTimer(100); // SetTimer(hWnd, g_modalDepth, 100ms)
        redrawAll(st);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        handleTimer(st);
        return true;
    }
    if (st.phase == kPhaseSelect) {
        handleSelectEvent(st, *event);
    } else if (st.phase == kPhaseEnter) {
        handleEnterEvent(st, *event);
    }
    return true;
}

} // namespace

bool newGameDialog(Application& app, bool restorePrevious, NewGameConfig& config) {
    // [RE 0x404E44] 依据: 0x404E44 反编译（4773B 三段状态机）; 0x406DE7 中
    //   sub_4502FE("JUMP.MKF") + runModal(newGameDialog, lParam); 返回 1=确认
    DialogState st;
    st.app = &app;
    st.game = &app.gameState();
    st.config = &config;
    st.restore = restorePrevious;

    if (!st.game->jump.load(app.gameDir() + "/Jump.mkf")) {
        RICH4_LOGE("newGameDialog: Jump.mkf load failed");
        return false;
    }
    auto uiBlob = st.game->jump.read(8);
    if (!uiBlob || !st.jumpUi.load(std::move(*uiBlob))) {
        RICH4_LOGE("newGameDialog: Jump.mkf[8] UI unavailable");
        return false;
    }
    auto avatarBlob = st.game->data.read(2);
    if (!avatarBlob || !st.avatars.load(std::move(*avatarBlob))) {
        RICH4_LOGE("newGameDialog: Data.mkf[2] avatars unavailable");
        return false;
    }

    // [RE 0x4990F4=2] 上轮通关遗留 AI 角色（灰度 + 红X + 不可选；跨局保留）
    for (int i = 0; i < kAvatarCount; ++i) {
        st.charState[i] = config.aiUsed[i] ? 2 : 0;
    }

    // restore（原版 0x406DE7 lParam!=0）：恢复玩家 0 角色，其余槽清空等待随机补齐
    if (restorePrevious && config.charId[0] >= 0) {
        st.charState[config.charId[0]] = 1;
        selectCharacter(st, 0, config.charId[0]);
        for (int i = 1; i < 4; ++i) {
            config.charId[i] = -1;
            config.isAi[i] = false;
        }
    }

    if (!loadMapPreview(st)) {
        return false;
    }

    const int result = runModal(app, &dialogEventHandler, &st);
    if (result == 1) {
        for (int i = 0; i < 4; ++i) {
            config.charId[i] = st.slots[i].charId;
            config.isAi[i] = st.slots[i].isAi;
        }
        // [RE 0x406DE7] 结尾清理: 本轮 AI（原版状态 4）→ 2，其余 → 0（下局置灰+红X 不可选）
        for (int i = 0; i < kAvatarCount; ++i) {
            bool used = (st.charState[i] == 2);
            if (st.charState[i] == 1) {
                for (int s = 0; s < 4; ++s) {
                    if (st.slots[s].charId == i && st.slots[s].isAi) {
                        used = true;
                        break;
                    }
                }
            }
            config.aiUsed[i] = used;
        }
    }
    RICH4_LOGI("new game dialog: result=%d map=%d players=%d travel=%d (RE 0x404E44)", result,
               config.mapIndex, config.playerCount(), config.travelMode);
    return result == 1;
}

} // namespace rich4
