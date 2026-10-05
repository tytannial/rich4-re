#include "game/app/lab_dialog.h"

#include <SDL3/SDL.h>
#include <cstdio>

#include "game/app/event_stack.h"
#include "game/app/map_tables.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/app/debug/debug.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x44101D / 0x4402D7] 图标条几何：panel[11] 帧 10..14，5 格步进 76。
//   blitElementToCanvas(canvas, &v1[12*i+132], v3+33, 44)：v1 = panel[11] 资源基址，
//   +132 = 12+12*10 → 图标帧 = 10+i（= id+1，id=researchItem+8）；v3 = 15+76i。
//   canvas 落场景 (20,280)；图标 blit 实参 (68+76i, 324)，panel[11] 帧头 offset 落到命中/描边起点。
constexpr int kLabIconFrame0 = 10;      // 機器工人（id 9 → 帧 id+1）
constexpr int kLabIconX0 = 68;          // blit 实参 X（帧头 offset 后 ≈ 35+76i）
constexpr int kLabIconY = 324;          // blit 实参 Y（帧头 offset 后 ≈ 294）
constexpr int kLabIconStep = 76;
constexpr int kLabIconCount = 5;
// [RE 0x4402D7] 命中区 x∈[0x23,0x196]=[35,406]、y∈[0x129,0x15F]=[297,351]，idx=(x-35)/76
constexpr int kLabHitX0 = 35;
constexpr int kLabHitX1 = 406;
constexpr int kLabHitY0 = 297;
constexpr int kLabHitY1 = 351;
// [RE 0x4402D7] 双描边 drawRectBorder(32+76i,294,0x47,59) / (33,295,0x45,57)
constexpr int kLabBorderX0 = 32;
constexpr int kLabBorderY = 294;
// [RE 0x4404A8/0x4404C4 push 0FFFF00h] 悬停/按下边框色 = RGB888 黄 0xFFFF00；
//   ⚠ word_46CAEC 是 640x480 surface 描述符（首 word = 宽 640），不是颜色（旧实现误读为 0x0280）
constexpr uint16_t kLabHoverColor = rgb888To555(0xFFFF00);
// [RE 0x441082] 底图 g_tipFrame+96 = 12+12*7 → 帧7（400×89 五格木纹面板；blitBackground 不透明）
constexpr int kLabPanelFrame = 7;
// [RE 0x441161] 标题框 g_tipFrame+72 = 12+12*5 → 帧5（249×170 提示框，色键 sub_456418）
constexpr int kLabTitleFrame = 5;
// [RE 0x4410D3 → 0x4553FE] 未解锁图标区灰度矩形 (15+76i,17,66,54)（canvas 坐标 → 落屏 (35+76i,297)）；
//   变换 = funcs_4553F1[dword_47637C=0] = 0x455442 灰度 (R+G+B+16)>>2（**非**通道减半变暗）
constexpr int kLabGrayW = 66;
constexpr int kLabGrayH = 54;
// [RE 0x4405E5] 左键按下 highlightRect(35+76i,297,66,54) 下沉浮雕（1px + 顶/左亮度减半）
constexpr int kLabPressW = 66;
constexpr int kLabPressH = 54;
// [RE 0x4402D7] researchLeft 写入恒 =5；item id = researchItem+8
constexpr int kLabResearchDays = 5;

struct LabState {
    Application* app = nullptr;
    UiImage* sheet = nullptr;  // panel[11]
    int sub = 0;               // 可选数 = corp.sub（等级）
    int hover = -1;
    int pressed = -1;          // 按住中的格（下沉浮雕），-1 无
};

// [RE 0x44101D] 底图/图标/灰度 + [RE 0x441161] 标题框/标题 + [RE 0x4402D7] 描边/名
void redrawLab(LabState& st) {
    Surface& dst = st.app->surface();
    GameState& g = st.app->gameState();
    // [RE 0x441082] blitBackground(canvas, g_tipFrame+96=帧7) 不透明铺面板底，canvas 落场景 (20,280)
    const UiImage& tip = g.estateTiles;  // [RE 0x48BAD8] g_tipFrame = data.mkf[517]
    if (tip.frameCount() > kLabPanelFrame) {
        blitElementOpaque(dst, tip.frame(kLabPanelFrame), 20, 280);
    }
    // 5 道具图标（panel[11] 帧 10+i，色键 blitElementToCanvas）
    if (st.sheet) {
        for (int i = 0; i < kLabIconCount; ++i) {
            blitElement(dst, st.sheet->frame(kLabIconFrame0 + i), kLabIconX0 + kLabIconStep * i,
                        kLabIconY, false);
        }
        // [RE 0x4410D3] i >= sub 未解锁 → sub_4553FE 对该图标区逐像素灰度
        for (int i = st.sub; i < kLabIconCount; ++i) {
            const int gx = kLabBorderX0 + 3 + kLabIconStep * i;  // = 35+76i
            const int gy = kLabBorderY + 3;                     // = 297
            // [NEW M4-A2] 逻辑区域 → 设备区域（scale=1 恒等）
            const int gxDev = dst.deviceX(gx);
            const int gyDev = dst.deviceY(gy);
            const int gw = dst.spanX(gx, kLabGrayW);
            const int gh = dst.spanY(gy, kLabGrayH);
            const int avail = dst.width() - gxDev;
            const int n = gw < avail ? gw : avail;
            if (gxDev < 0 || n <= 0) {
                continue;
            }
            for (int row = 0; row < gh; ++row) {
                if (gyDev + row < 0 || gyDev + row >= dst.height()) {
                    continue;
                }
                uint16_t* line = dst.pixels() + static_cast<size_t>(gyDev + row) * dst.width() +
                                 gxDev;
                grayscaleImage(line, static_cast<size_t>(n));  // [RE 0x455442] 0 像素保持（透明）
            }
        }
    }
    // [RE 0x441161/0x441179] 标题框 g_tipFrame+72=帧5 @ (220,140) + 标题（色键）
    if (tip.frameCount() > kLabTitleFrame) {
        blitElement(dst, tip.frame(kLabTitleFrame), 220, 140, false);
    }
    st.app->text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    st.app->text().drawText(dst, "請選擇欲開發道具", 220, 122, 2);
    // [RE 0x440474/0x4404BC] 悬停描边（双框）+ 道具名（off_47FF22[2*idx] = kItemBagNames[9+idx]）
    if (st.hover >= 0 && st.hover < kLabIconCount) {
        const int bx = kLabBorderX0 + kLabIconStep * st.hover;
        drawRectBorder(dst, bx, kLabBorderY, 0x47, 59, kLabHoverColor);
        drawRectBorder(dst, bx + 1, kLabBorderY + 1, 0x45, 57, kLabHoverColor);
        st.app->text().drawText(dst, kItemBagNames[9 + st.hover], 220, 154, 2);
    }
    // [RE 0x4405E5 → 0x451B9E] 按住格下沉浮雕（仅按住期间；移动/抬起全量重绘即复原）
    if (st.pressed >= 0 && st.pressed < kLabIconCount) {
        const int bx = kLabHitX0 + kLabIconStep * st.pressed;
        pressDown(dst, bx, kLabHitY0, kLabPressW, kLabPressH, 1, kChannelHalf);
    }
}

