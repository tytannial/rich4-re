# 事件槽 NPC（玩家 4..7）／四大恶人

- **地址域**：槽记录 `g_miscTable80`（`0x498E28..0x498E77`，5 槽 × 16 字节）；效果主体
  `onPlayerActionPhase 0x41B42D`（NPC 分支 `0x41B9D6..0x41C84E`）
- **调用链**：`nextPlayer 0x418EBD`（调度）→ `beginPlayerTurn 0x418C55`（回合入口）→
  `checkPlayerAction 0x40C912`（可否行动）→ `startPlayerMove 0x40DD1F`（步数）→
  `moveOneStep 0x40C05C`（随机游走）→ `onPlayerActionPhase 0x41B42D`（落地/恶行/抓回）
- **重写**：`src/app/turn_system.cpp`（调度/移动/落地/恶行/释放/抓回）、`jail_dialog.cpp`（释放面板）、
  `game_panel.cpp`/`map_render.cpp`（渲染）、`new_game.cpp`（开局/读档）、`save_data.cpp`（存档）
- **状态（2026-09-26，提交 `205609e`）**：全部实现——行走闭环、释放/抓回、`LABEL_88` 四恶行、
  物件分支、`updatePlayerStates` NPC 计时递减、`eliminatePlayer` 保释人破产关回、冬眠卡 NPC、
  渲染/面板、存档 NPC 单源 + 读档恢复。**本文档为唯一权威规格**（旧版 §1.7 的字段表/惡犬 528/
  "updatePlayerStates 对 NPC 首行 return"/"home 固定"等均为误读，已订正）。

## 0. 身份与效果（帮助 `[HELP 47/52/53/56]`）

| 槽 p | NPC | 名表 | 开局 busy | 恶行（收益给**保释人** `bailer`） | 消息模板 |
|---|---|---|---|---|---|
| 4 | 小偷 | 小偷 | 1（監獄） | 同格路人→偷其一半点券；踩禮物/寶箱/路障/地雷/炸彈→替 bailer 拿对应道具/+500 点券（仅 `timerB==0`） | `0x463AE4`「偷取%s\n\n%d點點券！」、`0x463B36`… |
| 5 | 強盜 | 強盜 | 1（監獄） | 同格路人→抢 1 卡给 bailer；踩銀行格（cellType 14）→全体玩家银行 ×0.2 给 bailer | `0x463AF7`「奪取%s%s！」、`0x463B02`「強盜搶奪銀行\n\n得款%d元\n\n給%s！」 |
| 6 | 流氓 | 流氓 | 2（醫院） | 停留他人住宅→同 owner 同路段地价(+28)之和×moneyMul；他人商業→corp+34 建设价×moneyMul | `0x463B21`「勒索%s\n\n%d元保護費！」 |
| 7 | 間諜 | 間諜 | 2（醫院） | 停留他人住宅→estate+44 最近租金；他人商業→corp+48 最近收费；他人行業設施→specPt+40 fund | `0x463B36`「取走過路費\n\n%d元！」、`0x463B49`「取走盈餘\n\n%d元！」 |

> 比例常量 `dbl_463B60 = 0.2`（強盜抢银行）；`g_moneyMul` 物价指数乘数（流氓）。
> 保释费：`dword_475C44/475CA4`（人类面板 300 点券；AI 分支候选 `points >= 700`）。
> `off_47EDAA/47EDAE/47EDB6/47EDBA/47EDBE`（动作名，BIG5 已复核）=
> 禮物/寶箱/路障/地雷/定時炸彈；格式串 `0x463AC0`「小偷偷得%s\n\n給%s！」。

## 1. 数据模型

### 1.1 槽记录（`NpcSlot80`，`g_miscTable80`，`0x498E28 + 16*i`，i = p−4，i=0..3 为 4..7，i=4 为槽 8）

