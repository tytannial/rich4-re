# 卡片效果（P4）专项分析与实现规格

> 配套面板档案：`441baa-inventory-panels.md`（栏 UI / 网格几何 / 消耗约定 / 数据结构）。
> 本文覆盖 **30 张卡的效果函数逆向与实现规格**，为 P4 卡牌实现的执行依据。
> 帮助依据 `[HELP 57..86]`（逐卡语义）。**已实现**：id7 改建 / id22 送神 / id23 请神。
> 逆向依据：`g_cardEffectFuncs` 表(`0x475D5C`)逐指针 + 各效果函数 decompile/disasm（IDA 核实）。

## 0. 效果函数契约（`useCardFlow 0x441BAA`）

- 分派：`r = cardEffect[cur](Application&)`（表项 `dword@(0x475D5C + 4*cardId)`，索引 id 1..30）。
- **返回非 0 = 结束**（已消耗/已生效，跳出面板）；**返回 0 = 重弹面板**（条件不满足/取消，**不消耗卡**）。
- **消耗在效果函数内部**：成功路径显式 `cardBagRemove(cur, id)`（`sub_441343(cur,id)`），面板本身不扣。
- 取消（右键/Esc/未选）→ `return 0` → 上层重选；不扣卡。
- AI（`alive & 6` 且 `aiCardItem & 1`）由 `sub_41E69E` 判可用后自动选卡 → 走同一效果函数（其内 `sub_41E6F2` 选目标、跳过 `sub_446AE8`）。

### 三类交互范式（决定实现骨架）

| 范式 | 代表卡 | 选择方式 | 关键依赖 |
|------|--------|----------|----------|
| **A 脚下格**（作用于当前玩家所站格的 objId） | 改建7 / 購地3 / 拍賣8 | 无对话框，读 `cellEnts[players[cur].cellEntId].objId` | `pickMapObject` 同源 |
| **B 选玩家** | 均贫2 / 轉向6 / 停留14 / 烏龜30 / 同盟29 / 查封28 / 漲價27 / 夢遊16 / 陷害17 / 查稅26 / 搶奪13 | `sub_446AE8(flags)`→返回玩家编码，`sub_40D293(v)` 解玩家索引 | 选人模态 |
| **C 选玩家名下地块**（返回 objId，作用于其一条路段/一栋） | 天使9 / 惡魔10 / 怪獸11 / 拆除12 / 換地4 / 換屋5 | `sub_446AE8(flags 末位≠0)`→直接返回 estate/corp `objId`（2000+/4000+），**不经 sub_40D293** | 选地块 + `strcmp` 同路段 |

> ⚠️ **核心依赖多模式**：`sub_446AE8(flags)` 低位决定「选玩家」还是「选玩家名下具体地块」：
> 均贫 `0x0E0E0E00`（末位 0=玩家）/ 惡魔 `0x0E0E0E06`（末位 6=地块，返回 objId）/ 轉向 `0x0E0C0010`。
> **重写现有 `sub_446AE8`（item_effects 选目标）需核对其对「选地块」模式的支持**（B/C 范式分派点，实现期先验）。

## 1. 全 30 卡效果表（指针已逐字节复核，**修正 id30**）

