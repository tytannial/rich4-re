# 0x41982D landingEvent 收租/收费专项（P1）

> 本文档归档 `landingEvent`（0x41982D）case 0 他人**住宅用地**收租（loc_419A67）的完整逆向结论。
> 总览见 `gameplay-map-mechanics.md` §5 P1；P2 事件格见 `41982d-p2-events.md`；
> 全量收费公式（物價指數/轮盘/商業用地/行業設施點/到期）见 `pricing-formulas.md`。
> **实现状态**：他人住宅用地收租 ✅（`src/app/turn_system.cpp`）；商業用地 corp / 行業設施點 specPt 收费 ✅
> （`turn_system.cpp` + `facility_dialog.cpp`/`roulette_dialog.cpp`，公式见 `pricing-formulas.md` §4/§5）；
> **同盟分账 ✅ 2026-09-27**（同盟者联合租金并入 + 按 v132 拆分转账 + 消息 0x46399A；高亮含
> 同盟地块；破产解除同盟 0x40CE74）；死神代付 / 卡片免租 ✅（`resolveFeePayer`）；
> 收租角色台词（付款方 `sub_44F4ED`/`sub_44F42D`、收款方 `sub_44F354`）→ P4。

## 0. 时机与入口

| 时机 | 原版 | 说明 |
|------|------|------|
| 移动结束 | `sub_448A7E` → state=3（0x40DA4B） | 条件：`(travel & 0x83) == 3`（**临时载具计时中**，非步行）+ 无 state37/stateFlags BYTE0 + 落在他人有建筑（`estate.level!=0` / `corp.sub!=0`）的住宅用地/商業用地 → `addPlayerDebt(owner-1, cur, 30*g_moneyMul)` **仅记账** + 面向目标（`sub_454FB4`）+ FLC 526 画在**建筑屏幕坐标**（`sub_40B066` 取绘制列表 x/y，x-55/y-55）|
| 停留结算 | `sub_418E7F` → `landingEvent`（0x41982D）case 0 → **loc_419A67** | 实际收租：音效槽 20 → 状态免收 → 联合租金 → 神明调整 → showMessage → 转账 |

- case 0 分派：objId 2000..3999 = **住宅用地 estate**、4000..5999 = **商業用地 corp**（0x41A168 起，
  公式 → `pricing-formulas.md` §4，✅ 已实现）、6000..7999 = **行業設施點 specPt**（同上 §5，✅ 已实现）、
  8000+ = evtCell（仅提示）。
- 重写中 `landingEvent` 由 `calcPlayerWait`（0x418E7F）在**移动后 case 0 倒计时结束**时调用，
  返回值 = 等待帧数（`kLandingWait=0x90`，原版 0x88）。
- **`travel`（+17 `g_playerVehicle`）编码**（0x40721F/0x41C84F 实测）：低 2 位 = 载具类型
  （0=步行 / 1=機車 / 2=汽車）、bit2-7 = 载具剩余回合；**`4n+3` = 临时载具计时中**
  （`sub_41C84F` 每回合 `-4`，归零后恢复默认载具或步行）。
  加油站免付条件 `(travel & 3) == 0`（步行）印证此编码。
  → **普通步行/機車/汽車（travel=0/1/2）不满足 `(travel & 0x83) == 3`**：
  收过路费**不面向、不停留**（原版 `sub_448A7E` 不设 state=3）；
  case 3 的面向 + FLC 526 仅出现在临时载具计时场景（重写载具计时未实现，路径暂不可达）。

## 1. 他人住宅用地收租（0x419A67..0x41A00E）