| 偏移 | IDA 符号 | 语义 |
|---|---|---|
| +0x00 | `word_498E28`（`g_miscTable80[8*i]` word 索引） | pixelX（`getObjectPosition` / 小地图白框读） |
| +0x02 | `word_498E2A` | pixelY |
| +0x04 | `word_498E2C`（=`word_498DEC[8*p]`） | **当前格 cellEnt**（移动真源） |
| +0x06 | `word_498E2E` | 来路格 prevCell |
| +0x08 | `byte_498E30`（=`byte_498DF0[16*p]`） | **保释人** `bailer`（0..3；release 写 `g_currentPlayer`） |
| +0x09 | `byte_498E31` | 朝向 dir（`sub_407A8C(来路,当前)`） |
| +0x0A | `byte_498E32`（**=`byte_498DF2[16*p]`**） | **busy 行动状态**：0=自由游走 / 1=監獄 / 2=醫院 / 3=槽8 機器娃娃 |
| +0x0B | `byte_498E33`（=`byte_498DF3`） | status：1=監獄 / 2=醫院（由**保释建筑**决定）+ **bit7 抓回闩锁** |
| +0x0C | `byte_498E34`（=`byte_498DF4`） | timerA（负=到期标记；冬眠卡设 5） |
| +0x0D | `byte_498E35`（=`byte_498DF5`） | timerB（小偷类恶行守卫；hibernate 清零） |
| +0x0E | `byte_498E36`（=`byte_498DF6`） | timerC（>0 → `startPlayerMove` 不移动 wait=-126） |
| +0x0F | `byte_498E37`（=`byte_498DF7`） | timerD（>0 → 步数固定 1） |

**关键订正（旧文档误读）**：绝对视角的 `byte_498DF2[16*p]`（p≥4）就是本记录的 `busy`
（`0x498DF0+16*4+2 == 0x498E32`）。原版**没有**独立的"槽占用/轮转位"——`498DF2` 无任何写入点，
"参与轮转"的判据就是 `busy==0`。

### 1.2 重写映射（单真源）

- `GameState::npcSlots[5]`（`NpcSlot80`，16 字节反射布局；槽 8 也在此）为**唯一真源**；
  旧重写的 `slotBusy2/slotStatus/slotBailer/slotCell/...` 派生数组已全部删除。
- `players[4..7]` 仅作**运行时运动镜像**（`cellEntId/prevCellEnt/spriteX/Y/dir/alive/kind=3`），
  供 `moveOneStep`/渲染/`onPhase` 复用；`moveOneStep` 每步回写槽记录（cell/prev/pixel/dir）。
- 存档：`save_data.cpp` 直接写 `npcSlots`（80B，槽 4..8 顺序与原版 `fwrite(&g_miscTable80,16,5)` 一致）。

### 1.3 开局初值（`newGameInit 0x407325..0x407363`）

```
memcpy(&g_miscTable80, unk_47ECEC, 80)   // 槽4/5 busy=1、槽6/7 busy=2、槽8 busy=3；其余全 0
memset(g_jailFlags, 0, 8); memset(g_hospitalFlags, 0, 8)
byte_496B34/B35 = g_jailFlags[4]/[5] = 1   // 小偷/強盜「在押」列表
byte_496B66/B67 = g_hospitalFlags[6]/[7] = 1 // 流氓/間諜
```
即**开局关押** = `busy=1/1/2/2`（不占回合、不绘制、不可动），坐标/格/bailer 全 0。

### 1.4 NPC 精灵/动画（`loadWalkResources 0x40B93B` a1∈4..7）

- `timerB!=0`（原 `498DF5!=0`，特殊）→ 组 5：`map.mkf[4*p+364]` / `[4*p+367]`
- 当前格 `cellEnt+39` 最高位 0（普通路）→ `[4*p+364 站]/[4*p+365 走]`，移动音效槽 11
- 最高位 1（渡水/走进）→ `[4*p+366]`（slot{1,3,5,7}），音效槽 15
- p==8（機器娃娃）→ `[521]/[522]`
- `drawPiece`（0x40829D NPC 循环 `ebx 0..4`，`edi=ebx*16`）：**`busy==0` 才画**；
  目标 state×group 槽未载时回退移动/站立槽。

## 2. 运行链

### 2.1 调度 `nextPlayer 0x418EBD`

```
v2 = cur+1；v2==playerCount → cur=4；cur==8 → cur=0、v1=1（过天）
槽 4..7（LABEL_19）：if (!busy[cur]) 选中（LABEL_20→break）；busy!=0 → cur+1 继续
全在押 → 8 → 回 0 过天（若玩家 0 死亡且已放置则继续找下一活人）
```
选中后：`advanceDay`（若过天）→ `updatePlayerStates 0x41C84F` → `byte_498EA0[cur]|=0x80`。
在押 NPC（busy≠0）不产生回合；自由 NPC 依次 4→5→6→7。

### 2.2 回合入口 `beginPlayerTurn 0x418C55`

