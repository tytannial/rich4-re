#include "game/app/game_init.h"

#include "game/app/date_dialog.h"
#include "game/application.h"
#include "game/core/log.h"

#include <cstdio>
#include <cstdlib>

namespace rich4 {

bool gameInit(Application& app) {
    // [RE 0x4015D6] gameInit
    // 依据: 0x4015D6 反编译; 失败路径弹 "DirectDraw Initial Error!" / "DirectDraw SetMode Error!";
    //       由 WinMain(0x401B9C) 调用, 返回非 0 才进入主循环
    // 迁移: DirectDrawCreate + SetCooperativeLevel(FULLSCREEN|EXCLUSIVE) +
    //       SetDisplayMode(640,480,16) → SDL 窗口 + 逻辑分辨率（Application::init）

    // [RE 0x4015D6] srand(GetTickCount())
    std::srand(static_cast<unsigned>(SDL_GetTicks()));

    // [RE 0x4502FE] 打开 MKF（原版资源管理器按需解压，当前用 MkfArchive 占位）
    // 原版: 0x4015D6 中 sub_4502FE("data.mkf"/"speaking.mkf"/"panel.mkf"/"effect.mkf")
    GameState& state = app.gameState();
    const std::string& dir = app.gameDir();
    if (!state.data.load(dir + "/Data.mkf")) {
        RICH4_LOGE("gameInit: Data.mkf load failed");
        return false;
    }
    if (!state.speaking.load(dir + "/Speaking.mkf")) {
        RICH4_LOGW("gameInit: Speaking.mkf unavailable");
    }
    if (!state.panel.load(dir + "/Panel.mkf")) {
        RICH4_LOGW("gameInit: Panel.mkf unavailable");
    }
    if (!state.effect.load(dir + "/Effect.mkf")) {
        RICH4_LOGW("gameInit: Effect.mkf unavailable");
    }

    // [RE 0x411E8F] 读取 RICH4.CFG（设置区 + 28 项键位表，见 docs/formats/cfg.md）
    // 依据: 0x411E8F CFG 存在时读 16B 设置 + 56B 键位；不存在时设置区保留默认值
    //       （byte_497158=1, 497159=1, 49715A=4, 49715B=4, 49715C=1, 49715D=1）
    //       CFG 第 8..11 字节（byte_497158+8）与 dword_497160 重叠，但读取后即被
    //       尾部 dos_getdate 钳位值覆盖（0x411F21..0x411A7C），不落回 settings[8..11]
    const std::string cfgPath = dir + "/RICH4.CFG";
    if (std::FILE* fp = std::fopen(cfgPath.c_str(), "rb")) {
        std::fread(state.settings, 1, sizeof(state.settings), fp);
        std::fclose(fp);
    }
    app.input().loadConfig(cfgPath);

    // [RE 0x411E8F] loadConfig 尾部: dos_getdate → 钳位 [1998,2010] → dword_497160
    state.gameDate = clampedSystemDate();

    // [RE 0x453B55] DirectSound 初始化（22050Hz/8bit/mono 主缓冲）→ SDL 音频流
    // [RE 0x4541E3] 音效库 Effect.mkf; [RE 0x47E773/0x47E793] 音乐表;
    // [RE 0x49715A] 音乐音量 / [RE 0x49715B] 音效音量（settings[2]/[3]）
    if (!app.audio().open(22050)) {
        RICH4_LOGW("gameInit: audio unavailable (silent mode)");
    }
    app.audio().setEffectArchive(&state.effect);
    app.audio().setMusicDir(app.mediaDir() + "/Music");
    app.audio().setEffectVolume(state.settings[3]);
    app.audio().setMusicVolume(state.settings[2]);

    // [RE 0x4020FA] 光标初始化（Data.mkf[0] 光标资源，43 帧，定义见 cursor.h）
    if (auto cursorRes = state.data.read(0)) {
        if (app.cursor().init(std::move(*cursorRes))) {
            // [RE 0x4021F8] sub_4021F8(41, 1, 0)：默认箭头光标（帧 41，23x23，offset 1,1）
            app.cursor().select(41, 1, 0);
        } else {
            RICH4_LOGW("gameInit: cursor resource invalid");
        }
    } else {
        RICH4_LOGW("gameInit: cursor resource (Data.mkf[0]) unavailable");
    }

    // [PORT Win32:SetWindowsHookExA(WH_KEYBOARD)] fn(0x401010) 全局键盘钩子
    // 原版 0x4015D6 安装钩子读取 word_497168 键位表实现快捷键；
    // SDL3 无全局钩子，改由 SDL_EVENT_KEY_DOWN 分发（定义见 include/game/platform/input.h）

    // TODO(RE 0x44F935): 调色板初始化
    // TODO(RE 0x45175D): 视频/AVI 初始化
    // TODO(RE 0x454176/0x4545BA): 其他子系统初始化
    // TODO(RE 0x456F80(0x5E880)): 分配 387200 字节全局工作缓冲

    // 原版 dword_48A010[0]=0; nIDEvent=0
    app.events().clearExit();
    return true;
}

} // namespace rich4
