# 事件槽 NPC（玩家 4..7）／四大恶人

- **地址域**：犯人表 `0x498DE8..0x498E72`；效果主体 `onPlayerActionPhase 0x41B42D`（NPC 分支 0x41B9D6..0x41C84E）
- **调用链**：`nextPlayer 0x418EBD`（调度）→ `beginPlayerTurn 0x418C55`（回合入口）→ `checkPlayerAction 0x40C912`（可否行动）→ `startPlayerMove 0x40DD1F`（步数）→ `moveOneStep 0x40C05C`（随机游走）→ `onPlayerActionPhase 0x41B42D`（落地/恶行/抓回）
- **重写**：`src/app/turn_system.cpp`（调度/移动/落地）、`jail_dialog.cpp`（释放）、`game_panel.cpp`/`map_render.cpp`（渲染）、`new_game.cpp`（开局）
- **本文档为 P5「四大恶人」实现依据**；旧 `41982d-p2-events.md` §1.7 的两处错误（`byte_498DF0` 误标"外观"、"走完一圈回归目标"）以本文为准。

## 0. 身份与效果（帮助 `[HELP 47/52/53/56]`）

| 槽 p | NPC | 名表 | 开局标志 | 恶行（收益给**保释人** `bailer`） | 消息模板 |
|---|---|---|---|---|---|
| 4 | 小偷 | 小偷 | `jailFlags[4]=1` | 卡片格→抽 1 卡给保释人；寶箱→保释人 +500 点券；（LABEL_88 else 窃土地过路费） | `0x463AC0`「小偷偷得%s\n\n給%s！」、`0x463AD3` |
| 5 | 強盜 | 強盜 | `jailFlags[5]=1` | 踩寶箱(cellType 14)→**全体玩家存款×0.2** 转给保释人；LABEL_88 抢卡 | `0x463B02`「強盜搶奪銀行\n\n得款%d元\n\n給%s！」、`0x463AF7`「奪取%s%s！」 |
| 6 | 流氓 | 流氓 | `hospitalFlags[6]=1` | 踩他人住宅/商業→**同路段地价之和 ×moneyMul**（保护费）转给保释人 | `0x463B21`「勒索%s\n\n%d元保護費！」 |
| 7 | 間諜 | 間諜 | `hospitalFlags[7]=1` | 踩他人住宅→窃**上次过路费**(estate+44)；踩行業設施點→盗**公司累积盈余**(specPt fund) | `0x463B36`「取走過路費\n\n%d元！」、`0x463B49`「取走盈餘\n\n%d元！」 |

> 比例常量 `dbl_463B60 = 0.2`（強盜抢银行）；`g_moneyMul` 物价指数乘数（流氓保护费）。
> 保释费：`dword_475C44/475CA4`（人类面板 300 点券 / AI 分支 `>=700`），费用表见 `41982d-p2-events.md` §1.2。
> ⚠️ `off_47EDAA/47EDAE/47EDB6`（"%s" 主语，疑为各 NPC 动作名）BIG5 待阶段2 `get_bytes`+cp950 逐字复核（AGENTS：勿信 `get_string`）。

## 1. 数据模型

### 1.1 犯人表（原版 16 字节/条，基址 `0x498DF0 + 16*p`，p = 绝对玩家号 4..7）

`.data` 实测 `0x498DE8` 区**初值全 0**（见 §1.3）。字段：

