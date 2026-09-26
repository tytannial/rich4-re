# 0x41B42D onPlayerActionPhase（落地事件分发）

- **地址**：0x41B42D（5154 字节 / 161 基本块 / 复杂度 112）
- **调用**：`playerActionStateMachine`（0x40D7C4）case 1，`byte_48BB00`（落地标志）
  → 每走完一格调用一次（`moveOneStep` 成功置位，下一帧进入本函数）
- **重写**：`src/app/turn_system.cpp` `onPlayerActionPhase()`（M2 进行中）

## 入口数据

```c
if (!dword_48BAF8) audioStopEffect(&g_effectSlots[2*dword_4749D4]);   // 停留：停移动音效
if (g_currentPlayer >= 4) v0 = word_498DEC[8*g_currentPlayer];        // 事件槽：格子号
else v0 = g_playerCellEntId[52*g_currentPlayer];                      // 玩家：cellEnt ID
v1  = g_cellEnts + 40*v0;
v62 = (uint8) *(u32*)(v1+36);            // cellEnt 格子类型（1..14）
v60 = (*(u32*)(v1+36) & 0xF00) >> 8;     // 占用玩家掩码（256<<player）
v63 = (*(u32*)(v1+36) & 0xFF0000) >> 16; // cellTable 槽号（0 = 无）
v61 = v63 ? g_cellTable[24*v63-24] : 0;  // cellTable 类型（1..18）
```

- `g_cellTable`（0x496D08，24B/项）：+0 类型、+1 朝向、+2 cellEnt u16、+4 剩余回合、
  +5 占用玩家+1（`byte_496D0C`/`byte_496D0D` = +4/+5）
- `g_playerCellNo`（+64）与 `byte_496BA7`（+63，重写 `Player.cellTableIdx`）为玩家
  占用的 cellTable 槽（1-based）

## 分支结构

| 分支 | 处理 |
|------|------|
| 玩家 8 | `sub_40FAFD(v63,…)` + `sub_40E14D(v63)`（AI 事件槽） |
| 玩家 4..7（事件槽 NPC） | `switch(v61)`：case 1..10/12 → 不附身；case 11/13/14/16/17/18 → NPC 差异分支（小偷给 bailer 道具 / 5·6·7 炸伤/停步）；**LABEL_88 恶行**（cur4/5 同格偷点券/抢卡/抢银行；cur6/7 停留勒索/窃租金红利，`sub_41D2C6` 转账）；**LABEL_145 抓回**（status 同类型建筑）。✅ 2026-09-26 完整规格 `498df0-event-slot-npc.md` §2.6/§2.7/§2.8 |
| 玩家 0..3 | 见下 |

### 玩家 0..3

1. **LABEL_22**（`v62 != 14 || state37 || !dword_48BAF8 || v61 == 16` 时）
   - 同格有已淘汰玩家（`v60` 掩码去自己，`sub_40D293` 位扫描）且停留 →
     付 `1000*moneyMul` 破产金（文本 `unk_463AB1`）给银行（`sub_41D2C6(p,-1,…)`）+ `sub_40CC56`
   - `g_playerCellNo[p]` 非 0（在医院/监狱）：
     - `byte_496D0C[24*cellNo-24] -= 1`，到 0 → 停音效 + `sub_40E14D` + FLC 525 +
       `hospitalizePlayer(p,5)`（出院/出狱）
     - 同格有其他存活玩家且其无 cellNo → 转移 cellNo（传染，`byte_496D0D` 占用表）
2. **switch(v61)**（cellTable 类型）
   - case 1..10/12：停留且玩家 <4 → `sub_40EAD7(p, v0, v63)`（神明/事件效果）
   - case 11：`sub_40E14D(11)` + FLC 532/552 + `hospitalizePlayer(p,3)`
   - case 13：`sub_445ADA(p)` 抽卡 + `off_47FEDA` 卡名 + `sub_44F230` 给卡
   - case 14：点数 +500（音效 `dword_482382`，文本 `byte_463AD3`）
   - case 16：`givePlayerCard(p,2)`
   - case 17：FLC 525 + `hospitalizePlayer(p,3)`
   - case 18：`g_playerCellNo[p] = v63` + `byte_496D0C = 38` + `byte_496D0D = p+1`
     （进入特殊格，如加油站/监狱）
3. **LABEL_145**（玩家 4..7 的收尾也在其中）
   - `(byte_498DF3[p] & 0x7F) == 1 && v62 == 4` → 状态位 `|0x80` 或 `jailPlayer(p,0)`
   - `(byte_498DF3[p] & 0x7F) == 2 && v62 == 5` → 状态位 `|0x80` 或 `hospitalizePlayer(p,0)`

## 依赖函数

| 地址 | 名称 | 说明 |
|------|------|------|
| 0x40EAD7 | （未命名） | 玩家进入 cellTable 物件格：占用槽、属性加成（`word_4749E2/474A06/474A2A`）、`switch(类型 1..15)` 神明效果（FLC 540/541/542 + 金额 `sub_440706` + 转账） |
| 0x40E14D | `releaseCellTableSlot` | 释放槽：占用者属性回退、清槽、前 12 槽重新 `createMapObject` |
| 0x41D2C6 | `transferMoney` | 转账（现金/银行互补、不足裁剪 + `sub_40CD87`、特殊点 >100） |
| 0x40CD87 | `eliminatePlayer` | 淘汰：标记占用位、清资产、股票清仓、`sceneRequest` |
| 0x40D293 | `bitScanPlayer` | 掩码位扫描 |
| 0x40E32C | （未命名） | 神明显灵动画（24 帧 + `sub_40E14D`） |

## 收租不在本函数

普通玩家踩他人**住宅用地/商業用地**的过路费在 `playerActionStateMachine`（0x40D7C4）**case 3**
（0x40DA90..0x40DB3E）：读 `cellEnt+32` objId 2000..4000 → `g_estates + 52*(id-2000)`，
无主跳过，有主按等级计算金额（`g_moneyMul` 系数）→ `sub_41D2C6`。

## 重写现状与差异

- 已实现：`transferMoney` / `releaseCellTableSlot` / `eliminatePlayer` / `bitScanPlayer`
  （`src/app/economy.cpp`）；`Player.bonusAtk/bonusDef/bonusMove`、`SpecPt.fund/fundPaid` 字段
- 未实现：`sub_40EAD7` 神明效果（重写已实现主体，见 `map-object-refresh.md`）、cellTable case 11/13/14/16/17/18 的 FLC/卡片（重写已实现）、
  事件槽 NPC（4..7）分支 **（✅ 2026-09-26 已实现）**
- 数据缺口：`CellEnt` 未存 +36 bit0-7（格子类型）与 +38 标志；
  收支统计 `dword_496BC4/496BC8`、银行总资金 `dword_499080`、类型计数
  `byte_497321/497322/497323` 未建模
