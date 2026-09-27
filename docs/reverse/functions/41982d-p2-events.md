# 0x41982D landingEvent P2 事件格研究（case 2..16）

> 本文档为 `sub_41982D`（landingEvent）case 2..16 的原版实现研究，供 P2 实现使用。
> 入口/分派见 `gameplay-map-mechanics.md` §3；调用链与返回值见 §2。
> 所有 case 均**返回 0x80**（直接下一位）；需要交互的 case 在函数内用 `runModal` 阻塞完成。

## 0. 总览

| type | 名称 | case | 处理函数 | 研究状态 |
|------|------|------|---------|---------|
| 1 | 公園 | 1 = default | —（无事件） | 已实现（返回 0x80） |
| 2 | 新聞 | 2 | `newsEvent`（36 事件表） | ✅ 已实现 2026-09-26（`news_dialog.cpp`；36 判定+36 效果；拍卖 idx 7 桩；详见 `44b6df-news-events.md`） |
| 3 | 命運 | 3 | `fateEvent`（37 事件表/49 效果） | ✅ 已实现（2026-09-26，`fate_event.cpp`；卡片联动见专项文档） |
| 4 | 監獄 | 4 | `jailBailDialog`（门外保释） | ✅ 保释面板（人類 UI+AI）；NPC 格占位待 P5 |
| 5 | 醫院 | 5 | `hospitalVisitDialog`（门外辦理出院） | ✅ 出院面板（阶段机+护士动画）；NPC 格占位待 P5 |
| 6 | 企鵝挖寶 | 6 | `miniGameDigRun` 0x415215 | ✅ 交互版 2026-09-25（`minigame_dialog.cpp`） |
| 7 | 七彩氣球 | 7 | `miniGameBalloonsRun` 0x4154DC | ✅ 交互版 2026-09-25（`minigame_dialog.cpp`） |
| 8 | 喜從天降 | 8 | `miniGameMoneyRun` 0x4155FC | ✅ 交互版 2026-09-25（`minigame_dialog.cpp`） |
| 9 | 樂透 | 9 | `lotteryVisit` | ✅ 2026-09-25 |
| 10 | 得５０點 | 10 | 0x41B184 | ✅ 已实现（+50 點券） |
| 11 | 得３０點 | 11 | 0x41B21E | ✅ 已实现（+30 點券） |
| 12 | 得１０點 | 12 | 0x41B2A3 | ✅ 已实现（+10 點券） |
| 13 | 卡片 | 13 | 0x41B302（`sub_441E12` 抽卡） | ✅ 已实现 |
| 14 | 銀行 | 14 | `sub_4379C9`（UI）+ `sub_436668`（停留） | ✅ 全链已实现（柜员机/停留主UI/週轉/催收/月度结息/15 号分红，2026-09-23；见 `bank-system.md`） |
| 15 | 百貨公司 | 15 | `sub_42E931`（商店） | ✅ 2026-09-25（`42e931-department-store.md`） |
| 16 | 魔法屋 | 16 | `sub_43380A` | ✅ 2026-09-26（`43380a-magic-house.md`） |

## 1. case 4 監獄 / case 5 醫院

> **实现状态（2026-09-23）**：入狱/住院（`jailPlayer` 0x43D593 / `hospitalizePlayer` 0x43EC3F：移格/
> 标志/FLC 538·524/保险理赔/累计）✅；回合递减与到期释放（`updatePlayerStates` 0x41C84F +
> `endPlayerState` 0x40D6BE）✅；保险期（**+62 = 保险期**，非卡片数）递减与理赔（`insurancePayout`
> 0x44BA63）✅；**门口保释/办出院 case 4/5 ✅**（`jail_dialog.cpp`，见 §1.2；NPC 格释放流程待 P5）；
> **开局默认在押 NPC ✅**（`newGameInit` 0x40734F..0x407363，见 §1.2 前置）。
> debug 测试：`Ctrl+Shift+J` 入狱 / `Ctrl+Shift+L` 住院（3 天）。
>
> **关键字段**：`Player.insuranceDays`（+62 `g_playerCardCnt` 0x496BA6，**保险期**剩余天数；原重写
> 误名 cardCount 并用于"发卡计数"——已订正：原版开局给的是**道具**（givePlayerCard 实为给道具，
> 见 §3.2）；`g_jailFlags` 0x496B30 / `g_hospitalFlags` 0x496B60（重写 `jailFlags`/`hospitalFlags`）；
> 監獄格/醫院格 = loadMapData 扫描 special 8002/8001（重写 `jailCellEntId`/`hospitalCellEntId`）。

### 1.1 入狱（`jailPlayer`，0x43D593）

`jailPlayer(player, days)`（cdecl，调用点 0x41C7D7）：

```
v4 = BYTE2(stateFlags[player])          // 当前監獄剩余天数（dword_496B9A BYTE2）
if (v4 != 0) { stateFlags BYTE2 = (days + v4) & 0x7F; }   // 累加
else {
    // 首次入狱完整流程：
    sub_40D761(player)                   // 清理当前格占用/状态
    sub_44F2C2(player, days)             // 停载具/清状态
    playerAlive &= 0xF                   // 清除 bit4-7（神明/状态位）
    cellEnts[playerCellEntId].occMask &= ~(256 << player)
    playerCellEntId = word_48BAE0        // 監獄格（special=8002 的 cellEnt）
    word_496B76[player] = 0
    playerRestoreDir = 15
    spriteX/Y = evtCells[2] 坐标（綠島，28*2 = +56/+58）
    stateFlags BYTE2 = days
    sub_40FC00(player)
    g_jailFlags[player] = 1                 // 監獄在押标志
    if (settings[1]) FLC 538（data.mkf） // 动画过程开关
}
refreshGameUi(...) + sub_44EF41(player, 2, 语音)  // 角色语音
sub_44BA63(player, g_moneyMul * 2000 * days)      // 罚款：2000 × 物价指数 × 天数
byte_496BAA[player] += days                        // 累计入狱天数（统计）
```

- `g_jailFlags`（0x496B30，8 字节）= 監獄在押标志（每玩家 1 字节）
- 住院同理：`hospitalizePlayer`（0x43EC3F）—— `g_hospitalFlags`（0x496B60）、FLC 524、
  sprite = `evtCells[1]`（醫院，+28/+30）、`stateFlags` **BYTE3** = 天数、
  目标格 = `word_48BAE2`（special=8001 的 cellEnt）、罚款同样 2000×moneyMul×天数
- **罚款 = 保险理赔**（0x44BA63）：`+62 保险期` 非 0 时由保险公司 specPt（costType==4）支付给玩家
  （`"保險期間\n\n得到理賠金\n\n%d元"` 0x4658FA + `transferMoney(保险公司+100, player, M×2000×days, 1)`）；
  无保险时**无扣款**（原版如此，帮助"入院付 1000/天"仅指保险理赔口径）
- **每回合递减**（0x41C84F 玩家分支）：stateFlags BYTE2/BYTE3 天数 -1，1 → 0x80（到期标记）；
  下回合 0x80 → `releaseJailNpc`/`releaseHospitalNpc`（玩家 <4：`sub_40D6BE` 恢复占用/朝向 +
  清 g_jailFlags/8B；重写在结束时同时清 BYTE2/BYTE3）
- **保险期递减**（0x41CC4B）：+62 每回合 -1，1 → 0x80；到期（0x41CAE3）清 0
- **入狱 FLC**：538 @ (0,40) flags=0x120001 sound=94（`settings[1]` 动画过程开启时）；
  **住院 FLC**：524 @ (0,210) flags=0x1E0001 sound=92（重写 `playEventFlc` 阻塞播放）

> 帮助 `[HELP 37]` 称"入院每天一千元"，实测代码为 **2000×物价指数/天**（帮助文本与实现不一致）。