| id | 卡 | 效果函数 | 范式 | 依赖原语 | 分档 |
|----|----|----------|------|----------|------|
| 1 | 均富卡 | `0x4420D8` | 自身 | 遍历/`addPlayerDebt`/`refreshPanel(41D433)` | A |
| 2 | 均貧卡 | `0x4421B4` | B | `sub_446AE8`/`sub_40D293`/`addPlayerDebt`/`fly` | A |
| 3 | 購地卡 | `0x442325` | A | `transferMoney`/`addPlayerDebt`/`rebuildMiniMap`/`cfgLandPerm` | A |
| 4 | 換地卡 | `0x442622` | C | `sub_446AE8(选地)`/`markPickBuffer`/`highlightBlink`/owner 互换 | C |
| 5 | 換屋卡 | `0x442B02` | C | 同 4 + `sub_40B4F8`(筛有建筑) | C |
| 6 | 轉向卡 | `0x442F4D` | B | `turnToAdjacentCell(0x40C78C)`/`fly` | A |
| 7 | **改建卡** | `0x44309B` ✅ | A | `cardRebuildEffect`（已实现，`type^=1`/`selectFacilityDialog`） | — |
| 8 | 拍賣卡 | `0x443225` | A | `runAuction(0x43BDE5)`/`rebuildMiniMap`/`addPlayerDebt` | A |
| 9 | 天使卡 | `0x4434C0` | C | `angelUpgrade(0x40B110)` 整段/`playGodBuildFlc`/`strcmp` | B |
| 10 | 惡魔卡 | `0x4436E0` | C | 整段拆平/`addPlayerDebt`/`markPickBuffer`/`highlightBlink`/`forceHotelCheckout` | B |
| 11 | 怪獸卡 | `0x443917` | C | `demolishEstate(0x40AB4A)`/FLC(`sub_450441/45144F`)/`addPlayerDebt` | A |
| 12 | 拆除卡 | `0x443B0F` | C | `deleteMapObject(0x40E14D)`/`forceHotelCheckout`/FLC | A |
| 13 | 搶奪卡 | `0x443E3D` | B | `selectCardOrItemDialog(0x44192A)`/`takePlayerCard`/`addPlayerDebt` | A |
| 14 | 停留卡 | `0x443F80` | B | 设目标 `skipMove(+56)`/`fly` | B |
| 15 | 冬眠卡 | `0x4440EA` | 自身 | 设 `byte54(+54)` 冬眠/`addPlayerDebt`（帮助 idx58 文本重复红卡，**待 decompile 定语义**） | B |
| 16 | 夢遊卡 | `0x4441DC` | B | `sub_444691`(复仇)/`applyExemptCard`/`passOnCardDialog`/设 `state37(+55)` | B |
| 17 | 陷害卡 | `0x4444BF` | B | `jailPlayer(0x43D593)`/`sub_444691`/`applyExemptCard`/`passOnCardDialog` | B |
| 18 | 復仇卡 | `0x4420D5`❌占位 | — | **被动** `sub_444691`（受罚时拖施害者下水） | 被动 |
| 19 | 嫁禍卡 | `0x4420D5`❌占位 | — | **被动** `passOnCardDialog(0x44476A)`（已实现，转嫁他人） | 被动 |
| 20 | 免費卡 | `0x4420D5`❌占位 | — | **被动** `sub_444A60`（付费 > 阈值时抵用一次） | 被动 |
| 21 | 免罪卡 | `0x4420D5`❌占位 | — | **被动** `applyExemptCard(0x444BB2)`（已实现，抵消坐牢/陷害） | 被动 |
| 22 | **送神符** | `0x444C45` ✅ | 自身 | `useBanishGodCard`（已实现） | — |
| 23 | **請神符** | `0x444E1A` ✅ | 自身 | `useInviteGodCard`（已实现） | — |
| 24 | 紅卡 | `0x444F25` | 选股票 | `cursorSelect`+`stockNewsApply(0x429040)` 涨停3天 | B |
| 25 | 黑卡 | `0x44503F` | 选股票 | 同 24 + `addPlayerDebt`（跌停3天） | B |
| 26 | 查稅卡 | `0x4451F0` | B | `sub_444A60`(免費)/`passOnCardDialog`/`transferMoney` 收 20% 现金税 | B |
| 27 | 漲價卡 | `0x44542D` | B | 设 `estate.flag(+23)` 高半字节 + 5 天（整段加倍收费） | B |
| 28 | 查封卡 | `0x445593` | B | 设 `estate.flag` 低半字节 + 5 天（整段免收租） | B |
| 29 | 同盟卡 | `0x445710` | B | 设双方 `ally(+65)/allyActive(+61)` + 7 天 | B |
| 30 | 烏龜卡 | **`0x4458DF`**（旧表误记 `0x446AFB`=道具機器娃娃） | B | 设目标 `fixedStep(+57)`（下次走 1 步，可复合） | B |

