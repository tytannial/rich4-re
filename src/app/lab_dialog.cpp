#include "game/app/lab_dialog.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/app/map_tables.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x44101D / 0x4402D7] 图标条几何：panel[11] 帧 10..14，5 格步进 76。
//   blitElementToCanvas(canvas, panel11+12*10+132?, ...) → 图标帧 = 10+i（= id+1，id=researchItem+8）。
//   canvas 落场景 (20,280)；图标 blit 实参 (68+76i, 324)，panel[11] 帧头 offset 落到命中/描边起点。
constexpr int kLabIconFrame0 = 10;      // 機器工人（id 9 → 帧 id+1）
constexpr int kLabIconX0 = 68;          // blit 实参 X（帧头 offset 后 ≈ 35+76i）
constexpr int kLabIconY = 324;          // blit 实参 Y（帧头 offset 后 ≈ 294）
constexpr int kLabIconStep = 76;
constexpr int kLabIconCount = 5;
// [RE 0x4402D7] 命中区 x∈[35,407] y∈[297,351]，idx=(x-35)/76
constexpr int kLabHitX0 = 35;
constexpr int kLabHitX1 = 407;
constexpr int kLabHitY0 = 297;
constexpr int kLabHitY1 = 351;
// [RE 0x4402D7] 双描边 drawRectBorder(32+76i,294,0x47,59) / (33,295,0x45,57)
constexpr int kLabBorderX0 = 32;
constexpr int kLabBorderY = 294;
// [RE 0x46CAEC] 悬停边框色 RGB555 绿 0x0280
constexpr uint16_t kLabHoverColor = 0x0280;
// [RE 0x4402D7] researchLeft 写入恒 =5；item id = researchItem+8
constexpr int kLabResearchDays = 5;

struct LabState {
    Application* app = nullptr;
    UiImage* sheet = nullptr;  // panel[11]
    int sub = 0;               // 可选数 = corp.sub（等级）
    int hover = -1;
};

// [RE 0x44101D] 底图/图标/变暗 + [RE 0x441161] 标题框/标题 + [RE 0x4402D7] 描边/名
void redrawLab(LabState& st) {
    Surface& dst = st.app->surface();
    GameState& g = st.app->gameState();
    // [RE 0x441082] blitBackground(canvas, g_tipFrame+96=帧8) 铺面板底，canvas 落场景 (20,280)
    const UiImage& tip = g.estateTiles;  // [RE 0x48BAD8] g_tipFrame = data.mkf[517]
    if (tip.frameCount() > 8) {
        blitElement(dst, tip.frame(8), 20, 280, false);
    }
    // 5 道具图标（panel[11] 帧 10+i）
    if (st.sheet) {
        for (int i = 0; i < kLabIconCount; ++i) {
            blitElement(dst, st.sheet->frame(kLabIconFrame0 + i), kLabIconX0 + kLabIconStep * i,
                        kLabIconY, false);
        }
        // [RE 0x4410D3] i >= sub 未解锁 → sub_4553FE 对图标区变暗
        for (int i = st.sub; i < kLabIconCount; ++i) {
            scaleSurfaceChannels(dst, kLabBorderX0 + kLabIconStep * i + 3, kLabBorderY + 3, 65, 53,
                                 kChannelHalf);
        }
    }
    // [RE 0x441161/0x441179] 标题框 g_tipFrame+72=帧6 @ (220,140) + 标题（色键）
    if (tip.frameCount() > 6) {
        blitElement(dst, tip.frame(6), 220, 140, false);
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
}

bool labEventHandler(const SDL_Event* event, void* user) {
    auto& st = *static_cast<LabState*>(user);
    if (!event) {
        st.hover = -1;
        redrawLab(st);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hover = -1;
        if (x >= kLabHitX0 && x < kLabHitX1 && y >= kLabHitY0 && y < kLabHitY1) {
            hover = (x - kLabHitX0) / kLabIconStep;
            if (hover >= kLabIconCount) {
                hover = -1;
            }
        }
        if (hover != st.hover) {
            if (hover >= 0) {
                st.app->audio().playEffect(0);  // [RE 0x4403FA] g_uiSoundHover
            }
            st.hover = hover;
            redrawLab(st);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            // [RE 0x440640] idx < sub 才接受
            if (st.hover >= 0 && st.hover < st.sub) {
                st.app->audio().playEffect(1);  // [RE 0x4405F2] g_uiSoundClick
                st.app->events().requestExit(st.hover);
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

void labDevelopDialog(Application& app, Corp& cp) {
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
            idx = runModal(app, &labEventHandler, &st);
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
