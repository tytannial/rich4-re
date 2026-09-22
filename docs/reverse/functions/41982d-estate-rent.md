# 0x41982D landingEvent 收租/收费专项（P1）

> 本文档归档 `landingEvent`（0x41982D）case 0 他人**住宅用地**收租（loc_419A67）的完整逆向结论。
> 总览见 `gameplay-map-mechanics.md` §5 P1；P2 事件格见 `41982d-p2-events.md`；
> 全量收费公式（物價指數/轮盘/商業用地/行業設施點/到期）见 `pricing-formulas.md`。
> **实现状态**：他人住宅用地收租 ✅（`src/app/turn_system.cpp`）；商業用地·行業設施點收費 / 同盟分账 / 死神代付 / 卡片
> 免租 → P4（本文档保留结论供后续接入）。

## 0. 时机与入口

| 时机 | 原版 | 说明 |
|------|------|------|
| 移动结束 | `sub_448A7E` → state=3（0x40DA4B） | 条件：`travel & 0x83 == 3`（步行）+ 无 state37/stateFlags BYTE0 + 落在他人有建筑（`estate.level!=0` / `corp.sub!=0`）的住宅用地/商業用地 → `addPlayerDebt(owner-1, cur, 30*g_moneyMul)` **仅记账** + 面向目标（`sub_454FB4`）+ FLC 526（x-55,y-55）|
| 停留结算 | `sub_418E7F` → `landingEvent`（0x41982D）case 0 → **loc_419A67** | 实际收租：音效槽 20 → 状态免收 → 联合租金 → 神明调整 → showMessage → 转账 |

- case 0 分派：objId 2000..3999 = **住宅用地 estate**、4000..5999 = **商業用地 corp**（0x41A168 起，
  公式已逆向 → `pricing-formulas.md` §4，实现待做）、6000..7999 = **行業設施點 specPt**（同上 §5）、
  8000+ = evtCell（仅提示）。
- 重写中 `landingEvent` 由 `calcPlayerWait`（0x418E7F）在**移动后 case 0 倒计时结束**时调用，
  返回值 = 等待帧数（`kLandingWait=0x90`，原版 0x88）。

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
if (estate.flag(+23)) v8 *= 2;
rebuildPickBuffer(1);                                        // 同路段高亮准备
for (每块地产) if (同 owner 或 v140 且同名/连锁) sub_456C0A(pickBuffer, ..., 2000+idx, 0xFFFF);
if (v141 > 1) sub_451985();                                  // 多块联合 → 动画延时
sprintf(text, "%s\n\n此地屬%s\n\n請付%d元%s", 地名, 地主名, v8, "過路費");   // 0x4639B3
// 有同盟分账（v140）时用 0x46399A: "%s\n\n屬%s與%s\n\n請付%d元%s"
showMessage(text, 1500);
v12 = applyGodRentModifier(cur, "過路費", v8);               // 0x41D709 付款方神明
if (v12) {
    addPlayerDebt(cur, owner-1, v12/100);                    // 0x40DF69 记账（金额/100）
    // ---- 卡片检查（未实现，P4）----
    if ((v12 >= 2000*g_moneyMul || v12 > 现金+存款) && sub_4413AD(cur, 20) && sub_444A60(cur, owner-1, v12))
        v12 = 0;                                             // 免費卡（租金超 2000×物价时抵用）
    if ((v12 >= 2000*g_moneyMul || v12 > 现金+存款) && sub_4413AD(cur, 19))
        v15 = sub_44476A(cur, 1, v12);                       // 嫁禍卡（换付款人）
    // ---- 死神/恶魔代付（P4）----
    v15 = findDeathGodCarrier(cur);                          // 0x40FBB8 找携带槽 14/15 的其他玩家
    if (v15 != -1) { showMessage("死神顯靈\n\n由%s賠償%s"); }
    if (v15 != owner-1 && v15 != v140-1) {
        sub_44F4ED(v15, owner-1, v12); sub_44F354(owner-1, v12);
        if (v140) { sub_41D2C6(v15, owner-1, v12 - v12*v132); sub_41D2C6(v15, v140-1, v12*v132); }
        else      { sub_41D2C6(v15, owner-1, v12, 0); }
        estate.price(+44) = v12;                             // 最近租金
    }
}
```

- **转账 flags=0**：`sub_41D2C6(from,to,amount,flags)` 中 flags&1=收款方入现金、否则入银行；
  收租用 0 → **地主收进银行存款**（付款方仍现金优先，不足扣银行）。
- `v132 = v134 / (v8+v134)`：同盟分账比例（同盟也持同路段地块时）。
- 记账 `addPlayerDebt(cur, owner-1, v12/100)` 与转账方向相反（矩阵语义 `[a1][a2]` = a2 欠 a1），
  推测为面板"应付"显示/破产清算用；重写不建模。

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

## 5. 重写差异与 P4 待办

- **已实现**：音效槽 20、地主状态免收、联合租金（同路段/連鎖店）、flag 翻倍、付款方神明调整、
  showMessage 提示、`transferMoney(p, owner, rent, 0)`、`estate.price = rent`。
- **未实现（P4）**：
  - 同盟分账（`g_playerAlly` 双方按 `v134/(v8+v134)` 拆分；重写字段已建模）
  - 死神/恶魔代付（`findDeathGodCarrier`）
  - 免費卡/嫁禍卡检查（依赖卡片系统 `sub_444E1A`）
  - 同路段高亮（`rebuildPickBuffer` + `sub_456C0A`）与浮动数字（`sub_44F354`）
  - 商業用地 / 行業設施點收費（0x41A168，公式已逆向 → `pricing-formulas.md` §4/§5）
- **case 3 语义修正**：早期重写在 case 3 立即转账 30×`moneyMul`，与 landingEvent 收租重复；
  已移除（保留面向 + 记账日志 + `stepDwell`）。
