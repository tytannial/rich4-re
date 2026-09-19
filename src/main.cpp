#include "game/application.h"

#include "game/core/log.h"

#include <string>

int main(int argc, char** argv) {
    std::string gameDir = "resources/game";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: rich4 [--game <dir>]\n");
            return 0;
        }
    }

    rich4::Application app;
    if (!app.init(gameDir)) {
        RICH4_LOGE("Application init failed");
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
