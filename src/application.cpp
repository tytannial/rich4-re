#include "game/application.h"

#include "game/app/game_init.h"
#include "game/app/game_shutdown.h"
#include "game/app/main_menu.h"
#include "game/app/new_game.h"
#include "game/core/log.h"

#include <filesystem>

namespace rich4 {

bool Application::init(const std::string& gameDir) {
    // [RE 0x401B9C] WinMain 初始化段
    // 依据: 0x401B9C 反编译; RegisterClassA("Rich4") + CreateWindowExA(0, "Rich4",
    //       "Rich4", 0x80000000=WS_POPUP, 屏幕宽高) + sub_4015D6() + ShowWindow(5)
    // 迁移: Win32 窗口类/全屏窗口 → SDL_CreateWindow（窗口化，现代化目标）;
    //       显示模式由 0x4015D6 的 SetDisplayMode(640,480,16) 决定 → 逻辑分辨率 640x480
    m_gameDir = gameDir;
    // [NEW] 媒体目录（音乐/视频）：优先 <gameDir>/Media，回退 <gameDir>/../Media
    m_mediaDir = gameDir + "/Media";
    if (!std::filesystem::exists(m_mediaDir + "/Music")) {
        m_mediaDir = gameDir + "/../Media";
    }

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
    // 主循环语义: LABEL_5 每次进主菜单前重置 word_4991B6/4991B8/byte_46CAFC;
    //       sub_4029FD 返回按钮索引后 switch 分发:
    //       0/4 新游戏（sub_406DE7 取消返回 0 → continue 回主菜单）, 1 读档,
    //       3 退出（sub_401815 + DestroyWindow）
    while (m_running) {
        // [RE 0x4991B6/0x4991B8/0x46CAFC] LABEL_5 重置
        m_state.gameMode = 0;
        m_state.mapIndex = 0;
        m_state.gameInited = false;
        const int scene = mainMenuScene(*this); // [RE 0x4029FD]
        if (!m_running) {
            // 原版 Msg.message == 18(WM_QUIT) 时 WinMain 直接返回
            return;
        }
        switch (scene) {
            case 0:
            case 4:
                // [RE 0x401B9C] case 0/4: sub_406DE7 → ... → sub_401981(0)
                // 取消（newGameInit 返回 0）时 continue 回主菜单（原版 if (!newGameInit) continue）
                // case 4 时 word_4991B6 = 1（时空之旅）
                if (!startGame(*this, scene == 4)) {
                    RICH4_LOGI("startGame cancelled, back to main menu (RE 0x406DE7)");
                }
                continue; // 游戏内循环返回后回主菜单
            case 1:
                // [RE 0x401B9C] case 1: 读档进入游戏（对话框内确认后经 sub_401966(1) 返回）
                if (!loadGameFromSlot(*this, m_state.pendingLoadSlot)) {
                    RICH4_LOGI("loadGameFromSlot failed, back to main menu (RE 0x402AC5)");
                }
                continue;
            case 3:
            default:
                // 原版 case 3: sub_401815(); DestroyWindow(hWnd)
                return;
        }
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
        // [PORT Win32:WM_LBUTTONUP] 模态 UI 点击标志（转盘 sub_43F7C6 提前停止；
        //   原版检测 514/517/257，重写用按下以便即时反馈）
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            m_uiClicked = true;
        }
        // [PORT Win32:SetWindowsHookExA(WH_KEYBOARD)] 原版全局键盘钩子 fn(0x401010)
        // **先于**窗口过程无条件运行（钩子 return 0 不吞消息，故两者都处理原始按键）;
        // 迁移: 每事件先执行钩子动作（含取消键抬起→合成右键），再交给窗口过程
        // （定义见 Application::handleKeyboardHook）
        handleKeyboardHook(event);
        // [RE 0x4019DD] wndProc
        // 依据: 0x4019DD 窗口过程; WM_DESTROY(2) 走 sub_401815+PostQuitMessage,
        // 默认分支转发给 dword_48A010[nIDEvent]（模态栈顶），无处理器时 DefWindowProcA
        const bool consumed = m_events.dispatch(event);
        if (!consumed) {
            defaultEvent(event);
        }
        // [RE 0x401010] 钩子累积 word_46CB07 键位状态（定义见 handleKeyboardHook 依赖）
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
    // 键盘钩子行为见 handleKeyboardHook（0x401010）
    (void)event;
}

void Application::handleKeyboardHook(const SDL_Event& event) {
    // [RE 0x401010] 全局键盘钩子 fn：在窗口过程之外直接动作
    // 依据: fn(0x401010) 读 word_497168 键位表：
    //   word_497168/6A/6C/6E（游標上/右/下/左移）→ SetCursorPos(±10)
    //   word_497170（確定執行）→ PostMessageA(WM_LBUTTONDOWN/UP)（word_46CB09 去重）
    //   word_497172（取消指令）→ 抬起时 PostMessageA(WM_RBUTTONUP)（合成右键抬起，模态按右键机制处理）
    // 迁移: SetCursorPos/PostMessageA → SDL_WarpMouseInWindow/SDL_PushEvent（逻辑像素 ±10）
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
        return;
    }
    const uint8_t vk = sdlKeycodeToVk(event.key.key);
    if (vk == 0) {
        return;
    }
    const uint16_t* bind = m_input.bindings();
    const bool down = event.type == SDL_EVENT_KEY_DOWN;

