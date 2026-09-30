#include "game/application.h"

#include "game/app/debug/debug.h"
#include "game/app/game_init.h"
#include "game/app/game_shutdown.h"
#include "game/app/main_menu.h"
#include "game/app/load_dialog.h"
#include "game/app/new_game.h"
#include "game/app/victory.h"
#include "game/core/log.h"

#include <chrono>
#include <cstring>
#include <filesystem>

namespace rich4 {

bool Application::init(const std::string& gameDir, bool headless, const DisplayConfig& display) {
    // [RE 0x401B9C] WinMain 初始化段
    // 依据: 0x401B9C 反编译; RegisterClassA("Rich4") + CreateWindowExA(0, "Rich4",
    //       "Rich4", 0x80000000=WS_POPUP, 屏幕宽高) + sub_4015D6() + ShowWindow(5)
    // 迁移: Win32 窗口类/全屏窗口 → SDL_CreateWindow（窗口化，现代化目标）;
    //       显示模式由 0x4015D6 的 SetDisplayMode(640,480,16) 决定 → 逻辑分辨率 640x480
    m_gameDir = gameDir;
    m_headless = headless;
    // [NEW] 媒体目录（音乐/视频）：优先 <gameDir>/Media，回退 <gameDir>/../Media
    m_mediaDir = gameDir + "/Media";
    if (!std::filesystem::exists(m_mediaDir + "/Music")) {
        m_mediaDir = gameDir + "/../Media";
    }

    // [NEW] headless 测试运行（docs/testing.md）：SDL dummy 驱动——无真实窗口/显卡/声卡；
    //   Surface 为 CPU 位图，绘制路径与 saveBmp 不依赖 GPU，VSync 关闭配合虚拟时钟极速推进；
    //   音频 open 失败走既有 silent 兜底
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        RICH4_LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // 窗口默认 2x 整数倍缩放（1280x960），避免非整数缩放的像素不均匀（贴图错位感）
    if (!m_window.create("Rich4", display.windowWidth, display.windowHeight)) {
        return false;
    }
    // [NEW M4-A1] 运行期画布尺寸：默认 native 设计尺寸；SDL 逻辑呈现同步为画布尺寸
    // [NEW M4-A2] uiScale != 1 时画布 = 设计尺寸 × uiScale（canvas 显式指定优先）；
    //   autoScale（wide/free preset）画布 = 窗口 drawable，uiScale = 画布高/480
    float drawScale = display.uiScale > 0.0f ? display.uiScale : 1.0f;
    int canvasW = display.canvasWidth;
    int canvasH = display.canvasHeight;
    m_autoScale = display.autoScale;
    if (m_autoScale) {
        int dw = 0;
        int dh = 0;
        SDL_GetWindowSizeInPixels(m_window.handle(), &dw, &dh);
        canvasW = dw > 0 ? dw : display.windowWidth;
        canvasH = dh > 0 ? dh : display.windowHeight;
        drawScale = static_cast<float>(canvasH) / Surface::kHeight;
    } else {
        if (canvasW <= 0) {
            canvasW = static_cast<int>(Surface::kWidth * drawScale + 0.5f);
        }
        if (canvasH <= 0) {
            canvasH = static_cast<int>(Surface::kHeight * drawScale + 0.5f);
        }
    }
    if (!m_renderer.create(m_window.handle(), canvasW, canvasH, !headless)) {
        return false;
    }
    // [RE 0x48A08C] 后台绘制缓冲（RGB555）
    if (!m_surface.create(m_renderer.handle(), canvasW, canvasH)) {
        return false;
    }
    // [NEW M4-A2] 绘制缩放：逻辑 640x480 坐标 → 设备像素（画布）
    m_surface.setScale(drawScale);
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
    // [RE 0x401B9C] WinMain 主循环：LABEL_5（回菜单：清通关进度 g_clearedMaps + 重置
    //   word_4991B6/4991B8/byte_46CAFC + mainMenuScene）/ LABEL_6（续局：不清进度，
    //   newGameInit(byte_46CAFC=1) 保留配置与上轮 AI 灰名单）。游戏结束按 g_sceneRequest 分派。
    GameState& st = m_state;
    while (m_running) {
        int scene;
        if (m_runFlow == 0) {
            // LABEL_5 [RE 0x401C8C..0x401CA6]：清通关进度 + 重置模式/地图/进入标志
            std::memset(st.clearedMaps, 0, sizeof(st.clearedMaps)); // [RE 0x401C9E] g_clearedMaps=0
            st.gameMode = 0;       // word_4991B6
            st.mapIndex = 0;       // word_4991B8
            st.gameInited = false; // byte_46CAFC
            if (st.debugQuickstart) {
                scene = 0; // [NEW] headless 测试开局：跳过主菜单（docs/testing.md）
                RICH4_LOGI("quickstart: bypass main menu (RE 0x4029FD)");
            } else {
                scene = mainMenuScene(*this); // [RE 0x4029FD]
            }
            if (!m_running) {
                return; // 原版 Msg.message == 18(WM_QUIT) 时 WinMain 直接返回
            }
        } else {
            // LABEL_6：通关续局(1) / 失败读档继续(2)——不清通关进度、不走主菜单
            scene = (m_runFlow == 2) ? 1 : 0;
        }
        m_runFlow = 0;

        bool started = false;
        switch (scene) {
            case 0:
            case 4:
                if (scene == 4) {
                    st.gameMode = 1; // [RE 0x401CBF] word_4991B6=1（时空之旅）
                }
                // mode1 == gameMode（newGameInit 里 restore 由 gameInited 决定，LABEL_6 保留进度）
                started = startGame(*this, st.gameMode == 1);
                if (!started) {
                    RICH4_LOGI("startGame cancelled, back to main menu (RE 0x406DE7)");
                }
                break;
            case 1:
                started = loadGameFromSlot(*this, st.pendingLoadSlot); // [RE 0x402AC5]
                if (!started) {
                    RICH4_LOGI("loadGameFromSlot failed, back to main menu (RE 0x402AC5)");
                }
                break;
            case 3:
            default:
                return; // 原版 case 3: gameShutdown + DestroyWindow
        }
        if (!started) {
            continue; // 取消 → 下轮 LABEL_5 回主菜单
        }

        // [RE 0x401D6F] 游戏结束按 sceneRequest 分派（enterGameLoop 已退出，sceneRequest 保留至今）
        if (st.quitGame) {
            return;
        }
        const int v10 = st.sceneRequest;
        st.sceneRequest = 0;
        m_audio.setSwitchDays(0); // [RE 0x401DC4] g_musicTimer=0（切歌计时清零，迁入 Audio）
        switch (v10) {
            case 2: // 通关结算 → 续新游戏（LABEL_6，不清通关进度）
                gameClearFlow(*this);
                if (st.quitGame) {
                    return;
                }
                m_runFlow = 1;
                m_enterSwitchMusic = true; // [RE 0x401D14] LABEL_6 快速入局 enterGameLoop(1)
                break;
            case 3: // 通关结算 → 回主菜单（LABEL_5）
                gameClearFlow(*this);
                break;
            case 4: { // [RE 0x403D74] 失败读档继续：弹原版 loadDialog 选档 → 载入续局；取消→主菜单
                const int lslot = loadDialog(*this);
                if (lslot >= 0) {
                    st.pendingLoadSlot = lslot;
                    m_runFlow = 2;
                    // [RE 0x40412D/0x404135] loadDialog 尾：g_musicTimer≠0 → 清0+musicStop；
                    //   musicPlayTrack(0) 顺序下一首。LABEL_6 scene1 enterGameLoop(1) 的
                    //   换曲由 loadGameFromSlot 路径同标志消费（原版两处调用都推进游标）
                    m_audio.setSwitchDays(0);
                    m_enterSwitchMusic = true;
                }
                break;
            }
            case 1:
            case 0:
            default:
                break; // → 下轮 LABEL_5 回主菜单
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
        // [NEW M4-A2] free/wide：窗口尺寸变化 → 延迟到 renderFrame 重建画布与逻辑呈现
        if (m_autoScale && (event.type == SDL_EVENT_WINDOW_RESIZED ||
                            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) {
            m_resizePending = true;
            continue;
        }
        // [RE 0x4019DD WM_ACTIVATEAPP] 窗口失焦暂停音乐、回焦恢复（对等原版 pause mid/resume）
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            m_audio.setMusicPaused(true);
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            m_audio.setMusicPaused(false);
            continue;
        }
        // [PORT Win32:GetCursorPos] 鼠标事件坐标转换（定义见 transformMouseEvent）
        transformMouseEvent(event);
        // [NEW] 真实鼠标移动清除合成输入覆盖与调试选择器（脚本 select 仅在无人动鼠标时生效）
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            m_mouseOv = false;
            debug::setSelectedObject(0);
        }
        // [PORT Win32:WM_LBUTTONUP] 模态 UI 点击标志（转盘 sub_43F7C6 提前停止；
        //   原版检测 514/517/257，重写用按下以便即时反馈）
        // [RE 0x4528B9/0x4544F6/0x45144F] 同步置演出打断标志（consumeSkipInput 消费）
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            m_uiClicked = true;
            m_skipInput = true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN &&
            (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN ||
             event.key.key == SDLK_SPACE)) {
            m_skipInput = true;
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

// [NEW M4-A2] free/wide：按窗口 drawable 重建画布与逻辑呈现（uiScale = 画布高/480）
void Application::applyWindowResize() {
    m_resizePending = false;
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(m_window.handle(), &w, &h);
    if (w <= 0 || h <= 0 || (w == m_surface.width() && h == m_surface.height())) {
        return;
    }
    // 旧画布上的光标保存块作废（按旧区域写回新画布可能越界）；区域快照的恢复有大小
    // 校验兜底（restoreRegion 尺寸不匹配直接返回）
    m_cursor.invalidate();
    m_surface.destroy();
    if (!m_surface.create(m_renderer.handle(), w, h)) {
        RICH4_LOGE("applyWindowResize: surface recreate failed (%dx%d)", w, h);
        return;
    }
    m_surface.setScale(static_cast<float>(h) / Surface::kHeight);
    m_renderer.setLogicalSize(w, h);
    RICH4_LOGI("window resize: canvas %dx%d scale=%.3f (M4-A2 free/wide)", w, h,
               static_cast<double>(m_surface.scale()));
}

void Application::mouseDevicePos(int& x, int& y) const {
    // [NEW M4-A2] 设备像素坐标（SDL 逻辑呈现 = 画布尺寸）
    if (m_mouseOv) {
        // [NEW] headless 合成输入覆盖（debug::move/click 直投；输入的为设计逻辑坐标）
        x = m_surface.logicalToDevice(m_mouseOvX);
        y = m_surface.logicalToDevice(m_mouseOvY);
        return;
    }
    float wx = 0.0f;
    float wy = 0.0f;
    SDL_GetMouseState(&wx, &wy);
    float lx = wx;
    float ly = wy;
    SDL_RenderCoordinatesFromWindow(m_renderer.handle(), wx, wy, &lx, &ly);
    x = static_cast<int>(lx);
    y = static_cast<int>(ly);
}

void Application::mouseLogicalPos(int& x, int& y) const {
    // [NEW M4-A2] 设计逻辑坐标（UI 层坐标：设备 / scale；scale=1 恒等）
    int dx = 0;
    int dy = 0;
    mouseDevicePos(dx, dy);
    x = m_surface.deviceToLogical(dx);
    y = m_surface.deviceToLogical(dy);
}

void Application::transformMouseEvent(SDL_Event& event) {
    // [PORT Win32:GetCursorPos] 替换依据: 原版全屏 640x480，鼠标屏幕坐标即游戏坐标；
    // SDL 窗口可缩放 + letterbox 逻辑呈现，需把窗口坐标换算为 640x480 逻辑坐标
    float lx = 0.0f;
    float ly = 0.0f;
    // [NEW M4-A2] 输出设计逻辑坐标（SDL 逻辑呈现 = 设备；再 /scale → 逻辑）
    const float s = m_surface.scale();
    const float inv = s > 0.0f ? 1.0f / s : 1.0f;
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (SDL_RenderCoordinatesFromWindow(m_renderer.handle(), event.motion.x, event.motion.y,
                                                &lx, &ly)) {
                event.motion.x = lx * inv;
                event.motion.y = ly * inv;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (SDL_RenderCoordinatesFromWindow(m_renderer.handle(), event.button.x, event.button.y,
                                                &lx, &ly)) {
                event.button.x = lx * inv;
                event.button.y = ly * inv;
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
            // [NEW M4-A2] dx/dy 为设计逻辑像素 → 设备步长 × scale（scale=1 恒等）
            const float s = m_surface.scale();
            const float k = s > 0.0f ? s : 1.0f;
            lx += static_cast<float>(dx) * k;
            ly += static_cast<float>(dy) * k;
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
    // [NEW M4-A2] free/wide：resize 后重建画布（须在 beginFrame 前）
    if (m_resizePending) {
        applyWindowResize();
    }
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

    // [NEW] 调试：任意帧合成按键（--game-key "ctrl+shift+i,1500"；支持 ctrl+/shift+ 修饰）
    for (const auto& [spec, frame] : m_gameKeys) {
        if (m_frameCount != frame) {
            continue;
        }
        std::string name = spec;
        SDL_Keymod mod = SDL_KMOD_NONE;
        for (;;) {
            if (name.rfind("ctrl+", 0) == 0) {
                mod |= SDL_KMOD_CTRL;
                name = name.substr(5);
                continue;
            }
            if (name.rfind("shift+", 0) == 0) {
                mod |= SDL_KMOD_SHIFT;
                name = name.substr(6);
                continue;
            }
            break;
        }
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key == SDLK_UNKNOWN) {
            RICH4_LOGW("game-key: unknown key '%s' at frame %d", spec.c_str(), frame);
            continue;
        }
        RICH4_LOGI("game-key frame=%d spec=%s", m_frameCount, spec.c_str());
        auto pushKey = [&](SDL_Keycode k, SDL_EventType type) {
            SDL_Event ev{};
            ev.type = type;
            ev.key.key = k;
            ev.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
            ev.key.mod = mod;
            SDL_PushEvent(&ev);
        };
        if ((mod & SDL_KMOD_CTRL) != 0) {
            pushKey(SDLK_LCTRL, SDL_EVENT_KEY_DOWN);
        }
        if ((mod & SDL_KMOD_SHIFT) != 0) {
            pushKey(SDLK_LSHIFT, SDL_EVENT_KEY_DOWN);
        }
        pushKey(key, SDL_EVENT_KEY_DOWN);
        pushKey(key, SDL_EVENT_KEY_UP);
        if ((mod & SDL_KMOD_SHIFT) != 0) {
            pushKey(SDLK_LSHIFT, SDL_EVENT_KEY_UP);
        }
        if ((mod & SDL_KMOD_CTRL) != 0) {
            pushKey(SDLK_LCTRL, SDL_EVENT_KEY_UP);
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
    // [NEW M4-A2] 光标按设备像素定位（A2 光标按 scale 放大绘制）
    int mx = 0;
    int my = 0;
    mouseDevicePos(mx, my);
    // [NEW] 演出段隐藏软件光标（现代化增强，原版全程自绘）：事件 FLC 阻塞播放/冻结
    //   （eventFlcActive）与跳伞入场（parachuteActive）期间不显示；此处统一驱动，
    //   与 Cursor::setHidden 的模态界面级用法（乐透开奖/月结全身像）并存
    const bool perfHidden = m_state.parachuteActive || m_state.eventFlcActive;
    if (perfHidden != m_cursor.hidden()) {
        m_cursor.setHidden(m_surface, perfHidden);
    }
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

    // [NEW M4-A2] --stats：每 120 帧输出 FPS / 平均帧耗时（定位放大后的性能瓶颈）
    if (m_stats) {
        const uint64_t now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (m_statsWindowUs == 0) {
            m_statsWindowUs = now;
        }
        if (++m_statsFrames >= 120) {
            const double msPerFrame =
                static_cast<double>(now - m_statsWindowUs) / 1000.0 / m_statsFrames;
            RICH4_LOGI("perf: %.1f FPS (%.2f ms/frame) canvas=%dx%d scale=%.2f",
                       msPerFrame > 0.0 ? 1000.0 / msPerFrame : 0.0, msPerFrame, m_surface.width(),
                       m_surface.height(), static_cast<double>(m_surface.scale()));
            m_statsFrames = 0;
            m_statsWindowUs = now;
        }
    }

    // [NEW] headless 调试脚本执行器每帧步进（docs/testing.md；无脚本时仅一次入队检查）
    debug::tick(*this);
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
