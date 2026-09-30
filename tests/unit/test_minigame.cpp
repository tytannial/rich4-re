#include "microtest.h"

#include "game/app/minigame.h"

// [NEW] L0 单测：喜從天降（case 8）娃娃跟随鼠标判定
//   [RE 0x4135F4/0x41364D] moneyTick（0x413248）反汇编；纯函数 moneyDollFollowStep。
//   锁定三条原版语义，防回归：
//     ① 死区 ±8（0x413661 `cmp eax,8; jle` —— **含等于 8**）
//     ② 死区内 dir 不清零、行走帧不推进（0x413664 直接跳到跟随段之后 = 原版 quirk，
//        勿"顺手修"成 dir=0）
//     ③ 移动方向与步长（0x41366A/0x413673 diff>0 → dir=1 左行 10px；
//        0x41367F/0x413688 diff<0 → dir=2 右行 10px）
//   另：0x41369B 行走帧 = (walk+1) % half。

MT_TEST(money_doll_deadzone_keeps_dir_and_walk) {
    int dir = 1;
    int walk = 7;
    // diff = +8 / -8 / 0 均落在死区内 → 位置、dir、walk 全不变
    MT_EQ(rich4::moneyDollFollowStep(320, 312, dir, walk, 10), 320);
    MT_EQ(dir, 1);
    MT_EQ(walk, 7);
    MT_EQ(rich4::moneyDollFollowStep(320, 328, dir, walk, 10), 320);
    MT_EQ(dir, 1);
    MT_EQ(walk, 7);
    MT_EQ(rich4::moneyDollFollowStep(320, 320, dir, walk, 10), 320);
    MT_EQ(dir, 1);
    MT_EQ(walk, 7);
}

MT_TEST(money_doll_step_left_and_right) {
    int dir = 0;
    int walk = 0;
    // diff = +9（娃娃在光标右侧）→ 左行 dir=1、dollX -= 10、行走帧 +1
    MT_EQ(rich4::moneyDollFollowStep(320, 311, dir, walk, 10), 310);
    MT_EQ(dir, 1);
    MT_EQ(walk, 1);
    // diff = -9（娃娃在光标左侧）→ 右行 dir=2、dollX += 10
    dir = 0;
    walk = 0;
    MT_EQ(rich4::moneyDollFollowStep(320, 329, dir, walk, 10), 330);
    MT_EQ(dir, 2);
    MT_EQ(walk, 1);
}

MT_TEST(money_doll_walk_wraps_and_converges) {
    int dir = 0;
    int walk = 9;  // half = 10 → 推进后回绕到 0
    MT_EQ(rich4::moneyDollFollowStep(320, 300, dir, walk, 10), 310);
    MT_EQ(walk, 0);

    // 逐 tick 逼近后停在死区内（不再移动）——原版"追到光标附近即停"行为
    int x = 100;
    dir = 0;
    walk = 0;
    for (int i = 0; i < 40; ++i) {
        x = rich4::moneyDollFollowStep(x, 320, dir, walk, 10);
    }
    MT_CHECK(x >= 312 && x <= 320);
    // 反向逼近同理
    x = 600;
    for (int i = 0; i < 40; ++i) {
        x = rich4::moneyDollFollowStep(x, 320, dir, walk, 10);
    }
    MT_CHECK(x >= 320 && x <= 328);
}