- `cur∈4..7 && !busy` → `drawMiniMap(1) + sub_41D546()`（NPC 入场刷新；重写逐帧重绘等价）
- `checkPlayerAction 0x40C912` NPC 段：`busy==0 && !a1 && timerA==0 && timerC==0 → 2`；否则 0
- case 2/5：**`cur>=4` 直接 `startPlayerMove`**，跳过 AI 用卡/道具/交易（重写已补守卫）
- case 0（在押）→ `calcPlayerWait` → `checkPlayerAction(1)=0` → wait=-125 跳过

### 2.3 移动 `startPlayerMove 0x40DD1F`（4..7）

`timerC!=0` → state=0、wait=-126（不移动）；否则 `steps = timerD ? 1 : rand()%9+2`（2..10），
state=1，`dword_4749D4 = playerMoveGroup ? 15 : 11`，循环播移动音；`byte_498EA3=0`。

### 2.4 随机游走 `moveOneStep 0x40C05C`（cur>=4）

- 当前格 `word_498E2C`；收集 4 出口（cellEnt +24），排除来路 `word_498E2E` 与
  `occMask` bit30..33（`v6=0x40000000` 起右移）；随机选 next（无出口则回头）
- 更新 `prevCell ← 当前格`、`当前格 ← next`；占用位 `4096<<(p-4)`（旧格清、新格置；p==8 不占）
- **速度无条件 `dist × flt_4631DC(0.125)`**（无载具分支；重写 `playerMoveGroup` 分支等价）
- 每帧插值 `g_miscTable80/word_498E2A`；到达写回**本步起点**坐标（重写直接写到达坐标）
- 朝向 `byte_498E31 = sub_407A8C(来路, 当前)`；`timerB!=0` 时 `byte_498EA4` 动画帧计数 0..5
- 每步 `loadWalkResources`

### 2.5 落地 `onPlayerActionPhase 0x41B42D`（NPC 分支）

入口：`v0=word_498DEC[8*cur]`（当前格），`v62`=cellType、`v63`=objId、`v61`=cellTable 类型。
NPC（4..7）→ `switch(v61)`（§2.6）→ **LABEL_88 恶行**（§2.7）→ **LABEL_145 抓回**（§2.8）。

### 2.6 恶人踩物件特例（switch objType；`timerB!=0` 时 13/14/16/17/18 小偷分支跳过）

| objType | 小偷 p==4 | 5/6/7 |
|---|---|---|
| 1..10/12 神明 | 无操作（不附身） | 同 |
| 11 惡犬 | `deleteMapObject(11)`（槽硬编码）+ **FLC 532**/音 93（帧 3）+ `steps=0` + `hospitalizePlayer(cur,3)`（NPC 立即永久在押，无 524） | 同 |
| 13 禮物 | `deleteMapObject(13)` + refresh + 「小偷偷得禮物\n\n給%s！」+ 停移动音 + `drawGiftCard(bailer)`：成功→音效+refresh+「得到%s！」+ `playValueLine(bailer, 道具价)` + `sub_41D546`；`steps!=0` 恢复循环音 | 无操作 |
| 14 寶箱 | 音效 + `deleteMapObject(14)` + refresh +「小偷偷得寶箱…」+ 停音 + refresh +「得到５００點券！」+ `points[bailer]+=500` + 大笑台词 | 无操作 |
| 16 路障 | `deleteMapObject(v63)` + refresh +「小偷偷得路障…」+ 停音 + `givePlayerCard(bailer, 2)`（**不停步**）+ 恢复循环音 | 停音 + 删 + refresh + `steps=0`（无消息） |
| 17 地雷 | 删 + refresh +「小偷偷得地雷…」+ 停音 + `givePlayerCard(bailer, 3)` | **停留**：删 + FLC 525/音 82 + `steps=0` + `hospitalizePlayer(cur,3)` |
| 18 定時炸彈 | 删 + refresh +「小偷偷得定時炸彈…」+ 停音 + `givePlayerCard(bailer, 4)` | 无操作 |

> `givePlayerCard 0x445A4D`（重写 `givePlayerItem`）：id≤8 需礼物池库存、每槽上限 9。
> `deleteMapObject` 参数为 **1-based 槽号**：11=惡犬、13=禮物、14=寶箱（固定槽）。

### 2.7 LABEL_88 恶行（`0x41C17A`）

入口守卫：`cur>=4 && busy==0 && timerB==0`（`busy!=0 || timerB!=0 → LABEL_145`）。

**cur 4/5（路过/停留均触发）**：
- 同格掩码 `~(1<<bailer) & presentMask(occMask bit8..15)` → `sub_40D293` 取**最低位**；该玩家
  存活才执行（死者跳过、不尝试下一位）