    if (down) {
        // 索引 0-3：游標上/右/下/左移（原版钩子以单键 wParam 比较，忽略修饰键）
        int dx = 0;
        int dy = 0;
        if (vk == (bind[0] & 0xFF)) {
            dy = -10;
        } else if (vk == (bind[1] & 0xFF)) {
            dx = 10;
        } else if (vk == (bind[2] & 0xFF)) {
            dy = 10;
        } else if (vk == (bind[3] & 0xFF)) {
            dx = -10;
        }
        if (dx != 0 || dy != 0) {
            RICH4_LOGI("keyboard hook: cursor move (%d,%d) (RE 0x401010)", dx, dy);
            float wx = 0.0f;
            float wy = 0.0f;
            SDL_GetMouseState(&wx, &wy);
            float lx = wx;
            float ly = wy;
            SDL_RenderCoordinatesFromWindow(m_renderer.handle(), wx, wy, &lx, &ly);
            lx += static_cast<float>(dx);
            ly += static_cast<float>(dy);
            if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), lx, ly, &wx, &wy)) {
                SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            }
            return;
        }
    }
    // 索引 4：確定執行 → 模拟左键（按下/抬起配对）
    if (vk == (bind[4] & 0xFF)) {
        if (down && !m_hookLeftDown) {
            m_hookLeftDown = true;
            RICH4_LOGI("keyboard hook: confirm click (RE 0x401010)");
            pushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
        } else if (!down && m_hookLeftDown) {
            m_hookLeftDown = false;
            pushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
        }
        return;
    }
    // 索引 5：取消指令 → 抬起时合成右键抬起（原版 lParam<0 → PostMessage WM_RBUTTONUP）
    if (!down && vk == (bind[5] & 0xFF)) {
        RICH4_LOGI("keyboard hook: cancel (RE 0x401010)");
        pushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT);
    }
}

void Application::pushMouseButton(uint32_t type, uint8_t button) {
    // [RE 0x401010] PostMessageA(hWnd, WM_LBUTTONDOWN/UP, ...) 迁移（定义见 handleKeyboardHook）
    // 原版在 GetCursorPos 位置合成鼠标消息；lParam = x | (y << 16)
    float wx = 0.0f;
    float wy = 0.0f;
    SDL_GetMouseState(&wx, &wy);
    SDL_Event synth{};
    synth.type = type;
    synth.button.button = button;
    synth.button.clicks = 1;
    synth.button.x = wx;
    synth.button.y = wy;
    SDL_PushEvent(&synth);
}