## 2. 代表卡精确逻辑（已 decompile，作实现模板）

### 均富卡 `0x4420D8`（A 自身·削富济使用者）
```
cardBagRemove(cur,1); 台词 off_48123A[角色]
sum=Σ(存活 cash), n=存活数, avg=sum/n
对每个存活: if cash>avg → addPlayerDebt(i, cur, (cash-avg)/100); cash=avg   // 只削高于均值者，超额÷100 记债给使用者
refreshPanel(41D433); return 1
```
> 注：只削减到均值（非"补到均值"），存款不计，超额 1/100 转使用者债权。

### 均貧卡 `0x4421B4`（B 选玩家·两人现金取均）
```
v = 人类? sub_446AE8(0x0E0E0E00) : sub_41E6F2(0); if !v return 0   // 取消→不消耗
t = sub_40D293(v)
cardBagRemove(cur,2); 台词 off_48123E[cur]
avg=(cash[t]+cash[cur])/2
if cash[t]>avg: addPlayerDebt(t, cur, (cash[t]-avg)/100)
cash[cur]=cash[t]=avg
if 使用者非人类: flyObjectSprite(cur→t)
refreshPanel; 台词 off_48132E[t]; sub_41D546(); return v
```

### 購地卡 `0x442325`（A 脚下格·强制收购，与改建卡同范式）
```
objId = cellEnts[cellEntId].objId
if 2000<objId<4000 (estate) es=estates[52*(objId-2000)] 或 4000<objId<6000 (corp) cp=corps[..]:
    owner=x[25]; if owner==0 || owner==cur+1 → return 0（不消耗）
    price=(level*priceBase + priceAdd)*moneyMul     // estate:+26/+30/+28；corp:+26/+36[0]/+34
    if price>cash[cur]: showMessage(byte_46530C"您的現金不足！",1500); return 0
    addPlayerDebt(seller, cur, (level*moneyMul+flt_46531C)/flt_465320)   // 补偿原主(记债)
    台词 off_481242[cur]; x[25]=cur+1（过户）; rebuildMiniMap(0)
    if cfgLandPerm: x[estate+48/corp+52]=addPackedDate(dword_497160, g_landPermDays[cfg])
    transferMoney(cur, seller, price, 0)            // 使用者付、原主收(入银行)
    台词 off_481332[seller]; sub_41D546(); cardBagRemove(cur,3); return 1
```

### 惡魔卡 `0x4436E0`（C 选玩家地产·整段夷平）
```
v = sub_446AE8(0x0E0E0E06)（末位6→返回 objId）; if !v return 0
cardBagRemove(cur,10); 台词 off_48125E
if 2000<v<4000: name = estates[52*(v-2000)]+2
  for 每 estate e: if strcmp(e+4, name)==0 (同路段):
     if e.owner: addPlayerDebt(owner-1, cur, 30*moneyMul*level)   // 补偿原主
     markPickBuffer(g_pickBuffer, 193600, idx+2000, 0xFFFF); e.level=0; e.type=0   // 拆平+标记高亮
else if 4000<v<6000: 同拆单块 corp + forceHotelCheckout
if 非人类: flyObjectSprite; highlightBlink(); refreshGameUi(0,0,1); return v
```
> 「同路段」=`strcmp(cellEnt/corp 地块名)`；高亮 `markPickBuffer(…,0x2F440=193600, objId, 0xFFFF)`+`highlightBlink`（重写 `captureHighlightShapes`/`drawEstateHighlight` 已有）。天使卡 `0x4434C0` 同结构但 `angelUpgrade` 加盖。

## 3. 被动卡链（18/19/20/21，**不走主动效果表**，`0x4420D5=return 0`）