- 小偷：`victim.points >> 1`（0 跳过）→ 停移动音 + `0x463AE4` + `points[victim]-=half`、
  `points[bailer]+=half`；`steps!=0` 恢复循环音
- 強盜：`discardRandomCard(victim)`（0 跳过）→ 停音 + `0x463AF7` + `sub_4412E4(bailer,card)`
  （重写 `giveCardToBag`）；`steps!=0` 恢复循环音
- **強盜且 cellType==14（銀行）**：遍历玩家（存活且非 bailer）`(int)(bank×0.2)` →
  `sub_41D2C6(victim, bailer, amount, 5)`（银行优先扣、收款入现金）；停音 +
  `0x463B02(total)` 2000ms；`steps!=0` 恢复循环音

**cur 6/7（仅停留 `steps==0`；先 `refreshGameUi(0,0,1)`）**，按 `cellEnt+32` special 分类：
- (2000,4000) estate：owner 非 0 且 ≠ bailer-1 时 —
  流氓(6)：同 owner 同**路段名**（`strcmp(estate+4)`）全部 estate `+28`（priceAdd）求和 ×moneyMul；
  間諜(7)：`+44`（price 最近租金，0 跳过）
- (4000,6000) corp：同条件 — 流氓：`+34`（buildPrice）×moneyMul；間諜：`+48`（lastFee，0 跳过）
- (6000,8000) specPt：**仅間諜**（且 owner≠bailer）：`+40`（fund，0 跳过）→
  `sub_41D2C6(6000+idx−5900, bailer, fund, 0)`（资金池 100+idx）
- 其余金额转 `sub_41D2C6(owner-1, bailer, 金额, 0)`（现金优先扣、收款入银行）

### 2.8 抓回（`loc_41C7A6`，函数尾**每步无条件执行**）

```
st = status & 0x7F
(st==1 監獄 && cellType==4 監獄) 或 (st==2 醫院 && cellType==5 醫院):
    status bit7 未置 → 置位（首次）
    已置 → jailPlayer/hospitalizePlayer(cur, 0) + steps=0
```
**同类建筑才抓**（status 由保释建筑决定；跨建筑不触发）。release 起点类型必匹配 → bit7 必已置，
绕回同类建筑即抓。

### 2.9 释放/关押

- `releaseJailNpc 0x43D7BF` / `releaseHospitalNpc 0x43EE6E`（a1>=4）：`bailer=currentPlayer`、
  `busy=0`、`cell=監獄/醫院格`、`prevCell=0`、pixel=格坐标、**`status=1/2`**、
  **起点类型匹配 → status|=0x80**（恒真）、`jail/hospitalFlags=0`、`loadWalkResources`
- `jailPlayer 0x43D593` NPC 段：清占用（`~(256<<player)` = `4096<<i`）、`busy=1`、
  `status=0`、timer 全 0、`jailFlags[p]=1`；**不动坐标/格/bailer**（渲染靠 busy 隐藏）
- `hospitalizePlayer 0x43EC3F` NPC 段：同构，`busy=2`、`hospitalFlags[p]=1`
- 无坐牢/住院天数：唯一放出路径 = 保释面板（`jailBailPanelProc 0x43CAAB` /
  `hospitalVisitPanelProc 0x43DA27` → release；AI `jailBailDialog 0x43D304`）

### 2.10 回合联动

- `updatePlayerStates 0x41C84F`（**NPC 分支不 return**）：timerA~D 负值清 0（timerA 清零时
  `byte_498EA0 &= ~0x40` + `releaseWalkResources(a1, 当前动画组)` + `loadWalkResources`）；
  正值递减 1→0x80
- `eliminatePlayer 0x40CD87`（真人破产）：清破产者 jail/hospitalFlags；槽 0/1 若自由且
  `bailer==破产者` → `jailPlayer(i+4,0)`；槽 2/3 → `hospitalizePlayer`
- `cardEffectHibernate 0x4440EA`：对 `busy==0` 的槽 4..7 → `timerB=0`、`timerA=5`（冬眠 5 回合）

### 2.11 槽 8：機器娃娃（道具 id1，`0x446AFB`）—— 同构槽记录

- 使用（`0x446AFB`）：扣道具 id1 + 台词列 0 → **写槽 8 记录**（pixelX/Y=玩家精灵、cell/prev=玩家格、
  bailer=玩家号、dir、**busy=0**）→ `currentPlayer=8` → `startPlayerMove`（9 步，循环音槽 9）
