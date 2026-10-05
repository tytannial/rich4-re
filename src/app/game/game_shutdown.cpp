#include "game/app/game_shutdown.h"

#include "game/application.h"
#include "game/core/log.h"

namespace rich4 {

void gameShutdown(Application& app) {
    // [RE 0x401815] gameShutdown
    // 依据: 0x401815 反编译; if (!byte_46CB05) 保证幂等; 依次关闭音频/MKF/输入/调色板,
    //       卸载键盘钩子, Release 主表面/后台表面/DirectDraw 接口
    GameState& state = app.gameState();
    if (state.shutdownDone) {
        return;
    }

    // [RE 0x4543C4] 音频关闭
    app.audio().close();

    // [RE 0x450404] 依次关闭 4 个 MKF: effect/panel/speaking/data
    state = GameState{};

    // [RE 0x419228] 输入/计时清理; [RE 0x44F9B3] 调色板释放
    // [RE 0x454240] 锁释放; [RE 0x453D28] 其他清理
    // TODO(RE 0x419228/0x44F9B3/0x454240/0x453D28): 对应子系统实现后补全

    // [PORT Win32:UnhookWindowsHookEx] 键盘钩子卸载
    // 替换依据: 0x401815 UnhookWindowsHookEx(hhk)；SDL 侧无全局钩子，无需对应

    // [PORT DDraw:Release] 主表面(0x48A0DC)/后台表面(0x48A0E0)/lpDD 释放
    // 替换依据: SDL3 纹理由 Renderer 析构统一释放（Application::shutdown）

    state.shutdownDone = true; // [RE 0x46CB05] byte_46CB05 = 1
}

} // namespace rich4