```
audioPlayEffect(&g_effectSlots[2*20]);                       // [0x4823EA] 收租音效槽 20
if (ownerCanCollectRent(owner-1, estate.flag(+23), "過路費") != 1) goto end;  // 0x41D559
v140 = g_playerAlly[owner-1];                                // 地主同盟对象（1-based，0=无）
if (estate.type(+24)) {                                      // 连锁店
    v8   = estateRouteRent(owner, 0);                        // 数量×2000
    v134 = estateRouteRent(v140, 0);
} else {                                                     // 普通住宅用地
    v8   = estateRouteRent(owner, estate.name(+4));          // 同路段 fees[level] 之和
    v134 = estateRouteRent(v140, estate.name(+4));
}
if (estate.flag(+23)) v8 *= 2;                               // **仅地主侧**，v134 不翻
rebuildPickBuffer(1);                                        // 同路段高亮准备
for (每块地产) if (同 owner 或 v140 且同名/连锁) sub_456C0A(pickBuffer, ..., 2000+idx, 0xFFFF);
if (v141 > 1) sub_451985();                                  // 多块联合 → 动画延时
if (v140) { v8 += v134; v132 = v134/v8; }                    // 同盟：总额 + 分成比例
sprintf(text, "%s\n\n此地屬%s\n\n請付%d元%s", 地名, 地主名, v8, "過路費");   // 0x4639B3（单人）
// v140 非 0 时改用 0x46399A: "%s\n\n屬%s與%s\n\n請付%d元%s"（含 v134==0 也显示「與B」）
showMessage(text, 1500);
v12 = applyGodRentModifier(cur, "過路費", v8);               // 0x41D709 付款方神明（作用总额）
if (v12) {
    addPlayerDebt(cur, owner-1, v12/100);                    // 0x40DF69 记账（同盟时 v140 另记一笔）
    // ---- 卡片检查（✅ resolveFeePayer）----
    if ((v12 >= 2000*g_moneyMul || v12 > 现金+存款) && sub_4413AD(cur, 20) && sub_444A60(cur, owner-1, v12))
        v12 = 0;                                             // 免費卡（租金超 2000×物价时抵用）
    if ((v12 >= 2000*g_moneyMul || v12 > 现金+存款) && sub_4413AD(cur, 19))
        v15 = sub_44476A(cur, 1, v12);                       // 嫁禍卡（换付款人）
    // ---- 死神/寶箱代付（✅ resolveFeePayer）----
    v15 = findDeathGodCarrier(cur);                          // 0x40FBB8 找携带槽 14/15 的其他玩家
    if (v15 != -1) { showMessage("死神顯靈\n\n由%s賠償%s"); }
    if (v15 != owner-1 && v15 != v140-1) {                   // 付款人=收款人 → 不转账不更新租金
        sub_44F4ED(v15, owner-1, v12); sub_44F354(owner-1, v12);   // 付款/收款方台词 [P4]
        if (v140) { sub_41D2C6(v15, owner-1, v12 - trunc(v12*v132)); sub_41D2C6(v15, v140-1, trunc(v12*v132)); }
        else      { sub_41D2C6(v15, owner-1, v12, 0); }
        estate.price(+44) = v12;                             // 最近租金
    }
}
```

- **转账 flags=0**：`sub_41D2C6(from,to,amount,flags)` 中 flags&1=收款方入现金、否则入银行；
  收租用 0 → **地主/同盟收进银行存款**（付款方仍现金优先，不足扣银行）。
- **同盟分账 ✅ 2026-09-27**（`turn_system.cpp` 收租段）：`v132 = v134/(v8+v134)`（分母 v8 已含
  flag 翻倍与同盟租金）；`trunc(v12*v132)` 给同盟、余额给地主；`estate+44` 仅在付款人
   ≠ 地主/同盟时更新。~~差異：免費卡前置门槛未移植~~ ✅ 2026-09-29 M3-B：`resolveFeePayer`
   已加 `fee ≥ 2000×M || fee > 付款人现金+存款` 门槛（免費/嫁祸同，0x419E58/0x419EB8）。