| 偏移(绝对) | 地址 | 语义 | 现有重写 |
|---|---|---|---|
| +0 | `byte_498DF0[16*p]` | **保释人玩家号** `bailer`（=0..3；`releaseJailNpc` 写 `g_currentPlayer`） | **缺**：新增 `slotBailer[8]` |
| +2 | `byte_498DF2[16*p]` | **占用/参与轮转**（0=进入该槽回合；≠0=nextPlayer 跳过） | `slotBusy2[8]`（映射对） |
| +3 | `byte_498DF3[16*p]` | 状态 1=監獄/2=醫院 + **bit7 抓回触发位** | **缺**：新增 `slotStatus[8]`（bit7 用 `slotTrigger` 或位） |
| +5 | `byte_498DF5[16*p]` | 特殊动画/恶行进行标志（各效果 case 守卫 `==0` 才执行） | `slotFlag5[8]` |
| +6 | `byte_498DF6[16*p]` | 等待标志（startPlayerMove → 不移动 wait） | `slotFlag6[8]` |
| +7 | `byte_498DF7[16*p]` | 单步标志（步数固定 1） | `slotFlag7[8]` |
| +8 | `g_miscTable80[8*i]` | 像素 x（`word_498DEC` 系列，i=p-4） | `slotPixelX[8]` + `miscTable80` |
| +10 | `word_498E2A[8*i]` | 像素 y | `slotPixelY[8]` |
| +4 | `word_498E2C[8*i]` | **当前格 cellEnt**（=`word_498DEC[8*p]`，两者同一） | `slotCell[8]` / 复用 `players[p].cellEntId` |
| +6 | `word_498E2E[8*i]` | 来路格（=prevCell） | `slotPrevCell[8]` / `players[p].prevCellEnt` |
| +1 | `byte_498E31[16*i]` | 朝向 dir | `slotDir[8]` / `players[p].dir` |
| +10 | `byte_498E32[16*i]` | **行动状态**（1=在押/坐牢；0=自由游走） | `slotBusy[8]` |
| +12/14/16 | `byte_498E34/36/37[16*i]` | 计时/计数（全 0 且 498E32=0 → checkPlayerAction 返回 2） | `slotTimerA/C/D[8]` |

**等价换算**（原版混用绝对 p 与相对 i=p-4，同一内存）：
`byte_498DF0[16*p]` == `byte_498E30[16*(p-4)]`（0x498DF0+16*4=0x498E30）；
`word_498E2C[8*(p-4)]` == `word_498DEC[8*p]`（0x498E2C=0x498DEC+16*4）。
重写一律用 `p-4` 索引干净数组，**不复现多视图重叠**。

### 1.2 "两者结合"落地方案（用户确认）

原则：**位置/朝向/格复用 `players[4..7]`（复用 moveOneStep/onPhase/渲染管线、改动最小、防崩现有）；NPC 专属逻辑用 `slot*` 干净数组一一对应原版犯人表字段；存档/读法贴合原版语义。**

| 语义 | 归属 | 备注 |
|---|---|---|
| 当前格 / 来路 / 朝向 / 坐标 | `players[p].cellEntId/prevCellEnt/dir/spriteX/spriteY` | 复用现管线；moveOneStep 现有分支直接用 |
| 在场/在押 | `players[p].alive`（1=场上）+ `slotBusy[p-4]`（1=坐牢） | `kind` 置特殊值避开 AI/人类分支 |
| 精灵外观 | `pieceSprites[p]` ← `map.mkf[4*p+364..367]`（`loadWalkResources` NPC 段） | **非** charIndex；见 §1.4 |
| 保释人 | **新增** `slotBailer[8]` | |
| 状态1/2 + 触发位 | **新增** `slotStatus[8]`（含 bit7） | |
| 占用轮转 | `slotBusy2[8]`（既有，映射 498DF2） | nextPlayer 唯一判据 |
| 等待/单步/恶行旗 | `slotFlag5/6/7[8]` | |
| 计时 | `slotTimerA/B/C/D[8]` | |

### 1.3 开局初值（`newGameInit 0x407325..0x407363`）

```
memcpy(g_miscTable80, unk_47ECEC, 80)      // NPC 初始坐标/格表（见下）
memset(g_jailFlags, 0, 8); memset(g_hospitalFlags, 0, 8)
byte_496B34=1; byte_496B35=1;               // jailFlags[4]/[5]（小偷/強盜）
byte_496B66=1; byte_496B67=1;               // hospitalFlags[6]/[7]（流氓/間諜）
```
**不写** `498DF0/DF2/DF3/E32/E2C`（.data 全 0）。`unk_47ECEC` 前 5 项 +10 处有值 `1,1,2,2,3`（疑为各槽初始格/坐标）。

### 1.4 NPC 精灵/动画（`loadWalkResources 0x40B93B` a1∈4..7）

