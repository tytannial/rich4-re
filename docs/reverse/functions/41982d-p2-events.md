# 0x41982D landingEvent P2 事件格研究（case 2..16）

> 本文档为 `sub_41982D`（landingEvent）case 2..16 的原版实现研究，供 P2 实现使用。
> 入口/分派见 `gameplay-map-mechanics.md` §3；调用链与返回值见 §2。
> 所有 case 均**返回 0x80**（直接下一位）；需要交互的 case 在函数内用 `runModal` 阻塞完成。

## 0. 总览

| type | 名称 | case | 处理函数 | 研究状态 |
|------|------|------|---------|---------|
| 1 | 公園 | 1 = default | —（无事件） | 已实现（返回 0x80） |
| 2 | 新聞 | 2 | `newsEvent`（36 事件表） | ✅ 结构已明 |
| 3 | 命運 | 3 | `fateEvent`（37 事件表） | ✅ 结构已明 |
| 4 | 監獄 | 4 | `jailBailDialog`（门外保释） | 🟡 入狱/回合/免收租 ✅；保释 UI ❌ |
| 5 | 醫院 | 5 | `hospitalVisitDialog`（门外辦理出院） | 🟡 住院/回合/免收租 ✅；出院 UI ❌ |
| 6 | 企鵝挖寶 | 6 | `sub_415215`（小游戏，得分） | 🟡 入口已明 |
| 7 | 七彩氣球 | 7 | `sub_4154DC`（小游戏，得分） | 🟡 入口已明 |
| 8 | 喜從天降 | 8 | `sub_4155FC`（小游戏，得分） | 🟡 入口已明 |
| 9 | 樂透 | 9 | `sub_4315CC` | 🟡 入口已明 |
| 10 | 得５０點 | 10 | 0x41B184 | ✅ 已实现（+50 點券） |
| 11 | 得３０點 | 11 | 0x41B21E | ✅ 已实现（+30 點券） |
| 12 | 得１０點 | 12 | 0x41B2A3 | ✅ 已实现（+10 點券） |
| 13 | 卡片 | 13 | 0x41B302（`sub_441E12` 抽卡） | ✅ 已实现 |
| 14 | 銀行 | 14 | `sub_4379C9`（UI）+ `sub_436668`（停留） | 🟡 入口已明 |
| 15 | 百貨公司 | 15 | `sub_42E931`（商店） | 🟡 入口已明 |
| 16 | 魔法屋 | 16 | `sub_43380A` | 🟡 入口已明 |

## 1. case 4 監獄 / case 5 醫院

> **实现状态（2026-09-23）**：入狱/住院（`jailPlayer` 0x43D593 / `hospitalizePlayer` 0x43EC3F：移格/
> 标志/FLC 538·524/保险理赔/累计）✅；回合递减与到期释放（`updatePlayerStates` 0x41C84F +
> `endPlayerState` 0x40D6BE）✅；保险期（**+62 = 保险期**，非卡片数）递减与理赔（`insurancePayout`
> 0x44BA63）✅；**门口保释/办出院（case 4/5 停留用 `jailBailDialog`/`hospitalVisitDialog`）与 AI 保释
> ❌ 待做**（人类界面 `sub_43CAAB`/`sub_43DA27` + panel.mkf[63/64/65] 尚未逆向）。
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

> ⬜ **未实现**（case 4/5 停留入口）；以下逻辑已归档，人类选择界面（`sub_43CAAB`/`sub_43DA27` +
> panel.mkf[63/64/65]）与 `releaseJailNpc` 的事件槽 NPC 分支（P5）待专项逆向。

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
- 玩家 0..3 释放：`sub_40D6BE(player)` + 清 `g_jailFlags/8B`
- 四大惡人（小偷/流氓/強盜/間諜）即保释出狱的事件槽 NPC（帮助 `[HELP 47/52/53/56]`），
  出狱后在路面行动（行窃/保护费/抢卡/盗领）—— P5 事件槽 NPC 实现依据

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

- `byte_498DF3`（0x498DF3，16B/玩家）：bit0-6 = 状态（1=監獄、2=醫院），bit7 = 触发标志
  （与 `g_jailFlags/8B` 的分工待确认——推测 `byte_498DF3` 为"到达门外"状态）
- 入狱/住院期间：`onPlayerActionPhase` 的 `g_playerCellNo` 递减（`byte_496D0C[24*cellNo-24]`），
  到 0 → `sub_40E14D` 释放 + FLC 525 + `hospitalizePlayer(p,5)`/`jailPlayer(p,5)`（出院/出狱）；
  同格有其他存活玩家且无 cellNo → 传染转移（见 `41b42d-on-player-action-phase.md`）

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

## 5. case 3 命運（`fateEvent`，37 事件）

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
| `g_fateOrder` | 0x496B38 | 37 项命运事件表（顺序固定，非洗牌） |
| `g_fatePos` | 0x4990B4 | 当前索引（0..36） |
| `word_475FB4[37+4*地图]` | 0x475FB4 | 插画资源索引（前 33 项共用 + 每地图 4 项专属） |
| `funcs_44DC44` | 0x475EF0 | 效果函数表（0x44BE16, 0x44BFB1, …） |
| `fateEventCheck` | 0x44BB4B | 触发判定（待深入） |

## 6. case 6/7/8 小游戏（入口已明）