- **记账 `addPlayerDebt` ✅ 2026-09-27**（0x419DB1/0x419DD8）：付款人 → 地主
  `(v12-v134)/100` + 同盟 `v134/100`（单人一笔 `v12/100`），在免费卡/嫁祸之前执行；
  矩阵语义 `[a1][a2]` = a2 欠 a1（推测为面板"应付"显示/破产清算用），`clamp≥0`。

## 2. 三个辅助函数

### 2.1 `estateRouteRent` 0x419744（联合租金）

```
a2 != 0（地名）: Σ fees[level]  for 全部 estate: type==0 && owner==a1 && strcmp(name)==0
a2 == 0       : Σ 2000        for 全部 estate: type!=0 && owner==a1   （连锁店按店面总数）
return g_moneyMul * 总和
```

- `fees[level]` = estate+32+2*level（6 级）。
- 同路段 = **同名**（MAPDAT 地名，如"信義路"），非地图邻接。

### 2.2 `ownerCanCollectRent` 0x41D559（地主状态免收）

| 顺序 | 条件（地主） | 提示（0x463BB8 起，BIG5） |
|------|-------------|---------------------------|
| 1 | `flag & 0xF0` 与 `flag & 0x0F` 均非 0 | `房屋查封中\n\n免收%s！` |
| 2 | `g_playerAlly[owner] == 付款人+1` | `與%s同盟中\n\n免收%s！` |
| 3 | `cellTableIdx == 15`（死神） | `死神顯靈\n\n免收%s！` |
| 4 | `stateFlags` BYTE0 | `%s住宿中\n\n免收%s！` |
| 5 | BYTE1 | `%s消失中` |
| 6 | BYTE2（監獄） | `%s坐牢中` |
| 7 | BYTE3（醫院） | `%s住院中` |
| 8 | `byte54`（+54） | `%s冬眠中` |
| 9 | `state37`（+55） | `%s夢遊中` |

全部不满足 → 返回 1（可收）。命中 → `showMessage(text, 1500)` + 返回 0。

### 2.3 `applyGodRentModifier` 0x41D709（付款方神明）

| `cellTableIdx` | 神明 | 效果 | 提示 |
|----------------|------|------|------|
| 1 | 小財神 | `rent/2` | `小財神顯靈\n\n%s減免一半！` |
| 2 | 大財神 | `0` | `大財神顯靈\n\n免付%s！` |
| 5 | 小窮神 | `rent + rent/2` | `小窮神顯靈\n\n%s加付50％！` |
| 6 | 大窮神 | `2*rent` | `大窮神顯靈\n\n加倍付%s！` |

金额变化时才 showMessage（`v4 != a4`）。

## 3. 相关数据结构

| 项 | 地址/偏移 | 说明 |
|----|-----------|------|
| `g_playerAlly`（+65） | 0x496BA9 | 同盟对象（1-based，0=无）。原 IDB 名 `g_playerVehicle` 是误读：面板 0x416256 画 `pieceSprites[+65-1]` 帧 2 = 同盟头像；`clearAllyPair`（0x40CC1A）解除双方 +65 与 byte_496BA5 |
| `addPlayerDebt` | 0x40DF69 | `dword_496BB4[26*a1+a2] += a3`（clamp≥0）；`a3>0` 且 a1 的同盟 == a2+1 → 解除同盟 |
| 欠款矩阵消费者 | `sub_40D2D3` | 返回 a1 的最大债主（AI 决策 `sub_41E779` 等 17 处）|
| `findDeathGodCarrier` | 0x40FBB8 | 返回携带槽 14/15（寶箱/死神）的其他玩家（代付）|
| `sub_41D2C6` | 转账 | flags&4 付款银行优先；flags&1 收款入现金（否则入银行）；不足 → `sub_40CD87` 破产 |
| 卡片检查 | `sub_4413AD`/`sub_444A60`/`sub_44476A` | 卡片 20 = 免費卡（`[HELP 60]`：租金超 2000×物价时抵用）；卡片 19 = 嫁禍卡（`[HELP 80]`：让人代付/换付款人）|
| 文本 | 0x4639B3 / 0x46399A / 0x463BB8..0x463CAE | 见上表（BIG5，`get_string` 会在 \n 截断，须 `get_bytes` 解码）|
| 费用名表 | `off_47517C` | 0x47517C 起 **13 项指针数组**：0=過路費(0x46388A)/1=房租費/2=加油費/3=修車費/4=店租費/5=住宿費/6=旅遊費/7=保險費/8=電腦費/9=工程費/10=水費/11=電費/12=購物費（房租/店租/電費 = 原版预留未接通）；corp/specPt 按映射表 `byte_475284[type+7]` / `byte_47528E[index]` 取值 → `pricing-formulas.md` §2|