- `498DF5!=0`（特殊）→ `map.mkf[4*p+364]` / `[4*p+367]`（动画组 5）
- 当前格 `cellEnt+39>=0`（普通路）→ `map.mkf[4*p+364]` / `[4*p+365]`，移动音效槽 11
- `498EA0 & 0x30 == 0x10`（走进状态）→ `map.mkf[4*p+366]`
- p==8（機器娃娃）→ `[521]/[522]`
即每 NPC 一组约 4 张 map.mkf 资源（`364+4*p` 起），装进 `dword_498EB4/EBC/EC0`（重写 `walkRes[p][*]`/`pieceSprites`）。

## 2. 运行链（逐步实证）

### 2.1 调度 `nextPlayer 0x418EBD`
- `cur==8`（機器娃娃）→ 回 `workerPlayer`。
- `cur<4 && alive&0x30` → 状态恢复（landAfterMove + alive&=0xF）。
- 正常轮转：`cur+1`；`cur==playerCount` → `cur=4`；LABEL_19：`if(!498DF2[cur]) 选中(cur)`，否则继续 `cur+1`；`cur==8` → 回 0、`v1=1`（过天）。
- 选中事件槽（`cur>=4`）后：`advanceDay`(若 v1) → `updatePlayerStates 0x41C84F` → `byte_498EA0[cur]|=0x80`。
- **要点**：`498DF2==0` = 该 NPC 槽**参与轮转**（非"占用即跳过"的字面反义，实为"跳过位"）。现有 `!slotBusy2[cur-4] break` 正确；但现有另有 `players[s].alive!=0 找 NPC` 兜底（line 3112），实现时统一为 `slotBusy2` 单一判据。

### 2.2 回合入口 `beginPlayerTurn 0x418C55`
- `if cur>=4 && cur<8 && !498DF2[cur]` → `drawMiniMap(1) + sub_41D546(1)`（NPC 入场刷新）。
- `checkPlayerAction 0x40C912`：NPC 段 `if(!498E32 && a1==0 && !498E34 && !498E36) return 2`；否则落 `return 0`。
- switch：case 0→`calcPlayerWait`；case 1→`enablePlayerControl`（仅真人）；**case 2/5：`if(cur>=4 || alive&0x30) goto startPlayerMove`**——★ **NPC 直接走 startPlayerMove，跳过 AI 用卡/道具/交易**（真人 AI 才 rand&1 用卡）。重写必须补 `p>=4` 守卫（R2）。

### 2.3 移动 `startPlayerMove 0x40DD1F`（4..7）
- `if 498DF6[cur]` → state=0, wait=-126（不移动）；
- 否则 `steps = 498DF7[cur] ? 1 : rand()%9+2`（2..10），state=1，音效槽 11/15。
- 机器娃娃(8)：steps=9。

### 2.4 随机游走 `moveOneStep 0x40C05C`（cur>=4）
与真人分支**同构**，数据源为犯人表：
- 当前格 `word_498E2C`，起点=该格坐标；收集 4 出口，排除来路 `word_498E2E` 与占用位；随机选 next（无出口则回头）。
- 更新：`word_498E2E ← 当前格`、`word_498E2C ← next`；占用位 `4096<<(p-4)`（旧格清、新格置；p==8 不占）。
- 插值 `miscTable80/word_498E2A`（像素），速度用 `flt_4631DC`（固定慢速，无载具分支）。
- 朝向 `byte_498E31 = sub_407A8C(来路, 当前)`。
- **现有 moveOneStep 已用 `players[p]` + `4096<<(p-4)` 掩码**：给 NPC 复用 `players[4..7].cellEntId/prevCellEnt/spriteX/Y` 即几乎零改（需设 `playerMoveGroup[p]` 走慢速分支）。

### 2.5 落地 `onPlayerActionPhase 0x41B42D`（NPC 分支）
入口：`v0 = word_498DEC[8*cur]`（当前格），读 `cellEnt+36`：`v62`=类型、`v63`=cellTable 槽、`v61`=cellTable 类型。`cur==8`（娃娃）→ 弹飞物件 `bounceObject+deleteMapObject`。NPC(4..7)→ `switch(v61)`（§2.6）后汇入 **LABEL_88**（§2.7），收尾**抓回**（§2.8）。