### 1.2 门外保释/辦理出院（`jailBailDialog` / `hospitalVisitDialog`）

> **前置：开局默认在押 NPC（0x40734F..0x407363）** —— `newGameInit` 末尾在
> `memset(g_jailFlags,0,8)` / `memset(g_hospitalFlags,0,8)`（0x40732D/0x40733E）之后：
>
> ```
> byte_496B34 = 1   // g_jailFlags[4]      = 小偷（監獄）
> byte_496B35 = 1   // g_jailFlags[5]      = 強盜（監獄）
> byte_496B66 = 1   // g_hospitalFlags[6]  = 流氓（醫院）
> byte_496B67 = 1   // g_hospitalFlags[7]  = 間諜（醫院）
> ```
>
> 即：**不需要任何玩家被捕，踩監獄/醫院格就应有在押者显示**。NPC 名表 `dword_47ED5A[4..7]`
> = 小偷/強盜/流氓/間諜（索引 0..3 为占位 `0x11111111` 不用；玩家名走 `g_players`）。
> 重写补于 `newGame.cpp`（`jailFlags[4/5]=1`、`hospitalFlags[6/7]=1`；同时修掉重开局残留）。
> 读档 `loadGameFromSlot`（0x402dfc/0x402e0e）与玩家淘汰 `sub_40CD87` 也会写这两组标志。

> **实现状态（2026-09-23）**：**監獄保释 ✅**（`jail_dialog.cpp` `jailBailDialog`：人类 UI 还原
> 0x43CAAB/0x43C8FB + AI 还原 0x43D3DF + 音乐栈 push/resume + FloatMessage 点券不足；
> `landingEvent` case 4 接入）；**醫院办理出院 ✅**（`hospitalVisitDialog`：人类 UI 还原 0x43DA27
> 阶段机（#0127 开场 → 选择 → #0129 出院 → released 退出 / #0130 告别 / #0002 不足）+
> 护士眼睛/嘴部微动画 + AI 0x43EAA5 + case 5 接入）。
> **NPC 格（i≥4）保释/出院动画 ✅**（人类面板，2026-09-23）：点券 < 300 → #0002；否则
> `yesNoDialog(320,240)` → **擦格 + panel[64] 帧(i-4) 全身像标记**（監獄 @(365,450)、
> 醫院 @(420,450)；落点 = 实参 − 帧 offset）+ **答谢浮动消息**（監獄板帧1 @(210,150)、
> 醫院 @(200,200)；文本 `dword_475BE2[i]` = #0123~#0126）→ `releaseJailNpc`/`releaseHospitalNpc`
> → 消息播完面板重绘并退出（監獄 `byte_48C4C9` / 醫院 `byte_48C4F6`）。
> **事件槽数据与路面行走 ✅ 2026-09-26**（release*/busy 槽记录/`LABEL_88` 恶行/抓回；
> `498df0-event-slot-npc.md`）。

case 4 进入 `jailBailDialog`（case 5 为 `hospitalVisitDialog`，结构相同）：

```
if (无任何在押者：g_jailFlags[0..7] 全 0) return;        // 无人可保释
if (玩家是人类 alive==1) {
    dword_48C4B4 = panel.mkf[63]                      // 監獄界面（醫院 = panel.mkf[65]）
    dword_48C4BC = panel.mkf[64]                      // 共用资源
    dword_48C4B8 = allocUiElement(100, 95, 0, 0)
    sub_43C8FB(); musicPlayScene(15);                 // 醫院：sub_43D88F + scene 16
    runModal(sub_43CAAB, 0);                          // 保释选择界面（医院：sub_43DA27）
    释放资源
} else {
    // AI：rand()&1 决定是否保释
    if (rand() & 1) {
        按 aiPersonality 筛选候选人（0..3 己方 / 4..7 敌方；个性 0=己方、1=随机、2=敌方）
        v11 = 随机选一名在押者
        v12 = kPrisonBailCost[v11]                    // 點券费用表 dword_475C44
        if (點券足够（<4 玩家：>v12；>=4 NPC：>=700）) {
            showMessage(sprintf("保釋%s", 名字), 1500)
            playerPoints[currentPlayer] -= v12
            if (v11 >= 4) releaseJailNpc(v11)             // NPC 释放（医院：releaseHospitalNpc）
            else { stateFlags BYTE2 = 0x80; g_jailFlags[v11] = 0; }  // 玩家释放
        }
    }
}
```

- 點券费用表 `dword_475C44`（監獄）/ `dword_475CA4`（醫院）：`{30,30,30,30,300,300,300,300}`
  （玩家 0..3 = 30 點券，事件槽 NPC 4..7 = 300）
- 保释界面：`sub_43CAAB`（監獄，panel.mkf[63]/[64]）/ `sub_43DA27`（醫院，panel.mkf[65]/[64]）
- 文本：`unk_465169` = `"保釋%s\n#0127您好！請問您要替誰…"`（監獄）；
  `unk_465207` = `"保釋%s\n\n%s\n\n送您出國旅遊…"`（醫院）

### 1.3 事件槽 NPC 犯人表（`releaseJailNpc` / `releaseHospitalNpc`）

保释后（a1 >= 4 的事件槽 NPC）：

```
byte_498E30[16*i] = g_currentPlayer          // 保释人（i = a1-4）
word_498E2C[16*i] = word_48BAE0/word_48BAE2  // 監獄/醫院格
g_miscTable80[16*i] / word_498E2A[16*i] = 格坐标
byte_498E32[16*i] = 0
byte_498E33[16*i] = 1（監獄）/ 2（醫院）；若格子类型 == 4/5 则 |= 0x80
g_jailFlags[a1] / g_hospitalFlags[a1] = 0              // 释放
loadWalkResources(a1)
```

- `word_48BAE0` = special 8002 的 cellEnt（監獄格）；`word_48BAE2` = special 8001（醫院格）
  （`loadMapData` 0x408023..0x408068 扫描）
- **每项 16 字节**（`i = a1-4`，共 4 项 = 0x498E28..0x498E68；权威字段表见
  `498df0-event-slot-npc.md` §1.1）：
  `+0 u16 格坐标 x`（`g_miscTable80`） / `+2 u16 格坐标 y`（`word_498E2A`） /
  `+4 u16 監獄/醫院格 cellEnt`（`word_498E2C`） / `+6 u16 = 0`（`word_498E2E`） /
  `+8 u8 保释人`（`byte_498E30`） / `+9 u8 朝向`（`byte_498E31`） /
  `+10 u8 行动状态 busy`（`byte_498E32`，**与绝对视角 `byte_498DF2[16*p]` 同一字节**；
  0=自由游走 / 1=監獄 / 2=醫院） /
  `+11 u8 状态`（`byte_498E33`：1=監獄 / 2=醫院，格子类型 == 4/5 时再 `|= 0x80`）
- `releaseJailNpc` 0x43D7BF / `releaseHospitalNpc` 0x43EE6E：`a1 < 4` 走 `sub_40D6BE(a1)`（玩家
  恢复占用/朝向）；`a1 >= 4` 填上表 + 清标志；末尾都调 `loadWalkResources(a1)`（供该槽在地图现身）
- **答谢消息表 `dword_475BE2`**（0x475BE2，内存布局实测）：`u32 voice[4]`（= 0x96/0xB7/0xA7/0x3E）
  + `u16 pad` + `LPCSTR thanks[4]`；原版代码 `&dword_475BE2[i]+2`（i=4..7）正好取到
  `thanks[i-4]` = #0124/#0125/#0123/#0126（对应 NPC 4..7：小偷/強盜/流氓/間諜），
  文本 `#NNNN` 前缀自动触发对应语音。重写 `kNpcThanks[4]`（jail_dialog.cpp）監獄/醫院共用
