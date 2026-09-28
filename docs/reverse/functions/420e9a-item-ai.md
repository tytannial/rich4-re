# 0x420E9A 道具 AI 用道具链（itemPanelFlow AI 分支）

> 与卡片 AI 链（`441baa-card-effects.md` §AI）同构：性格判定 + 每道具目标函数表 +
> 预选目标全局。实现后 AI/托管玩家会在回合开始自动使用道具（`aiCardItem` bit1）。
> 原版依据：`0x420E9A`（判定入口）、`funcs_420EE6` @ `0x4753A0`（13 项目标函数表）、
> `0x48BE64`（预选目标数组）、`0x47FEE1`（=`kItemAiPersona`，g_itemNames 结构 +7）。

## 1. 结构与数据

| 项 | 地址 | 语义 |
|----|------|------|
| 判定入口 | `0x420E9A` | `persona = byte_47FEE1[8*id] - aiPersonality`；≥2 → 0；==1 → 1/3 概率；否则 `funcs_420EE6[id]()` |
| 性格表 | `0x47FEE1` | = `kItemAiPersona[14]`（重写已有，`map_tables.h`；百货/交易 AI 同用） |
| 目标函数表 | `0x4753A0` | 13 项（id1..13），下表 |
| 预选目标 | `0x48BE64` | `sub_420EEE(idx)` 读取；**至少 [0]**（编码随道具而异） |
| 调用者 | `0x418E18` | `beginPlayerTurn` case 2/5：`rand()&1` → 卡（`0x441BAA`）或道具（`0x447D97`） |

### 1.1 四种候选域采集器（**距离约束的来源**，2026-09-28 专项修正关键）

| 采集器 | 地址 | word_48B8B4（路径）/ word_48B8C4（列表） | 内容 | 使用者 |
|--------|------|------------------------------------------|------|--------|
| 前方路径 | `sub_40B221(p,n)` | **从当前格**沿出口前进 n 步（排除来路 prev/排除 occMask bit30-33 出口；多出口随机并置返回位=有分叉；死路折回 prev 继续） | 格 id | 娃娃/路障主/遙控骰子（均要求返回 0=**路径唯一**才赌） |
| 回溯路径 | `sub_40B343(p,n)` | **从上一格**前进 n 步（排除当前格），死路**截断**（后续=0） | 格 id | 地雷/炸彈/路障兜底 |
| 视口格列表 | `sub_409EF9` | — | `word_48B8C4` = 以当前视口中心重建拾取缓冲采集的**纯格 id**（occMask bit8-23 干净才登记，无对象覆盖） | 地雷/炸彈/路障兜底（与回溯路径取**交集**）/ 传送机 |
| 视口实体列表 | `sub_40A45C(-1)`（=渲染拾取缓冲全 440×440 采集，半径版=中心±r） | — | **混合编码**：格 id / 有主地块对象 id（2000+/4000+）/ 玩家 `0x80\|bit` / 槽 `0x80\|slot<<8` | 機器工人（对象 id）/ 飛彈（找债主玩家位） |
| 中心采集 | `sub_40A0B1(x,y,r)` | — | **以任意世界坐标为中心重投影**采集：玩家位掩码 + 有主 estate/corp（**无格、无槽**）；r=-1 → 全画布（±14 格） | 飛彈溅射（r=100）/ 核弹评估（r=-1） |

> 教训（2026-09-28 实机反馈"AI 无视距离放道具/用了没效果"根因）：初版把全部候选域
> 简化成"视口 `mapHitRegions` 任意格"，丢失了 sub_40B221/343 的**路径距离约束**与
> 骰子的**点数=路径索引**语义（→ 落不到目标）。凡"目标预选"类 AI，先查候选域采集器。

## 2. 逐道具 AI 目标函数（`funcs_420EE6`，全部反编译核实）