- 结束（`nextPlayer 0x418EBD`）：`currentPlayer=槽8 bailer`、`busy 0→3` → 该玩家重开回合
- 踢物件（`onPhase 0x41B42D`）：`bounceObject(槽, cell, prevCell)` + `deleteMapObject`；
  收尾 `0x40D7C4 case0` 用 `sub_40FAD6`（46 槽 flyCount 全 0）等弹飞动画播完才交还回合
- 渲染/资源/音效见 `item-effects.md` §1.6；**旧实现的独立 `worker*` 字段已删除**（单真源 `npcSlots[4]`）

## 3. 渲染 / 面板

- 地图棋子：`busy==0` 才画（含坐标/槽守卫）；在押立即隐藏（无 FLC/无天数）
- 小地图 `drawMiniMap 0x416E6D`：事件槽**不画标记**（旧文档"帧 6 标记"为误）；仅当
  `busy==0` 且为当前玩家时按 `pixelX/Y` 记白框位置
- 玩家信息面板 `0x415F69` else 分支：`panel.mkf[0]` 帧 5 背景 + NPC 名
  `dword_47ED5A[cur]`（重写 `kNpcNames`）@(582,40) + **bailer 棋子帧 2** @(524,64)
- 保释面板：在押格按 `jailFlags/hospitalFlags` 显示头像；NPC 费 300 点券；确认 →
  擦格 + `panel[64]` 帧(i-4) @(365,450) + 答谢消息（`dword_475BE2`）→ release → 扣费

## 4. 存档

- `saveGameToSlot 0x402FD1`：`fwrite(&g_miscTable80, 16, 5)`（槽 4..8）+ `jail/hospitalFlags`
- 重写：`SaveLayout::kNpcSlots=436` / `kJailFlags=9953` / `kHospitalFlags=9961`；`save_data.cpp`
  直接写 `npcSlots` 单源（旧版误将 `misc8A/misc8B` 写到 jail/hospital 位置，已修）；
  `loadGameFromSlot` 恢复 npcSlots + flags 并 `rebuildEventNpcFromSlots` 重建镜像/占用位
  （完整读档仍为 P5 TODO）
- 时光机快照 `TurnSnapshot` 含 npcSlots 80B（原版 `saveMapBlocksSnapshot` 不含 NPC 槽，
  重写保留以避免行为回归——差异注记）

## 5. 验收 / 调试

- 调试键（`--debug`，v2 2026-09-29 收口命令层）：`Ctrl+Shift+8` dump 5 槽状态
  （busy/status/bailer/cell/timers/标志/镜像，= `npc.dump`）；释放全部在押 NPC 改走
  `--exec "npc.release <4..7> [fromJail]"`（逐 NPC，bailer=当前玩家；旧 `Ctrl+Shift+9` 已删）
- 实机剧本：开局不在图 → 踩監獄/醫院格保释 → 上路；绕回**同类**建筑抓回、再保释；
  小偷/強盜/流氓/間諜恶行按 §0 表格；小偷踩 13/14/16/17/18 给 bailer 对应道具/点券；
  5/6/7 踩路障/地雷差异；保释人破产自动关回；冬眠卡停 5 回合；存读档 NPC 一致

## 6. 已订正的旧文档错误

| 旧描述 | 实际（IDA） |
|---|---|
| `498DF2` 独立"槽占用/轮转位"，重写需 `slotBusy2` | `498DF2 == 498E32 == busy`（同一字节，无第二字段） |
| 惡犬 NPC 走 FLC 528 | **532**（`deleteMapObject(11)+sub_45144F(...,532,...,93)`） |
| `updatePlayerStates` 对 p>=4 首行 return | NPC 分支递减 timerA~D，timerA 归零重置动画 |
| 抓回 = 踩任一監獄/醫院格、按 home 关回 | `status(保释建筑)` 与落地格**同类型**才抓 |
| release 起点≠home 才置 bit7 | release 起点类型必匹配 → **恒置 bit7** |
| `slotStatus` 用原生固定归属（4/5→監獄） | release 按**保释建筑**设 status（跨建筑场景不存在，实际一致） |
| 小地图画 NPC 标记（帧 6） | 不画标记，仅当前玩家白框定位 |
| 恶行"小偷=卡片格抽卡"等概述 | §2.6/§2.7 完整规格（含同格偷点券/抢卡/抢银行/勒索） |
