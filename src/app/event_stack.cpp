#include "game/app/event_stack.h"

#include "game/application.h"
#include "game/core/clock.h"
#include "game/core/log.h"
#include "game/core/trace.h"

namespace rich4 {

void EventStack::push(Handler handler, void* user, bool centerBase, bool fillBars) {
    // [RE 0x48A010] 0x4018E7 中 dword_48A010[++nIDEvent] = handler
    if (m_depth >= kMaxDepth) {
        RICH4_LOGE("event stack overflow (depth=%d)", m_depth);
        return;
    }
    m_stack[m_depth++] = Frame{handler, user, centerBase, fillBars};
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

int runModal(Application& app, EventStack::Handler handler, void* user, uint32_t tickMs,
             bool centerBase, bool fillBars) {
    // [RE 0x4018E7] runModal
    // 依据: 0x4018E7 反编译; dword_48A010[++nIDEvent]=handler; PostMessage(0x401) 通知进入;
    //       局部消息泵直到 WM_USER+2(0x402); 返回 Msg.lParam; --nIDEvent
    // 迁移: Win32 局部消息泵 → SDL 事件循环（Application::pumpEvents/renderFrame）;
    //       0x401/0x402 消息 → handler(nullptr) / EventStack::requestExit;
    //       SetTimer/WM_TIMER → 循环内按 tickMs 合成 SDL_EVENT_TIMER（选人界面动画用）
    app.events().push(handler, user, centerBase, fillBars);
    app.events().clearExit();
    trace::logf("modal push h=%p tick=%u depth=%d", reinterpret_cast<const void*>(handler),
                tickMs, app.events().depth());
    // 保存外层 timer 间隔：嵌套模态（如 settingsDialog）返回后需恢复，
    // 否则外层游戏内循环（16ms tick）的定时器会被清零而停止
    const uint32_t savedTimer = app.events().timerInterval();
    app.events().setTimer(tickMs);
    if (handler) {
        // 对应 PostMessage(hWnd, 0x401, 0, lParam)：通知栈顶处理器模态进入/重绘
        // （处理器可在此时通过 setTimer 调整间隔，如选人界面状态切换 100ms→50ms）
        // [NEW M4-D] 经 dispatchModalAware：640 基准模态在宽画布下自动水平居中
        app.dispatchModalAware(nullptr);
    }
    uint64_t nextTick = nowMs() + app.events().timerInterval();
    uint64_t lastTickMs = nowMs(); // [NEW M4-H] 上次 timer 派发时刻（frame 事件 elapsed 基准）
    while (app.running() && !app.events().exitRequested()) {
        app.pumpEvents();
        app.renderFrame();
        const uint32_t interval = app.events().timerInterval();
        if (interval > 0) {
            // [NEW] 虚拟时钟（headless 测试）：每轮直接步进 interval 并派发 timer，
            //   不等墙钟（正常模式透传原逻辑，行为零变化）
            const bool step = virtualClock();
            if (step) {
                clockAdvanceMs(interval);
                nextTick = nowMs() + interval;
                lastTickMs = nowMs();
                SDL_Event timerEvent{};
                timerEvent.type = kModalTimerEvent;
                timerEvent.user.code = 0;
                // [NEW M4-D] 经 dispatchModalAware（640 基准模态宽画布居中；游戏内循环直通）
                app.dispatchModalAware(&timerEvent);
            } else {
                // [NEW M4-H] 渲染帧事件（tick 间插值）：每轮一次，code = 距上次 tick 的毫秒；
                //   虚拟时钟不派发 → headless 行为零变化。模态据此做只读渲染插值。
                {
                    SDL_Event frameEvent{};
                    frameEvent.type = kModalFrameEvent;
                    frameEvent.user.code = static_cast<int>(nowMs() - lastTickMs);
                    app.dispatchModalAware(&frameEvent);
                }
                // [NEW M4-C2 实机] tick 追补：绘制耗时超过 interval（放大窗口后每帧变慢）时，
                //   原实现 `nextTick = now + interval` 会把绘制耗时计入 tick 周期（20ms 绘制 →
                //   20ms tick → 骰子/行走/位移等按 tick 推进的动画整体拖慢）。改为按 interval
                //   网格累加 + 欠账连发补拍（上限 32/轮，足够覆盖极高分辨率绘制）。
                //   追不上时**保留欠账**（下轮继续补；仅欠账 > 250ms 才重锚防雪崩）——
                //   旧实现此处直接重置 nextTick 会把大分辨率下每轮剩余欠账永久丢弃（越放大越慢）。
                int catchUp = 0;
                uint64_t now = nowMs();
                while (now >= nextTick && catchUp < 32) {
                    nextTick += interval;
                    lastTickMs = now;
                    SDL_Event timerEvent{};
                    timerEvent.type = kModalTimerEvent;
                    timerEvent.user.code = 0;
                    app.dispatchModalAware(&timerEvent);
                    ++catchUp;
                    now = nowMs();
                }
                if (now >= nextTick && now - nextTick > 250) {
                    nextTick = now; // 长时间卡顿（欠账 >250ms）→ 重锚防雪崩
                }
            }
        }
    }
    app.events().setTimer(savedTimer);
    app.events().pop();
    const int result = app.events().exitResult();
    trace::logf("modal pop h=%p result=%d", reinterpret_cast<const void*>(handler), result);
    // 消费退出请求：原版 WM_USER+2 消息被局部消息泵取走，不会影响外层模态
    app.events().clearExit();
    return result;
}

} // namespace rich4