## 4. 商業用地 / 行業設施點收費（0x41A168..0x41B074，已逆向 → 详见 `pricing-formulas.md` §4/§5）

> ⚠️ 修正：此前把该段称为「商業用地/行業設施點收費」并按 `corp.sub` 分支推测是错的。实测结构 =
> **objId 分派**：4000..5999 → `corp`（**商業用地** `[HELP 22]`，按 `+24 type` 0..4 分派）、
> 6000..7999 → `specPt`（**行業設施點**，按 `+26 index` 0..15 分派，`[HELP 20]`「公司企業」11 行业在此）。
> 「公司企業/持股分紅」是独立的 12 支股票系统（`g_playerShares`），与本段无关。

- 商業用地收費（0x41A377 起）：`type==0`（公園）/`type>=4`（研究所）不收費；
  旅館 = `feeTable[sub] × M × 轮盘(1)天数`（另住宿記帳 `2000×M/天` + `20×M/天` 欠款 + BYTE0 天數）；
  購物中心 = `feeTable[sub] × M × 轮盘(2)倍數`；加油站 = `500 × 載具倍率 × 本次步數 × M`（步行免付，
  "沒開車就不用給錢"）；`flag(+28)` 非 0 → 翻倍（旅館/購物）。
- 行業設施點收費（0x41AB7D 起）：航空 `+34×轮盘(0)×M`（0 → "不用出國！"，付款后出國 n 天）、
  電腦 `+34×g_dayCount`（不乘 M）、保險 `+34×轮盘(3)×M`（記保險期 n 天）、汽車/石油 `+34×載具倍率×本次步數×M`、
  房地產=抽一塊地按其地價×M、idx12 `+34×本次步數×M`；其餘 index 不收費；**收款方 = objId−5900 的公庫**。
- 费用名：corp `g_costNames[byte_475284[type+7]]`、specPt `g_costNames[g_specPtCostMap[index]]`。
- `ownerCanCollectRent` / `applyGodRentModifier` / 免費卡（`[HELP 60]`）/ 嫁禍卡（`[HELP 80]`）/ 死神代付与 estate 同构。

### 4.0.1 旅館住宿生命周期（0x41A761 / 0x41C84F / 0x40D6BE）