| id | 道具 | 函数 | 候选域 + 策略 | 输出 `dword_48BE64[0]` |
|----|------|------|---------------|------------------------|
| 1 | 機器娃娃 | `0x420EFA` | 前方 4 步（**无分叉**）：路径格挂身槽 type∈{5,6,7,8,11} ∨「17地雷∧自家地」∨「16路障∧他人地∧同段租金>3000×M」 | —（无目标） |
| 2 | 路障 | `0x42107F` | 主：前方 4 步（无分叉）空格——estate 无主∧（同段自己≥2 或 level≠0）∧`M×priceAdd<cash`、corp 无主∧`M×buildPrice<cash`、普通格 type==15∧points>200；门槛组 bank+cash>10000∧luckB≥0∧fixedStep==0；兜底：回溯 6 步∩视口 ∧自家 estate 租金>6000×M 最高 | 格 id |
| 3 | 地雷 | `0x4213C5` | 回溯 6 步∩视口格 ∩ **他人有主产业** → 随机；交集中遇監獄/醫院格（有在押）直接选 | 格 id |
| 4 | 定時炸彈 | `0x421574` | 回溯 6 步∩视口 **任意格** → 随机（**不限他人产业**；监狱/医院分支同地雷） | 格 id |
| 5 | 機車 | `0x421644` | 无载具（`travel&3==0`）时 25% | — |
| 6 | 汽車 | `0x421675` | 载具 <2（步行/機車）时 25% | — |
| 7 | 飛彈 | `0x421717` | 债主（无则随机）须在 `sub_40A45C(-1)` 视口实体列表（**屏幕可见**）；`sub_40A0B1(债主,100)` 溅射区含任意玩家 ∨ 自家产业（`sub_4216AB`）→ 放弃 | 债主格 id |
| 8 | 遙控骰子 | `0x421827` | 入口拒绝：附身 7/8/15 ∨ fixedStep ∨ bank+cash<10000 ∨ luckB<0；前方 6 步（**有分叉直接放弃**），逐格查无神明/陷阱槽 ∧ **无主** estate（同段自己>1 ∧ `2.5×M×priceAdd<cash`）/ corp（`2.5×M×buildPrice<cash`）→ 命中；自家可加盖分支不置 found（原版死码） | **路径索引+1（=骰子点数，保证恰好落到该格）** |
| 9 | 機器工人 | `0x421BA6` | 视口实体列表中自家（estate type0 level<5 / corp sub<上限）收益 `fees[level]`/`feeTable[sub]` 最高 | 对象 id |
| 10 | 時光機 | `0x420EDF` | **AI 不用**（`xor eax,eax; retn`） | — |
| 11 | 傳送機 | `0x421CB6` | 视口**无主**地（estate type0 level≥3 / corp type≠0 sub≥3）且 `M×price < cash`，取 level 最高；门槛（bank+cash>10000∧luckB≥0）在遍历后判 | 格 id |
| 12 | 工程車 | `0x421E20` | 非工程車时 `rand()%15 <= aiPersonality` | — |
| 13 | 核子飛彈 | `0x421E62` | 他人**有建筑**地（level/sub≠0，**非** type≠0）→ 随机 ≤10 次：`sub_40A0B1(obj,-1)` 全画布采集——含玩家放弃；自家块数/他人块数 ∧ 自家Σlevel/他人Σlevel **均 < 1/(存活玩家数+2)**（`sub_40D2B4+2`）才命中（**不炸自家圈**保护） | objId |

常量：`dbl_463D48 = 2.5`（骰子/路障成本倍数）、`dbl_463D40 = 1.5`（烏龜，卡片侧已修）、
`dbl_463D38 = 0.682`（涨价占比，卡片侧）。

## 3. 重写现状（2026-09-28 修正后）

