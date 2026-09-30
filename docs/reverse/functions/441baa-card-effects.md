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
   | 28 查封 | [RE 0x42062B] **前方 6 步路径**（`sub_40B221(cur,6)`，不查分叉）逐格：路段**首次出现** ∧ 该段无自家 ∧ 他人 Σlevel≥7 / 债主 corp（type≠0∧sub≥3）→ 封该对象。**2026-09-28 订正**：旧版误用全视口列表（远处误封根因） |
   | 29 同盟 | 可见（非债主、未盟）中地产最多者 |
   | 30 乌龟 | [RE 0x420970] ①自己前方 3 步路径（无分叉）全「无主/自家可加盖」∧ Σ地价×**1.5<**现金（0x420BFB `fcompp/jnb` 核验方向）∧≥2 块∧bank+cash>10000∧luckB≥0 → 对自己用；②视口对手逐个 `sub_40B221(opp,3)`：其前方 3 格全为我家产 ∧ Σ(租金/feeTable[sub]/feeBase)≥10000×M ∧≥2 → 冻该对手。**2026-09-28 订正**：旧版全视口 Σ×1.5≥现金 方向与候选域双错 |
- **2026-09-28 全量回验（A4 专项，24 个目标函数逐个 vs 原版）**，修正清单：
  | # | 卡 | 修正 |
  |---|----|------|
  | 1 | 2/26 | `tgtVisibleByCash` 拆分：卡26 查税**无** 2×/3× 相对财富条件（0x420398）；候选扫描按**玩家号取最后满足者**（原版不 break 覆盖式，非市值最大） |
  | 2 | 4 换地 | 同段自家地检查 = **全图** estate 扫描（0x41EB72），非仅视口——漏检会换掉自己连锁段 |
  | 3 | 8 拍卖 | 债主分支补 `owner≠0` 守卫（0x41EFB0）——无主地 level≥2 曾误判 creditor+1==0 |
  | 4 | 10 恶魔 | **有债主时只查债主段**（0x41F302 if/else 逐段，不回退他人段）；他人段需 `own 块数==0` 且累计仅存活玩家 |
  | 5 | 11 怪兽 | 债主分支 **corp 优先于 estate**（0x41F5A6）；全场 corp(≥3)/estate(≥4) **分别选优、corp 整体优先**（0x41F67B），平手比 buildPrice/priceAdd（0x41F57C/0x41F4E7） |
  | 6 | 12 拆除 | 改**单遍视口混合列表**（0x41F6BF）；地雷(17)=只拆**自家**地上雷（旧版方向反）；路障(16)=他人**有主**产业（无主跳过）；新增**加油站分支**（自己有载具 ∧ 他人 type3 sub1，0x41F78E） |
  | 7 | 13 抢夺 | 债主分支 persona 条件 = **≠0**（0x41F9E5 `byte_47FDF1`），非 ==1 |
  | 8 | 14 停留 | corp 自留分支：门槛=**现金>10000**（非 bank+cash）∧ `M×feeTable[0]<现金`（0x41FC74） |
  | 9 | 25 黑卡 | 债主分支不查挂牌价 `word_496984`（0x42022E），top 分支才查（0x41F57C）；两者都算、债主结果优先 |
  | 10 | 27 涨价 | 单遍混合列表：estate 段命中即返（优先），corp 仅记**最高 sub** 候选兜底（0x42060E 旧版 corp 先返=反了）；债主持地 break 弃段（0x420535） |
  | 11 | 29 同盟 | 先**全图**找地产最多者（0x42087F），不在视口候选则**不结盟**（旧版取视口内最多者） |
  一致无需改：卡 1/3/7/9/16/17/22/23/24。诊断：`aiCardSelect ... rejected` 日志（persona 过而函数无候选）。
- **helper**：`sub_41E8E6`（AI 愿为该地花钱：自己同段有地 或 欠债人 ≥2 级地）；
  `sub_41970F`（owner 名下有建筑 estate 数）；`sub_40A45C`/`word_48B8C4` 视野拾取列表 →
  重写 `mapHitRegions`；`sub_40B221` 前方路径 → 重写 `aiItemPredictPath`（`ai_item.h`，
  与道具链共享）；常量 `dbl_463D38≈0.682`（涨价占比阈值）、`dbl_463D40=1.5`（烏龜）。
