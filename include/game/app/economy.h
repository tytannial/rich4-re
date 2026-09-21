#pragma once

#include <cstdint>

namespace rich4 {

struct GameState;
class Application;

// 经济系统（定义见 src/app/economy.cpp）。M2 起步：仅实现收租/税/罚款共用的
// 转账与物件释放；买地/银行/股票见 docs/reverse/functions/ 各档案。

// [RE 0x40D293] 取占用掩码中最低位的玩家号（0..7）；掩码为 0 返回 -1
int bitScanPlayer(uint32_t mask);

// [RE 0x41D2C6] transferMoney
// 依据: 0x41D2C6 反编译; from 付给 to：
//   from: 0..7 玩家（flags&4 从银行扣，否则从现金扣，不足由另一账户补，
//         仍不足 → 实付额裁剪 + sub_40CD87 淘汰）
//   to:   -1 = 银行/系统，0..7 = 玩家（flags&1 进现金，否则进银行），
//         >100 = 特殊点（索引 = to-101，记入 +40/+44 资金）
// 差异: 原版 dword_496BC4/dword_496BC8 收支统计与 sub_41D433 UI 刷新未建模
void transferMoney(Application& app, int from, int to, int32_t amount, int flags);

// [RE 0x40E14D] releaseCellTableSlot
// 依据: 0x40E14D 反编译; slot 为 1-based cellTable 槽：
//   占用者属性回退（word_4749E2/474A06/474A2A）→ 清槽（cellEnt/+4/+5）
//   → 前 12 槽（cellEnt 物件）重新随机放置 createMapObject(slot' , randomCellEnt(...))
// 差异: byte_497321/497322/497323 类型计数未建模
void releaseCellTableSlot(Application& app, int slot);

// [RE 0x40CD87] eliminatePlayer
// 依据: 0x40CD87 反编译; 玩家破产/淘汰：
//   cellEnt 占用位标记 → alive=0 → 释放携带物件 → 清空地产/公司/特殊点
//   → 全灭时 sceneRequest（原版 1/2/3 = 失败流程/回主菜单）
// 差异: 股票清仓（sub_428E23）、事件槽 NPC 清理、FLC 555 播放、defeatFlow 未实现
void eliminatePlayer(Application& app, int p);

} // namespace rich4