宿主卡施加惩罚/费用时调用被动判定链（已部分实现于命运/新闻/惩罚管线）：

| 被动卡 | 触发函数 | 语义 | 状态 |
|--------|----------|------|------|
| 免罪(21) | `applyExemptCard 0x444BB2` | 抵消坐牢/陷害/梦游/催眠一次 | ✅（`resolvePenaltyTarget`） |
| 嫁禍(19) | `passOnCardDialog 0x44476A` | 转嫁惩罚给他人 | ✅（`resolvePenaltyTarget`） |
| 復仇(18) | `sub_444691(a1)` | 受害者有復仇卡→`showCardGet(18)`+消耗+**对调惩罚回施害者**（`jmp 0x444753` 尾调） | ❌ 待实现 |
| 免費(20) | `sub_444A60(a1,a2,a3)` | 付费/罚金/税 > 阈值→`askDialog` 用卡抵扣（AI 阈值 `(rand%3000+3000)*M` 且 `a3<=cash` 才抵）；`showCardGet(20)`+消耗 | ❌ 待接入收费管线 |

**宿主链**：`夢遊(16)`/`陷害(17)` 在设状态前先过 `applyExemptCard`(免罪)→`passOnCardDialog`(嫁祸)→`sub_444691`(復仇)；`查稅(26)` 先过 `sub_444A60`(免費)→`passOnCardDialog`。→ **被动链与收费管线是 16/17/26 的前置**，实现这三张需同时补 `sub_444691`/`sub_444A60`。

## 4. 依赖原语盘点

**已实现、可直接复用**：`angelUpgrade`/`demolishEstate`/`forceHotelCheckout`/`deleteMapObject`/`jailPlayer`/`runAuction`/`stockNewsApply`/`selectCardOrItemDialog`/`passOnCardDialog`/`applyExemptCard`/`turnToAdjacentCell`/`giveCardToBag`/`cardBagRemove`/`takePlayerCard`/`givePlayerItem`/`transferMoney`/`addPlayerDebt`/`rebuildMiniMap`/`refreshGameUi`/`showMessage`/`showCardGet`/`flyObjectSprite`/`markPickBuffer`+`highlightBlink`（联动高亮）。

**需新建 / 补齐（按阻塞度）**：
1. ~~**天数递减管线**：`estate.flag`/`corp.flag` 高/低半字节 5 天、`allyActive` 7 天~~ ✅ 已补齐（`updatePlayerStates` 递减，含同盟双向 -20×M）。
2. **状态写入 helper**：`skipMove`(停留14)/`fixedStep`(烏龜30)/`state37`(夢遊16)/`byte54`(冬眠15) 载体字段已在，缺效果函数写入 + 回合消费已就绪。
3. **`sub_446AE8` 选地块模式**（C 范式）+ **`sub_40B4F8`**（换屋筛有建筑地）+ **`sub_40D293`**（选玩家返回值解码）—— B/C 范式入口，需先验现有实现覆盖度。
4. **被动链** `sub_444691`（復仇）+ `sub_444A60`（免費，接住宅/corp/specPt 三收费管线）。
5. **`sub_41D433`（refreshPanel）**：临时切 `currentPlayer`→`drawPlayerInfoPanel`→还原（均富/同盟刷新各玩家面板）。
6. **`cursorSelect`（选股票对话框，紅/黑卡）** + `stockNewsApply` 的"3 天倒计时"（停牌倒计时已有可参照）。
7. 各卡台词表（`off_4812xx[角色]`，`sub_44EF41`）与消息 BIG5 串 —— 逐卡实现时 `get_bytes` cp950 复核（勿信 `get_string`）。

## 5. 实现分档（建议顺序）

