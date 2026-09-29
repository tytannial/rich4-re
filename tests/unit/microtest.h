#pragma once

#include <cstdio>
#include <string>
#include <vector>

// [NEW] 极简单测框架（L0，docs/testing.md；无外部依赖，C++17）
namespace mt {

struct Reg {
    const char* name;
    void (*fn)();
};

inline std::vector<Reg>& all() {
    static std::vector<Reg> v;
    return v;
}

struct Add {
    Add(const char* n, void (*f)()) { all().push_back(Reg{n, f}); }
};

inline int g_fails = 0;
inline int g_checks = 0;
inline std::string g_gameDir = "../resources/MultiverseJourney"; // rich4_tests <gameDir>

} // namespace mt

#define MT_TEST(name)                                                      \
    static void name();                                                    \
    static ::mt::Add g_add_##name(#name, name);                            \
    static void name()

#define MT_CHECK(cond)                                                     \
    do {                                                                   \
        ++::mt::g_checks;                                                  \
        if (!(cond)) {                                                     \
            ++::mt::g_fails;                                               \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

#define MT_EQ(a, b)                                                        \
    do {                                                                   \
        ++::mt::g_checks;                                                  \
        auto va = (a);                                                     \
        auto vb = (b);                                                     \
        if (!(va == vb)) {                                                 \
            ++::mt::g_fails;                                               \
            std::printf("  FAIL %s:%d  %s(=%lld) != %s(=%lld)\n", __FILE__, __LINE__, #a, \
                        static_cast<long long>(va), #b, static_cast<long long>(vb));       \
        }                                                                  \
    } while (0)
