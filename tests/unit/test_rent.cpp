#include "microtest.h"

#include <cstring>

#include "game/app/turn_system.h"
#include "game/game_state.h"

// [NEW] L0 单测：estateRouteRent 同路段联合/连锁店公式（[RE 0x419744]，docs/pricing-formulas.md）
namespace {

rich4::Estate mkEstate(const char* name, uint8_t owner, uint8_t level, uint8_t type,
                       const uint16_t* fees) {
    rich4::Estate e{};
    std::strncpy(e.name, name, sizeof(e.name) - 1);
    e.owner = owner;
    e.level = level;
    e.type = type;
    for (int i = 0; i < 6; ++i) {
        e.fees[i] = fees[i];
    }
    return e;
}

} // namespace

MT_TEST(rent_same_street_sum) {
    rich4::GameState st;
    st.moneyMul = 1;
    st.estates.push_back(rich4::Estate{}); // index0 占位
    const uint16_t f[6] = {100, 200, 400, 800, 1600, 3200};
    st.estates.push_back(mkEstate("TAIPEI", 1, 2, 0, f));
    st.estates.push_back(mkEstate("TAIPEI", 1, 3, 0, f));
    st.estates.push_back(mkEstate("TAIPEI", 1, 1, 0, f));
    // 同路段三块：level 2+3+1 → fees[2]+fees[3]+fees[1] = 400+800+200
    const int32_t r = rich4::estateRouteRent(st, 1, st.estates[1]);
    MT_EQ(r, 1400);

    // 不同 owner / 不同名 / 不同 type 不参与
    st.estates.push_back(mkEstate("TAIPEI", 2, 5, 0, f));
    st.estates.push_back(mkEstate("KAOSIUNG", 1, 5, 0, f));
    const int32_t r2 = rich4::estateRouteRent(st, 1, st.estates[1]);
    MT_EQ(r2, 1400);
}

MT_TEST(rent_chain_stores_2000) {
    rich4::GameState st;
    st.moneyMul = 1;
    st.estates.push_back(rich4::Estate{});
    const uint16_t f[6] = {0, 0, 0, 0, 0, 0};
    st.estates.push_back(mkEstate("A", 1, 1, 1, f));
    st.estates.push_back(mkEstate("B", 1, 2, 1, f));
    st.estates.push_back(mkEstate("C", 2, 1, 1, f)); // 他人连锁不计
    st.estates.push_back(mkEstate("D", 1, 1, 0, f)); // 非连锁不计
    // 同 owner 连锁 2 处 × 2000
    MT_EQ(rich4::estateRouteRent(st, 1, st.estates[1]), 4000);
}

MT_TEST(rent_money_mul) {
    rich4::GameState st;
    st.moneyMul = 3;
    st.estates.push_back(rich4::Estate{});
    const uint16_t f[6] = {100, 200, 0, 0, 0, 0};
    st.estates.push_back(mkEstate("X", 1, 1, 0, f));
    // 单块 fees[1]=200 × M=3
    MT_EQ(rich4::estateRouteRent(st, 1, st.estates[1]), 600);
}