- `ai_item.cpp`：`aiItemSelect` = persona 判定 + 上表 13 分支；
  - 候选域 helper：`predictPathFwd` [RE 0x40B221]（出参 hasBranch）、`predictPathBack`
    [RE 0x40B343]、`viewportCells` [RE 0x409EF9]、`collectAround` [RE 0x40A0B1]（以
    mapHitRegions 屏幕 anchor 差为距离，**与 `expireAssets` 同口径** → "AI 评估范围=
    实际爆炸范围"；目标须在当前视口，见 §5）。
  - 门槛组 `aiSpendOk`；`isOwnEstate` [RE 0x4216AB]；`alivePlayerCount` [RE 0x40D2B4]。
  - 日志：`aiItemSelect: p%d item=%d target=%d curCell=%u`。
- `item_bag_dialog.cpp` AI 分支 [RE 0x447D97]：手持道具（**排除 id10 時光機**）随机起点、
  **最多试 4 件**、回绕；命中 → `showMessage("使用%s")` → `itemEffect`（每回合 ≤1 件）。
  洗牌/起点用 `dbg::roll(SlotAi)`（可测）。
- `item_effects.cpp` `useItemWorker` [RE 0x446AFB]：**无 alive 守卫**（原版 AI/托管同样
  放娃娃：takePlayerItem→台词→槽8 记录→currentPlayer=8→startPlayerMove 9 步）。
  `placeMapItem`/`useItemRobotWorker`/`launchMissile`/`useItemTeleport`/`useItemRemoteDice`
  AI 分支读 `st.aiItemTarget`（= `dword_48BE64[0]`，不入存档）。
- `GameState.aiItemTarget`（对应 `dword_48BE64`，不入存档）。
- `kItemAiPersona[13]`（核子飛彈）= **1**（2026-09-28 修正：原误录 2 → AI 几乎不用核弹；
  对照 `byte_47FEE1` stride-8 dump offset 0 字节全表复核 id1..13 = 0,1,1,1,0,0,2,0,1,2,1,2,1）。

## 4. 实现进度

1. 2026-09-26 初版：persona + 13 分支骨架 + AI 预选接入效果链（候选域简化为视口）。
2. **2026-09-28 距离语义修正**：§1.1 四采集器 + 骰子点数索引 + 地雷/炸彈拆分
   （`tgtMine`/`tgtBomb`）+ 娃娃 AI 解锁 + 飞弹/核弹保护 + persona[13] + AI 分支 ≤4 件。
   顺带：烏龜 `dbl_463D40=1.5`（`ai_card.cpp`）。

## 5. 差异与待查

- `collectAround` 以**当前视口**的 mapHitRegions anchor 差近似原版"以目标为中心重投影"：
  目标不在视口 → 返回 -1（该候选放弃）。飛弹本就要求债主在视口（原版同）；核弹原版可评估
  视口外目标（重投影全画布），重写仅评估视口内候选——AI 核弹使用率略低，可接受。
- 原版核弹 r=-1 采集 ±14 格画布（投影菱形），重写 = 视口全部实体记录，范围≈。
- `dword_48BE64` 槽数：确认仅 [0]（目标函数无一写 [1]）。
- 道具台词（`kItemLines`）已实现（`playItemLine`），AI 使用同样播放（原版同）。

## 6. 验收

- headless：`--seed 7 --quickstart 0,4,0 --script tests/scenarios/260_ai_items.txt`
  → `wait log "aiItemSelect"` PASS；放置类 target 为玩家回溯路线∩视口小格号。
- `80_autoplay_60d`：AI 使用炸彈/路障/機器娃娃（`itemWorker: p0 -> slot8` = AI 娃娃生效）、
  errors==0。
- 实机：托管/电脑回合（`aiCardItem` bit1 开）观察——地雷/炸彈只落在 AI 自己即将经过的
  路线附近；遙控骰子点数使其**恰好**停在无主可购地；核弹不炸自家密集区；飛弹债主不在
  屏幕内不发。
- 关闭「使用道具」开关后不再使用；与卡片链同场验证（`rand()&1` 二者互斥）。
