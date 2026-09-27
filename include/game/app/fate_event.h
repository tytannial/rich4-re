#pragma once

namespace rich4 {

class Application;
struct Player;

// [RE 0x44DB81] fateEvent：命運格落地事件（landingEvent case 3）
// 依据: 0x44DB81 主循环（g_fateOrder[g_fatePos] 抽取 → fateEventCheck 0x44BB4B 判定/载具改写
//       → panel.mkf[66] 画布 + data.mkf[word_475FB4[tableIdx]] 插画 @(25,44) + 文本 @(24,330)
//       + 角色头像 @(390,344) → 全屏 1600ms → 效果(1) → 800ms → 释放）；
//       效果表 funcs_44DC44 0x475EF0 共 49 项（0..32 通用 + 33..48 = 4 地图×4 坐牢）；
//       33..36 需 gameMode==0；tableIdx = (v>=33) ? 4*mapIndex+v : v。
//       详见 docs/reverse/functions/44db81-fate-events.md。
// 差异: 语音 sub_44EF41/sub_44F354 → TODO(P4)；byte_497324/5 统计省略。
void fateEvent(Application& app);

// [NEW] 调试：强制触发命运。idx < 0 = 按 g_fateOrder 抽取下一条可触发事件；
//   idx 为表索引（0..48，33..48 为地图专属）。
void fateDebugFire(Application& app, int idx);

// [RE 0x40D375] fateStartTravelState：强迫出國/外星人綁架状态（定义见 fate_event.cpp）。
//   abducted: false=出國（FLC 558 @(0,40) sound 96）/ true=綁架（FLC 533 sound 84）；
//   调用点：命运/事件 + specPt 航空付款后（0x41B05A，days=轮盘天数）
void fateStartTravelState(Application& app, Player& pl, int player, int days, bool abducted);

} // namespace rich4
