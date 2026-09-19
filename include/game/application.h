#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "game/app/event_stack.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/platform/input.h"
#include "game/platform/renderer.h"
#include "game/platform/window.h"
#include "game/render/cursor.h"
#include "game/render/surface.h"
#include "game/render/text.h"

namespace rich4 {

// 应用外壳：对应原始 WinMain (0x401B9C) 的窗口创建与消息循环职责。
// 依据: 0x401B9C 反编译; 注册窗口类 + CreateWindowExA + sub_4015D6 初始化 +
//       PeekMessageA 主循环 + sub_401815 清理
class Application {
public:
    bool init(const std::string& gameDir);
    void run();
    void shutdown();

    // 对应原版 PeekMessageA 消息泵 + WndProc(0x4019DD) 分发
    void pumpEvents();
    // 对应原版 InvalidateRect/WM_PAINT 驱动的表面绘制
    void renderFrame();

    bool running() const { return m_running; }
    void quit() { m_running = false; }

    EventStack& events() { return m_events; }
    GameState& gameState() { return m_state; }
    Window& window() { return m_window; }
    Renderer& renderer() { return m_renderer; }
    Input& input() { return m_input; }
    Audio& audio() { return m_audio; }
    Surface& surface() { return m_surface; }
    Cursor& cursor() { return m_cursor; }
    TextRenderer& text() { return m_text; }
    const std::string& gameDir() const { return m_gameDir; }

    // 鼠标位置（640x480 逻辑坐标，对应原版全屏坐标）
    void mouseLogicalPos(int& x, int& y) const;

    // [NEW] 调试：第 25 帧合成 ESC 按下（验证弹出层关闭/主菜单无操作）
    void setAutoEsc(bool on) { m_shotEsc = on; }

    // [NEW] 调试截图：第 frame 帧后保存 BMP 并退出（用于与原版画面比对）；
    //       cursorX/Y >= 0 时先移动鼠标到逻辑坐标（验证悬停等交互状态）；
    //       autoClick 时在第 10 帧合成左键点击；click2-6 在第 20/30/35/38/41 帧再点
    void setScreenshot(const std::string& path, int frame, int cursorX = -1, int cursorY = -1,
                       bool autoClick = false, int click2X = -1, int click2Y = -1,
                       int click3X = -1, int click3Y = -1, int click4X = -1, int click4Y = -1,
                       int click5X = -1, int click5Y = -1, int click6X = -1, int click6Y = -1,
                       int click7X = -1, int click7Y = -1) {
        m_shotPath = path;
        m_shotFrame = frame;
        m_shotCursorX = cursorX;
        m_shotCursorY = cursorY;
        m_shotClick = autoClick;
        m_shotClick2X = click2X;
        m_shotClick2Y = click2Y;
        m_shotClick3X = click3X;
        m_shotClick3Y = click3Y;
        m_shotClick4X = click4X;
        m_shotClick4Y = click4Y;
        m_shotClick5X = click5X;
        m_shotClick5Y = click5Y;
        m_shotClick6X = click6X;
        m_shotClick6Y = click6Y;
        m_shotClick7X = click7X;
        m_shotClick7Y = click7Y;
    }

private:
    // 对应原版 DefWindowProcA 分支（无模态处理器时的默认处理）
    void defaultEvent(const SDL_Event& event);
    // [PORT Win32:GetCursorPos] 窗口坐标 → 640x480 逻辑坐标
    void transformMouseEvent(SDL_Event& event);

    std::string m_gameDir;
    Window m_window;
    Renderer m_renderer;
    Input m_input;
    Audio m_audio;
    EventStack m_events;
    GameState m_state;
    Surface m_surface;
    Cursor m_cursor;
    TextRenderer m_text;
    std::string m_shotPath;
    int m_shotFrame = 0;
    int m_shotCursorX = -1;
    int m_shotCursorY = -1;
    bool m_shotClick = false;
    int m_shotClick2X = -1;
    int m_shotClick2Y = -1;
    int m_shotClick3X = -1;
    int m_shotClick3Y = -1;
    int m_shotClick4X = -1;
    int m_shotClick4Y = -1;
    int m_shotClick5X = -1;
    int m_shotClick5Y = -1;
    int m_shotClick6X = -1;
    int m_shotClick6Y = -1;
    int m_shotClick7X = -1;
    int m_shotClick7Y = -1;
    float m_shotWinX = 0.0f;
    float m_shotWinY = 0.0f;
    bool m_shotEsc = false;
    int m_frameCount = 0;
    bool m_running = false;
};

} // namespace rich4
