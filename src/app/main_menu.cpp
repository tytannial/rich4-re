#include "game/app/main_menu.h"
#include "game/app/ui_layout.h"

#include "game/app/debug/debug.h"
#include "game/app/event_stack.h"
#include "game/app/load_dialog.h"
#include "game/app/settings_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x46CB28] word_46CB28 / word_46CB2A
// 依据: DGROUP 原始数据 0x46CB28: BE00 7C01 4801 7C01 D401 7A01 4801 C201 3E00 7C01
//       为 5 组 u16 (x, y)；0x4029FD 循环 i<5 用 word_46CB28[2i]/word_46CB2A[2i] 布局
// 索引语义（由 0x40257A 点击分支与 WinMain switch 推断）:
//   0 新游戏 / 1 读档 / 2 设置(不退出) / 3 退出 / 4 新游戏(模式 1)
constexpr int kMenuButtonCount = 5;
constexpr int kMenuButtonX[kMenuButtonCount] = {190, 328, 468, 328, 62};
constexpr int kMenuButtonY[kMenuButtonCount] = {380, 380, 378, 450, 380};

// 帧布局（Data.mkf[1] SMP，11 帧）:
//   帧 0 = 640x480 背景画布; 帧 (1,2)/(3,4)/(5,6)/(7,8)/(9,10) = 按钮普通/高亮
// 依据: 0x4029FD 用 12*(2i+1) 布局普通态, 0x40257A 悬停用 12*(2i+2) 高亮态
constexpr int kMenuFrameNormal = 1;
constexpr int kMenuFrameHover = 2;

struct MainMenuState {
    Application* app = nullptr;
    UiImage* image = nullptr;

    // [RE 0x48A184] dword_48A184
    // 依据: 0x40257A 中鼠标移动时记录的悬停按钮索引，-1 表示无
    int hoverIndex = -1;
};

// 重绘菜单（背景 + 5 按钮 + 版本号）。
// 对应 0x4029FD 初始布局（sub_4562A5 把按钮画到画布 + sub_44FABC 绘制 "V3.11"）与
//      0x40257A 悬停高亮（sub_456418 画高亮帧 / sub_45643D 从画布恢复）
void redrawMenu(MainMenuState& state) {
    if (!state.image || state.image->frameCount() < kMenuFrameNormal + 2 * kMenuButtonCount) {
        return;
    }
    Surface& surface = state.app->surface();
    blitElementOpaque(surface, state.image->frame(0), 0, 0);
    for (int i = 0; i < kMenuButtonCount; ++i) {
        const int frameIndex =
            (i == state.hoverIndex) ? (kMenuFrameHover + 2 * i) : (kMenuFrameNormal + 2 * i);
        blitElement(surface, state.image->frame(frameIndex), kMenuButtonX[i], kMenuButtonY[i],
                    false);
    }
    // [RE 0x4029FD] sub_44F9D8(16, 0xF0F0F0, 0x101010, 3, 1) + sub_44FABC("V3.11", 638, 470, 6)
    // 原版: 版本号文本（0x4630D0）绘制在 (638,470)，样式 3 = 阴影+粗体，对齐 6
    TextRenderer& text = state.app->text();
    text.setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    text.drawText(surface, "V3.11", 638, 470, 6);
}

// 按钮命中检测（原版 0x40257A 用高亮帧 offset/尺寸计算矩形）
bool hitTestButton(const UiFrameView& frame, int index, int x, int y) {
    const int left = kMenuButtonX[index] - frame.offsetX;
    const int top = kMenuButtonY[index] - frame.offsetY;
    return x >= left && x < left + frame.width && y >= top && y < top + frame.height;
}