void Application::renderFrame() {
    // [RE 0x401B9C] 原版无主动渲染：由 InvalidateRect + WM_PAINT 驱动
    // 0x40257A WM_PAINT: sub_40235D(移除光标) + BltFast(后台→主表面) + sub_402250(重绘光标)
    // 迁移: 每帧上传 Surface 纹理; 光标按原版语义在绘制前移除、上传前重绘
    m_renderer.beginFrame();

    // [PORT WINMM:DirectSound] 音频混音喂流（每帧混合活动音效/音乐）
    m_audio.update();

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
    // [NEW] 调试：第 41 帧移动到第六个逻辑坐标并点击
    if (m_shotClick6X >= 0 && m_frameCount == 41) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick6X),
                                          static_cast<float>(m_shotClick6Y), &wx, &wy)) {
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
    // [NEW] 调试：第 43 帧移动到第七个逻辑坐标并点击
    if (m_shotClick7X >= 0 && m_frameCount == 43) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotClick7X),
                                          static_cast<float>(m_shotClick7Y), &wx, &wy)) {
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

    // [NEW] 调试：第 21 帧移动到指定逻辑坐标（只悬停不点击）
    if (m_shotHoverX >= 0 && m_frameCount == 21) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_shotHoverX),
                                          static_cast<float>(m_shotHoverY), &wx, &wy)) {
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
        }
    }

    // [NEW] 调试：第 25 帧合成 ESC 按下
    if (m_shotEsc && m_frameCount == m_shotEscFrame) {
        RICH4_LOGI("auto-esc: synthesize ESC at frame %d", m_frameCount);
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = SDLK_ESCAPE;
        ev.key.scancode = SDL_SCANCODE_ESCAPE;
        SDL_PushEvent(&ev);
    }

    // [NEW] 调试：在指定帧执行逻辑坐标拖拽（视角拖拽验证）
    if (m_gameDragX1 >= 0 && m_frameCount == m_gameDragFrame) {
        float wx1 = 0.0f;
        float wy1 = 0.0f;
        float wx2 = 0.0f;
        float wy2 = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameDragX1),
                                          static_cast<float>(m_gameDragY1), &wx1, &wy1) &&
            SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameDragX2),
                                          static_cast<float>(m_gameDragY2), &wx2, &wy2)) {
            RICH4_LOGI("game-drag frame=%d (%d,%d)->(%d,%d)", m_frameCount, m_gameDragX1,
                       m_gameDragY1, m_gameDragX2, m_gameDragY2);
            SDL_WarpMouseInWindow(m_window.handle(), wx1, wy1);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = wx1;
            ev.button.y = wy1;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.x = wx2;
            ev.motion.y = wy2;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.x = wx2;
            ev.button.y = wy2;
            SDL_PushEvent(&ev);
        }
    }

    // [NEW] 调试：在指定帧点击逻辑坐标（游戏内交互验证，如小地图旋转/工具条按钮）
    if (m_gameClickX >= 0 && m_frameCount == m_gameClickFrame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameClickX),
                                          static_cast<float>(m_gameClickY), &wx, &wy)) {
            RICH4_LOGI("game-click frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameClickX, m_gameClickY, wx, wy);
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

    // [NEW] 调试：在指定帧再次点击逻辑坐标（游戏内菜单打开后再点按钮）
    if (m_gameClick2X >= 0 && m_frameCount == m_gameClick2Frame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameClick2X),
                                          static_cast<float>(m_gameClick2Y), &wx, &wy)) {
            RICH4_LOGI("game-click2 frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameClick2X, m_gameClick2Y, wx, wy);
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

    // [NEW] 调试：第三次点击逻辑坐标（确认框 YES 等）
    if (m_gameClick3X >= 0 && m_frameCount == m_gameClick3Frame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameClick3X),
                                          static_cast<float>(m_gameClick3Y), &wx, &wy)) {
            RICH4_LOGI("game-click3 frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameClick3X, m_gameClick3Y, wx, wy);
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

    // [NEW] 调试：第四次点击逻辑坐标（面板内多级交互验证，如页签→子页签）
    if (m_gameClick4X >= 0 && m_frameCount == m_gameClick4Frame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameClick4X),
                                          static_cast<float>(m_gameClick4Y), &wx, &wy)) {
            RICH4_LOGI("game-click4 frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameClick4X, m_gameClick4Y, wx, wy);
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

    // [NEW] 调试：第五次点击逻辑坐标
    if (m_gameClick5X >= 0 && m_frameCount == m_gameClick5Frame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameClick5X),
                                          static_cast<float>(m_gameClick5Y), &wx, &wy)) {
            RICH4_LOGI("game-click5 frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameClick5X, m_gameClick5Y, wx, wy);
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

    // [NEW] 调试：在指定帧合成 ESC 按下+抬起（游戏内取消操作验证）
    if (m_gameEscFrame >= 0 && m_frameCount == m_gameEscFrame) {
        RICH4_LOGI("game-esc: synthesize ESC at frame %d", m_frameCount);
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = SDLK_ESCAPE;
        ev.key.scancode = SDL_SCANCODE_ESCAPE;
        SDL_PushEvent(&ev);
        ev.type = SDL_EVENT_KEY_UP;
        SDL_PushEvent(&ev);
    }

    // [NEW] 调试：在指定帧合成右键按下+抬起（小地图右键回玩家视角验证）
    if (m_gameRClickX >= 0 && m_frameCount == m_gameRClickFrame) {
        float wx = 0.0f;
        float wy = 0.0f;
        if (SDL_RenderCoordinatesToWindow(m_renderer.handle(), static_cast<float>(m_gameRClickX),
                                          static_cast<float>(m_gameRClickY), &wx, &wy)) {
            RICH4_LOGI("game-rclick frame=%d logical=(%d,%d) window=(%.0f,%.0f)", m_frameCount,
                       m_gameRClickX, m_gameRClickY, wx, wy);
            SDL_WarpMouseInWindow(m_window.handle(), wx, wy);
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = SDL_BUTTON_RIGHT;
            ev.button.clicks = 1;
            ev.button.x = wx;
            ev.button.y = wy;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            SDL_PushEvent(&ev);
        }
    }

    // [NEW] 调试：第 35 帧合成按键按下+抬起（--auto-key，验证热键捕获；支持 "ctrl+x"）
    if (!m_shotKey.empty() && m_frameCount == 35) {
        std::string name = m_shotKey;
        const bool ctrl = name.rfind("ctrl+", 0) == 0;
        if (ctrl) {
            name = name.substr(5);
        }
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key != SDLK_UNKNOWN) {
            SDL_Event ev{};
            if (ctrl) {
                ev.type = SDL_EVENT_KEY_DOWN;
                ev.key.key = SDLK_LCTRL;
                ev.key.scancode = SDL_SCANCODE_LCTRL;
                SDL_PushEvent(&ev);
            }
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.key = key;
            ev.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_KEY_UP;
            SDL_PushEvent(&ev);
            if (ctrl) {
                ev.type = SDL_EVENT_KEY_UP;
                ev.key.key = SDLK_LCTRL;
                ev.key.scancode = SDL_SCANCODE_LCTRL;
                SDL_PushEvent(&ev);
            }
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