### 2.6 恶人踩物件特例（switch v61，仅列 NPC 差异；`498DF5!=0` 一律跳过）
- **case 11（惡犬）**：NPC → FLC 528 + `hospitalizePlayer(cur, 0)`（被咬住院）；真人按载具 damagePlayer。
- **case 13（卡片格）**：`cur==4 小偷` → `deleteMapObject(13)` + 消息「小偷偷得%s给%s」 + `drawGiftCard(bailer)` + `playValueLine(bailer,价)`（替保释人抽卡）。
- **case 14（寶箱）**：`cur==4` → `g_playerPoints[bailer] += 500` + 消息 + `sub_44EF41(bailer,...)` 台词（保释人白得 500 点券）。
- **case 16（路障）**：`cur==4` → 删除 + 消息 + `givePlayerCard(bailer, 2)`。
- **case 17**：`cur==4` → 删除 + `hospitalizePlayer`（恶人踩住院格）。

### 2.7 LABEL_88 恶行（`0x41C447`，仅 `steps==0` 落定时结算；目标格 objId=`cellEnt+32`）
`owner = *表+25/24`；`owner-1 == bailer` 则跳过（不打自家人）。全部 `sub_41D2C6(owner-1 → bailer, 金额, flags)`：
- **住宅(2000..4000)**：流氓(6)=同 owner 同路段全部 estate `+28(地价)` 求和 ×`moneyMul`「勒索…保護費」；否则=`estate+44`「取走過路費」（上次租金）。
- **商業(4000..6000)**：流氓(6)=`corp+34` ×moneyMul；否则=`corp+48`。
- **行業設施(6000..8000)**：仅間諜(7)=`specPt+40`（累积盈余 fund）「取走盈餘」。
- 強盜抢银行见 case14 前的 `cur==5 && cellType==14` 段（`0x41C330`）：遍历各玩家 `g_playerBank×0.2` 转 bailer。

### 2.8 抓回（`loc_41C7A6`，函数尾必执行）
```
st = byte_498DF3[16*cur] & 0x7F
if st==1 && 落地格类型 v62==4（監獄）:
    if byte_498DF3 & 0x80:  jailPlayer(cur, 0);  steps=0     // 已标记→真抓回
    else:                   byte_498DF3 |= 0x80              // 首次→置触发位（防出门即抓）
if st==2 && v62==5（醫院）:  同理 hospitalizePlayer(cur, 0) / |=0x80
```
`jailPlayer NPC 分支(0x43D5??)`：`cellEnts[当前格].occMask &= ~(256<<p)`；`498E32=1`（坐牢）；`498E33..37=0`（清状态/计时）；`jailFlags[p]=1`。**不清 498DF2**（NPC 仍占槽，但因 498E32=1 → checkPlayerAction 返回 0 → 该回合不动）。`releaseJailNpc 0x43D7BF`：填 bailer、当前格=監獄格(`word_48BAE0`)、坐标、`498E32=0`（自由）、`498E33=1`（+目标 type==4 则 `|0x80`）、清 `jailFlags`、`loadWalkResources`。`releaseHospitalNpc 0x43EE6E` 对称（醫院格 `word_48BAE2`、status=2、type==5 置位）。

## 3. 渲染 / 面板

- 地图棋子：`map_render.cpp:299` 现循环 `< playerCount`，**需纳入 4..7**（以在场条件为准，坐标取 `miscTable80`/`players[p]`，精灵 `pieceSprites[p]`）→ R10。
- 小地图：`drawMiniMap 0x416E6D` 读 `498DF2` 画 NPC 标记（帧 6）。
- 右侧面板/玩家条：`sub_415F69/4166F8`，`cur>=playerCount` 画 `dword_47ED5A[cur]` 名（重写 `kNpcNames`）+ NPC 棋子（现有 `game_panel.cpp:152 return` 待补）。

## 4. 联动风险清单

