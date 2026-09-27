# 收费/价格计算公式总汇（地块与设施）

> 本文汇总原版所有「玩家付钱 / 收钱」计算公式（购地/建设/升级/过路费/设施使用费/罚款/到期清算），
> 逐条标注原始地址与反编译依据。数据依据：`sub_41982D`（landingEvent）case 0 全段
> （0x4198B9..0x41B074）、`expireAssets`（屏幕破坏 0x40AC7B）、`updateMoneyIndex`（物價指數 0x423ACF）、
> `roulettePrompt`/`slotMachineValue`（轮盘 0x44090E/0x43F23E）。
> 格子机制总览见 `gameplay-map-mechanics.md`；住宅用地收租细节见 `41982d-estate-rent.md`；
> 事件格见 `41982d-p2-events.md`。
>
> **术语对照（帮助原文）**：帮助分类「房地產」三页 = `[HELP 20] 公司企業`、
> `[HELP 21] 住宅用地`、`[HELP 22] 商業用地`。对应实现：
> - **`g_estates`（2000..）= 住宅用地**（`[HELP 21]`：公定價格購地→加蓋五級→同路段聯合收租；
>   改建卡→**連鎖店**「以店面總數計算」）。
> - **`g_corps`（4000..）= 商業用地及其上設施**（`[HELP 22]`：「公園、商場、旅館、加油站、研究所
>   五種建設」，设施名表 `g_facilityNames`/`off_475150` 作「購物中心」；「除公園、加油站外可加蓋
>   五個等級」；「公園與研究所不收費」）。項名 = 城市名（花蓮市/知本/墾丁/基隆市）。
> - **`[HELP 20]「公司企業」= 12 支股票系统**（認購≤2000 張/董事長/每月 15 號分紅，
>   `g_playerShares` 0x4971A0 / 现价 `flt_496994`）；其「11 种行业各有消費方式」的收费
>   实际落在 **`g_specPts`（6000..7999）的 `+26 index`**（航空/電腦/保險/汽車/石油/銀行/水力/
>   電力/百貨/房地產）与 corp 旅館（帮助「飯店」）上。

## 0. 速查表

| 类别 | 公式 | 地址 |
|------|------|------|
| 買住宅空地 | `(+28 priceBase + level × +30 priceAdd) × M` | 0x41A04A |
| 住宅用地升級 | `+30 priceAdd × M`（level<5 且 type==0） | 0x41993D |
| 住宅用地收租 | `estateRouteRent(owner, 路段名) ×(flag?2:1)` → 神明/卡片修正 | 0x419744 / 0x419A67 |
| 連鎖店收租 | `店面總數 × 2000 × M` | 0x4197A5 |
| 買商業用地 | `+34 buildPrice × M` | 0x41A890 |
| 建設施 | `+34 × M` + 選設施類別（`selectFacilityDialog`） | 0x41A200 |
| 設施升級 | `+36 feeTable[0] × M`，上限 `g_facilityMaxLevel[type]` | 0x41A2D9 |
| 旅館收費 | `feeTable[sub] × M × 轮盘天数` ×(flag?2:1) | 0x41A43F |
| 購物中心收費 | `feeTable[sub] × M × 轮盘倍数` ×(flag?2:1) | 0x41A49B |
| 加油站收費 | `500 × 載具倍率 × 本次步数 × M`（步行免付） | 0x41A529 |
| 航空公司（specPt1） | `+34 × 轮盘4位数 × M`（0 → 免费） | 0x41ABDE |
| 電腦公司（specPt3） | `+34 × g_dayCount`（**不乘 M**） | 0x41AC30 |
| 保險公司（specPt4） | `+34 × 轮盘3位数 × M` | 0x41AC5D |
| 汽車/石油公司（specPt5/6） | `+34 × 載具倍率 × 本次步数 × M`（步行免付） | 0x41AC93 |
| 房地產公司（specPt11） | `目标地(+28/corp+34) × M`；无目标 `1000 × M` | 0x41ADD5 |
| specPt12（銀行类） | `+34 × 本次步数 × M` | 0x41AE27 |
| 過路記帳（case 3） | `30 × M`（仅入欠款矩阵） | 0x40DB19 |
| 入狱/住院罰款 | `2000 × M × 天数` | 0x43D749 / 0x43EDF8 |
| 到期降級/沒收 | 欠 `30 × M × level` | 0x40AD63 |
| 物價指數 | `max(M, Σ存活玩家总资产 / 存活数 / 初始资金)` | 0x423ACF |

（`M` = `g_moneyMul` 物價指數；下表公式均已在 IDB 复核）

## 1. 公共乘数

### 1.1 物價指數 `g_moneyMul`（0x4990E8）

- 初始 1（`newGameInit` 0x4073B4 `mov ds:g_moneyMul, imm`；`[HELP 5]` "遊戲開始時，物價指數為１"）。
- 更新：`updateMoneyIndex`（0x423ACF，**每日** `advanceDay` 调用 0x41CFBF；✅ 已接入 `economy.cpp`）：
  ```
  idx = Σ playerTotalAssets(p)（仅存活） / 存活人数 / g_startMoneyVal(0x49908C)
  if (idx > g_moneyMul) g_moneyMul = idx      // 只升不降
  ```
- `playerTotalAssets` 0x4239B9（胜利判定同口径）：
  ```
  cash + bank − loan
  + Σ_{12支股票} 持股 g_playerShares[24p+2i] × 现价 flt_496994[9i]
  + Σ estate(owner): +28 priceBase + (type≠0 ? +30 : +30 × level)
  + Σ corp(owner)  : +34 buildPrice + +36 feeTable[0] × sub(等级)
  ```
- 读档恢复：`sub_448544` @0x44889C。
- 表现：过路费/地价/事件金额随之整体放大（`[HELP 5]`：平均值达初始资金 2 倍时指数上升）。

### 1.2 本次步数 `g_diceValue`（0x48BAFC）

掷骰定格时 `playerActionStateMachine` case 2：`g_stepsRemaining = rollDice(...); g_diceValue = g_stepsRemaining`
（0x40D9B2/0x40D9B7）。`g_stepsRemaining` = 剩余步数（移动递减），`g_diceValue` 保留**本次行走总步数**
（載具 1/2/3 骰时为总和，`[HELP 15]`），供加油費/修車費/specPt12 等「依行走步數收費」公式使用
（`[HELP 22]` 加油站："就必須依照行走步數付出加油費"）。

### 1.3 載具字段 `g_playerVehicle[104*p]`（玩家结构 +105?，stride 104）

- `& 3` = 載具等级：**0 = 步行（加油/修车免付**，`[HELP 20/22]` "沒開車就不用給錢"）；
  1 = 機車、2 = 轎車、3 = 预留 → 收费倍率 `1 << (v−1)` = 1×/2×/4×。
- ⚠️ 收费倍率与 `[HELP 15]` 載具骰子数（步行 1 / 機車 2 / 轎車 3）是两套数值，勿混用。
- 移动音效槽 = `(v & 3) + 11`（0x40D9ED：步行=11，1/2/3 = 12/13/14 載具声）。
- 买載具（道具 汽車 89 / 機車 97）：`sub_41C84F`（0x41C84F，待深入）。

### 1.4 累计天数 `g_dayCount`（0x4990E4）

`advanceDay`（0x41CF67）每日 `++`。僅 電腦費（specPt idx3）用作乘数。

### 1.5 轮盘/老虎机 `roulettePrompt` → `slotMachineValue`（0x44090E / 0x43F23E）

- `roulettePrompt(a1)`：弹轮盘 UI，画面帧 = `dword_48A05C[(a1 & 3) + 68]`（a1=0..3 四种主题），
  末尾 `JUMPOUT 0x4408F9` 调 `slotMachineValue(a41)` 取读数返回。
- `slotMachineValue(a1)`：4 滚轮动画（`byte_48C504[4]`，各滚 `rand()%10` 定格数字 r∈0..9，
  帧号 = 2r+1），逐步停轮后：
  ```
  value = 1000*(w0>>1) [仅 a1==0] + 100*(w1>>1) + 10*(w2>>1) + (w3>>1)
  ```
  → **a1==0：0..9999（4 位）；a1≠0：0..999（3 位）**。
- landingEvent 中的用法（第 41 参决定位数，调用点栈值）：
  | 调用 | 主题帧 | 语义（帮助原文） |
  |------|--------|------|
  | `roulettePrompt(0,…)` specPt1 航空公司 | 68 | 出國旅遊天數（0 → "不用出國！"）`[HELP 20]` |
  | `roulettePrompt(1,…)` corp type1 旅館 | 69 | 休息天數/消費（"休息%d天 費用%d元" 0x4639FF）`[HELP 22]` |
  | `roulettePrompt(2,…)` corp type2 購物中心 | 70 | 消費金額（"%dx%d倍=%d元" 0x463A14）`[HELP 22]` |
  | `roulettePrompt(3,…)` specPt4 保險公司 | 71 | 投保天數（並 += 保險期/點券）`[HELP 4/20]` |

## 2. 费用名表与映射

### 2.1 `g_costNames`（0x47517C，13 项指针）

`0=過路費 1=房租費 2=加油費 3=修車費 4=店租費 5=住宿費 6=旅遊費 7=保險費 8=電腦費 9=工程費 10=水費 11=電費 12=購物費`
（字符串 0x46388A..0x4638DA）。仅被 `landingEvent` 引用（7 处）；**房租費/店租費/電費 未被任何
映射表引用 = 原版预留**（帮助「公司企業」列 11 行业 > 实际接通实现）。

### 2.2 商業用地费用名映射（0x475284 起第 8 字节）

`landingEvent` @0x41A3B7：`costName = g_costNames[ byte_475284[corp.type + 7] ]`
（`qword_475284` 前 8 字节 = {180,120} 两枚配置值，非表）。

| corp type | 名称（`g_facilityNames`） | 映射值 | 费用名 |
|-----------|--------------------|--------|--------|
| 0 | 公園 | — | 不收费（0x41A386 直接跳出；`[HELP 22]` "不向使用者收取任何費用，也不能再升級"） |
| 1 | 旅館 | 5 | 住宿費（`[HELP 20]` 對應「飯店」） |
| 2 | 購物中心 | 0x0C | 購物費（`[HELP 22]` 列表段作「商場」——实为住宅等级外观名 `kBuildingNames[3]`，详述段作「購物中心」） |
| 3 | 加油站 | 2 | 加油費（`[HELP 20/22]` "沒開車就不用給錢"） |
| 4 | 研究所 | 6（未达） | 不收费（0x41A38F 跳出；`[HELP 22]` "不向路過者收取任何費用"） |

### 2.3 行業設施點费用名映射 `g_specPtCostMap[16]`（0x47528E）

`landingEvent` @0x41AE6B：`costName = g_costNames[ g_specPtCostMap[specPt.index(+26)] ]`

| idx | 映射 | 费用名 | idx | 映射 | 费用名 |
|-----|------|--------|-----|------|--------|
| 0 | 2 | 加油費 | 8 | 0 | 過路費 |
| 1 | 6 | 旅遊費 | 9 | 0 | 過路費 |
| 2 | 0 | 過路費 | 10 | 0 | 過路費 |
| 3 | 8 | 電腦費 | 11 | 9 | 工程費 |
| 4 | 7 | 保險費 | 12 | 0 | 過路費 |
| 5 | 3 | 修車費 | 13..15 | 0x0A | 水費 |
| 6 | 2 | 加油費 | 7 | 0 | 過路費 |

### 2.4 设施名表 `g_facilityNames`（0x475150）

`0=公園 1=旅館 2=購物中心 3=加油站 4=研究所 | 5..10=０級..五級`（等级名，UI 用）。

## 3. 住宅用地 estate（objId 2000..3999，52B；`[HELP 21]`）

| 操作 | 公式 | 依据 |
|------|------|------|
| 购地（无主） | `(estate.+28 + level × +30) × M`；`level` 为已有建筑数 | 0x41A04A |
| 升级（自己） | `+30 × M`；条件 `level<5 && type==0 && state37==0` | 0x41993D |
| 收租（他人） | `estateRouteRent(owner, +4 路段名)` = Σ 同路段普通住宅用地 `fees[level]`（"同路段的產業租金將聯合計算"）；`×(flag(+23)?2:1)` | 0x419744/0x419B09 |
| 連鎖店收租 | `estateRouteRent(owner, 0)` = 店面總數 × 2000 × M（改建卡產生，"不與同路段聯合、以店面總數計算"） | 0x4197A5 |
| 付款/收款 | `sub_41D2C6(cur, owner−1, rent, 0)`：付款方现金优先，**收款入银行**；`estate.+44 = rent` | 0x419F7D 段 |
| 欠款记账 | `addPlayerDebt(cur, owner−1, rent/100)`（矩阵显示用） | 0x419DF3 |
| case 3 过路 | 步行落在他人有建筑地产 → `addPlayerDebt(owner−1, cur, 30×M)` 仅记账 | 0x40DB19 |

`fees[6]` = estate+32+2×level（等级 0..5 过路费表），数值来自 MAPDAT（§9）。
完整收租流程（免收检查/神明/同盟/卡片/死神）见 `41982d-estate-rent.md` §1/§2。

## 4. 商業用地 corp（objId 4000..5999，56B；`[HELP 22]`）

字段：`+24 type(0..4 設施類別)`、`+25 owner+1`、`+26 sub=設施等級`、`+28 flag(查封→收费翻倍)`、
`+34 buildPrice`、`+36..+46 feeTable[6]`（等级 0..5）、`+48 最近收费`、`+52 到期日`。

| 操作 | 公式 | 依据 |
|------|------|------|
| 买地（无主） | `+34 × M` → owner = cur+1（现金不足 → "您的現金不足！"） | 0x41A890 |
| 建設施（自己，`type==0 && sub==0`） | 付 `+34 × M` → `selectFacilityDialog(0)` 弹「請選擇設施類別」(0x465289) 选 type；AI = `rand()%4+1`；`++sub` | 0x41A200..0x41A27C |
| 升级（自己） | `+36 × M`（= feeTable[0]）；上限 `g_facilityMaxLevel[0x474940] = {1,5,5,1,5}[type]` | 0x41A2C8/0x41A2D9 |
| 收费（他人，type 1..3 且 sub≥1） | 见下表；`ownerCanCollectRent(owner−1, +28, 费用名)` | 0x41A39C |
| 转账 | `sub_41D2C6(payer, owner−1, rent, 0)`（收款入银行）；`corp.+48 = rent` | 0x41A74F |

**按 type 的收费公式**（`base = feeTable[sub] × M`，`feeTable[sub]` = word[corp+36+2·sub]）：

| type | 设施 | 费用名 | 公式 | 依据 |
|------|------|--------|------|------|
| 1 | 旅館 | 住宿費 | `days = 轮盘(1)`；`rent = base × days × (flag?2:1)`；另 `addPlayerDebt(+20×M×days)`、`sub_44BA63(2000×M×days)`（住宿消费）、`stateFlags.BYTE0 = days−1（0→0x80 住宿中）`、`byte_496BAA += days`、`sub_40D5A5` | 0x41A43F/0x41A7A0 |
| 2 | 購物中心 | 購物費 | `mult = 轮盘(2)`；`rent = base × mult × (flag?2:1)`（"轉輪盤決定消費金額"） | 0x41A49B |
| 3 | 加油站 | 加油費 | 有載具：`rent = 500 × 載具倍率 × 本次步数 × M`；步行：0（"依照行走步數付出加油費"） | 0x41A4DB |
| 4 | 研究所 | — | 他人免付；**owner 停留** → 先走通用升级询问（0x41A2B3，同设施），升级/拒绝/满级后汇聚收尾 → 收尾内判定 type4 → `labDevelopDialog`「請選擇欲開發道具」(0x465298)：按等级逐级研发（一級機器工人→二級時光機→三級傳送機→四級工程車→五級核子飛彈），写 `corp+29 = 项目+1`、`corp+30 = 5`（研发剩余）；**首次建成研究所即弹**（建设施→收尾→判定） | 0x41B0B3..0x41B106/0x44101D |

> 研究所（type4）不收过路费，`[HELP 22]`："每升一級便可多開發一種道具……時間到研發完成，
> 該道具便會出現在業主的道具欄中"（P4 道具系统联动；帮助"不同項目所需研發時間不同"，
> 代码 `+30 = 5` 固定，细节待核）。
> 公園（type0）任何人免付；「除公園、加油站外可加蓋五個等級」↔ maxLevel {1,5,5,**1**,5}。

## 5. 行業設施點 specPt（objId 6000..7999，52B；`[HELP 20]`「公司企業」行业落点）

字段：`+24 owner+1`（0=无主）、`+26 index=行業`（§2.3）、`+34 price=单价`、`+32 sprite`。
无主 → 不收任何人费用（0x41AB77）；owner 自己停留的特殊效果见文末。

**他人（owner ≠ cur+1 且 owner ≠ 0）停留收费**（0x41AB7D..0x41AE34，`v59 = +26`）：

| index | 行业（帮助「公司企業」） | 费用名 | 公式 | 依据 |
|-------|--------------|--------|------|------|
| 1 | 航空公司 | 旅遊費 | `n = 轮盘(0)`（0..9999）；`n==0` → "不用出國！"(0x463A5F) 免付；否则 `rent = +34 × n × M`；成功付款后 `sub_40D375(payer, n, 0)`（**出國 n 天**，"出國期間不能收取過路費"） | 0x41ABDE/0x41B04D |
| 3 | 電腦公司 | 電腦費 | `rent = +34 × g_dayCount`（**不乘 M**，随游戏天数线性上涨；"付電腦使用費"） | 0x41AC30 |
| 4 | 保險公司 | 保險費 | `n = 轮盘(3)`；`rent = +34 × n × M`；`insuranceDays(0x496BA6, IDB 名 g_playerCardCnt 存疑，玩家+62) = (保险期 + n) & 0x7F`（"簽定保約，轉輪盤決定投保幾天"；`[HELP 4]`「保險期」= 有效剩餘天數 ✓）；**拥有者停留亦走此路**（0x41A9FE，轮盘、不加钱只延保期） | 0x41AC47/0x41AA01 |
| 5 / 6 | 汽車公司 / 石油公司 | 修車費 / 加油費 | 有載具：`rent = +34 × 載具倍率 × 本次步数 × M`；步行：0（"踩到加油站…沒開車就不用給錢"；帮助：汽車付**保養費**、石油付**加油費**） | 0x41AC93 |
| 11 | 房地產公司 | 工程費 | 人类 `sub_446AE8(0x2090086 住宅/商业+滚动)` / AI `sub_40B455`；**选中地块免费加盖 1 层**（angelUpgrade 0x41AD7E + FLC553 施工 → 封顶 FLC523）→ `rent = (estate+28 priceAdd 或 corp+34 buildPrice) × M`；取消/无可抽对象 → `1000 × M`。帮助"任選一處加蓋一層房屋，再依其**過路費**收施工費"：加盖 ✓、收费按**地价**（非過路費） | 0x41ACD8..0x41AE18 |
| 12 | （銀行类） | 過路費 | `rent = +34 × 本次步数 × M` | 0x41AE27 |
| 0/2/7..10/13..15 | 水力/電力 等 | 過路/加油/水費 | **v58 = 0 不收费**（预留：帮助「水力公司依土地總筆數付水費」「電力公司依房屋/連鎖店/建設級數總和付電費」在代码中**未接通**） | 0x41ABBD |

通用后处理（0x41AE39..0x41B06F）：`ownerCanCollectRent(owner−1, ...)` 免收检查 →
文本 `"%s\n\n此地屬%s\n\n請付%d元%s"`（0x4639B3，index==12 用 0x41AE8B 变体）→
`applyGodRentModifier` 神明调整 → 免費卡/嫁禍卡 → 死神代付 →
`sub_41D2C6(payer, objId−5900, rent, 0)`（**收款方为 5900+index 的"公库"而非玩家**——
即帮助"繳交費用，充作**累積盈餘**"；与 estate/corp 收进地主银行不同）→ `sub_41D1A9(cur, specPt)`（提示刷新）。

owner 自己停留（0x41A9C0 段，✅ 2026-09-26 实现）：index4 → 轮盘(3) 免费投保
（`insuranceDays = (v + n) & 0x7F`，0x41A9FE）；index11 → 弹选地块（同上 mode 0x2090086）→
**免费加盖 2 层**（0x41AAE8 + 0x41AAFC 两次 angelUpgrade，第 1 次封顶则一次；FLC553 施工 +
封顶 FLC523）；其余 index 无效果。他人收费路径同段加盖 1 层（0x41AD7E）。
> 帮助「銀行/百貨公司」行业条款（与公立功能相同、認兩仟股贈卡片/道具）属**股票系统**
> （`sub_436668` / `sub_4291D6`，见 §8），不在本表 index 收费分支内。

## 6. 减免 / 加成链（三类对象通用）

顺序（以 estate 0x419A67 段为模板，corp/specPt 同构）：

1. `ownerCanCollectRent` 0x41D559（**地主侧**免收）：查封(flag 高/低半字节，`[HELP 68]` 查封卡)/同盟(`[HELP 59]`)/死神/住宿/消失/坐牢/住院/冬眠/夢遊 → 免收并 showMessage。
2. 基础值（§3/§4/§5 公式）× `flag(+23/+28) ? 2 : 1`（estate/corp；`[HELP 83]` 漲價卡红光加倍/查封蓝光同字段）。
3. `applyGodRentModifier` 0x41D709（**付款人神明**，`[HELP 42/44/46/49]`）：小財神 抵赖一半（`/2`）、大財神 抵赖全部（`→0`）、小窮神 `+50%`、大窮神 加倍（`×2`）。
4. 免費卡（卡片 20，`[HELP 60]`："租金/罚金/缴税超 2000 元（×物价指数）时抵用一次"）：`(rent ≥ 2000×M || rent > cash+bank) && sub_4413AD(cur,20) && sub_444A60(...)` → rent=0。
5. 嫁禍卡（卡片 19，`[HELP 80]`："转嫁…或让人代付罚金"；旧文档称"分期/变卖"）：同条件 → `sub_44476A(cur,1,rent)` 换付款人。
6. 死神/惡魔代付：`findDeathGodCarrier` 0x40FBB8（携带槽 14/15 的玩家代付，"死神顯靈\n\n由%s賠償%s" 0x4639CC）。
   **✅ 2026-09-25 已实现** = `resolveFeePayer`（三收费管线 transferMoney 前；idx14=寶箱 系原版条件照抄，
   见 map-object-refresh.md §13.4）。
7. 转账 `sub_41D2C6(flags=0)`：付款方现金优先不足扣银行，仍不足 → `sub_40CD87` 破产淘汰；收款方**入银行存款**。

## 7. 土地/设施到期（`sub_40AC7B`，0x40AC7B）

购地/建设时按设置 `g_cfgLandPerm` 写到期日（estate+48 / corp+52；期限表
`g_landPermDays = dword_4751F0 = {0,0,2,0}`，`addPackedDate`）。`sub_40AC7B(list, flags, total, a4)`
按"到期格列表 `word_48B8C4`"（`sub_40A45C(now)` 筛选）处理：

| 位 | 对象 | 效果 |
|----|------|------|
| `flags&2` + `a3!=0` | estate 彻底到期 | 地主欠 `a4`：`addPlayerDebt(owner−1, a4, 30×M×level)`；清 owner/level/type/到期日（**归公**） |
| `flags&2` + `a3==0` | 部分到期 | `level−1`；若为连锁店（type≠0）→ level=0、type=0；欠 `30×M` |
| `flags&4` | corp | 同上（等级字段 = sub；到期清零后 `sub_40DFFA` 重算） |
| `flags&0x20` | 事件槽 NPC | `sub_40CD07`（囚犯释放）/ `hospitalizePlayer(k,0)` / `sub_40E14D` 释放 cellTable 槽 |

## 8. 事件格 / 金融金额（摘要，详见对应文档）

| 项 | 金额 | 依据 |
|----|------|------|
| 入狱 / 住院罚款 | `2000 × M × days`（`[HELP 37]` 写 1000/天，**以代码为准**；调用点 `sub_44BA63` @0x43D749 / @0x43EDF8） | `jailPlayer` 0x43D593 / `hospitalizePlayer` 0x43EC3F |
| 保释 / 办理出院 | 點券 `dword_475C44/475CA4 = {30,30,30,30,300,300,300,300}` | `41982d-p2-events.md` §1.2 `[HELP 34/37]` |
| 得點券格（得十點/得三十點/得五十點） | +50/+30/+10 點券 | case 10/11/12 `[HELP 29..31]` |
| 樂透 | 現金 1000 元/注，每月 15 号开奖，奖金累积 | ✅ 2026-09-25 `lottery_dialog.cpp`（`lottery-system.md`）；`[HELP 36/2]` |
| 銀行貸款 | 最高 100 万、借期 3 个月、免息；**到期全數歸還；借貸期間停發存款利息** | `sub_436668` `[HELP 35/4]`（待深入） |
| 公司企業（股票） | 12 支；每次認購 ≤ 2000 張；持股最多者=董事長免付費；每月 15 號按持股發盈虧分攤 | `sub_4291D6` `[HELP 20]`（待深入） |
| 同格破产金（乞丐施舍） | `1000 × M` | `onPlayerActionPhase` 0x41B42D `[HELP 39]`（P3） |
| 商店（百貨公司格） | 點券計價买卖卡片/道具（常量 6000/8000 区间） | `sub_42E931` `[HELP 27]`（P4） |

## 9. MAPDAT 实测数值表（mode 0 / map 0，`map.mkf[1]`）

### 9.1 estate（同路段全部同值；`level` 买地时已含建筑）

| 路段 | priceBase(+28) | priceAdd(+30) | fees(+32..+43) L0..L5 |
|------|-----------------|----------------|------------------------|
| 台北市 | 2500 | 500 | 500 / 1200 / 3000 / 7500 / 16000 / 30000 |
| 桃園市·台中市·高雄市 | 2000 | 400 | 400 / 1000 / 2400 / 6000 / 12500 / 22000 |
| 新竹市·彰化市·花蓮市·太魯閣 | 1000 | 200 | 200 / 500 / 1200 / 2800 / 6000 / 10000 |
| 台南市·宜蘭市 | 1500 | 300 | 300 / 750 / 2000 / 4800 / 10000 / 18000 |
| 屏東市·花蓮縣 | 800 | 150 | 150 / 400 / 1000 / 2400 / 5000 / 9000 |
| 台東縣 | 500 | 100 | 100 / 250 / 700 / 1800 / 3500 / 6600 |

### 9.2 corp（商業用地）

| 地块 | buildPrice(+34) | feeTable(+36..+46) |
|------|-----------------|---------------------|
| 花蓮市 / 知本 / 基隆市 | 4000 | 400 / 400 / 1000 / 2500 / 5000 / 8000 |
| 墾丁 | 5000 | 500 / 500 / 1200 / 3000 / 6000 / 10000 |

### 9.3 specPt

| 名称 | index(+26) | 费用名 | price(+34) | sprite |
|------|-----------|--------|------------|--------|
| 臺灣人壽 | 4 | 保險費 | 750 | 134 |
| 大宇百貨 | 10 | 過路費 | 0 | 139 |
| 中國信託 | 7 | 過路費 | 0 | 142 |

> 其余 7 张地图 / 两种模式的表值同法提取（`tools/mkf.py extract map.mkf --index 2*(4*mode+map)+1`
> + `docs/formats/mapdat.md` 布局）；重写加载时直接读 MAPDAT，不硬编码。

## 10. 重写现状与待办

| 公式 | 重写状态 |
|------|---------|
| 购地/升级/联合租金/连锁/flag 翻倍/神明/转账 | ✅ `turn_system.cpp`（见 `41982d-estate-rent.md`） |
| 商業用地（corp）购买/建设/升级/收费（type1..3） | ✅ 本文 §4（landingEvent corp 分支 0x41A86B/0x41A1E0/0x41A2B3/0x41A370；`selectFacilityDialog` UI 完整还原） |
| 旅館住宿状态（BYTE0 天数） | ✅ 收费后设置（0x41A761）；**住宿欠款 20×M×days ✅ 2026-09-27**（`addPlayerDebt(cur, 地主, …)` 0x41A7BC）；保险理赔已接（`insurancePayout` 0x44BA63） |
| 研究所（corp type4）研发 `labDevelopDialog` | ✅ 本文 §4 + `44101d-lab-develop.md`（`lab_dialog.cpp` 0x44101D 逐级解锁选择框；`updatePlayerStates` 0x41C84F 尾倒计时 → `givePlayerItem(researchItem+8)`，5 非卖品唯一入口）|
| 行業設施點（specPt）收费 | ✅ 本文 §5（`landingEvent` 6000..7999：航空/保險轮盘、電腦 `dayCount`、汽車/石油载具×步数、房地產按地价、index12；公库 `fund` 收款）；**出國 n 天状态 ✅ 2026-09-27**（航空付款后 `fateStartTravelState` 0x40D375，调用点 0x41B05A；条件 costType==1 && 天数≠0 && 付款人存活 && !sceneRequest） |
| 監獄/醫院罚款（保险理赔）+ 保险期 | ✅ 入狱/住院 `jailPlayer`/`hospitalizePlayer`（`turn_system.cpp`）+ `insurancePayout` 0x44BA63；保险期（+62 `g_playerCardCnt`，非卡片数）递减 0x41CC4B |
| `g_moneyMul` 更新 | ✅ 本文 §1.1（`updateMoneyIndex` 0x423ACF 每日接入 `advanceDay`；已消除"恒 1"） |
| `playerTotalAssets` | ✅ `economy.cpp`（股票市值 + 地产 + 公司；贷款/存款已计入） |
| 轮盘/老虎机读数（3/4 位） | ✅ **完整转盘 UI**（`roulette_dialog.cpp` `roulettePrompt`）：panel.mkf[68+theme] 14 帧 + 指针逐格步进（40ms/帧）+ 减速停止（间隔 1..5）+ 停留 40 帧；读数 = `byte_475D0C` 终格值。神明轮盘（`slotMachineValue` 4 滚轮）❌ 未接入 |
| 到期处理 `sub_40AC7B`（`expireAssets`，屏幕可见地块批量破坏） | ✅ `economy.cpp`（飛彈/核彈 `item_effects.cpp` 接入：归公/降级/事件槽释放）；地块期限到期回收在 `advanceDay` 内联（owner=0 + `rebuildMiniMap`）——旧标"到期清算"有误 |
| 事件格金额 | 见 `41982d-p2-events.md` |
