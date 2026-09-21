# 游戏内地图场景机制（M2 推进总览与验收检查表）

> 本文档汇总游戏内所有格子/场景机制的原版依据（IDA 地址）、重写进度与实机验收步骤。
> 每完成一项：更新状态、补充 IDB 注释、跑 `python tools/re_map.py`。
> 状态：❌ 未实现 / 🚧 进行中 / ✅ 已完成（已实机验收）

## 1. 数据模型

### 1.1 cellEnt（道路节点/普通格，`g_cellEnts` 0x498E80，40B/项）

| 偏移 | 含义 | 重写字段 |
|------|------|---------|
| +0/+2 | 像素坐标 x/y | `CellEnt.x/y` |
| +4 | 名称（卡片/命運/監獄/醫院/…） | — |
| +24..+30 | exits[4] 相邻格 | `CellEnt.exits` |
| +32 | **objId**（2000+ 地产 / 4000+ 公司 / 6000+ 特殊点 / 8000+ 事件格） | `CellEnt.special`（建议改名 objId） |
| +34 | 图块索引 | `CellEnt.sprite` |
| +36 | 位段：**bit0-7 格子类型（0..16）**、bit8-11 占用玩家掩码、bit16-23 cellTable 槽号 | `CellEnt.occMask`（当前未存类型位，需补） |
| +38 | 标志（清零语义） | — |

### 1.2 cellTable（物件格，`g_cellTable` 0x496D08，24B/项，46 项）

| 偏移 | 含义 |
|------|------|
| +0 | 类型（`kCellTypeInit[46]` = 1..14,15,15,16×10,17×10,18×10） |
| +1 | 朝向 |
| +2 | cellEnt u16 |
| +4 | 剩余回合（`byte_496D0C`） |
| +5 | 占用玩家+1（`byte_496D0D`） |

类型 15 = 神明物件（`kObjectNames[19]`：間諜/小財神/…/定時炸彈）；16/17/18 = 事件/特殊点/公司类物件。
`byte_497321/497322/497323` = 类型 16/17/18 的剩余计数。

### 1.3 objId 段

| 段 | 表 | 项大小 | 关键字段 |
|----|----|--------|---------|
| 2000..3999 | `g_estates` | 52B | +23 旗帜、+24 类型、+25 拥有者+1、+26 等级、+28/+30 价格、+44 收购价、+48 到期日 |
| 4000..5999 | `g_corps` | 56B | +24 类型、+25 拥有者+1、+34、+48 价格、+52 到期日 |
| 6000..7999 | `g_specPts` | 52B | +24 拥有者+1、+25 index、+32 sprite、+40/+44 资金、+48 价格 |
| 8000+ | `g_evtCells` | 28B | +24 朝向、+26 sprite |

## 2. 结算时机（三个入口）

| 时机 | 原版函数 | 重写 | 状态 |
|------|---------|------|------|
| 移动中每格 | `onPlayerActionPhase` 0x41B42D | 日志 stub | ❌ |
| 移动结束（收租/过路费） | `playerActionStateMachine` case 3（0x40DA4B）→ `sub_40DF69` | case 3 误标"停留动画" | ❌ |
| 停留格结算（购地/升级/事件） | `sub_41982D`（`landingEvent`，case 0..16） | 返回 0x80 的 stub | ❌ |

**调用链**：`sub_418C55`（回合开始）→ `sub_418E7F`（calcPlayerWait）→ `landingEvent = sub_41982D(当前格 cellEntId)`
→ 返回值写入 `byte_498EA5`（正 = 等待帧数，0x80 负值 = 直接下一位）。

## 3. 格子类型表（`sub_41982D` switch 输入 = cellEnt+36 低字节）

音效：`audioPlayEffect(&g_effectSlots[byte_475299[type] << 3])`（0x475299 = `{9,0,10,10,10,10,10,10,10,10,16,16,16,16,10,10,10}`）

