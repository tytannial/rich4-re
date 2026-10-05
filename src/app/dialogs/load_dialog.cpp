#include "game/app/load_dialog.h"

#include "game/app/event_stack.h"
#include "game/app/save_data.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/paths.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

#include <cstdio>

namespace rich4 {

namespace {

constexpr int kSlotCount = 6;      // SAVE0.DAT .. SAVE5.DAT
constexpr int kSlotHeight = 72;    // 槽位间距（原版 72*i）
constexpr int kSlotFirstY = 24;    // 首槽 y
constexpr int kSlotFrame = 10;     // 槽位元素帧（原版 12*10+12）
constexpr int kHighlightX = 127;   // 高亮框 x（原版 v7 = 127）
constexpr int kHighlightW = 452;   // 原版 452
constexpr int kHighlightH = 76;    // 原版 76

// [RE 0x45620F] 高亮色 16776960 = 0xFFFF00（RGB888 → RGB555）
constexpr uint16_t kHighlightColor = rgb888To555(0xFFFF00);

struct LoadDialogState {
    Application* app = nullptr;
    UiImage* uiImage = nullptr;     // data.mkf[520] 读档界面
    UiImage* avatarImage = nullptr; // data.mkf[2] 角色头像（12 帧 72x72）
    SaveSlot slots[kSlotCount];

    // [RE 0x48A34A] dword_48A34A 悬停槽索引（-1 无）
    int hoverIndex = -1;
};

// 重绘对话框（背景 + 有效存档槽 + 悬停高亮）。
// 对应 0x403D74 的绘制段与 0x40363A 的悬停高亮（sub_45620F）
void redrawLoadDialog(LoadDialogState& state) {
    Surface& surface = state.app->surface();
    if (!state.uiImage) {
        return;
    }
    const UiImage& ui = *state.uiImage;

    // [RE 0x403D74] 背景：sub_456418(dword_48A08C, 资源+12, 40, 15)（帧 0 在 40,15）
    blitElement(surface, ui.frame(0), 40, 15, false);

    for (int i = 0; i < kSlotCount; ++i) {
        const SaveSlot& slot = state.slots[i];
        if (!slot.valid) {
            continue;
        }
        const int y = kSlotHeight * i + kSlotFirstY;

        // 槽位元素（帧 10 在 129, y）
        if (kSlotFrame < ui.frameCount()) {
            blitElement(surface, ui.frame(kSlotFrame), 129, y, false);
        }

        // 日期文本（原版 setTextFont(16, 0xF0F0F0, 0x101010, 3, 1)）
        TextRenderer& text = state.app->text();
        text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        if (i == 0) {
            // 槽 0 = AUTO（字符串 aAuto 0x4630E9）
            text.drawText(surface, "AUTO", 165, y + 15, 2);
        }
        char buf[32];
        // 年份（datetime 高字，原版 sub_457D61 十进制转换）
        std::snprintf(buf, sizeof(buf), "%u", slot.datetime >> 16);
        text.drawText(surface, buf, 165, kSlotHeight * i + 60, 2);
        // 月/日（原版 "%d/%d"，BYTE1 / BYTE0）
        std::snprintf(buf, sizeof(buf), "%u/%u", (slot.datetime >> 8) & 0xFFu,
                      slot.datetime & 0xFFu);
        text.drawText(surface, buf, 165, kSlotHeight * i + 81, 2);

        // 地图图标：帧 (4*mode + map + 2) 在 (209, y)
        const int mapFrame = 4 * slot.gameMode + slot.mapIndex + 2;
        if (mapFrame < ui.frameCount()) {
            blitElement(surface, ui.frame(mapFrame), 209, y, false);
        }

        // 玩家头像：帧 = 角色 ID（byte_48A19B[104*i]），在 (289 + 72*k, y)
        if (state.avatarImage) {
            int x = 289;
            for (uint32_t k = 0; k < slot.playerCount && k < 4; ++k) {
                const int role = slot.roles[k];
                if (role < state.avatarImage->frameCount()) {
                    blitElement(surface, state.avatarImage->frame(role), x, y, false);
                }
                x += 72;
            }
        }
    }

    // 悬停高亮：黄色边框（原版 sub_45620F 两次绘制加粗）
    if (state.hoverIndex >= 0 && state.hoverIndex < kSlotCount &&
        state.slots[state.hoverIndex].valid) {
        const int y = kSlotHeight * state.hoverIndex + 22;
        drawRectBorder(surface, kHighlightX, y, kHighlightW, kHighlightH, kHighlightColor);
    }
}

// [RE 0x40363A] loadDialogWndProc
// 依据: 0x40363A 反编译; WM_MOUSEMOVE 列表区域 (0x82..0x240, 0x19..0x1C7) 计算槽索引
//       v4 = (y-24)/72 并画高亮; WM_LBUTTONDOWN 有效槽 postModalExit(slot);
//       WM_RBUTTONUP(0x205) postModalExit(-1); WM_USER+1 重置高亮
bool loadDialogEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<LoadDialogState*>(user);