// [RE 0x40257A] mainMenuWndProc
// 依据: 0x40257A 反编译; 处理 WM_PAINT(15)/WM_MOUSEMOVE(0x200)/WM_LBUTTONDOWN(0x201)/
//       WM_USER+1(1025); 点击 case 0/3/4 经 sub_401966 回传索引退出模态,
//       case 1 打开读档对话框(sub_403D74), case 2 打开设置(sub_411B53) 且不退出
bool mainMenuEventHandler(const SDL_Event* event, void* user) {
    auto& state = *static_cast<MainMenuState*>(user);

    if (!event) {
        // [RE 0x40257A] WM_USER+1(1025): dword_48A184 = -1 + 重绘
        state.hoverIndex = -1;
        redrawMenu(state);
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // [RE 0x40257A] WM_MOUSEMOVE: 遍历 5 个按钮矩形做命中检测
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int hit = -1;
        if (state.image) {
            for (int i = 0; i < kMenuButtonCount; ++i) {
                if (hitTestButton(state.image->frame(kMenuFrameHover + 2 * i), i, x, y)) {
                    hit = i;
                    break;
                }
            }
        }
        if (hit != state.hoverIndex) {
            // [RE 0x40257A] 悬停变化: 重绘旧按钮(sub_45643D 恢复) + 高亮新按钮(sub_456418)
            state.hoverIndex = hit;
            if (hit >= 0) {
                state.app->audio().playEffect(0); // [RE 0x402892] unk_48231A 悬停音效
            }
            redrawMenu(state);
        }
        return true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x40257A] WM_LBUTTONDOWN: switch (dword_48A184)
        switch (state.hoverIndex) {
            case 0:
            case 3:
            case 4:
                // sub_402460(0) 恢复绘制 + sub_401966(索引) 退出模态并回传
                state.app->audio().playEffect(1); // [RE 0x40262B] unk_482322 点击音效
                state.app->events().requestExit(state.hoverIndex);
                return true;
            case 1: {
                // [RE 0x40257A] case 1: sub_402460(0) + sub_403D74(0) 读档对话框;
                // 确认后 sub_401966(1) 退出主菜单返回 1，取消则重绘菜单
                const int slot = loadDialog(*state.app);
                if (slot != -1) {
                    state.app->gameState().pendingLoadSlot = slot;
                    state.app->events().requestExit(1);
                } else {
                    redrawMenu(state);
                }
                return true;
            }
            case 2:
                // [RE 0x40257A] case 2: sub_402460(0) + sub_411B53(0) 设置对话框;
                // 模态嵌套，关闭后恢复主菜单（不退出）
                settingsDialog(*state.app, 0);
                redrawMenu(state);
                return true;
            default:
                return true;
        }
    }

    return false;
}

} // namespace

int mainMenuScene(Application& app) {
    // [RE 0x4029FD] mainMenuScene
    // 依据: 0x4029FD 反编译; sub_450441(dword_48A0E4, 1, 0, 0) 取菜单资源;
    //       5 按钮布局 word_46CB28; sub_4018E7(sub_40257A, 0) 进入模态循环返回按钮索引
    // [RE 0x4549CF] sub_4549CF(0)：主菜单场景音乐（off_47E793[0] → track10.ogg）
    app.audio().playSceneMusic(0);

    MainMenuState state;
    state.app = &app;

    UiImage menuImage;
    if (auto blob = app.gameState().data.read(1)) {
        if (menuImage.load(std::move(*blob))) {
            state.image = &menuImage;
            // [RE 0x4563F5] sub_4563F5(dword_48A08C, 画布, 0, 0)：画布 blit 到后台缓冲
            // [NEW M4-D 实机] 预绘制包 640 基准 origin：非首次进入主菜单时（如
            //   "主菜单→选人→取消→主菜单"）m_modalBaseFilled 已为 true（active 从未中断，
            //   不再填黑两侧），无 origin 的预绘制会在宽画布左侧留残影（实机"双 NEW STAGE"）
            {
                SurfaceOriginGuard og(app.surface(), uiModalBaseX(app.surface()), 0);
                redrawMenu(state);
            }
        } else {
            RICH4_LOGE("main menu: Data.mkf[1] is not SMP/SPR");
        }
    } else {
        RICH4_LOGE("main menu: Data.mkf[1] load failed");
    }

    // TODO(RE 0x44F9D8/0x44FABC): "V3.11" 版本号（638, 470, 样式 6）——阶段 2 文本渲染

    RICH4_LOGI("main menu: 5 buttons (RE 0x4029FD)");
    // [NEW] named region 一次性登记（hitTest 同源矩形；脚本 clickr menu.<i>：0新游戏/1读档/
    //   2设置/3退出/4新游戏模式1——640 基准坐标，合成输入经模态框架自动 +base）
    if (state.image) {
        for (int i = 0; i < kMenuButtonCount; ++i) {
            const UiFrameView& f = state.image->frame(kMenuFrameHover + 2 * i);
            char nm[16];
            std::snprintf(nm, sizeof(nm), "menu.%d", i);
            debug::registerRegion(nm, kMenuButtonX[i] - static_cast<int>(f.offsetX),
                                  kMenuButtonY[i] - static_cast<int>(f.offsetY), f.width, f.height);
        }
    }
    // [RE 0x4018E7] sub_4018E7(sub_40257A, 0)：进入主菜单模态循环
    const int result = runModal(app, &mainMenuEventHandler, &state);
    // [RE 0x454ACB] sub_454ACB()：关闭主菜单音乐
    app.audio().stopMusic();
    return result;
}

} // namespace rich4