| 类型 | 音效槽 | 结算函数 | 语义 | 状态 |
|------|-------|---------|------|------|
| 0 | 9 | case 0（按 objId） | 普通格：地产/公司/特殊点购地/升级/收租 | ❌ |
| 1 | 0 | 0x41B3D0 default | 无事件（返回 0x80） | ❌ |
| 2 | 10 | `sub_44B6DF` | 全屏 UI（卡片/命运类） | ❌ |
| 3 | 10 | `sub_44DB81` | 全屏 UI | ❌ |
| 4 | 10 | `sub_43D304` | runModal + rand（監獄？） | ❌ |
| 5 | 10 | `sub_43E9A4` | runModal + rand（醫院？） | ❌ |
| 6 | 10 | `sub_415215` | 得分 | ❌ |
| 7 | 10 | `sub_4154DC` | 得分 | ❌ |
| 8 | 10 | `sub_4155FC` | 得分 | ❌ |
| 9 | 10 | `sub_4315CC` | runModal（新聞？） | ❌ |
| 10 | 16 | 0x41B184 | 得 50 点（FLC 537 + 文本 `byte_463A81`） | ❌ |
| 11 | 16 | 0x41B21E | 待分析 | ❌ |
| 12 | 16 | 0x41B2A3 | 待分析 | ❌ |
| 13 | 16 | 0x41B302 | 待分析 | ❌ |
| 14 | 10 | 0x41B396 | 待分析 | ❌ |
| 15 | 10 | 0x41B3B9 | 待分析 | ❌ |
| 16 | 10 | 0x41B3CB | 待分析 | ❌ |

类型 1..14 名称候选（cellEnt +4）：卡片/命運/魔法屋/百貨公司/公園/新聞/監獄/醫院/銀行/樂透/得５０點/七彩氣球/墾丁/知本（精确对应待实机核对）。

## 4. 通用辅助函数

| 原版 | 语义 | 依据 | 重写 |
|------|------|------|------|
| `sub_440BA8` | **文本 + YES/NO 询问框**：setTextFont(16, 0xF0F0F0, 0x101010, 3, 1) → 框 `g_tipFrame+72` 于 (220,140) → drawText → blit → `sub_453A32(220,320)` YES/NO | 0x440BA8 | `confirmDialog`（缺文本，待扩展） |
| `sub_440CAC` | 显示文本 N 毫秒（`sub_4528B9` 延时；负数 = 左移 100px） | 0x440CAC | ❌ |
| `sub_453A32` | YES/NO 框（返回 1=YES/0=NO） | 0x453A32 | `confirmDialog` ✅ |
| `sub_452946` | 玩家名 → 文本缓冲（sprintf 用） | 0x452946 | ❌ |
| `sub_41D476` | UI 刷新（(0,0,1)） | 0x41D476 | 待确认 |
| `rebuildMiniMap` | 小地图重建（a1=0 小/1 大） | 0x40A4E1 | `buildMiniMapMarks` ✅ |
| `sub_41D2C6` | 转账 | 0x41D2C6 | `transferMoney` ✅ |
| `sub_40E14D` | 释放 cellTable 槽 | 0x40E14D | `releaseCellTableSlot` ✅ |
| `sub_40CD87` | 淘汰（破产） | 0x40CD87 | `eliminatePlayer` ✅ 简化 |
| `sub_44EF41` | 角色语音/表情 | 0x44EF41 | ❌ |
| `sub_40DF69` | 收租转账（case 3 调用） | 0x40DF69 | ❌ |
| `sub_40FA61` | 购地前检查 | 0x40FA61 | ❌ |

## 5. 推进清单（验收检查表）

### P0 买地（`sub_41982D` case 0 → `loc_41A013`）

