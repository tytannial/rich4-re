#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <utility>
#include <vector>

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
    bool init(const std::string& gameDir, bool headless = false);
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
    // [NEW] headless 测试运行标志（docs/testing.md；dummy 驱动+虚拟时钟+输入直投）
    bool headless() const { return m_headless; }
    // [NEW] 媒体目录（音乐/视频）：<gameDir>/Media，回退 <gameDir>/../Media
    const std::string& mediaDir() const { return m_mediaDir; }

    // 鼠标位置（640x480 逻辑坐标，对应原版全屏坐标）
    void mouseLogicalPos(int& x, int& y) const;
    // [NEW] headless 合成输入的鼠标覆盖：调试命令 move/click 直投时设定逻辑坐标，
    //   mouseLogicalPos 优先返回它；真实鼠标事件到达（pumpEvents）即清除
    void setMouseOverride(int x, int y) {
        m_mouseOvX = x;
        m_mouseOvY = y;
        m_mouseOv = true;
    }

    // [PORT] 模态 UI 点击（原版 WM_LBUTTONUP 消息）：
    //   转盘 sub_43F7C6 等阻塞 UI 用点击提前结束/加速；每次消费后清除
    bool consumeUiClick() {
        const bool clicked = m_uiClicked;
        m_uiClicked = false;
        return clicked;
    }
    // [NEW] 合成点击（debug move/click 直投）也置位 uiClicked（与真实按下语义一致）
    void setUiClicked() {
        m_uiClicked = true;
        m_skipInput = true;
    }

    // [RE 0x4528B9/0x4544F6/0x45144F] 演出打断（跳过加速）标志：
    // 依据: 原版 sub_4528B9/sub_4544F6 延时消息泵与 flcPlay（g_flcInterruptible=
    //       flcOpen flags&2，0x48C880）检测 WM_LBUTTONUP(514)/WM_MBUTTONDOWN(517)/
    //       WM_KEYUP(257) 即提前结束等待并吞掉该消息（PM_REMOVE 不派发窗口过程）;
    // 迁移: 左键按下 或 Esc/Enter/Space 按下置位，showMessage/playLine/playEventFlc
    //       等演出循环 consumeSkipInput() 轮询消费（一次性，不穿透下层逻辑）
    bool consumeSkipInput() {
        const bool v = m_skipInput;
        m_skipInput = false;
        return v;
    }
    void setSkipInput() { m_skipInput = true; }

    // [NEW] 调试：第 25 帧合成 ESC 按下（验证弹出层关闭/主菜单无操作）
    void setAutoEsc(bool on, int frame = 25) {
        m_shotEsc = on;
        m_shotEscFrame = frame;
    }
    // [NEW] 调试：第 35 帧合成按键（SDL_GetKeyFromName 名称，如 "F5"），验证热键捕获
    void setAutoKey(const std::string& name) { m_shotKey = name; }
    // [NEW] 调试：第 21 帧移动鼠标到逻辑坐标（只悬停不点击）
    void setHoverPos(int x, int y) {
        m_shotHoverX = x;
        m_shotHoverY = y;
    }

    // [NEW] 调试：在指定帧点击逻辑坐标（游戏内交互验证，如小地图旋转/工具条按钮）
    void setGameClick(int x, int y, int frame) {
        m_gameClickX = x;
        m_gameClickY = y;
        m_gameClickFrame = frame;
    }

    // [NEW] 调试：在指定帧执行逻辑坐标拖拽 DOWN(x1,y1)→MOTION(x2,y2)→UP（视角拖拽验证）
    void setGameDrag(int x1, int y1, int x2, int y2, int frame) {
        m_gameDragX1 = x1;
        m_gameDragY1 = y1;
        m_gameDragX2 = x2;
        m_gameDragY2 = y2;
        m_gameDragFrame = frame;
    }

    // [NEW] 调试：在指定帧合成右键按下+抬起（小地图右键回到玩家视角验证）
    void setGameRClick(int x, int y, int frame) {
        m_gameRClickX = x;
        m_gameRClickY = y;
        m_gameRClickFrame = frame;
    }

    // [NEW] 调试：在指定帧再次点击逻辑坐标（如游戏内菜单打开后再点按钮）
    void setGameClick2(int x, int y, int frame) {
        m_gameClick2X = x;
        m_gameClick2Y = y;
        m_gameClick2Frame = frame;
    }

    // [NEW] 调试：第三次点击逻辑坐标（确认框 YES 等）
    void setGameClick3(int x, int y, int frame) {
        m_gameClick3X = x;
        m_gameClick3Y = y;
        m_gameClick3Frame = frame;
    }

    // [NEW] 调试：第四次点击逻辑坐标（面板内多级交互验证，如页签→子页签）
    void setGameClick4(int x, int y, int frame) {
        m_gameClick4X = x;
        m_gameClick4Y = y;
        m_gameClick4Frame = frame;
    }

    // [NEW] 调试：第五次点击逻辑坐标
    void setGameClick5(int x, int y, int frame) {
        m_gameClick5X = x;
        m_gameClick5Y = y;
        m_gameClick5Frame = frame;
    }

    // [NEW] 调试：在指定帧合成按键（spec 支持 "ctrl+5" / "ctrl+shift+i"，验证任意帧热键）
    void addGameKey(const std::string& spec, int frame) {
        m_gameKeys.emplace_back(spec, frame);
    }

    // [NEW] 调试：在指定帧合成 ESC（游戏内系统菜单验证）
    void setGameEsc(int frame) { m_gameEscFrame = frame; }

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
    // [RE 0x401010] 全局键盘钩子行为：方向键 SetCursorPos、確定模拟左键、取消模拟右键
    void handleKeyboardHook(const SDL_Event& event);
    // [RE 0x401010] 在当前鼠标位置合成鼠标事件（PostMessageA 迁移）
    void pushMouseButton(uint32_t type, uint8_t button);

    std::string m_gameDir;
    std::string m_mediaDir;
    bool m_headless = false;
    Window m_window;
    Renderer m_renderer;
    Input m_input;
    Audio m_audio;
    EventStack m_events;
    GameState m_state;
    Surface m_surface;
    Cursor m_cursor;
    TextRenderer m_text;
    bool m_uiClicked = false;
    bool m_skipInput = false; // [RE 0x4528B9/0x4544F6/0x45144F] 演出打断标志
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
    int m_gameClickX = -1;
    int m_gameClickY = -1;
    int m_gameClickFrame = 0;
    int m_gameClick2X = -1;
    int m_gameClick2Y = -1;
    int m_gameClick2Frame = 0;
    int m_gameClick3X = -1;
    int m_gameClick3Y = -1;
    int m_gameClick3Frame = 0;
    int m_gameClick4X = -1;
    int m_gameClick4Y = -1;
    int m_gameClick4Frame = 0;
    int m_gameClick5X = -1;
    int m_gameClick5Y = -1;
    int m_gameClick5Frame = 0;
    std::vector<std::pair<std::string, int>> m_gameKeys; // [NEW] --game-key 任意帧按键
    int m_gameEscFrame = -1;
    int m_gameDragX1 = -1;
    int m_gameDragY1 = -1;
    int m_gameDragX2 = -1;
    int m_gameDragY2 = -1;
    int m_gameDragFrame = 0;
    int m_gameRClickX = -1;
    int m_gameRClickY = -1;
    int m_gameRClickFrame = 0;
    bool m_shotEsc = false;
    int m_shotEscFrame = 25;
    int m_shotHoverX = -1;
    int m_shotHoverY = -1;
    std::string m_shotKey;
    // [RE 0x401010] word_46CB09：確定键模拟左键按下的去重标志
    bool m_hookLeftDown = false;
    bool m_mouseOv = false;
    int m_mouseOvX = 0;
    int m_mouseOvY = 0;
    int m_frameCount = 0;
    bool m_running = false;
    // [RE 0x401B9C] WinMain 主循环 LABEL_5/LABEL_6 驱动：0=走 LABEL_5（回主菜单，清通关进度）；
    //   1=LABEL_6 通关续新游戏（不清进度，newGameInit(1) 保留配置+灰名单）；2=LABEL_6 失败读档继续
    int m_runFlow = 0;
};

} // namespace rich4