| 阶段 | 地址 | 行为 |
|------|------|------|
| 设置 | 0x41A761 | `sub_40D761` 清旧状态（住宿/監獄/醫院）→ `stateFlags.BYTE0 = days-1`（days==1 → `0x80`）+ `byte66 += days` + 欠款 `20×M×days` + 理赔 `2000×M×days` |
| 走进 | 0x40D5A5 | 清原格占用（**"消失"**）→ `word_496BB2` = 旅館 corp 索引 + 保存朝向 `g_playerRestoreDir` + 面向旅館 + `alive \|= 0x20` → `sub_40DD1F` 启动移动（`g_stepsRemaining=1`，状态 1）。非当前玩家直接传送到旅館坐标 |
| 走进移动 | 0x40C05C（`alive & 0x20`） | 起点 = 旅館格坐标、终点 = **旅館 corp 坐标**（慢速插值，`flt_4631DC` 系数）；**半程** `alive &= 0xF`（玩家到达旅館，住宿期间不绘制棋子） |
| 倒计时 | 0x41C84F（nextPlayer 切换玩家后） | BYTE0 `n>0 → n-1`；`n==1 → (n-1)\|0x80`；bit7 → `sub_40D6BE` 结束 |
| 跳过 | 0x40C912(1) | `stateFlags != 0` → 返回 0 → `wait=-125` 跳过回合 |
| 状态文本 | 0x40C912(0) | `"%s住宿中\n\n還剩%d天！"`（天数 = `(BYTE0 & 0x7F) + 1`）；出國/坐牢/住院/冬眠 同构（0x4631E0..0x463234） |
| 结束 | 0x40D6BE | `alive \|= 0x10` + 朝向重算（面向格中心）+ 恢复格占用 → 玩家回合 `sub_40DD1F` 启动**走出移动** |
| 走出移动 | 0x40C05C（`alive & 0x10`） | 起点 = 旅館坐标、终点 = 旅館格坐标；**半程** `stateFlags = 0`（**全部状态在此清零**，恢复行动与显示）→ 完成后**不触发落地结算**（不重复收旅館费） |
| 强制结束 | 0x40DFFA | corp 到期回收（`expireAssets`）时把所有玩家 BYTE0 置 `0x80`（跳到最后一天） |

- **状态清零位置**：不在 `sub_41C84F`/`sub_40D6BE`，而在**走出移动半程**（0x40C05C）。
  監獄/醫院同构：`releaseJailNpc`/`releaseHospitalNpc` 调 `sub_40D6BE` 后由走出移动清 BYTE2/BYTE3。
- **住宿期间**：`ownerCanCollectRent` 检查地主 BYTE0 != 0 → 免收租（8 种提示之一）；
  渲染按 `stateFlags & 0xFF != 0` 跳过棋子绘制（角色"消失"）。
- 出國（BYTE1）无"走出"：`sub_40D4E5` 自行清 BYTE1 + 恢复占用 + `loadWalkResources` + FLC 533/558。
- 冬眠（byte54）/夢遊（state37）在 `stateFlags == 0` 时倒计时。

## 4.1 连锁店（改建卡创建 + 收费）

- **创建**（改建卡 card[7] → `cardRebuildEffect` 0x44309B）：
  ```
  estate 分支（要求 level(+26) != 0，必须有建筑）:
      wasChain = (type(+24) == 1)
      type ^= 1                    // 0=住宅 ↔ 1=连锁店
      if (!wasChain && level > 1) level = 1   // 变连锁店时等级固定 1
      sub_441343(player, 7)        // 消耗卡片
  corp 分支: selectFacilityDialog(1) 变更设施类型（公園/加油站时 sub 降为 1）
  ```
  - 卡片表 `off_47FDEA`（card[7]=改建卡）；效果函数表 `g_cardEffectFuncs`（0x475D5C，30 项）
  - 使用流程 `useCardFlow`（0x441BAA）：人类 `sub_4416F0` 选卡 / AI `sub_41E69E` 可用判定 → 效果函数
- **收费**：`estateRouteRent(owner, 0)` = 同 owner **全部连锁店数量 × 2000 × M**（不与同路段联合）；
  `flag(+23)` 翻倍、神明调整、地主状态免收、转账 flags=0 与住宅一致；不可加盖（升级守卫 `type!=0`）
- **外观**：`type != 0` → `estateFlag`（map.mkf[79+4*mode]）+ owner 色（0x4091DF）

## 4.2 收租联动高亮（整条街/连锁店闪烁）

- **标记**：收租时遍历全部地块，将联动组在 `g_pickBuffer` 标为 `0xFFFF`（`markPickBuffer` 0x456C0A，
  缓冲 440×440，`193600` 字节）：
  - 住宅用地 = 同 owner + 同名 + `type==0`
  - 连锁店 = 同 owner + `type!=0`（**跨街道全部连锁店**）
  - **同盟地块（`v140`）同组一并标记 ✅**（0x419C44/0x419BE2；两组计数合计 > 1 才闪烁）