| # | 风险 | 缓解 |
|---|---|---|
| R1 | nextPlayer 进空 NPC 槽 → `currentPlayer` 指空槽 → 渲染跳左上角/错误掷骰 | `slotBusy2` 单一判据；进槽前校验 `slotCell/坐标` 有效 |
| R2 | beginPlayerTurn case2/5 的 AI 用卡/道具链去翻 NPC 空卡包 | 补 `if(p>=4){startPlayerMove;break;}`（§2.2 实证） |
| R3 | NPC 走 LABEL_88 自己抢钱，误接真人 `landingEvent` 收租 → NPC 付租/破产 | NPC 落地走独立分支，不复用 case0 收租 |
| R4 | 恶人转账给 bailer → `transferMoney` 内含 `eliminatePlayer` 连锁；bailer 已破产 | 依赖 transferMoney 现有安全；实机验越界 |
| R5 | 占用位 `4096<<(p-4)`(bit12..15) 与 cellTable 物件/玩家位冲突 | 现有 mask 已含，验 `cellEnt+36` |
| R6 | 恶人踩神明格误 `attachObject` 附身 NPC（管线不支持） | 按 §2.6 特例（删物件/给 bailer 卡），**不**附身 NPC |
| R7 | NPC 回合 `g_playerControl` 未锁 → 玩家替 NPC 掷骰 | NPC 不进 case1；`kind` 非人类 |
| R8 | 存档/时光机：犯人表未入存档；`saveTurnSnapshot` 现仅 p<4 | 补 slot* + players[4..7] 入 save；快照不含 NPC（对） |
| R9 | 重开局/读档残留 slot → 脏 NPC | `newGameInit`/`loadGameFromSlot` 清零全部 slot* |
| R10 | 渲染上界 playerCount→9 误画未激活槽 | 以在场条件 + 坐标有效为准 |

## 5. 开放项与实机确认（2026-09-26）

1. **开局态 = B（实机 + 用户记忆确认）**：四大恶人开局**关押、不在地图游走、不占回合**；踩監獄/醫院格由保释面板（读 `jailFlags`）显示在押者，花 300 点券保释才 `releaseEventNpc` 上路随机游走。
   - **重写落地**：关押态用 `slotBusy2=1`（nextPlayer 跳过、map_render 不绘制）。`newGameInit` 设 `slotBusy2[4..7]=1`+`slotBusy[4..7]=1`+`slotStatus`。游走=`slotBusy2=0`。
   - **抓回=回关押态**：NPC 任一步落到監獄(type4)/醫院(type5)格 → `jailPlayer/hospitalizePlayer` NPC 分支置 `slotBusy=1`（坐牢）+ **`slotBusy2=1`（消失+停轮转）**+ `jailFlags/hospitalFlags=1`（待再保释）。此前只置 `slotBusy=1` 会"进回合卡着不消失"，已修。
   - 静态 `.data` 全 0 曾推得 A（开局即游走），与实际/B 不符 → 说明原版关押另有初始化路径或本重写以 B 语义为准（不阻塞功能）。
1b. **归属/收益最终决策（2026-09-26 用户确认，覆盖上面"按保释建筑"推断）**：
   - **重置归属 = 原生槽固定**：小偷/強盜(4/5)→监狱、流氓/間谍(6/7)→医院；`releaseEventNpc` 按 `npc<6?1:2` 设 `slotStatus`（原静态 `releaseJailNpc` 按保释建筑设 status，冲突时取原生固定）。→ 间谍被陷害关进监狱、从监狱保释→游走仍回**医院**；小偷被地雷炸入院、从医院保释→游走仍回**监狱**。
   - **抓回触发 = 踩到任一監獄(4)/醫院(5)格**（"遇建筑无论如何消失重置"），按 home 调 `jailPlayer/hospitalizePlayer` 关回原生建筑在押；bit7 闩锁：`release` 起点≠home→置位(走回即抓)，起点=home→不置(走一圈、二次抓)。
   - **收益对象 = `slotBailer`（最后保释人）**，与归属解耦（每次 release 覆盖）。
   - **恶人不受命运/魔法屋影响**：`onPhase` NPC 分支只处理抓回，踩命运(3)/魔法屋(16)格天然不触发；魔法屋惩罚目标选择须过滤 `p>=4`。**仅新闻(`g_newsFuncs`)对恶人的效果**留阶段2 逐条查 `p>=4` 分支。
   - 入狱/入院**不播 538/524 动画**、立即生效（`jailPlayer/hospitalizePlayer` NPC 分支无 `playEventFlc`）。
   - **无坐牢/住院天数**：`updatePlayerStates 0x41C84F` 对 `p>=4` 首行 return，恶人不参与真人 `stateFlags`/`cellNo` 天数递减；一旦入狱/入院即**永久停留**，唯一放出路径=玩家保释 `releaseEventNpc`（地雷/恶犬致恶人入院亦走此永久关押态，2.2 实现时不得加天数）。
