# 地图物件（神明/礼物/宝箱/恶犬/道具）生命周期与刷新机制

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x40E033` createMapObject / `0x40E14D` deleteMapObject / `0x40E32C` attachEnd（附身结束飘走）/ `0x40E669` moveObjectSprite（A→B 移动动画）/ `0x40EA62` canAttach / `0x40EAD7` attachObject（踩中效果主体）/ `0x40AA6C` randomCellEnt / `0x41B42D` onPlayerActionPhase（路过/停留触发）/ `0x41C84F` updatePlayerStates（回合寿命递减）/ `0x41CF67` advanceDay（月度换位）/ `0x40FAFD` bounceObject（弹飞）/ `0x40CD07` damagePlayer（载具损毁+受伤）/ `0x40AB4A` demolishEstate（地产降级/没收）/ `0x445ADA` drawGiftCard |
| 重写符号 | `src/app/map_objects.cpp`（attach/attachEnd/slotMachine/effects/relocate）+ `economy.cpp`（deleteMapObject）+ `turn_system.cpp`（分派/寿命）+ `map_render.cpp`（8方向帧/跟随/浮空数字/弹飞） |
| 状态 | **已实现（2026-09-24）**：神明全链路（附身/寿命7·13/轮替/老虎机/抽丢卡/没收/跟随渲染）+ 惡犬/禮物/寶箱 + 路障/地雷/炸彈**触发链**；**神明持续效果 ✅（2026-09-25，§13：landAfterMove 天使/惡魔/土地公 + 福神追加 godBlessUpgrade + 死神代付 resolveFeePayer + 欠款矩阵 addPlayerDebt）**；**事件槽 NPC（4..7）物件分支 ✅ 2026-09-26**（`498df0-event-slot-npc.md` §2.6）；[TODO P4] 道具放置入口与拆除卡（0x446BAA/C88/D69/0x446AFB）、角色台词语音 |

## 1. cellTable 槽位与字段（`g_cellTable` 0x496D08，46 项 × 24B）

| 偏移 | 字段 | 说明 |
|------|------|------|
| +0 | type | 0=空槽；1..18 见类型表（`kObjectNames`，`off_47ED76`） |
| +1 | dir | 朝向 0..7（渲染帧 = `(dir + 8 - view) & 7`） |
| +2 | cellEnt u16 | 所在逻辑格；附身时 = 拥有者玩家当前格（**不随玩家每步更新**，渲染走"跟随玩家"分支） |
| +4 | life | 剩余寿命（byte）：神明 7、死神 13、路障/炸彈 38；0=非挂身 |
| +5 | owner+1 | 挂主玩家号+1（0=未附着于玩家） |
| +6 | flyCount | 弹飞/移动动画剩余帧计数（`sub_40FAFD` 置 -1 起步；渲染每帧 -1，出视口清 0） |
| +7 | flyDir | 弹飞中的朝向备份（渲染帧用） |
| +8/+12 | f32 x/y | 浮动世界坐标（弹飞/移动动画当前位置） |
| +16/+20 | f32 vx/vy | 速度向量 = (目标格坐标 − 起点格坐标) × `dbl_46352C` |

**槽位布局**：槽 0..11 = **6 对神明**（类型 1..12：小/大財神、小/大福神、小/大窮神、小/大衰神、天使/惡魔、惡犬/土地公）；
槽 12=禮物(13)、13=寶箱(14)、14/15=死神(15，双槽)；槽 16..25=路障(16)、26..35=地雷(17)、36..45=定時炸彈(18)。
`createMapObject` 搜索段：type 15→槽 14..15、16→16..25、17→26..35、18→36..45、其他→槽 type-1 单槽。

**玩家侧关联字段**（104B/玩家）：`byte_496BA7`(+55) = **cellTableIdx**（挂身物件 ID=槽+1，同一字段承载 神明附身/路障/炸彈 三种挂身）；
`word_496BAC/BAE/BB0`(+60/+62/+64) = luckA/B/C 三属性修正。
物件侧回写：`g_cellEnts+38`（占用位）在挂身时清零、删除时若无主也清零。

## 2. 四种刷新/生命周期规律

### 2.1 配对轮替（神明 1..12）—— 核心规律
`deleteMapObject(a1, objId=槽+1)`（0x40E14D）末尾：

```c
if (slot < 12) {                      // 仅槽 0..11
    pairId = (slot & 1) ? objId - 2 : objId;   // 定位到配对偶槽 ID
    createMapObject(pairId + 1, randomCellEnt(旧格, 距>300), 0, 0);
}
```

即**槽 2k / 2k+1 为一对**（类型 2k+1 / 2k+2）：任一被消耗 → 在对向槽立即生成**配对的另一种类型**。
开局 `loadMapData` 只放类型 1,3,5,7,9,11（偶数槽）→ 场上每对恒 1 个神明；踩中/到期后换成对向。
礼物/宝箱/死神/道具（槽 ≥12）**不轮替**。

> **2026-09-28 修正（重写实现偏离 → 已修）**：重写 `releaseCellTableSlot` 曾把奇数槽被消耗时的
> 重建类型误算为 `idx+1`（=同奇槽大形态），导致**大财神/大福神/大穷神/大衰神/恶魔/土地公一旦被
> 轮换出即永久原地复现，永不翻回小形态**，小神/天使/恶犬逐轮绝迹（表现=“神越轮越大、该轮换的不轮换”）。
> 已抽为 `godPairType(idx)`（`economy.cpp`/`economy.h`）忠实原版 `0x40E278`：偶槽→`idx+2`、奇槽→`idx`；
> L0 单测 `tests/unit/test_god.cpp` 锁定双向。

### 2.1.1 神明刷新/交替触发全模型（何时“出现/消失/换位”，有无天数等待）

对“神明是否有天数刷新等待”的专项核实结论：**没有独立的计时刷新**。全部证据：

1. **出现（写入 cellTable 1..12）仅 4 个来源**（`0x40E033 createMapObject` 的 xref 全枚举）：
   - `0x407D6F loadMapData`：开局仅放偶槽 1,3,5,7,9,11（+ 13/14 礼物/宝箱）；
   - `0x40E278 deleteMapObject` 配对轮替：某槽被消耗的**瞬间**在对向槽 `randomCellEnt(旧格,距>300)` 重建；
   - `0x411AE0 surrenderPlayer` / `0x446BAA/C88/D69` 道具放置：只涉及 15 死神 / 16-18 道具，**不产神明**；
   - `Ctrl+Shift+O`（调试，非正常游戏）：按 1..18 循环在玩家当前格直放、`owner=0` 绕过配对，
     是“第一回合见/请到大神”的真实来源（**非初始放置 bug**）。
2. **`0x41CF67 advanceDay` 对 cellTable 的处理 = 仅新月 1 号**把槽 **13/14**（礼物/宝箱）delete→relocate→create；
   **从不**按天/月对神明 1..12 计时、换位或刷新 → 静止神明 `life=0 & owner=0` **永远站桩**。
3. **寿命递减只作用于“被附身”的神**：`0x41C84F→0x41CC6F` 尾段 `if(g_playerAttachedObj[p])` 对
   当前回合玩家的 cellTable 槽 `life-1`，归零→`attachEndAnim`(24 帧飘走)→`deleteMapObject`→**立即**对向轮替。
   调用点 = `0x419039 nextPlayer`（每玩家回合开始一次）；`0x41902E advanceDay`（一轮=1 天）→ 寿命 7≈7 天。
4. **交替的唯一“时间”成本**=神明被玩家得到并持有到寿命耗尽（或送神符/请神替换 / 玩家踩中他神触发
   `attachObject` 前置 `attachEnd`）；**没人碰则永不轮换**。即“天使→恶魔”“恶犬→土地公”非等待定时刷新，
   而是“天使被消耗/到期→恶魔立即刷新”，反之亦然（配对方向见 §2.1）。

重写一致性：`new_game.cpp:432-436`（初始）、`map_objects.cpp` `attachEnd→releaseCellTableSlot`、
`turn_system.cpp:3666`（寿命-1 节奏）、`turn_system.cpp:3822`（月度仅 13/14）均与原版吻合；
唯一偏差即上述 `godPairType` 方向 bug，已修。

### 2.2 月度换位（禮物13/寶箱14）
`advanceDay` 在 `sub_452117` 返回"新日期 == 1 号"时（**每月 1 号**，非每天）：
`deleteMapObject(13/14)` → `randomCellEnt(旧cellEnt)` → `createMapObject(13/14)`，
同时 `sub_439BFA`（月初结息）+ `++dword_499084`（分红期数）。重写已实现 ✅（turn_system.cpp:1952）。

### 2.3 附身跟随 + 寿命制（神明/死神）
`canAttach`（0x40EA62）：type ≤12 且 ≠11（惡犬），或 =15（死神）→ 可附身。
`attachObject(player, cellEnt, objId)`（0x40EAD7，前置 `canAttach`）：
1. 玩家已有附身 → 先 `attachEnd`（旧神飘走→删除→**轮替**）
2. `cellTableIdx = objId`；物件 cellEnt = 玩家当前格、+5 = player+1；旧格占用位清
3. **寿命 +4 = (type==15 ? 13 : 7)**
4. 三属性 `luckA/B/C += g_luckTbl[A|B|C][type]`
5. switch(type) 即时效果（§4）

寿命递减双入口（对同一 cellTableIdx）：
- `updatePlayerStates`（0x41C84F→0x41CC6F，回合开始）：-1，归零 → `attachEnd`
- `onPlayerActionPhase`（0x41B42D 前段，每次落地）：路障/炸弹挂件 -1，归零 → **爆炸处理**（§5 case 18）

`attachEnd`（0x40E32C，飘走动画）：物件精灵从玩家屏幕位置起 **24 帧、每帧 y-=10、朝向帧 `(+1)&7` 循环、帧间 60ms**；
顶边出地图区（y<40）提前结束；结束后 `deleteMapObject`（→轮替）。
若玩家处于 `stateFlags!=0`（住院/出国等挂起态）**跳过动画静默删除**。
結束对 type 5/6/7/8/15（窮/衰/死神）追加 `sub_44EF41(player, 2, 哭台词 off_4808A2)`。

### 2.4 道具类（路障16/地雷17/炸彈18）
玩家道具卡放置（`sub_446BAA/446C88/446D69` → `createMapObject(16/17/18)`，动画 `sub_40E669`）；
库存 `byte_497321/497322/497323`；**删除时对应库存 +1**（deleteMapObject 头部）；无自动刷新。
**[TODO P4]** 放置入口（卡片使用界面）未接。

## 3. 三属性修正表（`word_4749E2/474A06/474A2A`，u16[19]，索引=类型）

| type | A(+60) | B(+62) | C(+64) |
|------|-------|-------|-------|
| 1 小財神 | -100 | +100 | 0 |
| 2 大財神 | -200 | +150 | 0 |
| 3 小福神 | -100 | 0 | +100 |
| 4 大福神 | -200 | 0 | +150 |
| 5 小窮神 | +100 | -60 | 0 |
| 6 大窮神 | +200 | -100 | 0 |
| 7 小衰神 | +100 | 0 | -60 |
| 8 大衰神 | +200 | 0 | -100 |
| 9 天使 | -100 | +60 | +60 |
| 10 惡魔 | +100 | -60 | -60 |
| 12 土地公 | -500 | 0 | 0 |
| 15 死神 | +1000 | -200 | -200 |

语义（读取点实证）：A = AI「领先者评分」`2500×mul×拥有数 + 收入 − 支出 + 10×A`（`sub_437D1A`/悲情人物排名同源）；
B = AI 建设/升级门槛（`B >= 0` 才加盖，`sub_41FACC` 等 0x420000 段）+ 角色台词选择（`sub_44B896`：>100 吉 / 50..100 随机 / <0 凶，模板 `%s+神明名`）；
C = 另一类事件台词（同 `sub_44B896` a3 分支）。**玩家侧收费公式不直接乘三属性**（神明影响以 §4 即时效果+跟随机制体现）。

## 4. attachObject case 效果（0x40EAD7 switch）

所有 case 先播插图（`sub_450441(dword_48A0E4, 540+?, ...)` + `sub_45144F` 显示 + `sub_40E2A2(旁白)`，仅 `byte_497159` 语音开关时）。
**画面时序（原版 DDraw 表面保留语义）**：`sub_45144F` 播完 **末帧冻结留在 backbuffer**（函数不清屏），
`sub_40E2A2` 旁白在其上 `drawText` 后 2400ms 内**无任何重绘** → 文字始终叠加在插图末帧上；
重写 `playEventFlc(..., freeze=true)` + `showGodNarration`（不重绘、结束解冻）等价实现：

| type | 插图 | 旁白 | 效果 |
|------|------|------|------|
| 1 小財神 | 540 | 0x463250「呦喝！～發財發財…」损失减半 | `slotMachineDialog(0)` 老虎机得 v（**0=3 位 0..999**，见 ui-controls §26：vis=!(a1&1) 决定轮数）→ 对**其他全体存活玩家** `transferMoney(i, me, v, 1)`（0=付给我/1=方向标志）；v>700 大笑台词 |
| 2 大財神 | 541 | 0x463295「恭喜發財…誰也不能罰你的錢」 | `slotMachineDialog(1)` v → `addMoney(me, v, cash)`（0x41D3F4：+现金 +「意外之财」dword_496BC8 统计）；v≥5000×mul → `sub_44F354` 台词（按 9000/5000/2000×mul 分段选高兴台词，无副作用） |
| 3 小福神 | 542 | 0x4632CC「不要看我小…投資事半功倍」 | `drawFreeCard`(0x441E12) 抽 1 卡 → `"小福神附身\n\n得到%s！"`（0x4632FD %s=神名+卡名）→ `givePlayerCard` |
| 4 大福神 | 543 | 0x46330E「天官賜福…買地不用錢」 | 抽 2 卡 → 0x463353「大福神附身\n\n得到%s及%s！」→ 两张入包 |
| 5 小窮神 | 544 | 0x46336C「噎噎噎～賠錢！賠錢！」 | 先哭台词；`slotMachineDialog(4)` v → 对**其他全体** `transferMoney(me, i, v, 0)`（我付钱给每人） |
| 6 大窮神 | 545 | 0x463381「喔～錢掉了。」 | `slotMachineDialog(5)` v → `transferMoney(me, -1(银行), v, 0)` |
| 7 小衰神 | 546 | 0x46338E「有我小小衰神…一事無成」 | 哭台词；`discardRandomCard`(0x441E77) 随机丢 1 卡 → 0x4633AB「小衰神附身\n\n遺失%s！」 |
| 8 大衰神 | 547 | 0x4633C0「哈哈哈！真是好衰啊！」 | 哭台词；`discardHalfCards`(0x441ECE) 丢一半卡 → 0x4633D5「大衰神附身\n\n遺失一半卡片！」 |
| 9 天使 | 548 | 0x4633F0「哈雷路亞～…土地更加繁榮」 | 仅附身（三属性） |
| 10 惡魔 | 549 | 0x463419「與我同生共死…沿路破壞…」 | 仅附身（三属性） |
| 11 惡犬 | — | — | **不走 attachObject**（canAttach=0），见 §5 |
| 12 土地公 | 550 | 0x46344E「有土斯有財…要多少土地有多少」 | 仅附身（A=-500） |
| 15 死神 | 551 | 0x463495「嘿嘿嘿嘿…被我盯上…」 | 哭台词；`confiscateCards`(0x445B3F：没收玩家 13 类卡 → 前 8 类回礼物池 byte_497320，载具卡回道具库存 0x499160..) + `confiscateProps`(0x441F21：15 类道具 → 公共池 byte_499197)；寿命 13 |

插图资源 = Effect 段 `sub_450441(dword_48A0E4, 540..551)`（与 FLC 536/537 同空间）；旁白显示 `sub_40E2A2`。
**注意**：case 1 的「对其他玩家收钱」`sub_41D2C6(i, me, v, 1)` 第 4 参 = 方向（1=收款进我/0=付款出我，实测自 transferMoney 语义）。

## 5. onPlayerActionPhase 物件分派（0x41B42D；停留 = `!g_stepsRemaining`）

| 类型 | 触发条件 | 处理 |
|------|---------|------|
| 1..10,12 神明 | 玩家 停留 | `attachObject` |
| 11 惡犬 | 停留 | `deleteMapObject(11)`（→轮替**土地公**）；**无载具** → `damagePlayer`（载具报废+受伤+步行资源重载）+ 插图 532（0x463xxx 咬伤文本，`sub_45144F` 帧参数 (196609,93)）+ **住院 3 天**（`hospitalizePlayer(p,3)`）；有载具 → 插图 552（碾过）无事 |
| 13 禮物 | 停留 | `drawGiftCard`(0x445ADA：`byte_497320[0..7]` 8 类卡按库存展开随机抽 1) → `deleteMapObject(13)`（不轮替，月度重定位）→ 音效 dword_48237A → `"得到%s！"`（0x463AA8）→ `givePlayerCard` |
| 14 寶箱 | 停留 | 音效 dword_482382 → `deleteMapObject(14)` → 消息 0x463AD3「寶箱」→ **`playerPoints += 500`** → 大笑台词 |
| 16 路障 | **路过即触发**（无停留判定） | `deleteMapObject(16)`（库存+1）→ **`stepsRemaining = 0` 强制停止** → 哭台词（off_480D92） |
| 17 地雷 | 停留 | `deleteMapObject(17)` → `damagePlayer` → 爆炸插图 525（(196609,82)）→ 哭台词（off_480D96）→ 停止 → **住院 3** |
| 18 炸彈 | 停留 且 玩家无 cellTableIdx 挂件 | **挂身**：`cellTableIdx=objId`、+5=owner、**寿命=38**、旧格占用清 → 哭台词（off_480D9A）；（已挂/路过 → 不触发） |

**挂件每步处理**（分派 switch 之前，`cellTableIdx != 0` 时）：
- 寿命 -1；归零 →（炸弹）`deleteMapObject` + **若本格是地产 `demolishEstate(objId, 0)` 降一级** + `damagePlayer` + 爆炸图 525 + 停止 + **住院 5**；（路障等挂身但非路障/炸弹 → 同分支递减，见原版 v8 段）
- **同格转移**：本格存在其他玩家（cellEnt+36 高位玩家掩码）→ 挂件 `cellTableIdx`/+5/玩家归属 整体**转给该玩家**（停止专属音效 g_effectSlot3）

**事件槽 NPC（4..7）差异（✅ 2026-09-26 已实现，细节 `498df0-event-slot-npc.md` §2.6）**：
惡犬/地雷对 5/6/7 触发 FLC+住院；路障对 5/6/7 删+停步；**小偷(4)** 踩 禮物/寶箱/路障/地雷/炸彈 →
删物件 + 给 bailer 对应道具（2/3/4）/＋500 点券；同格行窃/抢银行/勒索见 `LABEL_88`（同文档 §2.7）。
**currentPlayer==8（機器娃娃虚拟槽，0x446AFB 道具 id1 设置）✅ 2026-09-27**：踩到物件 →
`bounceObject`（0x40FAFD 弹飞：设 f32 位置/速度+flyCount）+ `deleteMapObject`；收尾 case0 等
弹飞动画播完（`sub_40FAD6` 46 槽 flyCount 全 0）才交还回合。见 `item-effects.md` §1.6。
（旧标注"拆除卡虚拟工程车"为误名——拆除卡 12=0x443B0F，工程车=道具 12/0x4479D2。）

## 6. deleteMapObject 细节（0x40E14D）

```
slot = objId-1; type = cellTable[slot].type
type==16/17/18 → 对应库存 byte_497321+1/2/3；18 且 owner → 该玩家 cellTableIdx 清 0
type<17 且 owner(+5) → 该玩家 cellTableIdx=0 且 luckA/B/C -= 三表[type]（**还原属性**）
无 owner → cellEnts[+38] 占用清
清槽（cellEnt=0、life=0、owner=0）
slot<12 → 配对轮替重建（§2.1）
```
注：`byte_496BA7[104*p]=0` 在 16/18 owner 分支清（路障不占 +55？——实际删除时玩家 cellTableIdx 统一由 18 分支清；
神明/路障挂身时 +55 由 delete 的 owner 分支（`v5=52*(owner-1)` 段清 `byte_496BA7[v5*2]`）还原。**重写统一**：
`deleteMapObject` 里若 +5 有主 → 该玩家 cellTableIdx=0 + luckA/B/C 还原（炸弹爆炸动画例外已在 §5 归零分支处理）。

## 7. 渲染（`sub_40829D` 0x408C5E..0x4090AC 物件段）

条件 `cellEnt != 0 || flyCount != 0`：
1. **owner(+5) ≠ 0 且玩家无挂起状态**（stateFlags==0）→ **跟随绘制**：位置 = 玩家精灵投影 + 8 方向偏移表
   `g_attachOffX/Y[dir]`（`0x474951/0x474955`，值 x=[-10,-22,-22,-10,-22,+10,-10,+22] y=[-22,-22,-10,-22,+10,-10,+22,+10]）；
   炸弹(type18)且玩家已挂身 → 用 `0x474991/0x474995` 偏移（x=[-18,-44,-44,-18,-44,+18,-18,+44] y 同构放大），且 drawListId 置 `|0x40` →
   **头顶画寿命数字**（`"%d"` life，y-60，字色 2）；帧 = `(玩家dir + 8 - view) & 7`，再 `(+4)&7`（朝向翻转）
2. **flyCount ≠ 0** → 用 **f32 坐标**投影绘制，每帧 `pos += vel`、`flyCount--`；出视口 → flyCount=0 剔除；帧 = `(flyDir+8-view)&7`
3. 常规 → 逻辑格投影，帧 = `(dir+8-view)&7`
4. 图块 = `g_cellTableSprites[type]`（`dword_49692C` = data.mkf[type+395]），调色板 -1（原图），ID 高字节 = `k-127`（拾取/渲染排序键）

## 8. randomCellEnt（0x40AA6C）
候选条件：`(occMask & 0x80FFFF00) == 0`（无物件/玩家占用）且 `(exits[0] || exits[1])`（有路）；
参照格 `near>0` 时循环重选直到 `|Δx| ≥ 300 || |Δy| ≥ 300`（世界坐标，约 9 格）。

## 9. 数据/资源清单（重写常量）
- `g_luckTblA/B/C[19]` = 0x4749E2/0x474A06/0x474A2A（§3 表）
- 旁白文本（BIG5→UTF-8）：0x463250/463295/4632CC/4632FD/46330E/463353/46336C/463381/46338E/4633AB/4633C0/4633D5/4633F0/463419/46344E/463495；
  0x463AA8「得到%s！」、0x463AD3 宝箱消息、0x463AC0「%s偷到%s的%s」类 NPC 模板（P5）
- 插图资源号 540..551（类型 1,2,3,4,5,6,7,8,9,10,12,15）+ 525 爆炸 / 532 犬咬 / 552 车碾
- 音效：dword_48237A 礼物、dword_482382 宝箱、g_effectSlot3 炸弹挂身循环
- 台词表：off_4808A2（哭，神明离开/受伤通用）、off_48086A（大笑）、off_480862（暴富）、off_480D92/96/9A（路障/地雷/炸弹）

## 10. 请神符 / 送神符挂点（0x444E1A / 0x444C45，卡系统先行实现）

- **请神符（卡 id 23，0x444E1A）**：人类经 `pickNearestAttachable`(0x444D1A) —— 原版遍历
  **视口拾取表**（word_48B8C4），重写遍历渲染期 `mapHitRegions` 中 `0xA100|槽+1` 物件，
  过滤 `canAttach && owner==0 && cellEnt!=0`，取距玩家 cellEnt 世界坐标最近者 →
  `cardBagRemove(cur,23)` 丢卡 →（请神台词 off_481292 → [TODO P4]）→
  `flyObjectSprite`（神明格 → 玩家屏幕位置）→ `attachObject`（完整附身效果）。
  无候选静默失败不丢卡。AI 选择 0x41E6F2 → [TODO P5]。
  原版细节：动画前 `word_496D0A=0` 临时摘除神明 cellEnt（防飞行途中原地残影），动画后还原
  再 attach；`sub_40E669` 插值**自起点向终点**、投影越界按 (0,0) 处理（非跳过动画）。
  **时序差异（2026-09-24 实机核对后修正）**：原版 0x444E1A 顺序 = 新神飞行 → attach（此时内部
  才送走旧神）→ 旧神飘走期间新神贴图已还原大地图（残留观感）；重写改为
  **先送走旧神（attachEnd 飘走+轮替）→ 新神再飞身靠近 → 附身**。
- **送神符（卡 id 22，0x444C45）**：`cellNo`（挂身炸弹）→ `deleteMapObject` 直接清除；
  附身神明且类型 ∈ {5,6,7,8,10,15}（窮/衰/惡魔/死神 = **负面神**）→ `attachEndAnim` 送走
  （→配对轮替）；**正面神（財/福/天使/土地公）不可送走**、两者皆无 → 静默失败不丢卡；
  成功 → `cardBagRemove(cur,22)`。
- **重写位置**：`src/app/map_objects.cpp` `useInviteGodCard` / `useBanishGodCard` /
  `pickNearestAttachable`；调试入口 `Ctrl+Shift+I` / `Ctrl+Shift+H`（先 giveCardToBag 塞卡）。
- **拾取 id 修正**：原 cellTable 物件 hit id 直译 `(slot+1)<<8` 与 estate/corp/specPt 十进制段
  重叠（slot 7..30 会被误判）→ 改 `0xA100 | (slot+1)`（map_render/object_tip 同步；
  原版实为 `(slot<<8)+33024` 高地址段无冲突）。
- **卡使用外层**（手牌面板/0x443052 dispatch、点卡确认）属 P4 卡片 UI；本函数即其效果挂点。

## 11. 与重写现状差距（2026-09-23 分析基线）
- ✅ createMapObject / randomCellEnt / 月度换位（advanceDay）/ cellTableIdx 字段 / 整表存档 / 拾取提示 / 静态帧渲染
- ❌ deleteMapObject+轮替、attachObject、attachEnd 动画、寿命双入口递减、路过/停留分派（恶犬/礼物/宝箱/路障/地雷/炸弹）、
  8 方向帧、跟随绘制、浮空寿命数字、弹飞、giftPool/propStock/luckABC 字段与存档
- [TODO P4] 道具放置入口（卡片使用）、research 室道具；**機器娃娃槽8 ✅ 2026-09-27**（`item-effects.md` §1.6）
- [x] **事件槽 NPC 4..7 物件分支 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）；AI 使用三属性决策（sub_41FACC 等）待办

## 12. 验证方式
新开局：6 神明（小財神/小福神/小窮神/小衰神/天使/惡犬）+礼物+宝箱。
踩小財神 → 插图+老虎机 → 其他玩家钱转我 → 小財神悬浮头顶 → 7 回合后飘走 → **大財神出现在远处**；
步行踩惡犬 → 咬伤插图 → 住院 3 天 → 对向槽生成**土地公**；有车踩 → 无事；
踩礼物 → 随机得 1 卡；踩宝箱 → +500 点券；每月 1 号礼物/宝箱换位；
地雷（Debug 放置）→ 爆炸拆建筑+住院 3；炸彈挂身 38 → 头顶数字递减（炸弹偏移）→ 爆炸住院 5；存档重载一致。

## 13. 神明持续效果：landAfterMove + 福神追加 + 死神代付（2026-09-25）

### 13.1 `sub_40F381` landAfterMove（落地公共末尾，重写 `turn_system.cpp landAfterMove`）

**调用时机**（精确实测）：`landingEvent` 公共末尾 `loc_41B077`（0x41B077）——**estate/corp/specPt
各结算路径（含买地失败/拒绝/守卫等所有 `jmp loc_41B074` 汇聚点）在事件处理完毕后统一执行一次**；
事件格 case（4/5/10..14 等）直接 `jmp loc_41B111` **不经过**。另一补充调用点 =
`sub_418EBD+0x9C`（回合尾、`alive & 0x30` 临时载具/旅館走出分支）→ **✅ 2026-09-27 接入**
（`nextPlayer`：重绘 → 恢复朝向 `byte27` → `landAfterMove` → `landingSpecialMove`(0x448A7E) →
`alive &= 0x0F`）。

前置守卫：`stateFlags.BYTE0(住宿)==0` 且 `alive != 0`。按附身字段（slot+1）分派，仅 3 个神明有动作：

| idx | 神明 | 效果（objId=cellEnt+32） |
|-----|------|--------------------------|
| 9 | 天使 | `sub_40B110` **免费建设/加盖**；corp 无设施先 showMessage("天使")+2048 抑制重复；成功→消息 sprintf(aS_7 0x4634C0) **"天使顯靈\n\n加蓋一層房屋！"** +音效槽18 [dword_4823DA]；bit7 封顶→FLC 523（`sub_40B0CD`：523 @ (0,40) 音效90，前置 sub_40829D(-1,0) 全屏重绘不迁移）。**显示时序**：进入 `refreshGameUi(玩家坐标,0)`（0x40F427 视口对准）→ angelUpgrade 改内存 → 消息叠加 → 音效后 `refreshGameUi(0,0,1)`（0x40F506）加盖建筑出场（2026-09-26 重写补两次重绘） |
| 10 | 惡魔 | estate.level/corp.sub 非 0 → 有主记债 `addPlayerDebt(owner−1, 我, 30×M)`（**原版不排除自己**，自欠被 addPlayerDebt 跳过）→ 消息(0x4634D7 "小惡魔顯靈\n\n拆毀一層房屋！") → `demolishEstate(objId,0)` → **FLC 526 @ (建筑屏幕坐标−55,−55) 音效 95**（sub_40B066 取 obj 屏幕坐标） |
| 12 | 土地公 | 占为本有：owner==自己→无动作；他人→记债 `M×(estate+28/corp+34)×(level+2.0)/5.0`（flt_46350C=2.0/flt_463510=5.0）；**原无主且 cfgLandPerm → 记到期日**（addPackedDate，购地同款）；`owner=我+1` + rebuildMiniMap + 消息(0x4634F2 "土地公顯靈\n\n強佔土地！")；大笑台词 sub_44EF41 [TODO P4] |

其余附身值（含死神 15/窮衰神等）在 landAfterMove **无持续动作**。

### 13.2 `sub_40B110` angelUpgrade（天使/福神共用的免费建设执行）

- estate：`type==0 && level<5` **或** `type==1(连锁店) && level==0` → `++level`（==5 附 bit7=0x80）
- corp：`sub!=0 && sub<kFacilityMaxLevel[type]` → `++sub`（旧值 4 → bit7）；
  `sub==0` → **直接建设施（免费！）**：AI（alive&6）`rand%4+1` 随机；人类**弹 selectFacilityDialog(0)**
  （原版不检查取消，返回值直接用；重写取消回落 0=公園）
- 返回 bit0=有建设 / bit7=封顶（调用方播 523 动画）

### 13.3 `sub_40F8BE` godBlessUpgrade（福神盖房加倍，[HELP 43/48]）

**普通付费升级/建设施/买地成功后**追加一次免费 `sub_40B110`（即"升一级变两级"）。
调用点 = landingEvent **五条**成功路径（全部 `jmp loc_419A48`）：estate `++level` 未满 5
（0x419A39→0x419A48，跳过 estateChainSpeech 后）、corp 建设施成功（0x41A2AE）、corp 升级
未满 5（0x41A36B）、**estate 买地成功（0x41A154，扣现金+estateChainSpeech 后）**、
**corp 买地成功（0x41A993，扣现金后）**——买地即免费加盖一层（estate 0→1）/ 无设施
corp 直接建设施（**2026-09-26 实机补：原先只实现三条付费路径，买地两处漏掉**）。
满级(==5)路径不追加（走台词+523 分支）。消息 = sprintf(aS_7 0x4634C0, kObjectNames[idx]) **"小/大福神顯靈\n\n加蓋一層房屋！"**；
bit7 → 台词 off_480886[角色]（列15、expr0）**✅ P4-C** + FLC 523；未封顶 → 0x40FA49 跳
attachObject 共享块（0x40ECDE）**列8、expr3** **✅ P4-C**（勘误：旧记 off_48084A 随机有误）。
**注**：帮助"买地免费"= 买地**附赠免费加盖**（价格照扣 0x41A132/0x41A984），非价格归零；
代码中无福神价格减免（checkCarriedGod 0x40FA61 仅守卫 7/8/15，阻止时消息 aS_14 0x463514 =
  **"%s顯靈\n\n投資失敗！"**）。
**显示时序**（2026-09-26 实机修）：进入 `refreshGameUi(0,0,1)`（0x40F8EE）先重绘**即时状态**
（买地=空地+购买人棋子标记且 level 仍 0 / 付费升级=升级 1 级后建筑）→ angelUpgrade 改内存
→ 消息叠加在此画面上（showMessage 不重绘场景）→ 消息后 `refreshGameUi(0,0,1)`（0x40F9EF）
重绘 → **加盖建筑此时才出场**。天使 `landAfterMove` 同构（0x40F427 进入/0x40F506 音效后）。

### 13.4 死神代付 `sub_40FBB8` findDeathGodCarrier（[HELP 49/51]，pricing-formulas §6 步骤 6 落地）

收费金额定案（神明调整之后）→ 转账前，三处同构（住宅過路費 0x419ECC / 商業收費 0x41A6A4 /
行業設施收費 0x41AF99）：找**付款人外、存活、附身 idx∈{14,15}** 的首位玩家
（初版表 slot13=寶箱→14、slot14=死神→15——**原版条件即包含寶箱携带者**，照抄）；
命中 → showMessage("死神顯靈\n\n由%s賠償%s"(0x4639CC), 1500) → **付款人替换为代付者**。
重写 `resolveFeePayer`（三处收费管线 transferMoney 前）。原版此前还有嫁禍卡(19)/免費卡(20)
判定（sub_4413AD/sub_44476A/sub_444A60）→ P4。

### 13.5 欠款矩阵 `addPlayerDebt`（0x40DF69）

`playerDebt[creditor][debtor] += amount`（clamp ≥0；creditor==debtor 或负数无旧债跳过；
a3>0 且 creditor 同盟==debtor+1 → 解除同盟 sub_40CC1A：清双方 ally/+61 allyActive）。
**不直接扣现金**；消费方 = 破产 `sub_40CD87`（0x40CF63：**清破产者欠款列 `[k][p]=0` ✅
2026-09-27**，原版不做清偿转账）与最大债主查询 `sub_40D2D3`（返回 a1 欠最多者，AI 嫁祸/
催收用）；不入存档（快照 sub_44808A 无本表）。写法（欠款矩阵）均 ✅ 2026-09-27 接入：
恶魔/土地公（0x40F56A/0x40F7EA）、住宅收租（0x419DB1/DD8）、商業用地收费（0x41A5C0）、
住宿 20×M×days（0x41A7BC）、保释/出院返还 -100×M×days（0x43CF4D/0x43DE8D）、case3 30×M
（0x40DB35，收租 case3 时）。

### 13.5b 场景触发帧序（0x40D932 移动状态 case1，2026-09-25 实机修复）

原版每帧：`byte_48BB00`（一步到达标志）置位 → **先** `onPlayerActionPhase`（0x40D942）→
**后**判 `g_stepsRemaining==0`（0x40D947）决定继续 moveOneStep 或收尾转状态 3。
即**最后一步的停留处理（stopped=true）发生在移动收尾之前**。重写曾把
`remainingSteps==0 → 转状态3 break` 放在 pending 处理前面，导致所有停留类场景触发
（神明 attachObject/惡犬/禮物/寶箱/地雷/炸彈挂身）在落地格**整帧被跳过**（路障类路过
触发在中途步仍正常，易误判为"机制未实现"）。修复 = `turn_system.cpp` case1 顺序对调。

### 13.5c 物件消失动画的两处时序（2026-09-24 实机修正）

1. **删除 → 先刷新再弹消息**：原版礼物（0x41B938）/寶箱（0x41BB33）在
   `deleteMapObject` 与 `showMessage` 之间有 `refreshGameUi(0,0,1)`（全图重绘）——
   即时模式重写 `showMessage` 是往**当前 surface** 上画提示并阻塞 1500ms（原版 DDraw
   表面保留同款语义），不显式重绘就会出现"物件还留在画面上、消息先弹、消息结束后物件
   才消失"。修复 = case13/14 release 后 `renderGameFrame+renderFrame` 再 showMessage。
   恶犬/地雷走 playEventFlc（内部逐帧 renderGameFrame）天然正确；路障/炸弹无消息，
   下一帧主循环重绘等价。
2. **送神先消失再飞天**：原版 attachEnd（0x40E32C）动画前 `cellTable.ent = 0` +
   `sub_40829D(-1,0)` 全量重绘让挂身精灵**先从场景消失**，24 帧飘走为 DirectDraw 直接
   blit（不进绘制列表）。重写渲染"跟随玩家"分支原条件仅 `owner!=0`（跟 spriteX 画，
   不看 ent）→ 动画期间神明仍贴玩家。修复 = 跟随分支补 `cellEntId != 0` 门槛
   （map_render.cpp；fly≠0 弹飞分支在其后，飞行动画 ent 已清走弹飞分支不受影响，
   请神/送神卡飞行动画已核对）。

### 13.5d 老虎机滚动音 + 住院/入狱过渡动画时序（2026-09-24 实机修正）

1. **老虎机滚动音 = 单次循环，非每帧重播**：原版 `slotMachineValue` 进 do 循环前
   `audioPlayEffect(dword_475D3C, a2=1)`（[RE 0x43F2A6]，a2=DSBPLAY_LOOPING）播一次循环，
   case8 金额定格时 `audioStopEffect`（[RE 0x43F5F0]）停，对话框退出（0x4408BF）兜底再停。
   重写曾每 timer tick 调 `playEffect`（追加新声部）→ 多声部叠加成轰鸣。修复：Audio 加
   `playEffectLooping(id)`（Voice.looping，update 回卷不清除）+ `stopEffect(id)`（按 Effect.mkf
   索引移除声部）；`slotMachineDialog` 进入播一次循环、case8 停、退出兜底停。Effect 51=dword_475D3C[0]。
2. **住院 524 / 入狱 538 过渡动画（完整版，2026-09-24 第二轮修正）**：524=440×74 横条叙事带
   （救护车+抬人，62 帧）、538=440×440 押解带（35 帧）。原版三件套：
   ① `hospitalizePlayer/jailPlayer`：refreshGameUi(旧位置) → 传送 → 动画期间**地图表面不重绘**
     （DDraw 保留=角色显示在旧格）；
   ② `flcOpen` flags **BYTE2 = 场景切换帧**（dword_48C85C，[RE 0x450CED/0x450F04]）：
     `flcDecodeFrame` 解到第 N 帧 → 保存 FLC 区域 → `sub_40829D(-1,0)` **全量重绘** → 回写
     当前帧 → 后续帧基于该基准继续。**关键：`sub_40829D(-1,0)` 的 -1 = 用上次绘制视口
     （dword_48B2AC/48B2B0）——切换帧视口不随传送后的玩家位置重居中（原地只重建绘制列表：
     角色消失/建筑降级），动画播完返回主循环后视口才切到医院/监狱**（hospitalize/jail 末尾
     refreshGameUi(新位置,0) 仅置跟随标志）。524=0x1E0001→帧30（抬进医院）、538=0x120001→
     帧18（警车到中间）、525/526/532=帧3、552=帧1、神明插图/523/536/537=0（不切换）；
     重写 playEventFlc 切换帧以临时 manualView+viewSmooth=动画开始前 st.viewX/viewY 锁视口
     全量重绘 → 更新回卷基准；
   ③ 重绘后角色消失靠**棋子守卫** [RE 0x4086A1]：`spriteX && (!stateFlags || alive&0x20)` 才
     绘制（住院 BYTE3/入狱 BYTE2/住宿/出國全隐藏；0x20=走进旅館步行中例外，走出启动前
     stateFlags 已清）。重写曾只查 BYTE0 → 住院/入狱角色不消失。
   重写实现：`playEventFlc(switchFrame=N)`（保帧快照+第 N 帧 renderGameFrame 切换）+
   `map_render` 守卫收紧 + gameEventHandler timer `eventFlcActive` 冻结守卫。
   `jailPlayer`（538 押解带 @ (0,40)）完全对称。重写 `playEventFlc` 曾每帧 `renderGameFrame`
   （按已传送坐标重居中）→ 动画一开始就跳到医院/监狱。修复：`playEventFlc` 加 `preserveScene`
   参数（不重绘、每帧回卷带区域背景 + `overlayEventFlcFrame` 透明叠加，对应原版 saveBackground）；
   且 `gameEventHandler` timer 分支加 `eventFlcActive` 提前返回守卫（原版 flcPlay 的 PeekMessage
   只取不派发，动画期冻结状态机/视口）。住院/入狱调用点 `playEventFlc(..., preserveScene=true)`。

### 13.6 踩坑：IDA 字符串视图对 BIG5 格式串截断（2026-09-25）

`0x4634C0` 在 IDA 反编译里显示为 `aS_7 = "%s"`，**实际字节**（cp950 解码）为
`"%s顯靈\n\n加蓋一層房屋！"`；`0x463514` 同理（显示 `"%s"`，实为 `"%s顯靈\n\n投資失敗！"`）。
BIG5 双字节字符第二字节可落在 `\x5C`(反斜杠) 等干扰 IDA 字符串解析的位置。
**规则：移植 sprintf 消息文本时，对 IDA 显示的短格式串（尤其裸 `"%s"`）必须用
`python: b[va-0x401A00:..].decode("cp950")` 读原始字节复核**，否则会丢失"顯靈

…"后半段。

### 13.7 checkCarriedGod（0x40FA61）重写接入点

五处付费守卫（estate 购地 0x41A013 / estate 升级 0x4199AE / corp 购地 0x41A86B /
建设施 0x41A261 / 设施升级）原版统一调 0x40FA61：附身 idx 7/8/15（小衰神/大衰神/死神）→
showMessage(sprintf aS_14 = "%s顯靈\n\n投資失敗！", 1500) → 返回阻止。重写同名 helper
`checkCarriedGod(app, st, carried)`（turn_system.cpp）。

## 14. flag 蒙膜渲染（0x456C33，2026-09-27 地图专项②补）

原版 `sub_40829D` estate/corp 收集循环内：`flag(+23/+28) != 0` →
`sub_456C33(backbuffer, 12*(v138&1)+g_pickMask+12, sx, sy, flag&1)`——
按 g_pickMask 形状（estate 帧 `(8-(rot+dir))&1`、corp `+2`）非透明像素
`屏幕像素 |= 颜色`（OR 出半透明色彩蒙膜，区别于 0x456384 整色绘制）。

- 颜色 = `word_488EF0[4*(flag&1)+色深]`（RGB555 色深列 0）：
  - `flag&1=0`（涨价卡 flag=0x50）→ **0x7C00 红**
  - `flag&1=1`（查封卡 flag=0x51）→ **0x003F 蓝**
- 图层：地砖/cellEnt 图块之上、物件绘制列表之下（原版在排序前绘制）
- 重写：`map_render.cpp orColorMask`（匿名 ns）+ renderMap 内 estate/corp flag 循环；
  裁剪 = 全局裁剪矩形（地图区，与原版 dword_4861B8..C4 一致）