    if (!event) {
        // [RE 0x40363A] WM_USER+1(1025): dword_48A34A = -1 + setPauseDraw(1) + 重绘
        state.hoverIndex = -1;
        redrawLoadDialog(state);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hit = -1;
        if (x > 0x81 && x < 0x241 && y > 0x18 && y < 0x1C8) {
            const int index = (y - 24) / kSlotHeight;
            if (index >= 0 && index < kSlotCount && state.slots[index].valid) {
                hit = index;
            }
        }
        if (hit != state.hoverIndex) {
            state.hoverIndex = hit;
            state.app->audio().playEffect(0); // [RE 0x403722] unk_48231A 悬停音效
            redrawLoadDialog(state);
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x40363A] WM_LBUTTONDOWN(0x201): 有效存档 → postModalExit(槽索引)
        if (state.hoverIndex >= 0 && state.slots[state.hoverIndex].valid) {
            state.app->audio().playEffect(2); // [RE 0x403909] unk_48232A 选槽音效
            state.app->events().requestExit(state.hoverIndex);
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        // [RE 0x40363A] WM_RBUTTONUP(0x205): 取消（postModalExit(-1)）
        state.app->audio().playEffect(4); // [RE 0x403936] unk_482332 取消音效
        state.app->events().requestExit(-1);
        return true;
    }

    return false;
}

} // namespace

int loadDialog(Application& app) {
    // [RE 0x403D74] loadDialog
    LoadDialogState state;
    state.app = &app;

    UiImage uiImage;
    if (auto blob = app.gameState().data.read(520)) {
        uiImage.load(std::move(*blob));
    } else {
        RICH4_LOGE("loadDialog: Data.mkf[520] unavailable");
        return -1;
    }
    UiImage avatarImage;
    if (auto blob = app.gameState().data.read(2)) {
        avatarImage.load(std::move(*blob));
    }
    state.uiImage = &uiImage;
    state.avatarImage = &avatarImage;

    bool anyValid = false;
    for (int i = 0; i < kSlotCount; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "SAVE%d.DAT", i);
        if (readSaveHeader(readableDataFile(app.gameDir(), name), state.slots[i])) {
            anyValid = true;
        }
    }
    if (!anyValid) {
        RICH4_LOGW("loadDialog: no valid save files");
        return -1;
    }

    // [NEW M4-D 实机 2026-10-05] 删除模态外预绘制：此前无 640 基准 origin，宽画布下
    //   在左侧留未平移残影；runModal 进入时 handler(nullptr) 会以正确 origin 重绘
    RICH4_LOGI("load dialog: %d slots scanned (RE 0x403D74)", kSlotCount);
    // [NEW] named region：槽 0..5（x(129,577) y=24+72i 與 MOTION hit 同源）
    {
        static bool s_regLd = false;
        if (!s_regLd) {
            s_regLd = true;
            for (int i = 0; i < kSlotCount; ++i) {
                char nm[14];
                std::snprintf(nm, sizeof(nm), "ld.slot.%d", i);
                debug::registerRegion(nm, 130, kSlotFirstY + kSlotHeight * i, 446, kSlotHeight);
            }
        }
    }
    // [RE 0x4018E7] runModal(sub_40363A)：返回槽索引或 -1
    // [NEW M4-D 实机] 两側保留游戏画面（同大地图处理，不填黑）
    return runModal(app, &loadDialogEventHandler, &state, 0, true, false);
}

} // namespace rich4