- **差异**：视口可见性用 `mapHitRegions` 近似（原版拾取缓冲）——**AI 回合开始需先
  `renderGameFrame` 刷新视口**（重写 tick 序 update→render，否则读到上一玩家视口，
  2026-09-28 于 `beginPlayerTurn` case 2/5 修复）；部分排序/阈值简化（逐函数注释）；
  AI 每回合最多使用一张卡（原版同）；道具 AI（`itemPanelFlow` AI 分支）见 `420e9a-item-ai.md`。

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
- **2026-09-30 时序/条件/expr 勘误（IDA 逐卡复核，实机反馈「选定目标前的语音被放到生效时」）**：
  - **红/黑卡（24/25）**：使用者台词（槽23/24、expr0）在**函数最前（选股前）**——重写此前缺失；已补。
    人类取消选股不消耗但台词已播（原版同）。
  - **抢夺卡**：使用者（槽12、expr3）在**选卡/道具对话框前**；消耗(13)在目标台词（槽72、expr1）前。
  - **拍卖卡**：顺序 = 原主记债 → 使用者（槽7、expr3）→ 原主台词（槽67、expr1；主人≠自己才播）→ runAuction → 消耗。
  - **转向卡**：自己（槽35、expr0）/目标（槽65、expr2）台词在 `turnToAdjacentCell` **之后**播。
  - **乌龟/停留**：使用者台词**仅目标≠自己**时播；乌龟自 59 expr3 / 目标 89 expr2；停留自 43 expr3 / 目标 73 expr2。
  - **购地**：使用者（槽2、expr3）在转账**前**；查税：目标（槽85、expr2）在转账+消息**后**。
  - **換地/換屋**：语音为**条件单条**——交换前（自己持有任一块且对方那块等级≥自己那块）由使用者说
    槽3/4（expr3）；交换后（用交换前 owner 快照：a=我∧b≠我∧b有主∧lvb≥lva → b 的原主说，
    反之 a 的原主说）槽63/64（expr2）。重写此前无条件播"使用者+对方"两条=错。
  - **免费卡**：补收费方台词（槽79、expr1；a2=−1 公库/无收费方不播）；查税 a2=当前玩家、
    住宅/corp a2=原主−1、specPt a2=−1（0x419E51/0x41A62B/0x41AF1D/0x44532D 实参核实）。
  - 其余 expr 按 IDA 补齐：均富/均贫 3、天使 3、恶魔/怪兽/拆除/涨价/查封/查税 0、冬眠 3、梦游 3、
    受害者 1、陷害 3/1、同盟 3/0、復仇 0/2、嫁禍 0/2、改建 3、请神 0；原主类（购地62/怪兽70/拆除71/拍賣67）expr1。
  - 验证：`--trace-out` 抽查＝乌龟 使用者(expr3)→目标(expr2)、红卡 槽23 先于选股面板、
    拍卖 使用者(expr3)在原主前、抢夺 使用者在选卡前(消耗→目标 expr1)；卡片场景 23/23 PASS。
- **2026-09-30 二轮实机反馈修复（光标/音效/特写/冬眠，IDA 逐点核验）**：
  - **选目标动画光标**：`selectTargetDialog` mode 编码 = flags(低16) | BYTE1=过滤 | BYTE2=光标索引 |
    **BYTE3=帧数−1**（0x445EC1：dword_48C588=mode BYTE2、dword_48C58C=(BYTE3)+1，命中时
    `cursorSelect(type, frames, 10)`）。卡牌 mode 全部 `0x0E0Cxxxx` → 光标 12、**15 帧**；重写此前
    硬编码 frames=1（无动画）。已按 BYTE3+1 解析（`TargetSelCtx::cursorFrames`）。
  - **红/黑卡选股光标**：0x444FF0/0x4450B4 `cursorSelect(12,15,10)`、0x44500C/0x4450CE 恢复 41；
    `stockPickDialog` 进入/退出已接。
  - **无效使用错误音效**：0x441CD1 `if(!v4) audioPlayEffect(g_uiSoundMusicTip)`=UI 音效 3；
    人类分支 cardEffect 返回 0 时补播（AI 分支原版无）。
  - **卡片特写绘制顺序**：0x441F73 = tip 框(0x442023) → 文本(0x44203E) → **卡片 blitElementFullscreen
    (0x44205C) 最后贴**（165×256@(138,200) 覆盖 tip 框左下）；重写此前顺序颠倒（tip 压卡片）。
  - **特写残影**：0x441C90 `sub_451EDB(v3,0,0x8028)` 每次选卡返回后**恢复背景**（save/restoreBackground
    配对）；重写等价 = 卡牌栏绘制前 `renderGameFrame` 全屏重绘（`renderCardBag` 已加）。
  - **冬眠/梦游 128 天不醒根因**：0x41CB04 递减 `n==0 → 0x80`（到期标记），**醒来在下一回合的
    0x41C96B/0x41C9A7**：`bit7 → 清 0 + 释放动画组能力(sub_40B8D8) + loadWalkResources`（梦游另按
    礼物流通池计数恢复暂存载具 0x41CA04..）。重写缺 bit7 清 0 段 → `0x80↔0x7F` 死循环（128 天）；
    已在 `updatePlayerStates` 补全两段（含载具恢复/池扣减）。
  - **冬眠棋子特效**：0x4087BE（玩家 byte54≠0）/0x4089C8（事件槽 timerA≠0）→ 棋子**灰化**
    （sub_4555C5 调色板逐项 sub_4555EB：`gray=(R+G+B+40)>>2`）；重写新增
    `blitSpriteFrameGrayClipped` + SpriteDraw.gray（索引 0 仍透明）。
  - 系数复核（get_bytes）：dbl_4653D8=**0.2**（查税）、flt_4653BC=**200.0**（黑卡）、
    flt_46531C/324=**2.0**、flt_465320/328=**5.0**（购地/拍卖补偿）——与实现一致。
  - 验证：冬眠 byte54 3→2→1→128→**醒来清 0**（`wake: ... hibernate end` 日志断言 PASS）；
    卡片场景 23/23、128_card_hibernate/54_jail/66_hospital/248_panel_smoke 全绿。