- **人类面板的点数检查 = 费用表本身**（NPC 300，0x43D0DA/0x43E77A），**`>=700` 只在 AI 分支**
  （`jailBailDialog`/`visitAi` 的 NPC 目标）——两者勿混
- 玩家 0..3 释放：`sub_40D6BE(player)` + 清 `g_jailFlags/8B`
- 四大惡人（小偷/流氓/強盜/間諜）即保释出狱的事件槽 NPC（帮助 `[HELP 47/52/53/56]`），
  出狱后在路面行动（行窃/保护费/抢卡/盗领）—— **✅ 已实现 2026-09-26**；
  权威规格 `498df0-event-slot-npc.md`（恶行 §2.7 / 物件分支 §2.6 / 抓回 §2.8）

### 1.4 踩格触发（`onPlayerActionPhase` @0x41C7A6）
```
cl = byte_498DF3[16*player] & 0x7F
if (cl == 1 && 格子类型 == 4) {          // 監獄状态 + 監獄格
    if (byte_498DF3[16*player] & 0x80) { jailPlayer(player, 0); dword_48BAF8 = 0; }
    else { byte_498DF3[16*player] |= 0x80; }
} else if (cl == 2 && 格子类型 == 5) {   // 醫院状态 + 醫院格
    同上（hospitalizePlayer）
}
```

- `byte_498DF3`（0x498DF3，= 槽记录 `byte_498E33`）：bit0-6 = 状态（1=監獄、2=醫院），bit7 = 抓回闩锁
  （release 起点类型匹配即置位；`g_jailFlags/8B` 是保释面板的"在押列表"，两者分工已确认——
  抓回判据用 status，面板用 flags）
- 入狱/住院期间：`onPlayerActionPhase` 的 `g_playerCellNo` 递减（`byte_496D0C[24*cellNo-24]`），
  到 0 → `sub_40E14D` 释放 + FLC 525 + `hospitalizePlayer(p,5)`/`jailPlayer(p,5)`（出院/出狱）；
  同格有其他存活玩家且无 cellNo → 传染转移（见 `41b42d-on-player-action-phase.md`）

### 1.5 入狱来源：陷害卡（`sub_4444BF`，卡片 17）

```
v = 选目标（人类 sub_446AE8(0xE0C0010) 选人界面 / AI sub_41E6F2(0)）
if (v) {
    sub_441343(cur, 17);                                  // 移除陷害卡
    sub_44EF41(cur, 3, 语音 off_48127A[90*charIndex])     // 使用者语音
    v5 = sub_40D293(v);                                   // 目标解析
    if (cur 不是人类)
        sub_40E669(0, curX, curY, targetX, targetY, 100); // ★"被带走"移动演出（速度 100）
    if (v5 >= 4) jailPlayer(cur, v5, 5);                  // NPC 目标 5 天
    else {
        addPlayerDebt(v5, cur, 150*M);
        免罪卡(21) → sub_444BB2(v5)                        // 抵消
        嫁禍卡(19) → sub_44476A(v5,0,0) 换目标
        if (v5 == cur) jailPlayer(cur, cur, 4)            // 反弹自己 4 天
        else jailPlayer(cur, v5, 5)                       // 目标入狱 5 天
        sub_44EF41(v5, 1, 目标语音 off_48136A[...])
        復仇卡检查 → sub_444691 + jailPlayer(cur, cur, 5)
    }
    sub_41D546();
}
```

- **"被车带走"演出 = `sub_40E669`（移动动画）**：AI 使用陷害卡时从使用者位置移动到目标位置
  （人类使用的演出在选人界面 `sub_446AE8` 内，P4 待逆向）
- debug `Ctrl+Shift+J` 直接调 `jailPlayer`，因此**没有该演出**（FLC 538 是"警车到达綠島"）
- 卡片效果函数表 `g_cardEffectFuncs`（0x475D5C）索引 17 = 0x4444BF；索引 1..30 对照
  `kCardNames`（表项 18..21 = 0x4420D5 共用空实现？P4 卡片系统时逐个标注）

### 1.6 卡片/工具条功能结束的回合推进（`sub_40DEFE` 0x40DEFE）