// [RE 0x4402D7 WM_MOUSEMOVE] 命中（x∈[35,406] y∈[297,351]）→ idx，否则 -1
int hitLabSlot(int x, int y) {
    if (x < kLabHitX0 || x > kLabHitX1 || y < kLabHitY0 || y > kLabHitY1) {
        return -1;
    }
    return (x - kLabHitX0) / kLabIconStep;
}

bool labEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<LabState*>(user);
    if (!event) {
        st.hover = -1;
        st.pressed = -1;
        redrawLab(st);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int hover =
            hitLabSlot(static_cast<int>(event->motion.x), static_cast<int>(event->motion.y));
        if (hover != st.hover) {
            if (hover >= 0) {
                st.app->audio().playEffect(0);  // [RE 0x4403FA] g_uiSoundHover
            }
            st.hover = hover;
            st.pressed = -1;  // 移动重绘，清除下沉（原版重贴 canvas 覆盖）
            redrawLab(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x4405E5] hover 有效且 idx < sub → click 音效 + highlightRect 下沉
            if (st.hover >= 0 && st.hover < st.sub) {
                st.app->audio().playEffect(1);  // [RE 0x4405F2] g_uiSoundClick
                st.pressed = st.hover;
                redrawLab(st);
            }
            return true;
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x440640] idx < sub 才接受（原版抬起无音效）
            if (st.hover >= 0 && st.hover < st.sub) {
                st.app->events().requestExit(st.hover);
            } else {
                st.pressed = -1;
                redrawLab(st);
            }
            return true;
        }
        if (event->button.button == SDL_BUTTON_RIGHT) {
            st.app->audio().playEffect(4);  // [RE 0x440670] g_uiSoundCancel
            st.app->events().requestExit(-1);
            return true;
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        st.app->audio().playEffect(4);
        st.app->events().requestExit(-1);
        return true;
    }
    return false;
}

}  // namespace

void labDevelopDialog(Application& app, Corp& cp) {    // [NEW] named region：5 研發槽（hitLabSlot 0x4402D7 同源 x[35,406]/y[297,351]）
    {
        static bool s_regLab = false;
        if (!s_regLab) {
            s_regLab = true;
            for (int i = 0; i < 5; ++i) {
                char nm[14];
                std::snprintf(nm, sizeof(nm), "lab.slot.%d", i);
                debug::registerRegion(nm, kLabHitX0 + kLabIconStep * i, kLabHitY0,
                                      kLabIconStep, kLabHitY1 - kLabHitY0);
            }
        }
    }
    trace::logf("dialog open name=lab sub=%u", cp.sub);

    GameState& g = app.gameState();
    int idx = -1;
    if (g.players[g.currentPlayer].alive == 1) {
        // [RE 0x441051] panel[11] 帧 10..14
        UiImage sheet;
        auto blob = g.panel.read(11);
        if (blob && sheet.load(std::move(*blob)) && sheet.frameCount() >= kLabIconFrame0 + kLabIconCount) {
            LabState st;
            st.app = &app;
            st.sheet = &sheet;
            st.sub = cp.sub;
            // [NEW M4-D 实机] 研究所面板叠加在地图上 → 不填黑两侧
    idx = runModal(app, &labEventHandler, &st, 0, true, false);
        } else {
            RICH4_LOGW("labDevelop: panel.mkf[11] unavailable (RE 0x44101D)");
        }
    } else {
        // [RE 0x4411EC] AI 恒选最高解锁项（不弹框）
        idx = cp.sub - 1;
    }
    if (idx >= 0) {
        // [RE 0x4411F8/0x4411FB] researchItem = idx+1，researchLeft = 5（产出 id = item+8）
        cp.researchItem = static_cast<uint8_t>(idx + 1);
        cp.researchLeft = kLabResearchDays;
        RICH4_LOGI("lab develop: corp sub=%u -> researchItem=%u(%s) left=%d (RE 0x44101D)", cp.sub,
                   cp.researchItem, kItemBagNames[9 + idx], cp.researchLeft);
    }
}

}  // namespace rich4
