#include "game/application.h"

#include "game/app/debug/debug.h"
#include "game/app/save_data.h"
#include "game/app/game_loop.h"
#include "game/app/new_game.h"
#include "game/game_state.h"

#include "game/core/clock.h"
#include "game/core/debug_hooks.h"
#include "game/core/log.h"
#include "game/core/trace.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// [RE 0x45709C] start
// 依据: 0x45709C 为 CRT 入口 thunk（jmp sub_458CED）; sub_458CED 初始化 CRT 后调用
//       WinMain(GetModuleHandleA(0), 0, lpCmdLine, 10)
// 迁移: CRT 启动 + WinMain → 标准 main(argc, argv); lpCmdLine → --game <dir> 参数
int main(int argc, char** argv) {
#ifdef _WIN32
    // [PORT] 控制台日志按 UTF-8 显示（源码/执行字符集 = UTF-8，见 CMake /utf-8；
    //        默认 CP936 会把 UTF-8 中文 stderr 日志显示成乱码）
    SetConsoleOutputCP(CP_UTF8);
#endif
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
int gameClick4X = -1;
int gameClick4Y = -1;
int gameClick4Frame = 0;
int gameClick5X = -1;
int gameClick5Y = -1;
int gameClick5Frame = 0;
std::vector<std::pair<std::string, int>> gameKeys; // [NEW] --game-key "ctrl+5,1500"
    int gameEscFrame = -1;
    bool debugMode = false;
    int selftestSlot = -1;
    int loadGameSlot = -1;
    // [NEW] headless 测试钩子（docs/testing.md；无原版对应）
    int testSeed = -1;
    bool headless = false;
    bool testClock = false;
    bool traceOn = false;
    std::string traceOut;
    // [NEW] --quickstart <map[,players[,humans]]>：headless 开局（docs/testing.md）
    bool quickstart = false;
    int qMap = 0;
    int qPlayers = 4;
    int qHumans = 1;
    // [NEW] --script <file> / --exec "<a; b; c>"（可多次，按出现顺序执行；docs/testing.md）
    std::vector<std::string> scriptFiles;
    std::vector<std::string> execLines;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--script" && i + 1 < argc) {
            scriptFiles.emplace_back(argv[++i]);
        } else if (arg == "--exec" && i + 1 < argc) {
            execLines.emplace_back(argv[++i]);
        } else if (arg == "--quickstart" && i + 1 < argc) {
            quickstart = true;
            if (std::sscanf(argv[++i], "%d,%d,%d", &qMap, &qPlayers, &qHumans) < 1) {
                quickstart = false;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            testSeed = std::atoi(argv[++i]);
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--test-clock") {
            testClock = true;
        } else if (arg == "--trace") {
            traceOn = true;
        } else if (arg == "--trace-out" && i + 1 < argc) {
            traceOut = argv[++i];
            traceOn = true;
        } else if (arg == "--save-selftest" && i + 1 < argc) {
            selftestSlot = std::atoi(argv[++i]);
        } else if (arg == "--load-game" && i + 1 < argc) {
            loadGameSlot = std::atoi(argv[++i]);
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
        } else if (arg == "--game-click4" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameClick4X, &gameClick4Y, &gameClick4Frame) !=
                3) {
                gameClick4X = -1;
                gameClick4Y = -1;
            }
        } else if (arg == "--game-click5" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%d,%d", &gameClick5X, &gameClick5Y, &gameClick5Frame) !=
                3) {
                gameClick5X = -1;
                gameClick5Y = -1;
            }
        } else if (arg == "--game-key" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const size_t comma = spec.rfind(',');
            if (comma != std::string::npos) {
                gameKeys.emplace_back(spec.substr(0, comma),
                                      std::atoi(spec.c_str() + comma + 1));
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
                        "[--auto-click] [--auto-click2 x,y] [--game-click x,y,frame] "
                        "[--game-click2 x,y,frame] [--game-click3 x,y,frame] "
                        "[--game-click4 x,y,frame] [--game-click5 x,y,frame] [--game-drag x1,y1,x2,y2,frame]\n"
                        "  [NEW headless 测试钩子] --debug --seed <n> --headless --test-clock "
                        "--trace --trace-out <file.json> --quickstart <map[,players[,humans]]> "
                        "--save-selftest <slot> --load-game <slot>\n");
            return 0;
        }
    }

    if (selftestSlot >= 0) {
        using namespace rich4;
        char pn[128];
        std::snprintf(pn, sizeof(pn), "%s/SAVE%d.DAT", gameDir.c_str(), selftestSlot);
        std::vector<uint8_t> raw;
        if (!readSaveFile(pn, raw)) {
            std::printf("selftest: %s read/version fail\n", pn);
            return 1;
        }
        GameState a;
        if (!parseSaveBody(a, raw)) {
            std::printf("selftest: parseSaveBody fail (size %zu)\n", raw.size());
            return 1;
        }
        std::printf("selftest parse ok slot %d: map/mode=%d/%d players=%d humans=%d cur=%d "
                    "date=%u turn=%d day=%d moneyMul=%d rot=%d publicFund=%d\n",
                    selftestSlot, a.mapIndex, a.gameMode, a.playerCount, a.humanCount,
                    a.currentPlayer, a.gameDate, a.turnCounter, a.dayCount, a.moneyMul,
                    a.mapRotation, a.publicFund);
        std::printf("  tables: cellEnts=%zu estates=%zu corps=%zu specPts=%zu evtCells=%zu "
                    "cellTable=%zu newsPos=%d fatePos=%d\n",
                    a.cellEnts.size(), a.estates.size(), a.corps.size(), a.specPts.size(),
                    a.evtCells.size(), a.cellTable.size(), a.newsPos, a.fatePos);
        std::printf("  p0 cash=%d bank=%d loan=%d pts=%u charIdx=%u alive=%u | stocks[0] cur=%.1f "
                    "prev=%.1f spec=%d halt=%u news=%u reserved=%d vol=%d\n",
                    a.players[0].cash, a.players[0].bank, a.players[0].loan, a.players[0].points,
                    a.players[0].charIndex, a.players[0].alive, a.stocks[0][5], a.stocks[0][4],
                    static_cast<int>(a.stocks[0][1]), a.stockHalted[0], a.stockNews[0],
                    a.stockReserved[0], a.stockVolume[0]);
        std::printf("  snapValid=%d/%d/%d/%d blkSize=%zu/%zu\n", a.snapshots[0].valid ? 1 : 0,
                    a.snapshots[1].valid ? 1 : 0, a.snapshots[2].valid ? 1 : 0,
                    a.snapshots[3].valid ? 1 : 0, a.snapshots[0].block.size(),
                    a.snapshots[1].block.size());

        // 往返稳定性：save(a)=t1, parse(t1)=b, save(b)=t2；t1==t2 → save∘parse∘save 字节稳定
        if (!saveGameToSlot(".", 900, a)) {
            std::printf("selftest: saveGameToSlot(900) fail\n");
            return 1;
        }
        std::vector<uint8_t> r900;
        GameState b;
        if (!readSaveFile("./SAVE900.DAT", r900) || !parseSaveBody(b, r900)) {
            std::printf("selftest: reparse SAVE900 fail\n");
            std::remove("./SAVE900.DAT");
            return 1;
        }
        if (!saveGameToSlot(".", 901, b)) {
            std::printf("selftest: saveGameToSlot(901) fail\n");
            std::remove("./SAVE900.DAT");
            return 1;
        }
        std::vector<uint8_t> r901;
        readSaveFile("./SAVE901.DAT", r901);
        const bool stable = (r900 == r901);
        std::printf("  roundtrip: orig=%zu  a->SAVE900=%zu  b->SAVE901=%zu  STABLE(t1==t2)=%s\n",
                    raw.size(), r900.size(), r901.size(), stable ? "YES" : "NO");
        std::remove("./SAVE900.DAT");
        std::remove("./SAVE901.DAT");
        return stable ? 0 : 2;
    }

    rich4::Application app;
    // [NEW] headless 测试钩子接线（docs/testing.md）：
    //   --seed 固定随机序列（gameInit 播种读取）；--headless/--test-clock 虚拟时钟
    //   （delayMs 不睡仅累加，阻塞动画/消息循环快速收敛）；--trace 演出 trace 记录
    //   （assert trace 数据源），--trace-out 退出时导出 JSON。
    //   注：--headless 的 dummy 渲染旁路属后续步骤（当前=虚拟时钟+trace，仍建窗口）。
    if (testSeed >= 0) {
        rich4::dbg::setSeed(testSeed);
    }
    if (headless || testClock) {
        rich4::setVirtualClock(true);
    }
    if (headless || traceOn) {
        rich4::trace::setEnabled(true);
    }
    if (!app.init(gameDir, headless)) {
        RICH4_LOGE("Application init failed");
        return 1;
    }
    // [NEW] 调试模式（--debug）：启用 Ctrl+1..8 测试热键
    app.gameState().debugMode = debugMode;
    // [NEW] 装载 headless 测试脚本（docs/testing.md）
    rich4::debug::initFromCommandLine(app);
    for (const auto& f : scriptFiles) {
        rich4::debug::addScriptFile(f);
    }
    for (const auto& e : execLines) {
        rich4::debug::addExecLines(e);
    }
    if (quickstart) {
        // [NEW] headless 开局（跳过主菜单+选人；正常模式勿用——无操作会直接进局）
        app.gameState().debugQuickstart = true;
        app.gameState().quickMap = qMap;
        app.gameState().quickPlayers = qPlayers;
        app.gameState().quickHumans = qHumans;
    }
    if (loadGameSlot >= 0) {
        const bool ok = rich4::reloadGameFromSlotData(app, loadGameSlot);
        std::printf("load-game slot %d: %s\n", loadGameSlot, ok ? "OK" : "FAIL");
        if (ok) {
            rich4::renderGameFrame(app);
            const rich4::GameState& s = app.gameState();
            const std::string dump = "load_shot_s" + std::to_string(loadGameSlot) + ".bmp";
            app.surface().saveBmp(dump.c_str());
            std::printf("  after load: map=%d cur=%d cellEnts=%zu estates=%zu p0cash=%d "
                        "manualView=%d pendingSpawn=%d panelLoaded=%d -> %s\n",
                        s.mapIndex, s.currentPlayer, s.cellEnts.size(), s.estates.size(),
                        s.players[0].cash, s.manualView ? 1 : 0, s.pendingSpawnPlayer,
                        s.panelLoaded ? 1 : 0, dump.c_str());
        }
        if (!traceOut.empty()) {
            rich4::trace::exportJson(traceOut);
        }
        app.shutdown();
        return ok ? 0 : 1;
    }
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
    if (gameClick4X >= 0 && gameClick4Y >= 0) {
        app.setGameClick4(gameClick4X, gameClick4Y, gameClick4Frame);
    }
    if (gameClick5X >= 0 && gameClick5Y >= 0) {
        app.setGameClick5(gameClick5X, gameClick5Y, gameClick5Frame);
    }
    for (const auto& [spec, frame] : gameKeys) {
        app.addGameKey(spec, frame);
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
    if (!traceOut.empty()) {
        rich4::trace::exportJson(traceOut);
    }
    app.shutdown();
    // [NEW] 脚本断言失败 → 非零退出码（CI 契约，docs/testing.md）
    if (!scriptFiles.empty() || !execLines.empty()) {
        std::printf("%s\n", rich4::debug::reportSummary().c_str());
    }
    return rich4::debug::failures() ? 1 : 0;
}
