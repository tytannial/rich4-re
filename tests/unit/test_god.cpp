#include "microtest.h"

#include "game/app/economy.h"

// [NEW] L0 单测：神明配对轮替方向公式（[RE 0x40E278] deleteMapObject 末尾，
//   docs/map-object-refresh.md §2.1）。锁定"偶槽消耗→奇槽大形态、奇槽消耗→偶槽小形态"，
//   防止回归为"奇槽原地复现大形态"（2026-09-28 修正的真实 bug）。
MT_TEST(god_pair_even_to_big) {
    // 偶槽(小形态在场)被消耗 → 重建对向奇槽大形态
    MT_EQ(rich4::godPairType(0), 2);   // 小財神 → 大財神
    MT_EQ(rich4::godPairType(2), 4);   // 小福神 → 大福神
    MT_EQ(rich4::godPairType(4), 6);   // 小窮神 → 大窮神
    MT_EQ(rich4::godPairType(6), 8);   // 小衰神 → 大衰神
    MT_EQ(rich4::godPairType(8), 10);  // 天使   → 惡魔
    MT_EQ(rich4::godPairType(10), 12); // 惡犬   → 土地公
}

MT_TEST(god_pair_odd_to_small) {
    // 奇槽(大形态在场)被消耗 → 重建对向偶槽小形态（旧实现误为 idx+1 原地复现大形态）
    MT_EQ(rich4::godPairType(1), 1);   // 大財神 → 小財神
    MT_EQ(rich4::godPairType(3), 3);   // 大福神 → 小福神
    MT_EQ(rich4::godPairType(5), 5);   // 大窮神 → 小窮神
    MT_EQ(rich4::godPairType(7), 7);   // 大衰神 → 小衰神
    MT_EQ(rich4::godPairType(9), 9);   // 惡魔   → 天使
    MT_EQ(rich4::godPairType(11), 11); // 土地公 → 惡犬
}
