#include "game/app/event_stack.h"

#include "game/application.h"
#include "game/core/log.h"

namespace rich4 {

void EventStack::push(Handler handler, void* user) {
    // [RE 0x48A010] 0x4018E7 中 dword_48A010[++nIDEvent] = handler
    if (m_depth >= kMaxDepth) {
        RICH4_LOGE("event stack overflow (depth=%d)", m_depth);
        return;
    }
    m_stack[m_depth++] = Frame{handler, user};
}

void EventStack::pop() {
    // [RE 0x46CAD8] 0x4018E7 结束时 --nIDEvent
    if (m_depth > 0) {
        --m_depth;
    }
}

EventStack::Handler EventStack::top() const {
    return m_depth > 0 ? m_stack[m_depth - 1].handler : nullptr;
}

void* EventStack::topUser() const {
    return m_depth > 0 ? m_stack[m_depth - 1].user : nullptr;
}

bool EventStack::dispatch(const SDL_Event& event) {
    // [RE 0x4019DD] 窗口过程默认分支将消息交给 dword_48A010[nIDEvent]
    if (Handler handler = top()) {
        return handler(&event, topUser());
    }
    return false;
}

void EventStack::requestExit(int result) {
    // [RE 0x401966] postModalExit
    // 依据: PostMessageA(hWnd, 0x402, 0, lParam) —— 请求退出当前模态并回传结果
    m_exitRequested = true;
    m_exitResult = result;
}

void EventStack::clearExit() {
    m_exitRequested = false;
    m_exitResult = 0;
}

int runModal(Application& app, EventStack::Handler handler, void* user) {
    // [RE 0x4018E7] runModal
    // 依据: 0x4018E7 反编译; dword_48A010[++nIDEvent]=handler; PostMessage(0x401) 通知进入;
    //       局部消息泵直到 WM_USER+2(0x402); 返回 Msg.lParam; --nIDEvent
    // 迁移: Win32 局部消息泵 → SDL 事件循环（Application::pumpEvents/renderFrame）;
    //       0x401/0x402 消息 → handler(nullptr) / EventStack::requestExit
    app.events().push(handler, user);
    app.events().clearExit();
    if (handler) {
        // 对应 PostMessage(hWnd, 0x401, 0, lParam)：通知栈顶处理器模态进入/重绘
        handler(nullptr, user);
    }
    while (app.running() && !app.events().exitRequested()) {
        app.pumpEvents();
        app.renderFrame();
    }
    app.events().pop();
    const int result = app.events().exitResult();
    // 消费退出请求：原版 WM_USER+2 消息被局部消息泵取走，不会影响外层模态
    app.events().clearExit();
    return result;
}

} // namespace rich4
