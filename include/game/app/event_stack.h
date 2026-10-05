#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

namespace rich4 {

class Application;

// [RE SetTimer/WM_TIMER] 模态定时器事件类型
// 依据: 0x404E44 用 SetTimer 100ms/50ms 驱动选人界面动画（WM_TIMER 消息）
// 迁移: SDL3 无内置定时器事件，用 SDL_EVENT_USER 合成
constexpr uint32_t kModalTimerEvent = SDL_EVENT_USER;

// [NEW M4-H] 渲染帧事件（tick 间插值）：真实时钟下 runModal 每轮派发一次，
//   user.code = 距上次 timer 派发的毫秒数；虚拟时钟（headless）不派发 →
//   测试行为零变化。模态可据此在两次逻辑 tick 之间做"只读渲染插值"。
constexpr uint32_t kModalFrameEvent = SDL_EVENT_USER + 1;

// 模态事件处理器栈。
// [RE 0x48A010] dword_48A010
// 依据: 0x48A010 为 20 项函数指针数组; 0x4019DD 窗口过程把消息转发给
//       dword_48A010[nIDEvent]; 0x4018E7 压栈/出栈并返回 handler 的结果码
class EventStack {
public:
    // 原版 dword_48A010[20] 的数组容量
    static constexpr int kMaxDepth = 20;

    // event == nullptr 表示"模态进入/重绘"请求，对应 WM_USER+1(0x401)。
    using Handler = bool (*)(const SDL_Event* event, void* user);

    void push(Handler handler, void* user, bool centerBase = true, bool fillBars = true);
    void pop();
    Handler top() const;
    void* topUser() const;
    int depth() const { return m_depth; }
    // [NEW M4-D] 栈顶是否为"640 基准 UI"（主菜单/选人/游戏内模态；游戏内循环 push 时传 false）
    bool topCenterBase() const { return m_depth > 0 && m_stack[m_depth - 1].centerBase; }
    // [NEW M4-D 实机] 栈内是否存在游戏内循环层（centerBase=false）：
    //   fillBars 模态背景 native 重绘（renderModalBackdrop）仅当游戏世界可用时执行，
    //   主菜单/选人/新游戏等赛前模态栈内无该层（此时世界资源未就绪，不可重绘）
    bool hasGameplayLayer() const {
        for (int i = 0; i < m_depth; ++i) {
            if (!m_stack[i].centerBase) {
                return true;
            }
        }
        return false;
    }
    // [NEW M4-D] 栈顶是否铺底（全屏剧场模态 → 宽画布两侧填黑；叠加式面板 → 保留游戏画面）
    bool topFillBars() const { return m_depth > 0 && m_stack[m_depth - 1].fillBars; }

    bool dispatch(const SDL_Event& event);

    // [RE 0x401966] sub_401966（定义见 src/app/event_stack.cpp）
    void requestExit(int result);
    bool exitRequested() const { return m_exitRequested; }
    int exitResult() const { return m_exitResult; }
    void clearExit();

    // [RE SetTimer(hWnd, g_modalDepth, ms)] 模态定时器（WM_TIMER 迁移）
    // 依据: 0x404E44 用 SetTimer(hWnd, g_modalDepth, 100/50ms) 驱动选人界面动画;
    //       0x404E44 WM_TIMER 分支按 wParam == g_modalDepth 过滤
    // 迁移: Win32 定时器 → runModal 循环按 SDL_GetTicks 间隔合成 SDL_EVENT_TIMER 派发
    void setTimer(uint32_t ms) { m_timerMs = ms; }
    uint32_t timerInterval() const { return m_timerMs; }

private:
    struct Frame {
        Handler handler = nullptr;
        void* user = nullptr;
        bool centerBase = true;
        bool fillBars = true;
    };

    std::array<Frame, kMaxDepth> m_stack{};

    // [RE 0x46CAD8] nIDEvent
    // 依据: 栈顶索引; 0x4018E7 中 ++nIDEvent 压栈、--nIDEvent 出栈
    int m_depth = 0;
    bool m_exitRequested = false;
    int m_exitResult = 0;
    uint32_t m_timerMs = 0;
};

// [RE 0x4018E7] runModal（定义见 src/app/event_stack.cpp）
// tickMs > 0 时按该间隔向栈顶处理器派发 SDL_EVENT_TIMER（原版 SetTimer/WM_TIMER）
// centerBase：本层是否为"640 基准 UI"（宽画布下由 dispatchModalAware 自动水平居中）；
//   游戏内循环（enterGameLoop）传 false（地图区/右栏按宽屏布局派生绘制）
// fillBars：全屏剧场模态（银行/股市等铺底界面）→ 宽画布两侧填黑一次；
//   叠加式面板（设置/询问框/台词等不铺底）传 false（保留游戏画面），与 centerBase 正交
int runModal(Application& app, EventStack::Handler handler, void* user, uint32_t tickMs = 0,
             bool centerBase = true, bool fillBars = true);

} // namespace rich4
