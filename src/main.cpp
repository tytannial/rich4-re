#include "game/application.h"

#include "game/core/log.h"

#include <cstdio>
#include <string>

// [RE 0x45709C] start
// 依据: 0x45709C 为 CRT 入口 thunk（jmp sub_458CED）; sub_458CED 初始化 CRT 后调用
//       WinMain(GetModuleHandleA(0), 0, lpCmdLine, 10)
// 迁移: CRT 启动 + WinMain → 标准 main(argc, argv); lpCmdLine → --game <dir> 参数
int main(int argc, char** argv) {
    std::string gameDir = "resources/game";
    std::string shotPath;
    int shotCursorX = -1;
    int shotCursorY = -1;
    bool autoClick = false;
    int click2X = -1;
    int click2Y = -1;
    int click3X = -1;
    int click3Y = -1;
    int click4X = -1;
    int click4Y = -1;
    int click5X = -1;
    int click5Y = -1;
    int click6X = -1;
    int click6Y = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == "--shot" && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (arg == "--shot-cursor" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &shotCursorX, &shotCursorY) != 2) {
                shotCursorX = -1;
                shotCursorY = -1;
            }
        } else if (arg == "--auto-click") {
            autoClick = true;
        } else if (arg == "--auto-click2" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click2X, &click2Y) != 2) {
                click2X = -1;
                click2Y = -1;
            }
        } else if (arg == "--auto-click3" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click3X, &click3Y) != 2) {
                click3X = -1;
                click3Y = -1;
            }
        } else if (arg == "--auto-click4" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click4X, &click4Y) != 2) {
                click4X = -1;
                click4Y = -1;
            }
        } else if (arg == "--auto-click5" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click5X, &click5Y) != 2) {
                click5X = -1;
                click5Y = -1;
            }
        } else if (arg == "--auto-click6" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click6X, &click6Y) != 2) {
                click6X = -1;
                click6Y = -1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: rich4 [--game <dir>] [--shot <file.bmp>] [--shot-cursor x,y] "
                        "[--auto-click] [--auto-click2 x,y]\n");
            return 0;
        }
    }

    rich4::Application app;
    if (!app.init(gameDir)) {
        RICH4_LOGE("Application init failed");
        return 1;
    }
    if (!shotPath.empty()) {
        app.setScreenshot(shotPath, 45, shotCursorX, shotCursorY, autoClick, click2X, click2Y,
                          click3X, click3Y, click4X, click4Y, click5X, click5Y, click6X, click6Y);
    }
    app.run();
    app.shutdown();
    return 0;
}