2. **动画/音效（实机确认）**：`loadWalkResources` NPC 按 `slot=2*state+group` 取图，须覆盖**全部 state×group 槽**——走路组 `slot{0,2,4,6}`=`data.mkf[4p+364 站/365 走]`、渡水组 `slot{1,3,5,7}`=`[4p+366]`、`slotFlag5` 特殊组移动帧=`[4p+367]`；此前只载移动槽(0/2/3)，**每回合末进 `state3` 取空 slot6/7 → `pieceAnim[4..7]` 又从不加载 → `drawPiece` 回退 null 不绘制 = 棋子消失闪烁**，已补全并在 `drawPiece` 对 `p∈4..7` 加空槽回退（移动/站立槽）。渡水资源是 **slot3=`EC0`**（非 slot1）。当前格真源用 `players[p].cellEntId`（随 `moveOneStep` 推进），不可用释放时的 `slotCell`（否则卡在監獄渡水组="始终坐船"）。**移动音效**：`loadWalkResources` NPC 走路组→`moveSoundIndex=11`、渡水组→`15`（对齐真人 547/525 与原版 `dword_4749D4`；此前 NPC 分支漏设致坐船播行走音）。
3. 剩：`unk_47ECEC`(miscTable80 初值 1,1,2,2,3)语义、`off_47EDAA/47EDAE/47EDB6` NPC 动作名 BIG5 复核、恶行金额字段（见 §6 阶段2/3）。

## 6. 分期实现

- **阶段1（行走闭环）**：`game_state.h` 加 `slotStatus/slotBailer` → `newGameInit` 开局态（按实机定 A/B）→ `release*` 写犯人表 → `nextPlayer` 统一 `slotBusy2` → `beginPlayerTurn` 补 `p>=4` 守卫 → `checkPlayerAction` 用 `slotBusy/E32` → `moveOneStep` NPC 复用（设 `playerMoveGroup`）→ `onPhase` 新增 NPC 分支（**仅抓回**）→ `jailPlayer/hospitalizePlayer` 放开 p>=4（抓回=回炉）→ `map_render`/`game_panel` 渲染 NPC → 存档清/存 slot*。**验收**：NPC 上路随机走、走回建築被抓回复位、渲染正确、回合不崩、人类交互不被劫持。
- **阶段2（四恶行）**：§2.6/2.7 LABEL_88 按身份实现 + 恶人踩物件特例 + 转账给 bailer（含 R4）。
- **阶段3（收尾）**：淘汰清理 `eliminatePlayer 0x40CD87`（0x40CE95/0x40CED2）、时光机快照含 NPC、AI 托管连锁、debug 键、实机回归。

## 7. 待查清单

- [ ] `onPhase 0x41C17A`（498DF2 写）逐行 + case18/特殊格入口
- [ ] `cardEffectHibernate 0x4440EA` 对事件槽（498DF2）处理
- [ ] `eliminatePlayer` 对犯人表清理（0x40CE95/0x40CED2）
- [ ] `unk_47ECEC` 初值语义
- [ ] `off_47EDAA/EDAE/EDB6` BIG5 复核
- [ ] 惡人踩他人格是否触发"同格破产金/传染"（真人 LABEL_22 段 NPC 是否也走）
