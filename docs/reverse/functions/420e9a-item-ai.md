# 0x420E9A 道具 AI 用道具链（itemPanelFlow AI 分支）

> 与卡片 AI 链（`441baa-card-effects.md` §AI）完全同构：性格判定 + 每道具目标函数表 +
> 预选目标全局。实现后 AI/托管玩家会在回合开始自动使用道具（`aiCardItem` bit1）。
> 原版依据：`0x420E9A`（判定入口）、`funcs_420EE6` @ `0x4753A0`（13 项目标函数表）、
> `0x48BE64`（预选目标数组）、`0x47FEE1`（=`kItemAiPersona`）。

## 1. 结构与数据

| 项 | 地址 | 语义 |
|----|------|------|
| 判定入口 | `0x420E9A` | `persona = byte_47FEE1[8*id] - aiPersonality`；≥2 → 0；==1 → 1/3 概率；否则 `funcs_420EE6[id]()` |
| 性格表 | `0x47FEE1` | = `kItemAiPersona[14]`（重写已有，`map_tables.h`；百货 AI 同用） |
| 目标函数表 | `0x4753A0` | 13 项（id1..13），下表 |
| 预选目标 | `0x48BE64` | `sub_420EEE(idx)` 读取；**至少 [0]**（1 格/对象编码随道具而异） |
| 调用者 | `0x418E18` | `beginPlayerTurn` case 2/5：`rand()&1` → 卡（`0x441BAA`）或道具（`0x447D97`） |
| 视口列表 | `sub_40A45C(-1)` / `word_48B8C4` | 视口拾取（重写 → `mapHitRegions`） |
| 候选列表 | `sub_40B221(p,mode)` / `word_48B8B4`（6 项） | 按类筛选的格列表（重写按各卡过滤条件近似） |

## 2. 逐道具 AI 目标函数（`funcs_420EE6`，全部反编译核实）

| id | 道具 | 函数 | 策略 | 输出 `dword_48BE64[0]` |
|----|------|------|------|------------------------|
| 1 | 機器娃娃 | `0x420EFA` | 视口有负面挂身物件（cellTable 类型 5/6/7/8/11）或「他人地雷」或「他人地+路障且同段租金>3000×M」 | —（无目标） |
| 2 | 路障 | `0x42107F` | 视口空无主地：estate（同段自己≥2 块或无建筑）且 `M×priceAdd < cash`、corp `M×buildPrice < cash`；或 cellEnt 类型 15（?) 且点券>200；否则自己同段≥2 块、租金>6000×M 的最高租格 | 格 id |
| 3 | 地雷 | `0x4213C5` | 视口∩候选：他人有主 estate/corp 收集 → 随机；或監獄/醫院格（有在押） | 格 id |
| 4 | 定時炸彈 | `0x421574` | 同地雷（他人产业/特殊格 → 共享尾随机） | 格 id |
| 5 | 機車 | `0x421644` | 无载具（`travel&3==0`）时 25% | — |
| 6 | 汽車 | `0x421675` | 载具 <2（步行/機車）时 25% | — |
| 7 | 飛彈 | `0x421717` | 目标玩家=债主（无则随机存活）；在其可见玩家格中选该玩家格；`sub_40A0B1` 对准 + 溅射区无自己人 | 格 id |
| 8 | 遙控骰子 | `0x421827` | 视口 6 格（无神明/陷阱）：无主地（自己同段>1 块）且 `2.5×价格 < cash`；自己可升级地取最高 level | **格索引+1** |
| 9 | 機器工人 | `0x421BA6` | 自己（estate type0 level<5 / corp sub<上限）中收益 `fees[level]`/`feeTable[sub]` 最高 | 格 id |
| 10 | 時光機 | `0x420EDF` | **AI 不用**（`xor eax,eax; retn`） | — |
| 11 | 傳送機 | `0x421CB6` | 视口**无主**地（estate type0 level≥3 / corp type≠0 sub≥3）且 `M×price < cash`，取 level 最高 | 格 id |
| 12 | 工程車 | `0x421E20` | 非工程車时 `rand()%15 <= aiPersonality` | — |
| 13 | 核子飛彈 | `0x421E62` | 全体他人有建筑地收集 → 随机试 ≤10 次：3×3 内无玩家掩码、自己/他人等级比例均 < `1/(玩家数+2)` | objId |

常量：`dbl_463D48 = 2.5`（骰子/路障成本倍数）、`dbl_463D40 = 1.5`（烏龜，卡片侧已修）、
`dbl_463D38 = 0.682`（涨价占比，卡片侧）。

## 3. 重写现状

- `itemBagDialog`（`item_bag_dialog.cpp:178`）：**AI 直接 return**（`alive != 1`）——待加 AI 分支。
- `item_effects.cpp` AI 预选 stub（`v = 0; // [RE 0x420EEE]（P5）`）：**6 处**（行 263 路障 /
  295 地雷? / 350 炸彈? / 651 傳送機 / 743 工程車 / 890 遙控骰子）；行 535（传送自己）已实现。
- `kItemAiPersona` 已有；`GameState` 需加 `aiItemTarget`（对应 `dword_48BE64`）。

## 4. 实现计划

1. **`ai_item.cpp/.h`**（与 `ai_card.cpp` 并列）：`aiItemSelect(app, itemId)` = persona 判定 +
   上表 13 分支（输出 `st.aiItemTarget`；遥控骰子语义 = 格索引+1）；复用 `visibleOpponents/
   mapHitRegions` 等 helper（可将二者公共 helper 提到共享头）。
2. **`itemBagDialog` AI 分支**：`alive&6` + `aiCardItem&2` → 遍历手持道具 1..13（随机序）→
   `aiItemSelect` → `showMessage("使用%s")` + `itemEffect`（原版每回合最多一个）。
3. **`item_effects.cpp` 6 处 stub 接入** `st.aiItemTarget`（各自按原版编码解码：格 id / 索引+1）。
4. **`GameState.aiItemTarget`**（不入存档，原版同）。
5. 编译 + `re_map.py` + 文档（本文档 + `441baa-card-effects.md` 交叉引用、help-checklist
   idx18/87..99、AGENTS/m2-plan）+ 提交。

## 5. 差异与待查

- `word_48B8B4`（`sub_40B221` 筛选列表）语义未细究——重写按各道具的过滤条件直接在
  `mapHitRegions` 上近似（逐道具注释）。
- 飛彈/核彈的 `sub_40A0B1`（世界坐标为中心收集 3×3 格）→ 重写按世界坐标距离过滤。
- `dword_48BE64` 槽数：已确认 [0]；傳送機等双目标道具的 AI 是否用 [1] 未在目标函数中出现
  （AI 分支可能简化）→ 实现期对照 `item_effects.cpp` 各 AI stub 的读取槽位。
- 道具台词（`kItemLines`）已实现（`playItemLine`），AI 使用同样播放（原版同）。

## 6. 验收

- 托管/电脑玩家回合开始概率使用道具（`aiCardItem` bit1 开启时）：如機車/汽車（25%）、
  机器工人（自行加盖）、路障/地雷/炸彈（卡位/投放他人地）、遥控骰子（走向无主地）。
- 关闭「使用道具」开关后不再使用。
- 与卡片链同场验证（`rand()&1` 二者互斥）。
