#include "microtest.h"

#include <cstdio>

int main(int argc, char** argv) {
    // [NEW] 关掉 stdout 缓冲：单测崩溃（abort/异常终止）时块缓冲会丢掉全部输出，
    //   无法定位是哪个用例挂的；改为逐行直出（对正常路径无影响）
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