- **档A（原语齐备，接线即成，8 张）**：均富1 / 均贫2 / 購地3 / 轉向6 / 拍賣8 / 怪獸11 / 拆除12 / 搶奪13。
- **档B（补 1~2 个小机制，10 张）**：天使9 / 惡魔10 / 停留14 / 冬眠15 / 夢遊16 / 陷害17 / 紅24 / 黑25 / 查稅26 / 烏龜30 + 被动 18/20。
  - 需先补：天数递减、状态写入 helper、`sub_444691`/`sub_444A60`、`cursorSelect`+股价倒计时。
- **档C（需扩 `sub_446AE8` 选地块 + 双地块交换 UI，3 张）**：換地4 / 換屋5 /（查封27/同盟29 属 B，但同盟依赖 allyActive 递减）。

### 实现进度（2026-09-26，**30 卡全实现**）

- **实现位置**：`src/app/card_effects.cpp/.h`（30 张效果 + 被动 `triggerRevengeCard`/`applyFreeCard`）
  + `card_bag_dialog.cpp` `cardEffect` switch 接线 + `stock_market_dialog.cpp` `stockPickDialog`（红/黑选股）。
- **档A（8）**：均富/均貧/購地/轉向/拍賣/怪獸/拆除/搶奪。
- **档B（13 主 + 2 被动）**：天使（整路段批量 `level++`/corp `angelUpgrade` + FLC 523）、惡魔（整段
  夷平 + 记债 30×M×level）、停留（`skipMove`）、冬眠（全体 `byte54=5` + 150×M）、夢遊（`state37=4/5`
  + 清载具）、陷害（`jailPlayer` 5/4 天）、查稅（`cash×0.2` + 免费/嫁祸链）、漲價（`flag=0x50`）、
  查封（`flag=0x51` + 研究所停研发）、同盟（`ally/allyActive=7`，先解旧盟；**收租房地产联合
  收费 ✅ 2026-09-27**：收租分账 + 消息 0x46399A + 高亮含同盟地块 + 破产解除 0x40CE74）、烏龜（`fixedStep=2/3`）、
  紅/黑（选股 → `stockNews=0x30`/`0x03` + `stockNewsApply`；黑卡持股损失记债 股数×跌幅/200）。
- **档C（2）**：換地（交换同类地块 owner）；換屋（交换 `level/sub+type`，地权不变，0x40B4F8 动画）。
- **被动链**：18 復仇 `triggerRevengeCard`（梦游/陷害宿主触发，反伤由调用方执行）、19 嫁祸
  `passOnCardDialog`（已有）、20 免費 `applyFreeCard`（人类 `askDialog` / AI `(rand%3000+3000)×M` 阈值）、
  21 免罪 `resolvePenaltyTarget`（已有）。
- **机制补充**：`clearAllyPair` 提取导出（economy）；`updatePlayerStates` 补同盟天数递减（每次双向
  `-20×M` 记债，7→…→0x80）；`resolveFeePayer` 补 **免費卡抵用（返回 -1 免付）→ 嫁祸转嫁 → 死神代付**
  三步（原版顺序 0x419E58/0x419EB8/0x419ECC 等三处）；`turnToAdjacentCell` 迁 `turn_system.cpp` 导出。
- **AI 分支**：`useCardFlow` 仅 `alive==1` 进入 → 效果函数只走人类路径；AI 用卡链
  （`sub_41E69E` + `dword_48BE58` 预选目标，`sub_41E6F2`=读表）随 P5 接入。
- **台词（`sub_44EF41` 角色语音/气泡）**：各卡调用点已定位（off_4812xx 台词表），表数据提取随 P4 语音批次。
- 待实机验证：FLC 落点（怪獸 557 / 拆除 529 / 天使 523）、`selectTargetDialog` flags
  （`0x0E0C0410` 玩家 / `0x0E0C0506` 地块 / `0x0E0C0526` 地块+道具 / `0x0E0C0710` 玩家(陷阱类) /
  `0x0E0C0202` estate 换地 / `0x0E0C0204` corp 换地）、`stockPickDialog` 选股交互。

### AI / 托管用卡链（2026-09-26 实现）

