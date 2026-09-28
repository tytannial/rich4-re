#include "microtest.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc > 1) {
        mt::g_gameDir = argv[1];
    }
    for (const auto& r : mt::all()) {
        const int before = mt::g_fails;
        std::printf("[ RUN  ] %s\n", r.name);
        r.fn();
        std::printf("[ %s ] %s\n", mt::g_fails == before ? " OK " : "FAIL", r.name);
    }
    std::printf("UNIT checks=%d fails=%d -> %s\n", mt::g_checks, mt::g_fails,
                mt::g_fails == 0 ? "PASS" : "FAIL");
    return mt::g_fails ? 1 : 0;
}