- **闪烁**（`highlightBlink` 0x451985，仅标记数 `v141 > 1` 时）：
  - 16 帧亮度偏移 `g_highlightLut`（0x476380）= `{4,8,12,16,12,8,4,0,-4,-8,-12,-16,-12,-8,-4,0}`
  - 每帧 30ms（`sub_4528B9(30)`），用 `blitHighlightedMap`（0x4554FC）按标记 + 亮度调色板重绘
  - 循环后 `sub_4528B9(400)` 停留（最后一帧偏移 0 = 正常画面）
- **重写**：`startRouteHighlight`（turn_system.cpp）收集联动组 → `playHighlightBlink` **阻塞播放**
  （16 帧 × 30ms + 400ms，逐帧 `renderGameFrame` + `renderFrame` present）→ **闪烁结束后**才弹收费
  提示（对齐原版 `sub_451985` 在 `showMessage` 之前完成的时序）；`captureHighlightShapes`
  （map_render.cpp）从 `mapHitRegions` 提取原版 `g_pickMask` 形状快照（拾取缓冲写入形状），
  `drawEstateHighlight` 按 `highlightFrame` 对快照做 RGB555 亮度偏移（8bit 量 >>2）；
  `Ctrl+9` 可一键把联动组归当前回合玩家（测试用）
- **收费提示金额** = 神明调整**前**的联合租金（原版 `sprintf(...v8...)` 在 `sub_41D709` 之前）；
  神明调整后金额用于转账与 `estate+44`（与原版一致）
- **case 3 面向恢复**：`sub_448A7E` 条件（他人 + `level/sub != 0`）成立时保存原朝向到
  `g_playerRestoreDir`（+27）→ 面向目标 → 停留（重写 30 tick 近似 FLC 526）→ **恢复原朝向** →
  转状态 0（此前重写面向后未恢复）

## 5. 重写差异与 P4 待办

- **已实现**：音效槽 20、地主状态免收、联合租金（同路段/連鎖店）、flag 翻倍（仅地主侧）、
  付款方神明调整、showMessage 提示（含同盟 0x46399A）、`transferMoney(..., 0)` 拆分、
  `estate.price = fee`、**联动高亮**（`highlightBlink` 还原，含同盟地块）、
  **连锁店收费 + Ctrl+4 创建**（debug）、`estateRouteRent` 诊断日志、
  **同盟分账 ✅ 2026-09-27**（比例拆分/收款人守卫）、**破产解除同盟 ✅**（0x40CE74）、
  **商業用地 / 行業設施點收费**（0x41A168，公式 `pricing-formulas.md` §4/§5，含旅館轮盘+住宿/購物轮盘/
  加油站步数/6 行业公式 + 公库 `fund`）。
- **收租角色台词 ✅ P4-B**（付款方 `sub_44F4ED` **列18**「欠债怨念」/`sub_44F42D`、收款方
  `sub_44F354`；**勘误：`sub_44F354` 实为按金额档选句的台词表，非"浮动数字"**——
  `map-object-refresh.md` §5 已正确描述）；**2026-09-30 列勘误**：`playDebtorLine` 曾误用
  列16（连锁买地语），已修为列18；地主状态免收另有付款人台词列13（0x41D6DD）。
- ~~免費卡前置门槛~~ ✅ 2026-09-29 M3-B 已实现（`resolveFeePayer` `cardGate`，免費/嫁祸同门槛）
- **case 3 语义修正**：早期重写在 case 3 立即转账 30×`moneyMul`，与 landingEvent 收租重复；
  已移除（保留面向 + 记账日志 + `stepDwell`）。
- **调试**：`estateRouteRent` 打印每个匹配地块明细 + 联动块数/总额（定位"整条街未联动"类问题）；
  `Ctrl+9` 指向地块所在联动组归当前玩家；`Ctrl+4` 住宅↔连锁店切换（`type^=1`、`level=1`）。