- **触发**（`beginPlayerTurn` case 2/5，原版 `0x418E13..0x418E21`）：AI/托管回合开始 →
  `rand()&1` 决定用卡（`useCardFlow`）/ 道具（`itemPanelFlow`）；两函数内部自判
  `alive&6` + `aiCardItem` bit0/bit1（人类路径不受影响）。
- **判定与目标预选**：`src/app/ai_card.cpp` `aiCardSelect(cardId)` [RE `0x41E69E`]：
  `kCardAiPersona[卡] - aiPersonality` ≥2 → 不用；==1 → 1/3 概率；命中后执行
  `dword_475324[卡]`（AI 选目标函数表 31 项）写 `GameState.aiCardTarget`（[RE `0x48BE58`]；
  语义随卡：玩家掩码 `0x8000|mask` / 地块 objId / 股票 0-based 索引 / 改建设施号）与
  `aiCardTarget2`（[RE `0x48BE5C`] 抢夺卡目标卡 id）。
- **逐卡策略**（等价实现，逐函数逆向）：
  | 卡 | AI 策略 |
  |----|---------|
  | 1 均富 | 全员平均现金 > 10×自己 且自己 < 3000×M |
  | 2/26 均贫·查税 | 视野内债主优先（现金/倍数条件）/ 最富可见者 |
  | 3 购地 | 脚下他人有主地 + `sub_41E8E6`（愿花钱）且价 < 现金 |
  | 4 换地 | 脚下自己 ≤1 级地；视野内更优同类地（`priceAdd/level` 或 `buildPrice/sub` 更高） |
  | 5/6/18-21 | **不用**（表项 `0x41E6E3` = `return 0`） |
  | 7 改建 | estate 同段连锁化 / corp 设施改建（`target=rand%4+1`；他人 ≥3 级或债主 ≥2 级） |
  | 8 拍卖 | 脚下他人 ≥3 级地（债主 ≥2 级） |
  | 9 天使 | 自己同段 ≥3 块（type0 level<5）的路段随机一条 |
  | 10 恶魔 | 债主路段（level 和 ≥2 且 ≥7 块且自己 ≤1）/ 他人路段（level 和 ≥3 且 ≥9 块） |
  | 11 怪兽 | 等级最高他人建筑（债主优先） |
  | 12 拆除 | 怪兽优先 → 路面道具（16/17 无主或他人）/ `builtEstatesOf(owner)≥4` |
  | 13 抢夺 | 债主 persona==1 最贵卡 / 他人 persona==2 最贵卡（→ `aiCardTarget2`） |
  | 14 停留 | 自己升级位（同段有其他地或 level≥2）或站在自己 specPt/设施上的对手 |
  | 15 冬眠 | 25% 概率 |
  | 16/17 梦游·陷害 | 可见对手（排除已冬眠/持復仇卡 18），债主优先 |
  | 22 送神 | 自挂负面神 {5,6,7,8,10,15} 或炸弹 life<13 |
  | 23 请神 | 无正面神（1/2/3/4/12）→ `pickNearestAttachable` |
  | 24 红 | 自持市值最大、未停牌、未涨停股 |
  | 25 黑 | 持股最多对手（否则债主）的未持股、未停牌、非跌停股 |
  | 27 涨价 | 自己占比 ≥0.682 且 level 和 ≥7、他人 ≤3 的路段 / 自己高级 corp（非研究所/加油站） |
  | 28 查封 | 债主 ≥3 级设施 / 他人 level 和 ≥7 路段 |
  | 29 同盟 | 可见（非债主、未盟）中地产最多者 |
  | 30 乌龟 | 自己高产值路段（产值×0.682 ≥ 现金且 ≥2 块）或可见对手 |
- **helper**：`sub_41E8E6`（AI 愿为该地花钱：自己同段有地 或 欠债人 ≥2 级地）；
  `sub_41970F`（owner 名下有建筑 estate 数）；`sub_40A45C`/`word_48B8C4` 视野拾取列表 →
  重写 `mapHitRegions`；常量 `dbl_463D38≈0.682`（涨价占比阈值）。