工具条分发器 `sub_417D65` 在功能（帮助/设置/读档/存档/大地图/交易/查詢/**卡片** 等）结束后：

```
setPauseDraw(0); disablePlayerControl();      // 进入功能前先锁定
switch (功能) { ... case 8: useCardFlow(); ... }
if (sub_40DEFE() == 1 && (功能 != 读档 || 结果 != -1)) {
    enablePlayerControl(); setPauseDraw(1);   // 恢复等待操作
}
```

`sub_40DEFE`：
```
if (cur < 4 && (alive != 1 || stateFlags || state37 || byte54)) {
    byte_498EA0[cur] |= 0x80;    // 有状态：标记重入回合（重写 playerActionFlags |= 0x80）
    return 0;                    // 不恢复控制
}
return !byte_498EA2[cur] && !sceneRequest && !quitGame;
```

- 有状态（入狱/住院/住宿/冬眠/梦游/AI）→ `playerActionFlags |= 0x80` → 下一帧
  `beginPlayerTurn` → `checkPlayerAction` 状态分支（提示"坐牢中 還剩N天！"）→ `calcPlayerWait`
  返回 -125 → 下一位玩家
- 无状态 → `enablePlayerControl` 继续等待操作
- **useCardFlow（0x441BAA）**：人类 `runModal(sub_4416F0)` 选卡 → `sprintf("使用%s", 卡名)` →
  `sub_441F73(卡id, 文本)` 显示卡片图 → `g_cardEffectFuncs[卡id]()` 生效；效果返回 0（取消/失败）
  则循环继续选卡；AI 分支按个性/可用性从 `g_cardState60` 前 8 槽试选（`sub_41E69E` 判定）
- **P4 卡片系统实现时按 `sub_40DEFE` 接入回合推进**（debug `Ctrl+Shift+J`/`L` 已按此对齐）

### 1.7 事件槽 NPC 地面行走 → 恶人效果 → 回归監獄/醫院（**✅ 已实现 2026-09-26**）

> **权威规格已迁移至 `498df0-event-slot-npc.md`**（本小节仅作历史索引；下方字段表为 2026-09-23
> 初查版本，含两处误读已在专用文档订正：`498DF0`=保释人、`498DF2`=`498E32`=busy 同一字节）。
> 实现：`turn_system.cpp`（`npcVillainPhase` / 释放·抓回 / `moveOneStep` 槽同步 /
> `updatePlayerStates` NPC 计时）、`jail_dialog.cpp`、`game_panel.cpp`、`map_render.cpp`、
> `new_game.cpp`、`save_data.cpp`；调试 `Ctrl+Shift+8/9`。

**实现链（历史索引，细节以专用文档为准）**

1. **释放写入**：`releaseJailNpc 0x43D7BF` / `releaseHospitalNpc 0x43EE6E`（bailer/busy=0/
   cell=監獄·醫院格/status=保释建筑|0x80/清 flags/`loadWalkResources`）
2. **面板显示**：`sub_415F69` else 分支（帧 5 背景 + `dword_47ED5A[cur]` 名 + bailer 棋子帧 2）
3. **回合调度**：`nextPlayer 0x418EBD`（依次 4..7，`busy==0` 才进槽）
4. **行走/转向/坐标**：`onPlayerActionPhase 0x41B42D` NPC 分支；`moveOneStep 0x40C05C`；
   `sub_40C912`；`getObjectPosition 0x40AF12`
5. **踩格抓回**：`onPlayerActionPhase @0x41C7A6`（status 与落地格同类型 → `jailPlayer`/`hospitalizePlayer`）
6. **四大恶人效果**（`LABEL_88 0x41C447`）：同格偷点券/抢卡、过银行抢 20%、停留勒索/窃租金红利 —
   完整金额字段/消息/条件见 `498df0-event-slot-npc.md` §2.7

**初查清单（已全部完成）**

- [x] `onPlayerActionPhase` 事件槽分支逐 case（0x41B9D6..0x41C7A6）
- [x] 行走路径/圈数规则（busy/status/timer 语义、同类建筑抓回）
- [x] 恶人效果四段定位（LABEL_88/LABEL_102/cur6/7 段）
- [x] `sub_40CD87` 对犯人表的清理（保释人破产关回）
- [x] `sub_4440EA` 对事件槽的处理（timerA=5/timerB=0）

## 2. case 10/11/12 得點券（✅ 已实现）

| case | type | FLC | 文本（BIG5 地址） | 点数 | 语音 |
|------|------|-----|-------------------|------|------|
| 10 | 10 得５０點 | 537（0x219） | `"得點券５０點"`（0x463A81） | +50 | 槽 `charIndex*27 + rand&1`（off_48084A） |
| 11 | 11 得３０點 | 537 | `"得點券３０點"`（0x463A8E） | +30 | 槽 `charIndex*27 + 2`（off_480852） |
| 12 | 12 得１０點 | 537 | `"得點券１０點"`（0x463A9B） | +10 | **无**（0x41B2FD 直接 jmp default） |

流程（0x41B184/0x41B21E/0x41B2A3，均返回 0x80）：
```
res = sub_450441(data.mkf 句柄, 537, 0)        // data.mkf[537]
sub_45144F(res, 0xCC, 0xB4, flags=1, soundId=0x62)  // 阻塞播放：x=204,y=180,透明,Effect.mkf[98]
sub_456E11(res)                                // 释放
showMessage(text, 1000)                        // 3E8 = 1000ms
g_playerPoints += 50/30/10
[case 10/11] sub_44EF41(player, 0, 角色语音槽)   // case 12 无
```

- FLC 537 实测：声明 14 帧（数据 15）、31x39、8bit、头速度 71ms；首帧含未知 chunk 0x12 +
  调色板 0x04 + 全帧 RLE 0x0F，后续 0x07 全帧 BRUN
- `flags=1` → `g_flcTransparent`（0x450CED）：**透明像素 = 调色板索引 0**（0x4506C7 索引 0 保留目标值）；
  537 的 palette[0] = (0,139,83) 深绿
- `soundId=0x62` 经 `sub_454304` 确认 = **Effect.mkf[98]**（`sub_450441(dword_48A058=Effect 句柄, 98, 0)`），
  首帧解码后 `sub_45434F` 播放
- 重写：`turn_system.cpp` `playEventFlc`（阻塞逐帧 render + pumpEvents + 音频续喂）+
  `landingEvent` case 10/11/12 分支；`game_loop.cpp` `drawEventFlcFrame`（色键跳过）；
  `FliDecoder::colorKey()`；角色语音 → P4

## 3. case 13 卡片（✅ 已实现）

流程（0x41B302）：
```
res = sub_450441(data.mkf, 536, 0)          // data.mkf[536]
sub_45144F(res, 0xD0, 0xB4, flags=1, soundId=0x63)  // 阻塞：x=208,y=180,透明,Effect.mkf[99]
sub_456E11(res)
v = sub_441E12(currentPlayer)               // 从赠卡池抽卡入卡包；0 = 池空
if (v == 0) jmp default                     // 不弹提示
sprintf(chText, "得到%s！", dword_47FDEA[v*8])  // 卡片名（0x463AA8 格式串）
sub_441F73(v, chText)                       // 卡片图 + 提示框 + 1500ms
sub_44F230(currentPlayer, byte_47FDEF[v*8]) // 卡片获得语音（P4）
```

### 3.1 卡片表 `dword_47FDEA`（0x47FDEA，30 项 × 8 字节；项 0 为空占位）

| 偏移 | 含义 |
|------|------|
| +0 | 名称指针（BIG5） |
| +4 | **赠卡池初始数量**（`byte_47FDF6`，newGameInit 0x4071AC 写入 `g_propStock[]`） |
| +5 | **价格（点券）** `byte_47FDEF`；卡包满时舍弃最小值（0x44128F） |
| +6 | 目标类型（0/2，P4 使用流程待深入） |
| +7 | 标志（0/1/2，P4 待深入） |

卡 id 1..30 顺序：均富/均貧/購地/換地/換屋/轉向/改建/拍賣/天使/惡魔/怪獸/拆除/搶奪/停留/
冬眠/夢遊/陷害/復仇/嫁禍/免費/免罪/送神符/請神符/紅/黑/查稅/漲價/查封/同盟/烏龜
（改建卡 = 7，与 `cardRebuildEffect` 0x44309B 的"卡片 7"吻合；价格见 `kCardPrices`）。

### 3.2 卡包与赠卡池

| 数组 | 地址 | 语义 |
|------|------|------|
| `g_cardState60` | 0x499120 | 4 玩家 × 15 槽（`15*p+i`），槽内存**卡 id**，0=空；卡片格的抽卡放入此处 |
| `g_propStock` | 0x499198 | 30 种卡的**赠卡池**剩余数量（= `byte_499197[1..30]`；命名旧称"道具库存"有误） |
| `g_playerCards` | 0x49915B/0x49915C | givePlayerCard（0x445A4D）计数数组（15/玩家，同名卡上限 9，card≤8 受 `g_cardPool` 限制）；与 cardState 的分工待 P4 卡片使用流程厘清 |

辅助函数：

| 函数 | 地址 | 语义 |
|------|------|------|
| `sub_441262` | 0x441262 | 卡包非空槽计数 |
| `sub_44128F` | 0x44128F | 找价格最低（`byte_47FDEF` 最小）的卡（舍弃候选） |
| `sub_441343` | 0x441343 | 移除指定卡（后槽前移 + 尾槽清零 + 池恢复 `++byte_499197[card]`） |
| `sub_4412E4` | 0x4412E4 | 放入卡包：满 15 → 先舍弃最低价 → 找空槽 → `--byte_499197[card]` |
| `sub_441E12` | 0x441E12 | 抽卡：按池数量展开 0-based 索引列表（栈 136B）→ `rand()%n` → 返回索引+1 |

### 3.3 显示函数 `sub_441F73`（0x441F73）

- 卡片图 `data.mkf[570+cardId]`（**165×256 RGB555 无头位图** 84480B；头部 12B 是像素数据）
- 内嵌元素头 `dword_441204` = `{0x010000A5, 0, 0}`（w=0x00A5=165, h=0x0100=256, 偏移 0）
- `blitElementFullscreen(dst, elem, 138, 200)`（不透明）
- 提示框 `g_tipFrame + 72`（帧 5）@ (220,129) + 文本（居中 align 4）
- `audioPlayEffect(dword_482402)` = 游戏音效**槽 23**（Effect.mkf[62]）
- `sub_4528B9(0x5DC = 1500)` 延时；期间原版保存/恢复 (0,40,440,480) 背景
  （`sub_451E7E`/`sub_451EDB`，重写逐帧 `renderGameFrame` 等价）
- 重写：`turn_system.cpp` `drawFreeCard`/`giveCardToBag`/`cardBag*` + `showCardGet`（runModal 模态）

## 4. case 2 新聞（`newsEvent`，36 事件）

> **实现状态（2026-09-26）**：✅ 全 36 条已实现（`src/app/news_dialog.cpp` + `include/game/app/news_dialog.h`）：
> `newsEvent` 主循环 / `newsEventCheck` 36 判定 / `g_newsFuncs` 36 效果（排名·三税·灾害·股价停牌·
> 超贷坐牢·公司罚款）；panel[66] 画布 + data[441+idx] 插画 + 6 类标题 + 2400ms 阶段机；调试 `Ctrl+Shift+A`。
> **拍卖 idx 7 桩**（`sub_43BDE5` 下一轮）、语音 `sub_44EF41`/浮动数字 `sub_44F354` → `TODO(P4)`。
> **完整研究与逐条笔记见 `44b6df-news-events.md`**；以下为早期简化记录（部分细节以新文档为准）。

```
dword_48C5AC = panel.mkf[66]               // 新闻界面框架（388x251）
do {
    v1 = g_newsOrder[g_newsPos]      // 事件顺序表（36 项，洗牌后）
    v2 = newsEventCheck(v1)                    // 判定是否触发（返回 1 = 本次触发）
    if (v2 == 1) {
        data.mkf[v1 + 441] → blitBackground(25, 44)   // 事件插画（36 张：441..476）
        drawText(off_475ED8[byte_475EB4[v1]], 24, 8)  // 标题（6 类）
        funcs_44B7BD[v1](0)                // 事件效果（36 个函数）
    }
    if (++g_newsPos == 36) g_newsPos = 0;
} while (!v2);
显示 2400ms → funcs_44B7BD[v1](1)          // 效果后处理（如刷新 UI）
```

| 数据 | 地址 | 说明 |
|------|------|------|
| `g_newsOrder` | 0x499090 | 36 项事件顺序（`shuffleNewsOrder` 洗牌生成：Fisher-Yates 变体 + `rand`） |
| `g_newsPos` | 0x4990E0 | 当前索引（0..35，循环回绕） |
| `byte_475EB4[36]` | 0x475EB4 | 标题索引（6 类：0×6, 1×8, 2×2, 3×2, 4×4, 5×14） |
| `off_475ED8[6]` | 0x475ED8 | 标题文本：無責任新聞/政府公告/社會新聞/路況報導/氣象報導/財經新聞 |
| `funcs_44B7BD[36]` | 0x475E24 | 事件效果函数表（0x448ECA, 0x448F45, …） |
| `data.mkf[441..476]` | — | 36 张事件插画 |
| `newsEventCheck` | 0x448BE2 | 触发判定（未反编译，待深入） |

> ⚠️ 重写代码中 `GameState.cardShuffle`/`cardShufflePos`（`new_game.cpp`/`save_data.cpp`）
> 实为**新闻事件顺序表** `g_newsOrder`/`g_newsPos`——建议改名 `newsOrder`/`newsPos`。

## 5. case 3 命運（`fateEvent`，37 事件）✅ 已实现 2026-09-26

> 完整研究（49 效果/幸运判定 `sub_44B896`/卡片联动/出国状态）见 `44db81-fate-events.md`；
> 本节保留早期骨架笔记。已知订正：`g_fateOrder` 为**洗牌**（`sub_44BAEA`），非固定顺序。

```
dword_48C5E0 = panel.mkf[66]
do {
    v5 = g_fateOrder[g_fatePos]       // 命运事件表（37 项）
    v2 = fateEventCheck(v5)                    // 触发判定
    if (v2 == 1) {
        if (v5 >= 33) {                    // 地图专属事件（4 地图）
            data.mkf[word_475FB4[4*mapIndex + v5]]  // 插画（按地图）
            funcs_44DC44[4*mapIndex + v5](0)        // 效果（按地图）
        } else {
            data.mkf[word_475FB4[v5]]
            funcs_44DC44[v5](0)
        }
    }
    if (++g_fatePos == 37) g_fatePos = 0;
} while (!v2);
显示 1600ms → 对应效果函数(1)
```

| 数据 | 地址 | 说明 |
|------|------|------|
| `g_fateOrder` | 0x496B38 | 37 项命运事件顺序表（**开局 `newGameInit` 0x44BAEA 洗牌**；旧文档"固定顺序"有误） |
| `g_fatePos` | 0x4990B4 | 当前索引（0..36） |
| `word_475FB4` | 0x475FB4 | 插画资源索引 49 项（0..36 通用 + 37..48 = map1..3 的 33..36 变体） |
| `funcs_44DC44` | 0x475EF0 | 效果函数表 49 项（0x44BE16..0x44DB53） |
| `fateEventCheck` | 0x44BB4B | 触发判定（载具/股票/地产/卡数 + gameMode；已实现） |

## 6. case 6/7/8 小游戏（三游戏交互版 ✅ 2026-09-25，`minigame_dialog.cpp`）

| case | 函数 | 入口行为 | 返回 |
|------|------|---------|------|
| 6 企鵝挖寶 | `miniGameDig`（0x415215，WndProc `sub_414858`） | 9×9 埋宝 + 企鹅行走挖取 | 得分（ax） |
| 7 七彩氣球 | `miniGameBalloons`（0x4154DC，WndProc `sub_414BBC`） | 16 气球上升 + 点击射破 | 得分 |
| 8 喜從天降 | `miniGameMoneyRain`（0x4155FC，WndProc `sub_414FCD`） | 鼠标控娃娃接钱袋 | 得分 |

**已实现（2026-09-25）**：`src/app/minigame_dialog.cpp` + `include/game/app/minigame.h`
（`miniGameVisit`，landingEvent 0x41B146/0x41B15E/0x41B16C 接线）——
**三游戏交互版全部 ✅**（`miniGameDigRun`/`miniGameBalloonsRun`/`miniGameMoneyRun`）；
AI/托管/动画关闭/资源失败同走原版
共享 else 兜底：**[RE 0x415457]** `score = rand()%20+50`（50..69）→
`showMessage("得點券%d點", 2000)`（**0x463797**，cp950 字节复核：`B16F C249 A8E9 '%','d' C249`）→
台词 `sub_44EF41(p, 0, off_48084A[char*27 + (rand&1)])`（= `kValueLines[char][rand&1]` 列 0/1）→
调用方 `g_playerPoints[104*cur] += ax`（0x41B152）。
**门控**：原版 `g_playerAlive[cur]==1 && byte_497159`（=settings[1] 動畫過程）才进交互小游戏；
AI/托管/动画关闭 → else。当前人类亦走 else（交互版未接入，行为=原版关动画）。
**差异**：事件槽 NPC（4..7）走 `miniGameVisit` 的 else 分支不发分（`miniGameVisit` 对 p>=4 直接返回；
原版 NPC 亦随机得分——保留差异）。恶人自身行为在 `onPlayerActionPhase` NPC 分支
（`498df0-event-slot-npc.md`），与小游戏无关。

### 交互小游戏框架（2026-09-25 会话逆向，实现时直接照此）

**公共流程**（三游戏同构）：门控 → `audioRegisterEffects(表)` → 载 Panel.mkf 资源 →
初始化布局 → `musicPlayScene(场景)` 压栈 → `runModal(WndProc)` → `musicStackPopRestore` →
卸音效（`sub_454240`）→ 释放资源（`sub_456E11`）→ 返回 `g_miniScore` 得分。
**开场计数缩短 quirk（2026-09-25 实机订正）**：0x401 设 intro=99，但 **WM_PAINT(0xF)
分支立即把 99 降为 5（氣球 0x414FB3）/ 10（接钱 0x4151F1，50ms）**——首次 InvalidateRect
即触发，实际开场仅 ~0.5s 就出标题 FLC；挖寶 intro=10 无缩短。
**数字字形两种 blit 勿混（2026-09-25 实机两轮订正）**：HUD 小数字（帧 0..9）原版走
`blitElementFullscreen`(0x4563F5) = **不透明像素拷贝**——数字黑色填充的 RGB555 值恰为 0，
色键 blit 会把填充当透明吃掉只剩白线框；结算大字（帧 10..19）原版走
`sub_456418` = `0x455C52 blitElement` **色键**封装（大字背景需透明，opaque 会出黑底方块）。
- 共享全局块（游戏局部状态，重写收进 struct）：`dword_48BCEC`=得分、`byte_48BD58`=阶段
  （0 进行/1 请求结束/2 结算中）、`dword_48BD2C`=倒计时、`dword_48BBB4/BBB8/BBBC/BBC0`=四类计数。
- WndProc 消息：`0x400` WM_USER 进入（设倒计时 + 开场计数 99 + SetTimer）→ `0x401`→`0x404`
  开场 `sub_45144F(panel[78] FLC)` + `cursorSelect` + `setPauseDraw(1)`；`0x405`=开场结束；
  `WM_TIMER(0x113)` 每 tick：开场计数→倒计时→`byte_48BD58` 1→20tick 结算→`postModalExit(0)`；
  `WM_PAINT(0xF)` 仅 ValidateRect+Blt（绘制在 timer 内完成）。
- 大字结算 **[RE 0x414789]**：`sprintf("%d", 分)`，每位 = panel[79] 子帧 `(ch-'0')`，
  x=`353-66*len/2` 起步、字宽 66、y=150。
- HUD y=421（panel[79] 数字字形，`sprintf %03d/%02d` 逐位 blit）：倒计时 3 位 @(49,69,94) +
  单位 @(114)；四计数 @(185,205)/(276,296)/(367,387)/(458,478)；总分 @(549,569,589)。
- 音效表（Effect.mkf 索引序列）：挖宝 `dword_475057`={11,12,13,14,16,17,18,15}；
  气球 `dword_47509F`={19,20,21}（miss=4750A7=20、hit=4750AF=21）；接钱 `dword_4750BF`=空(-1)。
- 音乐：**[RE 0x4549CF]** 场景 id 挖宝 12 / 气球 11 / 接钱 10。

**case 6 企鵝挖寶**（100ms×150 tick=15s；开场 dword_48BD7C=10）：
- 9×9 网格：`word_474D7C`（可埋图，非 0 有效）+ `word_474D80`（步距 4 word：+0 有宝标志、
  +4 低 nibble=翻开态 / 高 nibble=宝物类型 1..5）。
- **[RE 0x412014]** 埋宝：5 类数量表 `g_digTreasureCounts`(0x411FC8)={3,12,3,9,1}（Σ28）；
  逐类 `v11=(v0*rand())>>15`（v0=64 起、每放一个 --）在有效未占用格中随机落位（类型 v12+1）。
- 点击（0x201/0x203）：读帧 **81** 像素掩码 `px%9 / px/9` → 格坐标 → **[RE 0x41211C]**
  若格有效且非上次目标 → 设企鹅目标（16.16 定点 `dword_48BD04/48BD08`，增量
  `dword_48BCFC/48BD00`=Δ/格数）→ **[RE 0x412287]** 行走、到达挖取 → **[RE 0x4124C8]**。
- 得分 **[RE 0x413A4A]**：`分 = 5*BBB4 + 20*BBC0 + 12*BBB8 + 8*BBBC`；
  结算档位 `dword_48BCCC`：分>55→4（音效 13）/ 40..55→6 / <40→5（音效 14）。
- 资源 panel：78 开场 FLC、79 数字、80 背景、81 网格掩码(640 宽)、82/83/84/85 企鹅帧、
  86..90 = 5 宝物帧。
- **✅ 2026-09-25 实现**（`miniGameDigRun`/`digHandler`/`digTick`/`drawDig`）：
  - 坐标表 **0x474D7C 为 exe 静态数据**（9×9×8B：x,y,state；x=0 无效共 69 格；
    5 格 state 高 nibble=1 = **冰屋邻格**，企鹅站上时中央 (320,225) 画 panel[80] 帧3 冰屋遮挡）。
  - panel[80] 帧义（看图确认）：0 雪山背景、**1 企鹅持铲站立**、**2 黑化企鹅**、
    **3 冰屋**、4..8 = 5 种雪堆宝（type+3）、9 = 挖后洞。
  - **type1 = 火煤球**（panel[86] 橙色火球 6 帧；数量 3）：挖到 → 黑化动画 15 tick →
    **直接结束**（音 15，不加分）；type2..5 → 计数++（音 16/17/17/18，表 byte_475051）+
    宝物展示 7 tick。
  - 行走 = **8 方向×4 帧**（panel[82] 32 帧；方向公式**以 0x412658 反汇编为准**
    （Hex-Rays 把 jle/jnz 分支条件解析反了，照抄反编译会**倒退走路**）：
    `col差>0 → 3-row差；col差==0 → row差>0?1:5；col差<0 → (row差+7)&7`；
    帧表语义（看图）0=下 1=右下 2=右 3=右上 4=上 5=左上 6=左 7=左下；挖掘同 8×4（panel[83]）；4 tick/格。
  - **记忆游戏规则**：宝物堆仅在开场（intro+标题 FLC 期间）显示，0x405 `digInitDraw(0)`
    重铺背景后**堆全部消失**，玩家凭记忆选格挖；标题 FLC（panel[78] READY GO 19 帧）
    在开场期每 tick 叠加绘制（重写曾漏 → 实机无 ready go）。
  - **挖洞时机**：case3 **phase2 画洞（无论有无宝）**、phase3 判宝——`dug` 置位在 phase2
    （空格也要留坑，原版表面残留语义）。
  - 结局动画 panel[84]（>55，音 13 循环）/panel[85]（<40，音 14 循环）各 4 轮；
    40..55 = 站立 15 tick（case6）。
  - 点击掩码 **panel[81] = RAW 640×480 8bit**（值=row*9+col，直接按字节索引）。
  - 原版靠**表面保留**残留挖过的洞 → 重写补 `dug[9][9]` 显式绘制（即时模式等价）。
  - 结算期（gamePhase==2）点击 → 倒计时置 1 跳过（原版 quirk）。

**case 7 七彩氣球 ✅ 交互版 2026-09-25**（`minigame_dialog.cpp` `miniGameBalloonsRun`，
100ms×150 tick=15s；开场等待（intro 99，**首次 WM_PAINT 降为 5**，0x414FB3）→
panel[78] 标题 FLC → cursorSelect(9,3,5) 枪光标）：
- 气球数组 `g_balloons[4i]`（i<16）：+0 x（0=空槽）、+2 y、+4 类型：
  0..8=分值 type+1（0..4 大气球/5..8 小气球）、**9**=分×2、**10**=分÷2、
  **11**=随机效果、**60**(0x3C)=已破（每 tick 高 nibble -0x10，3 tick 爆裂帧后清槽）、
  **0x80**=诱饵气球（特殊表哨兵，高 nibble≠0 → 静止不可点、8 tick 消失、画帧 1）。
- **绘制裁剪 y<387（2026-09-25 实机订正）**：原版气球画布带 clip rect（sky 区
  0..387）——气球从底部木条面板**后面**升起进入视野（初生 y=420 不可见）；
  重写 `drawBalloon` 用 `blitElementRegion` 限高 387-top 等价；顶部完全出界才清槽。
- **[RE 0x414BBC]** 点击命中盒：type≥6 → x±18/y±26；否则 x±22/y±30；**按下与抬起各射
  一枪**（0x201/0x203 同处理，原版 quirk）；一次射击可命中多个；音效单 buffer 语义
  （命中 21/miss 20 取最后处理者）。结算：0..8 → `分 += type+1`（cap 999）；9 → `分×=2`；
  10 → `分>>=1`；11 → `rand%6`：0=倒计时置 1（即终）、1=slowdown=20（静止 2s）、
  2/3=风力 -1/+1（速度 ×2 / ÷2）、4=分清零、5=分×2。
- **[RE 0x412F6F]** 每 tick：上升 `kBalloonSpeed[12]`={15,15,15,15,18,18,18,24,24,24,24,18}
  （byte_475004，风力修正 ×2/÷2；slowdown≠0 全体静止）；出画（blit 全裁剪）清槽；
  空槽且 phase==0：`rand()%1000` <20 → type=r>>2(0..4)、<28 → ((27-r)>>1)+5(5..8)、
  <30 → `kBalloonSpecial[10]`={9,9,9,9,9,10,10,10,10,0x80}（byte_475039）、97% 不出；
  生成列候选 x=40..600 步 80（列内无 y>300 气球才可）→ 随机列 @y=420 + 音效 19；
  **无气球 && phase==1 → phase=2**（提前结算）。
- **[RE 0x4146EE]** 初始：背景 panel[91] 帧 0 全屏 + HUD；**[RE 0x413F07]** HUD：
  倒计时 %03d @(49,69,94)+单位帧0 @(114)、得分 **%04d** @(529,549,569,589) @421
  （panel[79] 帧 0..9，不透明）。
- 资源 panel[91]（SMP 14 帧，**看图确认**）：0=游乐园背景（摩天轮/火箭/旋转木马 +
  底部木条 HUD 含白色数字框）、1..12=气球 type 0..11（带分值数字/×2 绿/? 紫）、
  13=爆裂碎片；panel[78]=标题 FLC（640×480 25 帧，三小游戏共用）；
  panel[79] 帧 10..19=结算大数字（紫色立体，字宽 66）。
- 结算：大字 2s（sub_45285E 2000ms）→ 返回得分 → 点券入账。

**case 8 喜從天降**（50ms×360 tick=18s；cursorSelect 41；开场 data.mkf[526] FLC）：
- **[RE 0x4155FC]** 前置参数：`word_48BD4C=110`（钱袋出生 y）、`word_48BD4E=320`（娃娃 x）、
  `word_48BD52=(娃娃宽-5)>>1`（接取半宽）、`word_48BD44=3/46=4/42=-1`、`dword_47504B=帧92`；
  娃娃图 = **panel[100+charIndex]**、钱袋 5 图 = panel[95..99]（`dword_48BD14[0..4]`）、
  场景 92/93/94（BCE4/BCF4）。
- **[RE 0x414FCD]**：开场 byte_48BD5A 播 data[526] FLC（`flcDecodeFrame` 逐帧，播完
  `byte_48BD58=2` 直接结算）；每 tick `sub_41417E(2)` HUD + `sub_413248()` 钱袋下落/接取；
  倒计时显示 `tick>>1`（秒）；评级 `word_48BD56`：≥60→3 / ≥50→2 / ≥40→0 / <40→1。
- 得分 **[RE 0x41417E]**：`分 = 10*BBB4 + 5*BBB8 + 3*BBBC + 1*BBC0`。
- **✅ 2026-09-25 实现**（`miniGameMoneyRun`/`moneyHandler`/`moneyTick`/`drawMoney`）：
  - 钱袋 4 类分值 **type0=10/type1=5/type2=3/type3=1**（[RE 0x4123D7] 生成
    rand%20：<9→3、9..14→2、15..17→1、18..19→0）+ **type4=炸弹袋**（云投，
    rand%20 不产生）；y=100 出生、vy=-16 上抛 +2/tick 重力 cap16、y>=130 匀速
    kMoneyFallSpeed{24,18,15,12,15}（byte_475010）、y>380 漏接消失（无惩罚）。
  - 摆动 = 摆幅(x)=(x-320)*50/210（flt_463774=210/463778=260）× 进度 (y-130)/250；
    缩放 blit **[RE 0x4568C2]** = 16.16 定点 0.5x→1.0x（32768*(1+进度)，flt_463784=32768）；
    旋转帧 (flags>>4)&7（bit7 清零回绕，panel[95..99] 各 8 帧）。
  - 财神 panel[93]（19 帧）y=126 巡游（state 0 右行/4 左行 ±12/tick、2/3 转身 5 tick、
    边界 110/530；每 richWait=rand%5 帧 `sub_4123D7(x,0)` 撒袋；帧表 byte_475015[6][6]）；
    **云 panel[94]** y=125 帧 0..11（财神朝屏幕中心方向时 70% 触发 [RE 0x413755]，
    第 8 帧 `sub_4123D7(cloudX,1)` 投**炸弹**，云 x = 财神对侧 160..300/360..500）。
  - 娃娃 panel[100+char]（25 帧 = 5 表情 + 20 行走）y=380 跟随鼠标（10px/tick、死区 ±8、
    dir 不清零=原地摆头 quirk [RE 0x4136CB]）；接取 = 钱袋摆动后中心入**当前娃娃帧包围盒**
    （(dollX-offX, 380-offY, w, h)）。
  - **接炸弹** → 表情 4 + `flcOpen(data[526], dollX-55, 295, 透明)` 逐帧爆炸 +
    `gamePhase=1`（钱袋全落 → 结算，评级跳过=4）。
  - 评级表情（[RE 0x4150ED]）：>=60→3 / >=50→2 / >=40→0 / <40→1（panel[100+ch] 帧 0..4）。
  - **音效表 g_moneySoundTable(0x4750BF) = {22, 23, 24, 15}**（stride 8 {index,handle}，
    -1@0x4750DF 终止；`audioPlayEffect` 第一参 = **表项指针**非句柄值）。
    云投弹 = Effect[22] 投掷 + **Effect[24] 引线嘶嘶循环**（`audioPlayEffect(0x4750CF,1)`）；
    接炸弹 = 停 24 + Effect[15] 爆炸；末颗炸弹漏接落地 = 停 24。
    （此前误判"空表静音"：get_bytes 读取范围截断在 0x4750BF 首项之前，未覆盖表体。）
  - 原版 0x401 的 sub_41473B 会跑一次完整 tick（财神先走一步）→ 重写省略（无观感差）。

## 7. case 9 樂透 ✅ 2026-09-25

- **已实现**：`src/app/lottery_dialog.cpp` + `include/game/app/lottery.h`（`lotteryVisit` / `lotteryDrawMeeting`）
- 投注界面（0x42F7FC，panel[12/13/14]，100ms）：网格 9×4 选号 + 状态机（#0011→#0013 / 钱不足 #0015→#0016 /
  买中 #0014）+ 已售格 -10 变暗 + FLC 循环 + 装饰动画；AI 现金 >1000 随机未售号自动投注
- 15 号开奖（0x431712 + 0x43010C，panel[15]/[16]/[17]，50ms）：**先分红（0x41D08F）后乐透（0x41D094）**；
  摇号（全玩家 ≤10 注随机、否则已售号随机）→ 中奖发放奖金池/清号；无人中奖池与号码保留
- 数据：`GameState.lotteryNumbers`（0x4990B8）+ `publicFund`（0x499080，原 stockPublicFund 改名；
  `transferMoney` to==-1 入池、破产清号）；存档 save 侧 + 时光机快照已接入
- 详见 `lottery-system.md`

## 8. case 14 銀行（`sub_4379C9` + `sub_436668`，入口已明）

```
case 14:
    sub_4379C9();                          // 银行界面（runModal + sub_436B0A + playerTotalAssets）
    if (g_sceneRequest == 0)
        sub_436668(cellEntId);             // 停留处理（贷款/还款）
```

- `sub_4379C9`（0x4379C9）：银行 UI（常量 1000、panel 资源 0x464BED 表）
- `sub_436668`（0x436668）：停留处理（`playerTotalAssets` + `sub_433B7E` + `sub_4521AA` + rand）
- 待深入：贷款（最高 100 万/3 个月/免息）、提前还款、路过提现金/存款、
  假日停业、银行董事长特别融资（帮助 `[HELP 20/35]`）

## 9. case 15 百貨公司（`sub_42E931`）✅ 2026-09-25

- 人类商店 UI：`runModal(sub_42D37F, 50ms)` + panel.mkf[10]（38 帧）/panel.mkf[11]；
  抽屉滑入动画（x -222→5 / y 640→227）+ 店员/招牌随机动画 + FloatMessage 引导；
  切页 (542,13)/离开 (556,246)/卖出区 (232,298) 5×3/商品列表点击购买
- 中奖：自家（`specPt.owner == cur+1`）50% 道具 / 50% 卡 + 价值语音（`sub_44F230`）
- AI：性格匹配卖卡/道具、点券<100 处理、预算一半买卡（价格降序）、`{7,1,6,0,3,2}` 买礼物道具
- 买卖公式：买 10×价 / 卖 1×价（点券 90% 就近舍入）累加 `specPt.fund/+44`
- 详见 **`42e931-department-store.md`**（重写 `src/app/shop_dialog.cpp`）

## 10. case 16 魔法屋（`magicHouseVisit` 0x43380A，**✅ 2026-09-26**）

- 详见 **`43380a-magic-house.md`** + **`43bde5-auction.md`**（重写 `src/app/magic_house_dialog.cpp` +
  `src/app/auction_dialog.cpp`；调试 `Ctrl+Shift+0`）。
- 人类：panel[18/19/20] 施法 UI（100ms 状态机 1..8，#0037..#0041 五段浮字 + 随机条件 +
  12 惩罚图标径向选择 RAW pick + 确认罩 + FLC 退出）；AI：条件+惩罚双随机（排除 6 自肥/11 拍卖）。
- 惩罚执行 0x431CAA 对目标名单（0x431842 条件选 ≤4 人）逐一：變賣卡片/命運三張/坐牢三天/
  原地停留/存現金/就地加蓋(FLC553)/得卡片/向後轉/變賣道具/拆除(FLC529)/住院三天/拍賣當格。
- 拍卖面板 0x43BDE5 同步实现（新闻 idx 7 激活）；#0042/#0043 死神召唤属投降流程 sub_433088
  ✅ 2026-09-27（`surrenderPlayer` 0x411AE0 + `deathGodSummonDialog`，见 `43380a-magic-house.md`）。

## 11. 实现建议与数据缺口

### 建议顺序（由简到繁）

1. ~~**case 10/11/12 得點券**~~ ✅ 已完成（2026-09-23；建立事件格 FLC 阻塞播放模板 `playEventFlc`）
2. ~~**case 13 卡片格**~~ ✅ 已完成（2026-09-23；赠卡池抽卡/卡包 15 槽/舍弃最低价 + `showCardGet` 显示）
3. ~~**case 4/5 監獄/醫院**~~ ✅ 已完成（2026-09-23；状态系统 + **保释/出院面板** `sub_43CAAB`/`sub_43DA27`
   UI + panel.mkf[63/64/65] + **开局默认在押 NPC 0x40734F**；**NPC 格释放/行走/抓回 ✅ 2026-09-26**，
   `498df0-event-slot-npc.md`）
4. ~~**case 2 新聞**~~ ✅ 2026-09-26（`news_dialog.cpp`：36 条判定 + 36 效果全实现，拍卖 idx 7 桩；
   `44b6df-news-events.md`）；**case 3 命運**（37 事件表 + 效果函数表）待做
5. ~~**case 6/7/8 小游戏**~~ ✅ 2026-09-25 三游戏交互版全实现（`minigame_dialog.cpp`，
   兜底 else 0x415457 同文件；详见 §6）
6. ~~**case 9 樂透**~~ ✅ 2026-09-25（投注 + 15 号开奖，`lottery-system.md`）；**case 15 百貨 ✅ / case 16 魔法屋**待推进；~~case 14 銀行~~ ✅ 2026-09-23

### 重写数据缺口

| 项 | 原版地址 | 说明 |
|----|---------|------|
| 卡片表/卡包/赠卡池 | 0x47FDEA / 0x499120 / 0x499198 | ✅ 已建模（`kCardNames`/`kCardPrices`/`cardState60`/`propStock`）；`g_playerCards`（0x49915B）与 `g_cardPool` 分工待 P4 |
| `g_jailFlags` / `g_hospitalFlags` | 0x496B30 / 0x496B60 | 監獄/醫院在押标志（8 字节）—— ✅ 已建模（`GameState.jailFlags/hospitalFlags`；开局 0x40734F 默认 NPC 4/5、6/7） |
| NPC 名表 | `dword_47ED5A` | 索引 4..7 = 小偷/強盜/流氓/間諜 —— ✅ 重写 `kNpcNames`（jail_dialog.cpp） |
| `byte_498DF3` | 0x498DF3 | **=`byte_498E33` 槽记录 status**（1=監獄/2=醫院 + bit7 抓回闩锁）—— ✅ 已建模（`NpcSlot80.status`） |
| 事件槽 NPC 犯人表 | 0x498E28 起 16B×5 | 槽记录（pixelX/Y/cell/prev/bailer/dir/busy/status/timerA~D）—— ✅ 已建模（`NpcSlot80 npcSlots[5]`，`498df0-event-slot-npc.md` §1.1） |
| `stateFlags` BYTE2/BYTE3 | 0x496B9A | 監獄/醫院剩余天数（重写 `Player.stateFlags` 已有 u32） |
| `byte_496BAA` | — | 累计入狱/住院天数（重写 `Player.byte66`） |
| 新闻表 | 0x499090 / 0x4990E0 / 0x475EB4 / 0x475E24 / 0x475ED8 | 重写 `cardShuffle`/`cardShufflePos` 应改名 `newsOrder`/`newsPos` |
| 命运表 | 0x496B38 / 0x4990B4 / 0x475FB4 / 0x475EF0 | 未建模 |
| 保释费用表 | 0x475C44 / 0x475CA4 | `{30,30,30,30,300,300,300,300}` |
| 監獄/醫院格 | `word_48BAE0`(8002) / `word_48BAE2`(8001) | loadMapData 扫描 special 值 |

### 待深入清单（后续会话）

- [ ] `newsEventCheck` / `fateEventCheck`（新闻/命运触发判定）
- [ ] `funcs_44B7BD[36]` / `funcs_44DC44[37]` 事件效果逐个标注
- [x] 小游戏 WndProc 三件套（`sub_414858`挖寶/`sub_414BBC`氣球/`sub_414FCD`接錢）+
      `sub_412014` 埋宝 + `sub_413A4A/41417E` HUD 公式 + `sub_414789` 大字结算
      + `sub_412287/4124C8`（企鹅行走/挖取状态机）+ `sub_4146EE/413F07/412F6F`（气球）
      + `sub_413248/4123D7/4123BA/41473B/41461B`（钱袋/云/财神/娃娃/绘制）+ `sub_4568C2`（缩放 blit）
      —— **2026-09-25 全部逆向并完成交互版实现**（§6）
- [x] `sub_43CAAB` / `sub_43DA27`（保释/辦理出院界面控件）—— 已实现（`jail_dialog.cpp`，2026-09-23）
- [x] `sub_436668` / `sub_436B0A`（银行停留/贷款）—— 已实现（`bank_stay_dialog.cpp`/`bank_dialog.cpp`，2026-09-23）
- [x] `sub_439BFA` / `sub_437E61`（月初结息）—— 已实现（`month_settle_dialog.cpp`，2026-09-23）
- [x] `sub_42BA97` / `sub_42B3EB`（15 号分红）—— 已实现（`dividend_dialog.cpp`，2026-09-23）
- [x] `sub_42D272` / `sub_42D237`（百货商店 UI + 商品表）—— 2026-09-25 完成，见 `42e931-department-store.md`
- [x] `sub_431CAA` / `sub_431842`（魔法屋施法）—— **2026-09-26 完成**（applyMagicPenalty/
      magicSelectTargets；含拍卖 0x43BDE5/0x43A2DD/0x439F0D 全链，见 `43380a-magic-house.md`/`43bde5-auction.md`）
- [x] `lotteryVisit`（乐透投注）+ `lotteryDrawMeeting`（15 号开奖链路）—— 已实现（`lottery_dialog.cpp`，2026-09-25）