| case | 函数 | 入口行为 | 返回 |
|------|------|---------|------|
| 6 企鵝挖寶 | `sub_415215`（0x415215） | `sub_412014`（小游戏主循环）+ FLC + `rand` + `sub_44EF41` 语音；得分存 `g_playerPoints += ax` | 得分（ax） |
| 7 七彩氣球 | `sub_4154DC`（0x4154DC） | `runModal` + 资源（`0x47509F` 表） | 得分 |
| 8 喜從天降 | `sub_4155FC`（0x4155FC） | `runModal` + 资源（`0x4750BF` 表） | 得分 |

- 三个小游戏都通过 `runModal` 阻塞运行，返回得分后由 case 分支累加到 `g_playerPoints`
- 待深入：`sub_412014`（挖宝）、各 runModal 回调、FLC/音效资源索引、得分公式

## 7. case 9 樂透（`sub_4315CC`，入口已明）

- `runModal` + `musicPlayScene` + `sub_450441` 资源加载（0x47566B 表）
- 待深入：投注界面、每月 15 号开奖（`advanceDay` 链路）、奖金池累积
  （帮助 `[HELP 36]`：现金 1000 投注，无人中奖累积，意外损失的钱也入奖金池）

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

## 9. case 15 百貨公司（`sub_42E931`，入口已明）

- 商店系统：`runModal` + `sub_42D272`/`sub_42D237`（卡片店/道具店 UI）+ `sub_441262`/`sub_447C6E`
- 常量 6000/8000（點券价格区间？）；帮助 `[HELP 27]`：點券买卖卡片/道具，踩到先进入卡片店
- 待深入：商品列表/价格表（`kPropStockInit` 已建模库存）、买卖流程、道具店切换

## 10. case 16 魔法屋（`sub_43380A`，入口已明）

- `runModal` + `sub_431CAA`（施法界面）+ `sub_431842`；文本 `"%s\n\n%s"`（0x464842）
- 帮助 `[HELP 38]`：女巫水晶球按条件施法，玩家决定惩罚对象
- 待深入：施法条件表、惩罚效果（`sub_431CAA` 内部）

## 11. 实现建议与数据缺口

### 建议顺序（由简到繁）

1. ~~**case 10/11/12 得點券**~~ ✅ 已完成（2026-09-23；建立事件格 FLC 阻塞播放模板 `playEventFlc`）
2. ~~**case 13 卡片格**~~ ✅ 已完成（2026-09-23；赠卡池抽卡/卡包 15 槽/舍弃最低价 + `showCardGet` 显示）
3. **case 4/5 監獄/醫院**（状态系统 ✅：入狱/住院/回合递减/到期释放/保险理赔；**保释界面 ❌ 待做**：
   `sub_43CAAB`/`sub_43DA27` UI + panel.mkf[63/64/65]；与 P3 `onPlayerActionPhase` cellNo 联动）
4. **case 2/3 新聞/命運**（事件表 + 36/37 个效果函数——可先实现"空效果+插画+标题"骨架）
5. **case 6/7/8 小游戏**（需输入/计时/得分逻辑）
6. **case 9/14/15/16 樂透/銀行/百貨/魔法屋**（各自独立子系统，建议按需推进）

### 重写数据缺口

| 项 | 原版地址 | 说明 |
|----|---------|------|
| 卡片表/卡包/赠卡池 | 0x47FDEA / 0x499120 / 0x499198 | ✅ 已建模（`kCardNames`/`kCardPrices`/`cardState60`/`propStock`）；`g_playerCards`（0x49915B）与 `g_cardPool` 分工待 P4 |
| `g_jailFlags` / `g_hospitalFlags` | 0x496B30 / 0x496B60 | 監獄/醫院在押标志（8 字节）—— 未建模 |
| `byte_498DF3` | 0x498DF3 | 玩家到达门外状态（16B/玩家）—— 未建模 |
| 事件槽 NPC 犯人表 | 0x498E28 起 16B×4 | 保释人/目标格/坐标/状态 —— 未建模 |
| `stateFlags` BYTE2/BYTE3 | 0x496B9A | 監獄/醫院剩余天数（重写 `Player.stateFlags` 已有 u32） |
| `byte_496BAA` | — | 累计入狱/住院天数（重写 `Player.byte66`） |
| 新闻表 | 0x499090 / 0x4990E0 / 0x475EB4 / 0x475E24 / 0x475ED8 | 重写 `cardShuffle`/`cardShufflePos` 应改名 `newsOrder`/`newsPos` |
| 命运表 | 0x496B38 / 0x4990B4 / 0x475FB4 / 0x475EF0 | 未建模 |
| 保释费用表 | 0x475C44 / 0x475CA4 | `{30,30,30,30,300,300,300,300}` |
| 監獄/醫院格 | `word_48BAE0`(8002) / `word_48BAE2`(8001) | loadMapData 扫描 special 值 |

### 待深入清单（后续会话）

- [ ] `newsEventCheck` / `fateEventCheck`（新闻/命运触发判定）
- [ ] `funcs_44B7BD[36]` / `funcs_44DC44[37]` 事件效果逐个标注
- [ ] `sub_412014`（挖宝小游戏）+ 气球/接钱 runModal 回调
- [ ] `sub_43CAAB` / `sub_43DA27`（保释/辦理出院界面控件）
- [ ] `sub_436668` / `sub_436B0A`（银行停留/贷款）
- [ ] `sub_42D272` / `sub_42D237`（百货商店 UI + 商品表）
- [ ] `sub_431CAA` / `sub_431842`（魔法屋施法）
- [ ] `sub_4315CC`（乐透投注）+ `advanceDay` 的每月 15 号开奖链路