- **2026-09-30 三轮：30 卡逐一 IDA 核对（进行中，已核 17/30）与修复**：
  - **冬眠特效勘误（实机反馈"应蓝白、显示灰白且花屏"）**：原版 `sub_4555C5` 按运行像素格式
    分派 4 变体（`detectPixelFormat` 0=RGB555/1=RGB565/**2=BGR565(R mask 0x1F)**/3=BGR555）。
    实机对照观感为蓝白 → 走 **0x45566E**：`gray=(R5+(G6>>1)+B5+40)>>2`、
    输出 565 = (R=gray, G=2gray, B=31) → RGB555 等价 **(R/G=gray&0x1F, B=31)**。
    此前误用纯灰且 `gray>31`（白/亮像素 gray 可达 33）时 `gray<<10` 溢出 bit15 → **花屏**。
    `blitSpriteFrameGrayClipped` 更名 `blitSpriteFrameFreezeClipped`（位域环绕照抄）。
  - **NPC 目标分支系统性缺失（6 卡）**——原版以玩家为目标的卡对 `bitScan >= 4`（事件槽 NPC）
    有专门分支，重写全部漏掉（多为"吞卡后 return 0 无效果"）：
    - 停留 0x443FE2 → `npcSlots[].timerC=1`；乌龟 0x4459D2 → `timerD=3`；
    - 梦游 0x44449E → `timerB=5`（仅 `timerA==0`）；陷害 0x44467D → `jailPlayer(cur, npc, 5)`；
    - 转向 0x44303A → 仅 `turnToAdjacentCell`（无台词）。
  - **梦游卡字段错写**：`byte58 += 5` → `byte66`（0x444372 `unk_496BAA`，同冬眠卡 0x4441A1）。
  - **`sub_41D546` 语义统一**：= `manualView=false` + 全屏重绘（原来部分卡只 renderGameFrame、
    部分卡完全没有）；停留/乌龟/转向他人分支已补。
  - 核对通过（与原版一致）：均富 0x4420D8、均贫 0x4421B5（NPC 原版无分支，保守 return 0）、
    购地 0x442325（金额/补偿 M×priceAdd×(lv+2)/5/现金不足不吞卡）、换地 0x442622、
    换屋 0x442B02、转向、拍卖 0x443225、抢夺 0x443E3D（吞卡仅在成功抢到后）、停留、冬眠、
    梦游、陷害、查税 0x4451F0（20%+免费/嫁祸链+重算转账）、涨价 0x44542D、查封 0x445593、
    同盟 0x445710、乌龟 0x4458DF。
  - **已知差异（待办）**：①AI/托管用卡 `flyObjectSprite` 飞行演出全部未接（原版 objId=0
    语义待查，人类不播）；②换地/换屋 `sub_45285E(500)` 交换后停留未接；③`passOnCardDialog`
    原版第二参（查税=2、梦游/陷害=0）未区分，重写统一 (app,user) 待核差异；④抢夺对 NPC
    （原版走 selectCardOrItemDialog 无专门分支）重写保守拒绝；⑤均贫/查税 NPC 同理。
  - 剩余核对：重建 0x44309B、天使 0x4434C0、恶魔 0x4436E0、拆除 0x443917、怪兽 0x443B0F、
    红/黑卡 0x444F25/0x44503F、被动 18/19/20/21（0x444691/0x44476A/0x444A60/0x444BB2）。
