#include "game/application.h"

#include "game/app/game_init.h"
#include "game/app/game_shutdown.h"
#include "game/app/main_menu.h"
#include "game/app/new_game.h"
#include "game/core/log.h"

namespace rich4 {

bool Application::init(const std::string& gameDir) {
    // [RE 0x401B9C] WinMain 初始化段
    // 依据: 0x401B9C 反编译; RegisterClassA("Rich4") + CreateWindowExA(0, "Rich4",
    //       "Rich4", 0x80000000=WS_POPUP, 屏幕宽高) + sub_4015D6() + ShowWindow(5)
    // 迁移: Win32 窗口类/全屏窗口 → SDL_CreateWindow（窗口化，现代化目标）;
    //       显示模式由 0x4015D6 的 SetDisplayMode(640,480,16) 决定 → 逻辑分辨率 640x480
    m_gameDir = gameDir;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        RICH4_LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // 窗口默认 2x 整数倍缩放（1280x960），避免非整数缩放的像素不均匀（贴图错位感）
    if (!m_window.create("Rich4", 1280, 960)) {
        return false;
    }
    if (!m_renderer.create(m_window.handle(), 640, 480)) {
        return false;
    }
    // [RE 0x48A08C] 后台绘制缓冲（640x480 RGB555）
    if (!m_surface.create(m_renderer.handle())) {
        return false;
    }
    // [RE 0x44F935] 文本渲染表面（512x200，原版 dword_4762CC）
    m_text.init();

    // [RE 0x4015D6] 游戏初始化（失败等价于原版 "DirectDraw Initial Error!" 弹窗后退出）
    if (!gameInit(*this)) {
        return false;
    }

    // TODO(RE 0x451677): START.AVI 开场动画（MCI "play vfw" 迁移待定）

    m_running = true;
    return true;
}

void Application::run() {
    // [RE 0x401B9C] WinMain 主循环（定义见 Application::init）
    // sub_4029FD() 返回按钮索引后 switch 分发:
    // 0/4 新游戏(sub_406DE7), 1 读档(sub_4190CF), 3 退出(sub_401815+DestroyWindow)
    const int scene = mainMenuScene(*this); // [RE 0x4029FD]
    if (!m_running) {
        // 原版 Msg.message == 18(WM_QUIT) 时 WinMain 直接返回，不进入场景分发
        return;
    }
    switch (scene) {
        case 0:
        case 4:
            // [RE 0x401B9C] case 0/4: sub_406DE7 → sub_401543 → sub_407AD2 → ... → sub_401981(0)
            // case 4 时 word_4991B6 = 1（新模式）
            if (!startGame(*this, scene == 4)) {
                RICH4_LOGE("startGame failed (RE 0x406DE7)");
            }
            break;
        case 1:
            // [RE 0x401B9C] case 1: 读档进入游戏（对话框内确认后经 sub_401966(1) 返回）
            if (!loadGameFromSlot(*this, m_state.pendingLoadSlot)) {
                RICH4_LOGE("loadGameFromSlot failed (RE 0x402AC5)");
            }
            break;
        case 3:
        default:
            // 原版 case 3: sub_401815(); DestroyWindow(hWnd)
            break;
    }
}

void Application::pumpEvents() {
    // [RE 0x401B9C] WinMain 中 PeekMessageA(&Msg, 0, 0, 0, PM_REMOVE) 消息泵
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 原版 Msg.message == 18(WM_QUIT) 时返回 Msg.wParam
        if (event.type == SDL_EVENT_QUIT) {
            m_running = false;
            continue;
        }
        // [PORT Win32:GetCursorPos] 鼠标事件坐标转换（定义见 transformMouseEvent）
        transformMouseEvent(event);
        // [RE 0x4019DD] wndProc
        // 依据: 0x4019DD 窗口过程; WM_DESTROY(2) 走 sub_401815+PostQuitMessage,
        // 默认分支转发给 dword_48A010[nIDEvent]（模态栈顶），无处理器时 DefWindowProcA
        if (!m_events.dispatch(event)) {
            defaultEvent(event);
        }
        // [PORT Win32:SetWindowsHookExA(WH_KEYBOARD)] fn(0x401010)（定义见 include/game/platform/input.h）
        // 原版全局键盘钩子先于窗口过程处理按键（读 word_497168 键位表）;
        // SDL 侧直接对所有事件做键位映射
        m_input.handleEvent(event);
    }
}

void Application::mouseLogicalPos(int& x, int& y) const {
    float wx = 0.0f;
    float wy = 0.0f;
    SDL_GetMouseState(&wx, &wy);
    float lx = wx;
    float ly = wy;
    SDL_RenderCoordinatesFromWindow(m_renderer.handle(), wx, wy, &lx, &ly);
    x = static_cast<int>(lx);
    y = static_cast<int>(ly);
}

