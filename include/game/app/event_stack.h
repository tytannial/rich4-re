#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

namespace rich4 {

class Application;

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

    void push(Handler handler, void* user);
    void pop();
    Handler top() const;
    void* topUser() const;
    int depth() const { return m_depth; }

    bool dispatch(const SDL_Event& event);

    // [RE 0x401966] sub_401966（定义见 src/app/event_stack.cpp）
    void requestExit(int result);
    bool exitRequested() const { return m_exitRequested; }
    int exitResult() const { return m_exitResult; }
    void clearExit();

private:
    struct Frame {
        Handler handler = nullptr;
        void* user = nullptr;
    };

    std::array<Frame, kMaxDepth> m_stack{};

    // [RE 0x46CAD8] nIDEvent
    // 依据: 栈顶索引; 0x4018E7 中 ++nIDEvent 压栈、--nIDEvent 出栈
    int m_depth = 0;
    bool m_exitRequested = false;
    int m_exitResult = 0;
};

// [RE 0x4018E7] runModal（定义见 src/app/event_stack.cpp）
int runModal(Application& app, EventStack::Handler handler, void* user);

} // namespace rich4