- **2026-09-30 四轮：剩余 13 卡核验完成（30/30 全卡核对完毕）与修复**：
  - **红/黑卡停牌天数**：原版 `unk_496987[36*股] = 32(0x20)`（红）/`2(0x02)`（黑）——高/低半字节各=2；
    advanceDay 每日对高半字节 −16、低半字节 −1 → 当天 + 后续 2 天。重写曾写 0x30/0x03（多 1 天）。
    已改 0x20/0x02（帮助"漲停板三天/跌停板三天" = 当天计入）。
  - **查税消息文案**：原版 `"抽取%s\n\n%d元稅金！"`（0x4653C0）；重写曾用 `"%s被課稅%d元"`。已改。
  - **嫁祸 AI 阈值（passOnCardDialog mode 参数）**：原版 `(user, a2, a3)`——a2=0（命运/新闻/梦游/
    陷害/resolvePenaltyTarget）AI 直接嫁祸；**a2=1（住宅/商業/行業設施收费 0x419EB8/0x41A683/0x41AF75，
    a3=费用）**：AI 仅当 `fee > 现金 或 (rand%4000+4000)×M < fee` 才嫁祸；**a2=2（查税 0x445360）**：
    AI 仅当 `4000×M < 现金×0.2`（dbl_465380）才嫁祸。重写补 mode/fee 参数并在 `resolveFeePayer`
    （mode=1）与查税（mode=2）接线（此前重写对所有场景无条件嫁祸）。
  - **改建卡 AI 分支**：原版 corp 分支人类弹 `selectFacilityDialog(1)`、AI 用 `sub_41E6F2(0)`
    预选类型（ai_card `tgtRebuild` 写 `aiCardTarget=roll(4)+1`）；重写此前对 AI 也弹人类面板
    （AI 回合阻塞风险）。已补 AI 分支。
  - 核对通过：改建 0x44309B（estate type^=1/连锁降 level；corp 公園/加油站降 sub；取消不吞卡）、
    天使 0x4434C0（同路段 level<5 +1、连锁 0→1；corp angelUpgrade<0→FLC523；人类也 fly）、
    恶魔 0x4436E0（同路段清 level/type + 债务 30×M×level；corp 债务 30×M×sub +
    forceHotelCheckout）、**怪獸 0x443917**（id11=怪獸卡：整块 demolishEstate mode2 + 债务
    30×M×level + FLC557；与 id 表 0x475D88/卡名 kCardNames[11]/卡价 60 点/帮助 idx65 四方吻合）、
    **拆除 0x443B0F**（id12=拆除卡：路面道具 deleteMapObject / 降一级 / 连锁变平地 /
    corp sub-1 归零清 type+forceHotelCheckout + 债务 30×M + FLC529）、復仇 0x444691、
    免罪 0x444BB2（"免罪卡生效！"+吞卡+台词）、免費 0x444A60（AI 阈值 `(rand%3000+3000)×M` ✓）、
    送神 0x444C45（挂身道具 deleteMapObject + 负面神 {5,6,7,8,10,15} attachEndAnim；正面神/
    无对象静默失败不丢卡）、请神 0x444E1A（pickNearestAttachable / AI 预选、神明 sprite 飞行、
    cellEnt 暂摘/还原）。
  - 已知差异（演出类，汇总）：AI/托管用卡 `flyObjectSprite`（objId=0 卡牌飞行）未接；
    换地/换屋 `sub_45285E(500)` 交换后停留未接；天使/恶魔/怪兽/拆除/停留/梦游/陷害/均贫/转向
    人类 fly 未接；`focusView` 若带重绘与原版 `refreshGameUi(x,y,0)`（不重绘）语义差异待实机核。
  - 研究所：**按用户裁定恢复 IDA 原逻辑**（研究中仍可重选覆盖 researchItem，触发点仅
    owner/type4/sub!=0/(flag&0x0F)==0；查封卡置 flag 低半字节才禁研究）。

## 6. 每卡实现前待细化清单

1. `sub_446AE8` 各 flags 精确语义（哪些位=含自己/含对手/选玩家 vs 选地块/过滤建筑）——`decompile 0x446AE8` 定表。
2. ~~換地/換屋 完整流程~~ ✅ 2026-09-30 已完整逆向：owner 互换（换地）/ level+type 互换（换屋，
   `sub_40B4F8`）；语音为条件单条（见 §5 台词段）；`0x442622`/`0x442B02` 反汇编核实。
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