void Application::transformMouseEvent(SDL_Event& event) {
    // [PORT Win32:GetCursorPos] 替换依据: 原版全屏 640x480，鼠标屏幕坐标即游戏坐标；
    // SDL 窗口可缩放 + letterbox 逻辑呈现，需把窗口坐标换算为 640x480 逻辑坐标
    float lx = 0.0f;
    float ly = 0.0f;
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (SDL_RenderCoordinatesFromWindow(m_renderer.handle(), event.motion.x, event.motion.y,
                                                &lx, &ly)) {
                event.motion.x = lx;
                event.motion.y = ly;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (SDL_RenderCoordinatesFromWindow(m_renderer.handle(), event.button.x, event.button.y,
                                                &lx, &ly)) {
                event.button.x = lx;
                event.button.y = ly;
            }
            break;
        default:
            break;
    }
}

void Application::defaultEvent(const SDL_Event& event) {
    // [RE 0x4019DD] 无模态处理器时调用 DefWindowProcA(hWnd, Msg, wParam, lParam)
    // 原版内层循环还在消息泵空转时检查场景切换标志（byte_46CB01/byte_46CAF8/byte_46CAF9），
    // 当前骨架阶段无游戏内场景，仅保留退出键。
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        m_running = false;
    }
}

void Application::renderFrame() {
    // [RE 0x401B9C] 原版无主动渲染：由 InvalidateRect + WM_PAINT 驱动
    // 0x40257A WM_PAINT: sub_40235D(移除光标) + BltFast(后台→主表面) + sub_402250(重绘光标)
    // 迁移: 每帧上传 Surface 纹理; 光标按原版语义在绘制前移除、上传前重绘
    m_renderer.beginFrame();

    // [NEW] 调试：把鼠标移到指定逻辑坐标（截图验证悬停状态用）
    if (m_shotCursorX >= 0 && m_frameCount == 1) {
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotCursorX),
                                          static_cast<float>(m_shotCursorY), &m_shotWinX,
                                          &m_shotWinY)) {
            SDL_WarpMouseInWindow(m_window.handle(), m_shotWinX, m_shotWinY);
        }
    }
    // [NEW] 调试：第 10 帧合成一次左键点击（窗口坐标，事件管线会转为逻辑坐标）
    if (m_shotClick && m_frameCount == 10) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.clicks = 1;
        ev.button.x = m_shotWinX;
        ev.button.y = m_shotWinY;
        SDL_PushEvent(&ev);
        ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
        SDL_PushEvent(&ev);
    }
    // [NEW] 调试：第 20 帧移动到第二个逻辑坐标并点击（先 warp 产生 MOUSE_MOTION 更新悬停）
    if (m_shotClick2X >= 0 && m_frameCount == 20) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick2X),
                                          static_cast<float>(m_shotClick2Y), &wx, &wy)) {
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = wx;
            ev.button.y = wy;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            SDL_PushEvent(&ev);
        }
    }
    // [NEW] 调试：第 30 帧移动到第三个逻辑坐标并点击
    if (m_shotClick3X >= 0 && m_frameCount == 30) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick3X),
                                          static_cast<float>(m_shotClick3Y), &wx, &wy)) {
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = wx;
            ev.button.y = wy;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            SDL_PushEvent(&ev);
        }
    }
    // [NEW] 调试：第 35 帧移动到第四个逻辑坐标并点击
    if (m_shotClick4X >= 0 && m_frameCount == 35) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick4X),
                                          static_cast<float>(m_shotClick4Y), &wx, &wy)) {
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = wx;
            ev.button.y = wy;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            SDL_PushEvent(&ev);
        }
    }
    // [NEW] 调试：第 38 帧移动到第五个逻辑坐标并点击
    if (m_shotClick5X >= 0 && m_frameCount == 38) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick5X),
                                          static_cast<float>(m_shotClick5Y), &wx, &wy)) {
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = wx;
            ev.button.y = wy;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            SDL_PushEvent(&ev);
        }
    }

    // [RE 0x401F98] 光标定时器（原版 fptc 20ms）：动画推进 + 位置跟踪
    int mx = 0;
    int my = 0;
    mouseLogicalPos(mx, my);
    m_cursor.update(m_surface, mx, my);
    if (!m_cursor.visible()) {
        // [RE 0x402250] sub_402250：绘制后重绘光标
        m_cursor.compose(m_surface, mx, my);
    }
    m_surface.present(m_renderer.handle());

    // [NEW] 调试截图（含光标），供与原版画面比对
    ++m_frameCount;
    if (!m_shotPath.empty() && m_frameCount >= m_shotFrame) {
        m_surface.saveBmp(m_shotPath.c_str());
        m_shotPath.clear();
        m_running = false;
    }

    // [RE 0x40235D] sub_40235D：绘制前移除光标，保持 Surface 为干净内容
    m_cursor.uncompose(m_surface);

    m_renderer.present();
}

void Application::shutdown() {
    // [RE 0x401815] gameShutdown（幂等）
    gameShutdown(*this);
    m_cursor.shutdown();
    m_text.shutdown();
    m_surface.destroy();
    m_renderer.destroy();
    m_window.destroy();
    SDL_Quit();
}

} // namespace rich4
