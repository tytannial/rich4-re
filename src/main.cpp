#include "game/application.h"

#include "game/core/log.h"

#include <cstdio>
#include <cstdlib>
#include <string>

// [RE 0x45709C] start
// 依据: 0x45709C 为 CRT 入口 thunk（jmp sub_458CED）; sub_458CED 初始化 CRT 后调用
//       WinMain(GetModuleHandleA(0), 0, lpCmdLine, 10)
// 迁移: CRT 启动 + WinMain → 标准 main(argc, argv); lpCmdLine → --game <dir> 参数
int main(int argc, char** argv) {
    std::string gameDir = "resources/game";
    std::string shotPath;
    int shotFrame = 47;
    int shotCursorX = -1;
    int shotCursorY = -1;
    bool autoClick = false;
    bool autoEsc = false;
    int autoEscFrame = 25;
    std::string autoKey;
    int hoverX = -1;
    int hoverY = -1;
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
    int click7X = -1;
    int click7Y = -1;
    int gameClickX = -1;
    int gameClickY = -1;
    int gameClickFrame = 0;
    int gameDragX1 = -1;
    int gameDragY1 = -1;
    int gameDragX2 = -1;
    int gameDragY2 = -1;
    int gameDragFrame = 0;
    int gameRClickX = -1;
    int gameRClickY = -1;
    int gameRClickFrame = 0;
    int gameClick2X = -1;
    int gameClick2Y = -1;
    int gameClick2Frame = 0;
    int gameClick3X = -1;
    int gameClick3Y = -1;
    int gameClick3Frame = 0;
    int gameEscFrame = -1;
    bool debugMode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--shot" && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (arg == "--shot-frame" && i + 1 < argc) {
            shotFrame = std::atoi(argv[++i]);
        } else if (arg == "--shot-cursor" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &shotCursorX, &shotCursorY) != 2) {
                shotCursorX = -1;
                shotCursorY = -1;
            }
        } else if (arg == "--auto-click") {
            autoClick = true;
        } else if (arg == "--auto-esc") {
            autoEsc = true;
        } else if (arg == "--auto-esc-frame" && i + 1 < argc) {
            autoEscFrame = std::atoi(argv[++i]);
        } else if (arg == "--hover" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &hoverX, &hoverY) != 2) {
                hoverX = -1;
                hoverY = -1;
            }
        } else if (arg == "--auto-key" && i + 1 < argc) {
            autoKey = argv[++i];
        } else if (arg == "--game-click" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameClickX, &gameClickY, &gameClickFrame) != 3) {
                gameClickX = -1;
                gameClickY = -1;
            }
        } else if (arg == "--game-drag" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d,%d,%d", &gameDragX1, &gameDragY1, &gameDragX2,
                            &gameDragY2, &gameDragFrame) != 5) {
                gameDragX1 = -1;
            }
        } else if (arg == "--game-rclick" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameRClickX, &gameRClickY, &gameRClickFrame) !=
                3) {
                gameRClickX = -1;
                gameRClickY = -1;
            }
        } else if (arg == "--game-click2" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameClick2X, &gameClick2Y, &gameClick2Frame) !=
                3) {
                gameClick2X = -1;
                gameClick2Y = -1;
            }
        } else if (arg == "--game-click3" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameClick3X, &gameClick3Y, &gameClick3Frame) !=
                3) {
                gameClick3X = -1;
                gameClick3Y = -1;
            }
        } else if (arg == "--game-esc" && i + 1 < argc) {
            gameEscFrame = std::atoi(argv[++i]);
        } else if (arg == "--auto-click2" && i + 1 < argc) {            if (std::sscanf(argv[++i], "%d,%d", &click2X, &click2Y) != 2) {
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
        } else if (arg == "--auto-click7" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d", &click7X, &click7Y) != 2) {
                click7X = -1;
                click7Y = -1;
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
    // [NEW] 调试模式（--debug）：启用 Ctrl+1..8 测试热键
    app.gameState().debugMode = debugMode;
    app.setAutoEsc(autoEsc, autoEscFrame);
    if (!autoKey.empty()) {
        app.setAutoKey(autoKey);
    }
    if (hoverX >= 0 && hoverY >= 0) {
        app.setHoverPos(hoverX, hoverY);
    }
    if (gameClickX >= 0 && gameClickY >= 0) {
        app.setGameClick(gameClickX, gameClickY, gameClickFrame);
    }
    if (gameDragX1 >= 0 && gameDragY1 >= 0) {
        app.setGameDrag(gameDragX1, gameDragY1, gameDragX2, gameDragY2, gameDragFrame);
    }
    if (gameRClickX >= 0 && gameRClickY >= 0) {
        app.setGameRClick(gameRClickX, gameRClickY, gameRClickFrame);
    }
    if (gameClick2X >= 0 && gameClick2Y >= 0) {
        app.setGameClick2(gameClick2X, gameClick2Y, gameClick2Frame);
    }
    if (gameClick3X >= 0 && gameClick3Y >= 0) {
        app.setGameClick3(gameClick3X, gameClick3Y, gameClick3Frame);
    }
    if (gameEscFrame >= 0) {
        app.setGameEsc(gameEscFrame);
    }
    if (!shotPath.empty()) {
        app.setScreenshot(shotPath, shotFrame, shotCursorX, shotCursorY, autoClick, click2X,
                          click2Y, click3X, click3Y, click4X, click4Y, click5X, click5Y, click6X,
                          click6Y, click7X, click7Y);
    }
    app.run();
    app.shutdown();
    return 0;
}