原版流程（0x41A013..0x41A138）：
1. 守卫：`g_playerState37[cur] != 0` 或 `byte_496BA7[cur] == 12` → 跳过
2. 价格 = `(level * word[est+30] + word[est+28]) * g_moneyMul`；`g_playerCash < 价格` → 钱不够分支 0x41A159
3. 文本 `sprintf(chText, "%s\n\n", est+4)`（地产名）
4. AI（`alive&6`）→ `sub_41D7D4(价格)` 决策；人类（`alive==1`）→ `sub_440BA8(chText)` 询问
5. `sub_40FA61(cur)` 检查 → `est.owner = cur+1` → `sub_41D476(0,0,1)` → 音效 `dword_4823D2` → `rebuildMiniMap(0)`
6. `g_cfgLandPerm != 0` → `est[+48] = sub_4521CB(dword_497160, dword_4751F0[g_cfgLandPerm])`（期限表 `{0,0,2,0}`）
7. `g_playerCash -= 价格`

- [ ] 数据：`Estate` 补 `+28`（u16）、`+30`（u16）价格字段（核对 `parseMapDat`）
- [ ] `confirmDialog` 支持文本（对齐 `sub_440BA8` 的绘制与坐标）
- [ ] `landingEvent` case 0 无主地产购地（含取消/钱不够分支）
- [ ] 音效槽 `dword_4823D2` 确认 + 播放
- [ ] 验收：实机走到无主地产 → 弹"地产名+价格"询问框 → 确定后地块变色/扣款/小地图更新；取消无变化；钱不够提示

### P1 升级 + 收租（同链路）

- [ ] case 0 自己地产升级（0x419911..0x419A52：level<5、费用 `word[est+30]*moneyMul`、询问、`level++`）
- [ ] case 0 他人地产收租（`loc_419A67`）
- [ ] 状态机 case 3 修正：移动结束收租（0x40DA4B → `sub_40DF69`，30*moneyMul）
- [ ] 验收：升级成功/失败、踩他人地产扣款

### P2 事件格（`sub_41982D` case 2..16）

- [ ] case 10 得 50 点 → case 6/7/8 得分 → case 2/3（全屏 UI）→ case 4/5/9（runModal）
- [ ] case 11..16（待分析）
- [ ] 验收：逐类型实机触发核对

### P3 移动中落地（`onPlayerActionPhase` 0x41B42D）

- [ ] 医院/监狱 `cellNo` 回合递减、到期释放
- [ ] 破产金（同格已淘汰玩家 1000*moneyMul）
- [ ] 同格 cellNo 传染转移
- [ ] cellTable 物件格（`sub_40EAD7` + `sub_40E32C` 显灵动画）
- [ ] 验收：进医院/监狱停留 N 回合、神明效果

### P4 卡片/道具/神明使用

- [ ] `givePlayerCard`(0x445A4D) / `takePlayerCard` / `sub_445ADA`(抽卡)
- [ ] `sub_444E1A` 使用流程（`sub_444D1A` 人类选择 / `sub_41E6F2` AI 选择 / `sub_40E669` 移动动画）
- [ ] 验收：用卡/用道具生效

### P5 AI / 存档 / 事件槽 NPC

- [ ] `sub_41E6F2` AI 决策、`alive&6` 自动行动
- [ ] `loadGameFromSlot`(0x402AC5) 完整恢复
- [ ] 事件槽 NPC（玩家 4..7）
- [ ] 验收：AI 自动走完回合、读档恢复完整状态

## 6. 待确认问题（后续一并处理）

1. 格子类型 1..16 与名称（卡片/命運/…）的精确对应 —— 需实机或 MAPDAT 核对
2. case 11..16 的语义（0x41B21E/0x41B2A3/0x41B302/0x41B396/0x41B3B9/0x41B3CB）
3. `CellEnt` +36 类型位当前未建模（`occMask` 仅存玩家位）—— 影响 case 分派，P0 需补
4. `sub_41D476` / `sub_40FA61` / `sub_41D7D4` 具体语义待分析
5. 事件槽 NPC（玩家 4..7）与 0x43xxxx 段（監獄/醫院/事件）的关系