- **差异**：视口可见性用 `mapHitRegions` 近似（原版拾取缓冲）；部分排序/阈值简化（逐函数注释）；
  AI 每回合最多使用一张卡（原版同）；道具 AI（`itemPanelFlow` AI 分支）待 P5。

### 角色台词（2026-09-26 实现）

- **表**：`off_48123A` = **12 角色 × 90 槽**指针（角色 stride 360 字节）；`tools/gen_card_lines.py`
  生成 `src/app/card_lines.cpp`（`kCardSpeech[12][90]`）。槽 0..29 = 卡 id1..30 使用者台词；
  对方/受害者槽常量见 `card_lines.h`（轉向自 35 / 停留自 43 / 烏龜自 59 / 均貧目标 61 / 購地原主 62 /
  換地 63 / 換屋 64 / 轉向目标 65 / 拍賣原主 67 / 怪獸原主 70 / 拆除原主 71 / 搶奪目标 72 /
  停留目标 73 / 陷害目标 76 / 復仇使用者 77 / 嫁祸目标 78 / 免費对方 79 / 查稅目标 85 /
  同盟目标 88 / 烏龜目标 89）。文本含 `#NNNN`（Speaking.mkf 语音）/ `@MM`（表情帧），
  经 `playLine`（0x44EF41）渲染（状态守卫/去重/气泡）。
- **受害台词**：`off_48089E` = 价值表（`0x48084A`）**第 21 列**（梦游受害者"睡觉"台词，
  `kCardVictimLines[12]`）。
- **接入**：`playCardLine(app, player, slot)`；30 卡按原版调用点插入（**改建 slot6 与嫁祸 18/78
  经 IDA 复核**；送神符原版无台词不播）；人类/AI 同播。
- 验证样本：均富"有錢大家花！"、購地"讓我把它據為己有！！"/原主"好大的膽子！！"、
  被搶"竟敢在太歲頭上動土？！"、烏龜自"慢慢走比較保險！"、梦游受害者"zzZZZ…"。

## 6. 每卡实现前待细化清单

1. `sub_446AE8` 各 flags 精确语义（哪些位=含自己/含对手/选玩家 vs 选地块/过滤建筑）——`decompile 0x446AE8` 定表。
2. 換地/換屋 完整流程（两块 objId 如何选、owner/level/type/expireDate 具体换哪些字段、快照失效）——`disasm 0x442622/0x442B02`（Hex-Rays 体内嵌数据失败）。
3. 天使/惡魔「整路段」收集的 name 比较基准（`cellEnt+4` 地名 vs estate `+2`？恶魔用 `estates[..]+2` 名，实现期核对同段判定与重写 `estateRouteRent` 是否同键）。
4. 冬眠卡 `0x4440EA` 真语义（帮助文本=红卡重复）——`decompile`。
5. 紅/黑/查稅/查封/漲價 的消息串与金额常量、`stockNewsApply` 参数（涨停/跌停 3 天写入格式）。
6. 每卡消耗点确认（多数在成功分支末 `cardBagRemove(cur,id)`；购地/惡魔 等先判 `return 0` 再消耗）。

## 7. 验收

逐卡：Debug 发卡 → 工具条卡片栏(case 8)使用 →
- 不满足条件 → 重弹面板、**不扣卡**；满足 → 生效 + 扣卡 + 台词/动画/消息。
- 金钱类核对现金/存款/债权变化；地块类核对 `rebuildMiniMap` + 小地图色块 + 右键提示；
  状态类核对下一回合表现（停留=不前进、烏龜=走1步、夢遊/冬眠=丧失收租买地、同盟=互免）；
  股价类核对行情面板 + 3 天后复原；被动链用 16/17/26 施于持有 18/19/20/21 的对手验证抵消/转嫁/抵扣/复仇。
- AI/托管（`alive&6` + `aiCardItem&1`）自动选卡使用一致性。
