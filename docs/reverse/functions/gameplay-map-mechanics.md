# 游戏内地图场景机制（M2 推进总览与验收检查表）

> 本文档汇总游戏内所有格子/场景机制的原版依据（IDA 地址）、重写进度与实机验收步骤。
> 每完成一项：更新状态、补充 IDB 注释、跑 `python tools/re_map.py`。
> 状态：❌ 未实现 / 🚧 进行中 / ✅ 已完成（已实机验收）
>
> **机制描述依据**：`help.mkf` 帮助文档（idx 1..99，8 分类，见 `src/app/help_dialog.cpp`
> `kCategories`；文本解码见 `docs/formats/text.md`）。本文用 `[HELP idx NN]` 标注引用条目；
> 格子类型对应由 **MAPDAT 实测**（`map.mkf[2*(4*mode+map)+1]` 的 cellEnt +4 名称 / +36 低字节）
> 与帮助页标题逐项核对确定。

## 1. 数据模型

### 1.1 cellEnt（道路节点/普通格，`g_cellEnts` 0x498E80，40B/项）

| 偏移 | 含义 | 重写字段 |
|------|------|---------|
| +0/+2 | 像素坐标 x/y | `CellEnt.x/y` |
| +4..+23 | 名称（20B，BIG5，`\0` 结尾）：普通格 = 城市/县名（台北市/墾丁/知本）、特殊地点格 = 地点名（公園/命運/…） | `CellEnt.pad4`（已用于右键提示，建议改名 `name[20]`） |
| +24..+30 | exits[4] 相邻格 | `CellEnt.exits` |
| +32 | **objId**（2000+ 住宅用地 / 4000+ 商業用地 / 6000+ 行業設施點 / 8000+ 事件格） | `CellEnt.special`（建议改名 objId） |
| +34 | 图块索引（`dword_474949` = map.mkf[24] 帧 `sprite-1`）：普通格 0（城市名，无图块）/ 33（道路）；特殊地点格 = 图块顺序 `2k-1`（1..31 奇数，见 §3） | `CellEnt.sprite` |
| +36 | 位段：**bit0-7 格子类型（0..16）**、bit8-11 占用玩家掩码、bit16-23 cellTable 槽号（`(slot+1)<<16`）；bit31 = 运行时标志（MAPDAT 初始值 0/1，+39 常为 -128 与之联动；重写用于出生点/道路判定） | `CellEnt.occMask`（低字节类型位已由 `landingEvent` 读取） |
| +38 | 标志（清零语义） | — |
| +39 | 状态（MAPDAT 初值：普通 0；監獄 8、醫院 16；带 bit31 的格子 -128） | — |

### 1.2 cellTable（物件格，`g_cellTable` 0x496D08，24B/项，46 项）

| 偏移 | 含义 |
|------|------|
| +0 | 类型（`kCellTypeInit[46]` = 1..14,15,15,16×10,17×10,18×10） |
| +1 | 朝向 |
| +2 | cellEnt u16 |
| +4 | 剩余回合（`byte_496D0C`） |
| +5 | 占用玩家+1（`byte_496D0D`） |

类型名 `kObjectNames[19]`（`off_47ED76` 0x47ED76，**索引 = 类型值**，0-based）：
0=間諜（初值未用）、1=小財神、2=大財神、3=小福神、4=大福神、5=小窮神、6=大窮神、7=小衰神、8=大衰神、
9=天使、10=惡魔、11=惡犬、12=土地公、13=禮物、14=寶箱、15=死神（2 槽）、16=路障（10 槽）、
17=地雷（10 槽）、18=定時炸彈（10 槽）。
依据：`sub_40EAD7` `switch(cellTable[24*slot])` case 1..15 用 `off_47ED76[类型]` 取名，且行为与帮助吻合
（case 3 送 1 卡=小福神、case 4 送 2 卡=大福神、case 5/6 送钱=小/大窮神、case 7/8 丢卡=小/大衰神）；
图块 `data.mkf[类型+395]` 实测（407=土地公、408=禮物、410=死神、411=路障）。
类型 16/17/18 由玩家道具放置（`sub_446BAA`/`sub_446C88`/`sub_446D69` → `createMapObject(16/17/18)`）；
`byte_497321/497322/497323` = 类型 16/17/18 的剩余计数。
`[HELP 39..56]` 为这些神明/人物的行为描述（土地公占地、大小財神/福神/窮神/衰神/天使/惡魔效果与 7 天轮替、
死神 13 天、四大惡人保释（**✅ 事件槽 NPC 全链 2026-09-26，`498df0-event-slot-npc.md`**）、
惡犬咬伤住院三天等）—— P3/P4 神明效果实现依据。

### 1.3 objId 段（cellEnt+32）

| 段 | 表 | 项大小 | 关键字段 |
|----|----|--------|---------|
| 2000..3999 | `g_estates` | 52B | **住宅用地**（帮助 `[HELP 21]`）：+4 路段名（BIG5）、+23 旗帜（非 0 时收租翻倍；高/低半字节均非 0 = 房屋查封中）、+24 类型（0=普通建筑/≠0 連鎖店）、+25 拥有者+1、+26 等级、+27 朝向、+28 购地价、+30 升级价、+32..+43 fees[6]（按等级）、+44 最近租金（收租时写入）、+48 到期日 |
| 4000..5999 | `g_corps` | 56B | **商業用地及其上設施**（帮助 `[HELP 22]`；**非 `[HELP 20]`「公司企業」**——股票分红是另一套 12 支股票，见 `pricing-formulas.md` 顶部）：+4 地块名（城市名：花蓮市/知本/墾丁/基隆市）、+24 设施类型（0..4 = 公園/旅館/購物中心/加油站/研究所，名表 `off_475150`；帮助列表段作「商場」—— 实为住宅等级外观名 `kBuildingNames[3]`）、+25 拥有者+1、+26 sub=设施等级、+27 朝向、+28 旗帜（查封→收费翻倍）、+34 建地价、+36..+46 feeTable[6]（升级费=表[0]，收费=表[sub]）、+48 最近收费、+52 到期日 |
| 6000..7999 | `g_specPts` | 52B | **行業設施點**（帮助 `[HELP 20]`「公司企業」11 行业收费落点）：+4 地标名（臺灣人壽/大宇百貨/中國信託）、+24 拥有者+1（0=无主不收费）、+25 价格表索引、**+26 index=行业**（费用名映射 `byte_47528E`）、+27 朝向、+32 sprite（134+）、**+34 单价（收费基数）**、+40/+44 资金、+48 价格 |
| 8000+ | `g_evtCells` | 28B | +4 地标名（醫院/綠島/野柳/…）、+24 朝向、+26 sprite |

实测（`map.mkf[1]`，mode 0 / map 0）：estate 50 项（全部 type=0）、corp 4 项（花蓮市/知本/墾丁/基隆市，
type/sub 初值全 0 = 尚未建设施）、specPt 3 项（臺灣人壽 index=4/保、大宇百貨 index=10、中國信託 index=7）、
evtCell 21 项。特殊地点格（type 1..16）的 objId 均为 0；`special=8001/8002` 为出生点标记。
各表价格/费用数值见 `pricing-formulas.md` §9。

## 2. 结算时机（三个入口）

| 时机 | 原版函数 | 重写 | 状态 |
|------|---------|------|------|
| 移动中每格 | `onPlayerActionPhase` 0x41B42D | `onPlayerActionPhase` ✅（P3：物件分派/乞丐/挂身炸弹/银行路过） | ✅ |
| 移动结束（面向目标 + 30×物价记账 + FLC 526） | `playerActionStateMachine` case 3（0x40DA4B），由 `sub_448A7E` 触发 | case 3 修正（仅日志/面向，不再转账） | ✅ |
| 停留格结算（购地/升级/收租/事件） | `landingEvent`（`sub_41982D`，case 0..16） | 购地 ✅ / 升级 ✅ / 他人住宅用地收租 ✅ / **商業用地 corp 收费 ✅**（旅館轮盘+住宿/購物轮盘/加油站步数） / **行業設施點 specPt 收费 ✅**（6 公式+公库） / 事件格：得點券 10/11/12 ✅、卡片 13 ✅、監獄/醫院 4/5 ✅、銀行 14 ✅、新聞 2 ✅（36 事件；拍卖 idx 7 桩）、小游戏 6/7/8 ✅、樂透 9 ✅、百貨 15 ✅；命運 3 ✅（2026-09-26，`fate_event.cpp`；49 效果+卡片联动）/ 魔法屋 16 ❌ | 🚧 |

> **收租在 landingEvent，不在 case 3**：case 3（`sub_448A7E` 触发，步行落在他人有建筑的住宅用地/商業用地）
> 仅 `addPlayerDebt(owner-1, cur, 30*g_moneyMul)` **记账**（欠款矩阵 `dword_496BB4`，不扣现金）+
> 面向目标 + FLC 526；实际扣款/转账在停留结算 `landingEvent` 他人住宅用地分支（0x419A67，见 §5 P1）。

**调用链**：`sub_418C55`（回合开始）→ `sub_418E7F`（calcPlayerWait）→ `landingEvent = sub_41982D(当前格 cellEntId)`
→ 返回值写入 `byte_498EA5`（正 = 等待帧数，0x80 负值 = 直接下一位）。

「路过」与「停留」是两套处理：路过走 `onPlayerActionPhase`（每格触发，如銀行可提現金/存款、
卡片格经过即得卡），停留格才走 `sub_41982D` 结算
（如監獄/醫院门外保释·辦理出院、銀行停留可貸款、百貨公司进入商店画面）`[HELP 25/27/34/35/37]`。

## 3. 格子类型表（`landingEvent` switch 输入 = cellEnt+36 低字节）

> 依据：跳转表 `0x4197E9`（17 项）+ `0x41B11E..0x41B3CB` 各 case 反汇编；
> 名称/图块由 MAPDAT 实测（`map.mkf[1]`：`name=` 类型名、`sprite=` 图块索引、`mask & 0xFF` = type）。
> 音效：`audioPlayEffect(&g_effectSlots[g_cellTypeSound[type] << 3])`
> （`g_cellTypeSound` 0x475299 = `{9,0,10,10,10,10,10,10,10,10,16,16,16,16,10,10,10}`）

| type | 名称 | 图块 sprite | 帮助页 | 音效槽 | case 处理 | 状态 |
|------|------|------------|--------|--------|-----------|------|
| 0 | 普通格（城市名/住宅用地/商業用地/行業設施點/事件格，按 objId 分派） | 0 / 33 | — | 9 | case 0（0x4198B9） | 🚧 P0 |
| 1 | 公園 | 1 | 24 | 0 | case 1 = default：无事件（返回 0x80） | ❌ |
| 2 | 新聞 | 3 | 33 | 10 | `newsEvent`（`news_dialog.cpp` 0x44B6DF：panel[66]+插画+36 事件表；拍卖 idx 7 桩） | ✅ 2026-09-26 |
| 3 | 命運 | 5 | 28 | 10 | `fateEvent`（命运事件；49 效果 + 卡片联动） | ✅ |
| 4 | 監獄 | 7 | 34 | 10 | `jailBailDialog`（保释面板 + AI；开局默认在押 小偷/強盜 0x40734F） | ✅ |
| 5 | 醫院 | 9 | 37 | 10 | `hospitalVisitDialog`（出院面板+护士动画；开局默认在院 流氓/間諜） | ✅ |
| 6 | 企鵝挖寶 | 11 | 26 | 10 | `miniGameDigRun`（`minigame_dialog.cpp` 0x415215）15s 企鹅挖宝（5 类宝物/火煤球即终/8 方向行走）→ `g_playerPoints += ax` | ✅ 2026-09-25 |
| 7 | 七彩氣球 | 15 | 23 | 10 | `miniGameBalloonsRun`（原版 `sub_4154DC`）→ `g_playerPoints += ax` | ✅ 交互版 2026-09-25 |
| 8 | 喜從天降 | 13 | 32 | 10 | `miniGameMoneyRun`（原版 `sub_4155FC`）18s 鼠标控娃娃接钱袋（财神撒袋/云投炸弹/评级表情）→ `g_playerPoints += ax` | ✅ 2026-09-25 |
| 9 | 樂透 | 17 | 36 | 10 | `lotteryVisit`（投注/开奖） | ✅ |
| 10 | 得５０點 | 19 | 31 | 16 | 0x41B184：FLC 537 + `"得點券５０點"`（0x463A81）+50 点 + 角色语音（P4） | ✅ |
| 11 | 得３０點 | 21 | 30 | 16 | 0x41B21E：FLC 537 + `"得點券３０點"`（0x463A8E）+30 点 + 角色语音（P4） | ✅ |
| 12 | 得１０點 | 23 | 29 | 16 | 0x41B2A3：FLC 537 + `"得點券１０點"`（0x463A9B）+10 点（原版无语音） | ✅ |
| 13 | 卡片 | 25 | 25 | 16 | 0x41B302：FLC 536 + `sub_441E12` 赠卡池抽卡（卡包 15 槽）+ `"得到%s！"`（0x463AA8） | ✅ |
| 14 | 銀行 | 27 | 35 | 10 | 0x41B396：`sub_4379C9` → `g_sceneRequest==0` 时 `sub_436668(cellEntId)` | ✅（`bank-system.md`） |
| 15 | 百貨公司 | 29 | 27 | 10 | 0x41B3B9：`sub_42E931(objId)`（卡片/道具店；实参 = cellEnt+32 special 6000+n） | ✅ |
| 16 | 魔法屋 | 31 | 38 | 10 | 0x41B3CB：`magicHouseVisit` 0x43380A | ✅ 2026-09-26 |

**三套顺序互不相同，重写时勿混用**：

1. **type 顺序**（cellEnt+36 低字节，switch 分派）：
   公園/新聞/命運/監獄/醫院/企鵝挖寶/七彩氣球/喜從天降/樂透/得５０/得３０/得１０/卡片/銀行/百貨/魔法屋
2. **图块顺序**（cellEnt+34 `sprite` = `2k-1`）：公園/新聞/命運/監獄/醫院/企鵝挖寶/**喜從天降/七彩氣球**/樂透/…/魔法屋
   —— 仅 type 7/8 互换（七彩氣球 sprite=15、喜從天降 sprite=13）
3. **帮助页顺序**（help.mkf 23..38，「特殊地點」目录显示序）：七彩氣球/公園/卡片/企鵝挖寶/百貨公司/命運/得十點/得三十點/得五十點/喜從天降/新聞/監獄/銀行/樂透/醫院/魔法屋

### 3.1 特殊地点机制（帮助文档）

| type | 名称 | 机制（`[HELP idx]` 原文归纳） |
|------|------|------------------------------|
| 1 | 公園 | 安全地带，不向使用者收费，也不能升级；别人地产环绕时的落脚点 `[HELP 24]`；也是商業用地可建设施之一（建费低廉、不收费、不能升级）`[HELP 22]` |
| 2 | 新聞 | 新闻事件有欢乐也有悲伤，内定的新闻事件随时发生 `[HELP 33]` |
| 3 | 命運 | 随机事件（可能因违法事件坐牢，也不全然是坏事，获利与否靠运气）`[HELP 28]` |
| 4 | 監獄 | 坐牢失去自由并丧失收租权利；停留監獄门外可将犯人保释出来；若有坏人经过会被抓回監獄 `[HELP 34]` |
| 5 | 醫院 | 住院不能出门并丧失收租权利，入院付每天 1000 元医药费；停留醫院门外可替病人办理出院 `[HELP 37]` |
| 6 | 企鵝挖寶 | 走到即进行挖宝小游戏 `[HELP 26]` |
| 7 | 七彩氣球 | 走到即进行射气球小游戏 `[HELP 23]` |
| 8 | 喜從天降 | 走到即进行接钱小游戏 `[HELP 32]` |
| 9 | 樂透 | 选号码以现金 1000 元投注，每月开奖；无人中奖则奖金累积至下月，运气/事件等意外损失的钱也集中为奖金 `[HELP 36]` |
| 10/11/12 | 得５０/３０/１０點 | 踩到即可獲得對應點券 `[HELP 31/30/29]` |
| 13 | 卡片 | 经过该标记即可免费获得一张卡片 `[HELP 25]`（玩家卡片上限 10 张，额满须舍弃 `[HELP 10]`） |
| 14 | 銀行 | 路过可提现金/存款；停留当格可贷款（最高 100 万，借期 3 个月，免息但借贷期间不发存款利息，可提前偿还）；每逢星期例假日停止营业 `[HELP 35]` |
| 15 | 百貨公司 | 购买卡片/道具的地方，必须以點券兌換；踩到先进入卡片店画面（可买/卖卡片），选择往道具店则买卖道具 `[HELP 27]` |
| 16 | 魔法屋 | 进入后女巫的水晶球会依出现的条件施法，再由玩家决定那些人的惩罚 `[HELP 38]` |

### 3.2 普通格（type 0，按 objId 分派）

| objId 段 | 对象 | 机制（帮助文档） |
|----------|------|------------------|
| 2000..3999 | 住宅用地 estate | 走到空地依公定價格購買（`[HELP 21]`"即為許可建地"）；可加蓋 5 個等級，等級越高租金越多；**同路段產業租金聯合計算**；連鎖店 = `estate+24≠0`（改建卡產生，不可再加蓋，收費 = 店面總數 × 2000 × `g_moneyMul`，與地段無關）`[HELP 21]`。玩家面板统计口径：土地=拥有土地总笔数、連鎖店=连锁店总店数、設施=商业区设施总数 `[HELP 3]`。**收租公式（已实现）**：普通住宅用地 = 同路段（同名）该地主全部 estate 的 `fees[level]` 之和 × `g_moneyMul`（`estateRouteRent` 0x419744）；`estate.flag(+23)` 非 0 再翻倍。数值表与全公式 → `pricing-formulas.md` §3/§9 |
| 4000..5999 | 商業用地 corp | **非 `[HELP 20]`「公司企業」**（持股分红是另一套 12 支股票 `g_playerShares`，见 `pricing-formulas.md` 顶部）。流程：買空地（`+34 × g_moneyMul`）→ 自己踩到「請選擇設施類別」（`selectFacilityDialog`，`off_475150`：公園/旅館/購物中心/加油站/研究所）→ 升級（`+36 × g_moneyMul`，上限 `{1,5,5,1,5}[type]` = 公園/加油站不可加盖 `[HELP 22]`）。他人踩到按 type 收費：旅館 = `feeTable[sub] × 轮盘天数`（住宿費）、購物中心 = `feeTable[sub] × 轮盘倍数`（購物費，"轉輪盤決定消費金額"）、加油站 = `500 × 載具倍率 × 本次步数`（"沒開車就不用給錢"）、公園/研究所不收費；研究所=業主停留逐级研发道具（`labDevelopDialog`，機器工人→時光機→傳送機→工程車→核子飛彈）→ `pricing-formulas.md` §4。**重写已实现**：購地 0x41A86B / 建設施 0x41A1E0（`selectFacilityDialog` 完整还原）/ 升級 0x41A2B3 / 他人收费 0x41A370（旅館/購物中心/加油站 + 神明/转账/`corp+48`）/ 旅館住宿状态 0x41A761；研究所研发 P4 |
| 6000..7999 | 行業設施點 specPt | 地标（臺灣人壽/大宇百貨/中國信託…，`+26 index` = 行业）；`[HELP 20]`「公司企業」的 11 种行业收費實際落點：航空公司=旅遊費（`+34 × 轮盘4位`，0 → "不用出國！"，付款后出國 n 天）、電腦公司=電腦費（`+34 × g_dayCount`）、保險公司=保險費（`+34 × 轮盘`，並記保險期）、汽車/石油公司=修車/加油費（載具倍率 × 本次步数，步行免付）、房地產公司=工程費（抽一塊地按其地價）、其他多不收費；**收款方為「公庫」（objId−5900）而非地主**（帮助"繳交費用，充作累積盈餘"）→ `pricing-formulas.md` §5。提示显示地主+名称+价格（0x417559） |
| 8000+ | 事件格 evtCell | 地图地标（醫院/綠島/野柳/…），仅显示名称与图块（0x417559） |

**「11 种行业」归属** `[HELP 20]`「公司企業」（原文逐行业列消费方式；实现在 specPt index 与 corp type 上，
公式 → `pricing-formulas.md` §4/§5；**帮助与代码不一致处以代码为准并在此标注**）：
航空公司=specPt1（"轉輪盤決定出國旅遊天數，並付費用，出國期間不能收取過路費"）、
飯店=corp 旅館（"轉輪盤決定休息天數，並決定消費金額"；`[HELP 22]` 商業用地旅館同机制）、
電腦公司=specPt3（"付電腦使用費" = 单价 × 累计天数）、
保險公司=specPt4（"簽定保約，轉輪盤決定投保幾天，並付出保費"；保單剩餘天數即 `[HELP 4]`「保險期」）、
汽車公司=specPt5（"踩到加油站…付出**保養費**，沒開車就不用給錢"；费用名表作「修車費」）、
石油公司=specPt6（"付出**加油費**，沒開車就不用給錢"）、
銀行=specPt 中國信託（index7，**收费未接通**；"功能與公立銀行完全相同"，公立銀行=特殊地點「銀行」格 case 14；
"董事長特別融資/週轉期停息/超限坐牢十天"属股票系统待深入）、
水力公司=水費（"依擁有土地的總筆數付水費"——映射存在（index13..15）但**收费分支未接通**）、
電力公司=電費（"依所有房屋、連鎖店、建設的級數總和付電費"——费用名**未被任何映射引用**，未接通）、
百貨公司=specPt 大宇百貨（index10，**收费未接通**；"功能與公立百貨公司完全相同，每認兩仟股贈送卡片或道具"
——贈卡属股票认购系统，"公立百貨"=特殊地點「百貨公司」格 case 15 的點券商店）、
房地產公司=specPt11（⚠️ 帮助："任選一處加蓋一層房屋，再**依其過路費**收取施工費用"；
代码：抽一块地**按其地价**收费、**不**加盖——以代码为准）。
公司持股分红/董事长/盈余发放对应的是 **12 支股票**（`g_playerShares`/现价 `flt_496994`，
每月 15 号分红，`sub_4291D6` 待深入），与 corp 表无关。

**拍卖**：有人倒闭、有人使用售地卡、事件发生、银行拍卖土地时举办，手头有现金即可参加，价高者得 `[HELP 20]`；
也可趁拍卖买地或透过【交易】指令买卖房屋 `[HELP 21]`。物价指数（`g_moneyMul`）以全体总财产平均值计算，
平均值加倍时上升，过路费/事件金额/地价随之倍数上涨，月底结算公布 `[HELP 5]`
——**公式**：`g_moneyMul = max(旧值, Σ存活玩家总资产 / 存活数 / 初始资金)`（`updateMoneyIndex` 0x423ACF，只升不降；
**每日** `advanceDay` 0x41CFBF 调用，已接入 `economy.cpp`）；
总资产口径 `playerTotalAssets` 0x4239B9 = 现金+存款−贷款+持股×现价+地产/设施估值 → `pricing-formulas.md` §1.1。

## 4. 通用辅助函数

| 原版 | 语义 | 依据 | 重写 |
|------|------|------|------|
| `sub_440BA8` | **文本 + YES/NO 询问框**：setTextFont(16, 0xF0F0F0, 0x101010, 3, 1) → 框 `g_tipFrame+72` 于 (220,140) → drawText → blit → `sub_453A32(220,320)` YES/NO | 0x440BA8 | `confirmDialog(text,cx,cy)` ✅ |
| `sub_440CAC` | **showMessage**：显示文本 N 毫秒（`sub_4528B9` 延时；负数 = 左移 100px）；背景框 = `g_tipFrame+72`（帧 5）+ 文本居中 (220,140) | 0x440CAC | `showMessage`（message_dialog）✅ |
| `sub_453A32` | YES/NO 框（返回 1=YES/0=NO） | 0x453A32 | `confirmDialog` ✅ |
| `sub_452946` | 玩家名 → 文本缓冲（sprintf 用） | 0x452946 | ❌ |
| `sub_41D476` | UI 刷新（(0,0,1)） | 0x41D476 | 待确认 |
| `rebuildMiniMap` | 小地图重建（a1=0 小/1 大） | 0x40A4E1 | `buildMiniMapMarks` ✅ |
| `sub_40E14D` | 释放 cellTable 槽 | 0x40E14D | `releaseCellTableSlot` ✅ |
| `sub_40CD87` | 淘汰（破产） | 0x40CD87 | `eliminatePlayer` ✅ 简化 |
| `sub_44EF41` | 角色语音/表情 | 0x44EF41 | ❌ |
| `sub_40DF69` | **addPlayerDebt**：欠款矩阵 `dword_496BB4[26*a1+a2] += a3`（a1 应收 a2，clamp≥0），不直接扣现金；命中同盟（a1 的 +65 == a2+1）→ `sub_40CC1A` 解除同盟 | 0x40DF69 | ❌（收租已不走矩阵） |
| `sub_40FA61` | 购地/升级前检查（携带神明槽 7/8/15 阻止 → showMessage 神明名） | 0x40FA61 | ✅（提示已接入） |
| `sub_419744` | **estateRouteRent**：联合租金——同路段（同名）普通住宅用地 `fees[level]` 之和 / 連鎖店数量×2000，× `g_moneyMul` | 0x419744 | `estateRouteRent` ✅ |
| `sub_41D559` | **ownerCanCollectRent**：地主免收租检查（查封/與付款人同盟/死神顯靈/住宿/消失/坐牢/住院/冬眠/夢遊 → showMessage + 返回 0） | 0x41D559 | `ownerCanCollectRent` ✅ |
| `sub_41D709` | **applyGodRentModifier**：付款方神明调整（小財神减半/大財神免付/小窮神+50%/大窮神加倍） | 0x41D709 | `applyGodRentModifier` ✅ |
| `sub_448A7E` | case 3 触发：步行（travel&0x83==3）落在他人有建筑的住宅用地/商業用地 → state=3 | 0x448A7E | case 3 入口 ✅ |
| `roulettePrompt` | **转盘 UI**：帧 `(a1&3)+68` 弹轮盘（0..3 主题），指针逐格步进+减速停止，读数 = `byte_475D0C` 终格值；供航空/保險/旅館/購物 费用乘数 | 0x44090E | ✅（`roulette_dialog.cpp` 完整还原；神明 `slotMachineValue` 4 滚轮 0x43F23E ❌ 未接入） |
| `selectFacilityDialog` | 「請選擇設施類別」建设选择 UI（返回 corp type） | 0x440AAC | ✅（`facility_dialog.cpp`；0x41A22A 建设施接入，改建卡 flag=1 0x4431C4） |
| `labDevelopDialog` | 研究所（corp type4）「請選擇欲開發道具」：逐级研发槽选 1（可选数=`corp.sub`，未解锁灰度），写 +29/+30；触发在收尾 loc_41B077（先升级询问、建设施后即弹）；`updatePlayerStates`(0x41C84F 尾) 倒计时归零 `givePlayerItem(researchItem+8)` | 0x44101D | ✅（`lab_dialog.cpp`；`44101d-lab-develop.md`；2026-09-27 帧7/帧5/灰度修正）|
| `updateMoneyIndex` | **物价指数**：`g_moneyMul = max(M, Σ存活 playerTotalAssets / 存活数 / g_startMoneyVal)`（只升不降；**每日** advanceDay 0x41CFBF） | 0x423ACF | ✅（`economy.cpp`；玩家面板 idx 5 显示 ❌） |
| `playerTotalAssets` | 总资产 = 现金+存款−贷款+Σ持股×现价+Σestate(+28+30×级/连锁)+Σcorp(+34+36×级) | 0x4239B9 | ✅（`economy.cpp`） |
| `expireAssets` | **屏幕可见地块批量破坏**（旧标"到期清算"有误）：`word_48B8C4` = `sub_40A0B1` 收集的视口内有主地块列表（`sub_40A45C` 计数）；flags&2 estate / flags&4 corp：a3≠0 归公（地主欠 `30×M×level`）+ rebuildMiniMap，a3==0 降级（连锁→清 type+level）；flags&0x20 事件槽释放（damagePlayer/住院/deleteMapObject）。调用点 = 卡片/道具全屏破坏效果（0x446FBC/447ACE/44913D/44AB2C）→ P4 | 0x40AC7B | ✅（`economy.cpp`；飛彈/核彈/新聞灾害已接入）|
| **到期回收**（真） | estate`+48`/corp`+52` == 当前日期 → owner=0（归公）+ 查封高半字节递减，**内联在 `advanceDay`**（非 expireAssets）；重写 `turn_system.cpp:2477` ✅ | 0x41CF67 尾段 | ✅ |
| `queryDialog` | **查詢面板**（工具条 case 6，[HELP 16]）：資產/地產/股票清單 3 页签 + 地產 5 子页签/翻页 + 玩家条；右侧資訊面板 4 页签（資金/地產/股票/其他）+ 物价指数 + 点击切换 | 0x424492/0x423CF3/0x423070/0x4225A3/0x422443/0x415F69/0x4182FA | ✅ 2026-09-25（`query_dialog.cpp`/`game_panel.cpp`；ui-controls §27） |
| `collectOwnedAssetIds` | 地产列表筛选（0=estate+corp / 1=estate / 2=corp / 3=有建筑普通地产 / 4=有建筑连锁店） | 0x423B3B | ✅ |
| `rollDice` / `g_diceValue` | 本次行走总步数（掷骰后保留，供加油/修车/过路×步数公式） | 0x419572 / 0x40D9B7 | ✅（`GameState.diceValue` 0x48BAFC，掷骰定格记录） |
| `sub_41D2C6` | 转账（flags&4 付款方银行优先；flags&1 收款方入现金，否则入银行；不足 → `sub_40CD87` 破产） | 0x41D2C6 | `transferMoney` ✅（收租 flags=0） |
| `miniGameVisit` | 挖宝/射气球/接钱小游戏（原 `sub_415215/4DC/5FC`；返回得分，调用方 `g_playerPoints += ax`；人类+动画开 → 三游戏交互版；否则共享 else 0x415457 兜底 rand()%20+50） | 0x415215 等 | ✅ 2026-09-25（`minigame_dialog.cpp`；`41982d-p2-events.md` §6） |
| `sub_441E12` | 抽卡（返回卡片 id，0=无） | 0x441E12 | ❌ |
| `sub_441F73` | 给玩家卡片（`sub_441E12` 结果 + 文本缓冲） | 0x441F73 | ❌ |
| `sub_44F230` | 卡片/道具获得语音 | 0x44F230 | ❌ |
| `newsEvent` / `fateEvent` | 新聞 / 命運 事件全屏 UI（36/37 事件表 + 判定 + 效果） | 0x44B6DF / 0x44DB81 | newsEvent ✅ 2026-09-26（`news_dialog.cpp`）；fateEvent ✅ 2026-09-26（`fate_event.cpp`，49 效果 + 卡片联动；公共辅助 `event_common`） |
| `jailBailDialog` / `hospitalVisitDialog` | 監獄（保釋）/ 醫院（辦理出院）处理 | 0x43D304 / 0x43E9A4 | ✅（`jail_dialog.cpp`；NPC 格释放/事件槽行走/抓回 ✅ 2026-09-26） |
| `lotteryVisit` | 樂透（投注/开奖） | 0x4315CC | ✅ 2026-09-25 |
| `sub_4379C9` / `sub_436668` | 銀行 UI / 停留银行处理 | 0x4379C9 / 0x436668 | ❌ |
| `sub_42E931` | 百貨公司（卡片店/道具店） | 0x42E931 | ❌ |
| `magicHouseVisit` | 魔法屋 | 0x43380A | ✅ `magic_house_dialog.cpp`（2026-09-26，含拍卖 `auction_dialog.cpp` 0x43BDE5，`43380a-magic-house.md`/`43bde5-auction.md`） |

## 5. 推进清单（验收检查表）

### P0 买地（`sub_41982D` case 0 → `loc_41A013`）

原版流程（0x41A013..0x41A138）：
1. 守卫：`g_playerState37[cur] != 0` 或 `byte_496BA7[cur] == 12` → 跳过
2. 价格 = `(level * word[est+30] + word[est+28]) * g_moneyMul`；`g_playerCash < 价格` → 钱不够分支 0x41A159
3. 文本 `sprintf(chText, "%s\n\n", est+4)`（住宅用地/路段名）
4. AI（`alive&6`）→ `sub_41D7D4(价格)` 决策；人类（`alive==1`）→ `sub_440BA8(chText)` 询问
5. `sub_40FA61(cur)` 检查 → `est.owner = cur+1` → `sub_41D476(0,0,1)` → 音效 `dword_4823D2` → `rebuildMiniMap(0)`
6. `g_cfgLandPerm != 0` → `est[+48] = sub_4521CB(dword_497160, dword_4751F0[g_cfgLandPerm])`（期限表 `{0,0,2,0}`）
7. `g_playerCash -= 价格`

- [x] 数据：`Estate` 补 `+28 priceAdd` / `+30 priceBase` / `+32..+43 fees[6]` / `name[19]`（+4..+22 BIG5）
- [x] `confirmDialog` 支持文本与坐标（对齐 `askDialog`：文本居中 (cx,140) + 框 (cx,cy)）
- [x] `landingEvent` case 0 无主住宅用地购地（守卫/算价/现金检查/询问/神明阻止/owner/音效槽 17/小地图/期限/扣款）
- [x] 询问文本核对（0x4639E1，BIG5）：`"%s\n\n費用:%d元\n\n是否買下此地?"`（IDA get_string 在 \n 处截断，须用 get_bytes 核对）
- [x] 空地"信物"（0x40829D @0x4091DF）：**level==0 有主 → 棋子图 pieceTiles（0x48AEA8）+ 帧=角色 ID**、不调色板；
      level>0 → `dword_48AE48[level]` **住宅建筑外观**（type==0）/ `dword_48AE60`（estateFlag，type!=0），owner 调色板；无主不绘制
- [x] 住宅建筑外观资源（0x4091F9 `mov eax, ds:dword_48AE48[eax*4]`）：`dword_48AE48[level]`（level 1..5）
      与 `g_specTiles[level-1]` **同址**（0x48AE48 + 4*level = 0x48AE4C 起的数组），
      即地图预加载图块 `map.mkf[5*base+38+level]`（重写 `specPointTiles[level-1]`）；
      误用 estateFlag 会表现为"升级后变连锁店外观"（已修 `12ea114`）
- [x] 回合缓冲：case 0 有 objId 路径统一返回 **0x88**（loc_41B111，等 8 帧后下一位）；objId==0 → 0x80
- [x] 吐槽机制（0x44F627）：同路段（同名）住宅用地 ≥3 时 `sub_44EF41` 角色语音/表情（买地后必定**列16**、升级后 1/3 概率**列17**）✅ P4-B（2026-09-30 列勘误：曾买地/升级用反）
- [x] 现金不足提示（`byte_46398B` 实测 = **"您的現金不足！"**，非"現金不夠支付！"）+ 神明阻止文本（`kObjectNames[槽]`）
- 差异（待补）：AI 决策 `sub_41D7D4`；提示框背景（`g_tipFrame+72`）✅；落地后处理 `sub_40F381`/`sub_448A7E`；研究所道具開發 `labDevelopDialog`（0x44101D，非"连锁奖励"——旧称误）✅ 2026-09-26 `lab_dialog.cpp`

### P1 升级 + 收租（同链路）

- [x] case 0 自己住宅用地升級（0x419911..0x419A52：level<5、type==0、state37==0，费用 `word[est+30]*moneyMul`，
      AI 自动 / 人类 askDialog、神明阻止、音效槽 18）—— `a491b8a`
- [x] 住宅建筑外观（`dword_48AE48[level]` = `specPointTiles[level-1]`，实机确认）—— `12ea114`
- [x] 状态机 case 3 修正：`sub_448A7E` 触发（步行落在他人有建筑的住宅用地/商業用地）→ 面向目标 +
      30×`moneyMul` **仅记账**（`addPlayerDebt` 欠款矩阵，不扣现金）+ FLC 526 —— 早期"立即转账"已移除
- [x] **case 0 他人住宅用地收租**（`loc_419A67`）：
  1. 音效槽 20（`dword_4823EA`）
  2. `ownerCanCollectRent`（0x41D559）地主状态免收：查封/與付款人同盟/死神顯靈/住宿/消失/坐牢/住院/冬眠/夢遊
  3. 租金 = `estateRouteRent`（0x419744）：**同路段**（同名普通住宅用地）`fees[level]` 之和 × `moneyMul`；
     **連鎖店**（type≠0）數量 × 2000 × `moneyMul`；`estate.flag(+23)` 非 0 → 翻倍
  4. `applyGodRentModifier`（0x41D709）付款方神明：小財神减半 / 大財神免付 / 小窮神 +50% / 大窮神加倍
  5. `showMessage("%s\n\n此地屬%s\n\n請付%d元過路費", 1500)` → `transferMoney(p, owner, rent, 0)`
     （**flags=0：收款入银行**）→ `estate.price(+44) = 本次租金`
- [x] 連鎖店收費（`estateRouteRent` 的 type≠0 分支：數量×2000，不并入路段联合计算）`[HELP 21]`
- [x] **收租联动高亮**（`highlightBlink` 0x451985 + `markPickBuffer` 0x456C0A）：收租时标记联动组
      （住宅=同 owner+同名；连锁店=同 owner+type≠0 跨街道）→ 标记数>1 播放 16 帧亮度闪烁
      （`g_highlightLut` 0x476380，30ms/帧 + 400ms 停留）；重写 `captureHighlightShapes` 从
      `mapHitRegions` 提取原版 `g_pickMask` 形状快照（estate 帧 0/1、corp 帧 2/3，落点 y−40）
      → `drawEstateHighlight` 做 RGB555 亮度偏移（快照语义 = 原版 pickBuffer，拆除后仍亮），
      **阻塞播放（先闪烁、结束后才弹收费提示，对齐原版时序）**
- [x] **case 3 触发条件对齐**（0x448A7E）：`(travel & 0x83) == 3`（**临时载具计时中**）才面向/停留——
      普通步行/機車/汽車（travel=0/1/2）不触发（重写此前无条件面向 → 已修，收过路费不再转向）；
      面向时保存/恢复原朝向（+27 `g_playerRestoreDir`）；FLC 526 画在建筑屏幕坐标（`sub_40B066`）
      仅该条件下播放（重写载具计时未实现）；收费提示金额 = 神明调整前（`sprintf(...v8...)` 在 `sub_41D709` 前）
- [x] **連鎖店创建**（改建卡 `cardRebuildEffect` 0x44309B：`level!=0` 时 `type^=1`，变连锁店 `level>1`→1；
      卡片 7、效果表 `g_cardEffectFuncs` 0x475D5C）；调试 `Ctrl+4` 支持；完整卡片流程（选卡/消耗）→ P4
- [x] 收租诊断日志（`estateRouteRent` 打印匹配明细 + `matched/sum/total`，定位联动问题）+ `Ctrl+9` 一键联动组
- [x] **神明持续效果（2026-09-25）**：`landAfterMove`(0x40F381) 落地公共末尾——天使(9)免费加盖/建设
      （`angelUpgrade` 0x40B110，封顶 FLC 523）、惡魔(10)拆一层（记债 30×M + demolishAtObjId +
      FLC 526@建筑坐标−55）、土地公(12)強佔（他人记债 M×price×(level+2)/5，无主免费+cfgLandPerm 日期）；
      福神(3/4)付费升级/建设施后 `godBlessUpgrade`(0x40F8BE) 免费追加一级；死神代付
      `resolveFeePayer`(0x419ECC/0x41A6A4/0x41AF99，含原版寶箱 idx14 照抄) + 欠款矩阵
      `addPlayerDebt`(0x40DF69)。详见 map-object-refresh.md §13
- [x] **同盟分账（2026-09-27）**：收租 `loc_419A67` 地主 +65 对象的联合租金 v134 并入总额、
      按 `v134/(v8+v134)` 拆分转账（消息 0x46399A「屬%s與%s」）；高亮含同盟地块；破产解除同盟
      `eliminatePlayer` 0x40CE74
- [x] 收租角色台词 ✅ P4-B（付款方 `sub_44F4ED` **列18**/`sub_44F42D`、收款方 `sub_44F354`；
      **`sub_44F354` 实为台词表非浮动数字**；2026-09-30 列勘误：欠债者曾误播列16 连锁买地语）
- [x] 免費卡前置门槛 ✅ 2026-09-29 M3-B（`resolveFeePayer` `cardGate`）
- [x] **全部收费公式逆向归档** → `pricing-formulas.md`（物價指數/輪盤/本次步數/載具倍率、住宅用地/商業用地/行業設施點
  三类对象的购买/建设/升级/收费/到期公式、费用名表与映射表、MAPDAT 实测数值）
- [ ] 验收：升级成功/失败、踩他人住宅用地扣款（现金/银行变化 + 提示文本）、同路段联合租金、連鎖店收費、神明调整

### P1.5 商業用地 corp（`sub_41982D` case 0 → 4000..5999）

- [x] **数据**：`Corp` 补 `researchItem(+29)/researchLeft(+30)/buildPrice(+34)/feeTable[6](+36..47)/lastFee(+48)`
- [x] **无主购地**（0x41A86B）：`+34×M`；守卫 state37/携带槽 12；现金不足提示；询问文本 `"%s\n\n費用:%d元\n\n是否買下此地?"`；
      `checkCarriedGod`；owner/音效槽 17/小地图/期限/扣款
- [x] **建设施**（0x41A1E0，`type==0`）：付 `+34×M` → `selectFacilityDialog`（人类选 0..4 / AI `rand()%4+1`）
      → `type`、`++sub`（建设施即 1 级）；音效槽 18
- [x] **设施选择 UI**（`selectFacilityDialog` 0x440AAC，新文件 `facility_dialog.cpp`）：帧 4 图标底图 (43,279) +
      帧 5 标题框 + 「請選擇設施類別」(220,122) + 悬停三层边框（0xFFFF00 黄）+ 设施名 (220,154)
- [x] **设施升级**（0x41A2B3）：`+36 feeTable[0]×M`，上限 `kFacilityMaxLevel={1,5,5,1,5}[type]`；
      询问文本 `"%s\n\n升級費用:%d元\n\n是否升級？"`（estate 升级同步修正为该原版文本）
- [x] **他人收费**（0x41A370）：守卫 `sub!=0` 且 `type 1..3`（公園/研究所免收）；费用名 `kCostNames[kCorpCostMap[type]]`；
      旅館 = `feeTable[sub]×M×rouletteValue(1)`（"休息%d天\n\n費用%d元！"）、
      購物中心 = `×rouletteValue(2)`（"您的消費金額為\n\n%dx%d倍=%d元"）、
      加油站 = `500×载具倍率×diceValue×M`（"加油站\n\n董事長%s\n\n請付%d元%s"，步行免付）；
      `flag(+28)` 翻倍、神明调整、`transferMoney(flags=0)`、`corp+48=最近收费`
- [x] **转盘 UI**（`roulettePrompt` 0x44090E，新文件 `roulette_dialog.cpp`）：panel.mkf[68+theme] 14 帧
      （帧 0 底盘/帧 1 中心/帧 2..13 十二格）+ 提示文本 `off_475CF8[theme]` + 指针逐格步进
      （`sub_43F127`，40ms/帧，12 格循环）+ 减速停止（间隔 1..5 每 3 步 +1，落在有效格）+ 停留 40 帧；
      读数表 `byte_475D0C`：出國 {1,0,1,2,3,2} / 旅館 {1,4,3,2} / 購物 {1,6,5,4,3,2} / 保險 {5,3,30,20,15,10}；
      音效 Effect.mkf[52]；旅館/購物中心收费已接入（转盘在收费提示前）
- [x] **旅館住宿状态**（0x41A761）：`stateFlags` BYTE0 = days-1（0→0x80，`ownerCanCollectRent` 住宿中免收租）、
      `byte66 += days`；住宿欠款 20×M×days 与保险理赔 2000×M×days 仅日志（欠款矩阵/保险期 P4）
- [x] `diceValue`（0x48BAFC 本次步数，掷骰定格记录）供加油站/汽车/石油计费
- [x] **地图绘制**（0x4093F3 sub_40829D）：`sub==0` 有主 → **角色标记** `pieceTiles`（map.mkf[25]）
      帧=角色 ID、不调色板（此前误画 `corpTiles[0]` 公園图块）；`sub!=0` 按 `type` 分派图块：
      0 公園→`corpTiles[0]`、1 旅館→`[sub]`、2 購物中心→`[5+sub]`、3 加油站→`[11]`、4 研究所→`[11+sub]`
      （`dword_48AE78/48AE90` 实为 `dword_48AE64` 的 +5/+11 项别名，无独立加载）；owner 调色板
- [x] **右键提示对齐**（0x41783C/0x417999）：estate `level==0`→"空  地"、`type!=0`→"連鎖店"、
      应收租金（type==0→同街联合 / type!=0→数量×2000×M / 无主→fees[level]）；corp `sub==0`→"空  地"
- [x] 研究所（type4）研发 `labDevelopDialog`（0x44101D，`lab_dialog.cpp` + `updatePlayerStates` 产出）✅ 2026-09-26
- [ ] 验收：购地/建设施（5 类图标选择）/升级（上限）/旅館·購物·加油站收费/住宿状态免收租

### P2 事件格（`sub_41982D` case 2..16，语义见 §3）

> 研究详情见 **`41982d-p2-events.md`**（各 case 的处理函数、数据结构、实现顺序与待深入清单）。
> 进度：**得點券 ✅ / 卡片格 ✅ / 監獄·醫院 ✅（2026-09-23）/ 銀行·樂透·小游戏·百貨 ✅ /
> 新聞 ✅（2026-09-26，`44b6df-news-events.md`；拍卖 idx 7 下一轮）**；
> 剩余 = 无（魔法屋 16 ✅ 2026-09-26）。

- [x] case 10/11/12 得點券（FLC 537 阻塞播放（透明=索引 0）+ showMessage + 50/30/10 點券；
      语音 P4）：`playEventFlc` + `landingEvent` case 10/11/12 + `drawEventFlcFrame`（详见 `41982d-p2-events.md` §2）
- [x] case 13 卡片（FLC 536 + `drawFreeCard` 赠卡池抽卡入卡包（15 槽，满则舍弃最低价）+ `showCardGet`
      卡片图/提示框/1500ms；语音 P4；详见 `41982d-p2-events.md` §3）
- [x] case 4/5 入狱/住院状态（`jailPlayer`/`hospitalizePlayer`：移到 8002/8001 格 + `stateFlags`
      BYTE2/BYTE3 天数 + FLC 538/524 + 保险理赔 2000×M×天）+ 回合递减/到期释放（`updatePlayerStates`
      + `endPlayerState`）+ 保险期（+62）递减与 `insurancePayout`；debug `Ctrl+Shift+J`/`L`
- [x] case 4/5 門外保釋/辦理出院 UI（`jailBailDialog`/`hospitalVisitDialog` + `sub_43CAAB`/`sub_43DA27`
      + panel.mkf[63/64/65]）与 AI 保释（费用表 `dword_475C44/475CA4` = 玩家 30/NPC 300 点券）；
      **含开局默认在押 NPC**（`newGameInit` 0x40734F：監獄 小偷/強盜、醫院 流氓/間諜；名表 `dword_47ED5A`）；
      **NPC 释放/行走/抓回/恶行 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）
- [x] case 2 新聞（`newsEvent` 0x44B6DF：`newsOrder` 抽取 + `newsEventCheck` 36 条判定 +
      `g_newsFuncs` 36 效果（释放/灾害/三税/排行/股价停牌/坐牢）；`news_dialog.cpp`，2026-09-26；
      **拍卖 idx 7 桩待下一轮**、语音/浮动数字 → P4；详见 `44b6df-news-events.md`）
- [x] case 3 命運（`fateEvent` 49 效果 + 卡片联动）✅ 2026-09-26，详见 `44db81-fate-events.md`
- [x] case 6/7/8 小游戏**兜底得分**（`miniGameVisit` 0x415457 else：rand()%20+50 点券 +
      "得點券%d點" + 台词；四大恶人忽略；2026-09-25）
- [x] case 7 七彩氣球**交互版** ✅ 2026-09-25（`miniGameBalloonsRun`，`minigame_dialog.cpp`）
- [x] case 6 企鵝挖寶 / case 8 喜從天降**交互版** ✅ 2026-09-25（`miniGameDigRun`/
      `miniGameMoneyRun`，`minigame_dialog.cpp`；细节 `41982d-p2-events.md` §6）
- [x] case 9 樂透（`lotteryVisit`，每月 15 号开奖——先分红后乐透；`lottery-system.md`）
- [x] case 14/15/16 銀行/百貨公司/魔法屋（`sub_4379C9`/`sub_42E931`/`sub_43380A`）—— 魔法屋 ✅ 2026-09-26（`43380a-magic-house.md` + `43bde5-auction.md`）
- [ ] case 1 公園 = default（无事件）—— 确认与重写返回一致
- [ ] 验收：逐类型实机触发核对（帮助页 23..38 可对照表现）

### P3 移动中落地 + 地图物件（`onPlayerActionPhase` 0x41B42D；`map-object-refresh.md`）

- [x] 医院/监狱状态回合递减、到期释放（`updatePlayerStates` ✅ 含**附身神明寿命递减→飘走轮替**；
      坐牢/住院丧失收租权利 `[HELP 34/37]`；门外保释/出院 ✅ P2）
- [x] 乞丐施捨（同格已淘汰玩家 1000×M 给银行 + 其随机换位 `sub_40CC56`；`[HELP 39]`）
- [x] 定时炸弹挂身（cellNo，life=38 每落地 -1）+ 同格传染转移 + 归零爆炸
      （删+拆地产一级+车毁+住院5；地雷/路障/炸弹**放置入口属 P4 道具卡，留 TODO**）
- [x] cellTable 物件格全链路（`attachObject` 0x40EAD7 + `attachEnd` 0x40E32C 飘走动画 +
      **配对轮替**（小↔大/惡犬↔土地公…）+ 渲染跟随/浮空寿命数字/8 方向帧/弹飞；神明行为见
      `[HELP 40..56]`：大/小財神老虎机收钱/免付、大/小福神送卡、大/小窮神送钱/租金加倍、
      大/小衰神丢卡、天使/恶魔三属性、死神 13 天没收全部、惡犬咬伤住院三天/车辆无事、
      禮物得道具、寶箱 +500 点券）
- [x] 路过銀行柜员机 ✅（原有）；停留结算仍由 landingEvent（P2）
- [ ] 验收：进医院/监狱停留 N 回合、神明效果（**待实机**）
- [x] **事件槽 NPC（p≥4）物件分支 + LABEL_88 恶行 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）；
      [TODO P4] 角色台词语音 sub_44EF41

### P4 卡片/道具/神明使用

- [x] `givePlayerItem`(0x445A4D) / `takePlayerItem`(0x445AA2) / `drawGiftCard`(0x445ADA) ✅（P3 随礼物接入）
- [x] 路障/地雷/炸彈放置 ✅（`placeMapItem`，`item_effects.cpp`；flyObject 0x40E669）+ **機器娃娃
      `sub_446AFB`（道具 id1，槽8）✅ 2026-09-27**（p==8 弹飞删除 bounceObject 0x40FAFD +
      收尾等 `sub_40FAD6` flyCount 清空；见 `item-effects.md` §1.6；旧标"拆除卡工程车"为误名）
- [x] 请神符效果 `useInviteGodCard`(0x444E1A/0x444D1A) / 送神符效果 `useBanishGodCard`(0x444C45) ✅
      （`map_objects.cpp`；卡使用 UI 未接，调试 `Ctrl+Shift+I`/`H` 塞卡直调）；
      cellTable 拾取 id 修正为 0xA100|槽+1（原直译 (slot+1)<<8 与 estate 段重叠）
- [ ] `sub_444E1A` 使用流程（`sub_444D1A` 人类选择 / `sub_41E6F2` AI 选择 / `sub_40E669` 移动动画）
- [ ] 验收：用卡/用道具生效

### P5 AI / 存档 / 事件槽 NPC

- [ ] `sub_41E6F2` AI 决策、`alive&6` 自动行动
- [ ] `loadGameFromSlot`(0x402AC5) 完整恢复（NPC 槽 ✅ 2026-09-26）
- [x] **事件槽 NPC（玩家 4..7）／四大恶人 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）
- [ ] 验收：AI 自动走完回合、读档恢复完整状态

## 6. 待确认问题（后续一并处理）

1. ~~格子类型 1..16 与名称的精确对应~~ —— 已由 MAPDAT 实测 + 帮助页标题确定（§3）
2. ~~case 11..16 语义~~ —— 已由跳转表 + 反汇编确定（§3）
3. `CellEnt` +36 bit31 / +39 状态语义（MAPDAT 初值：bit31 与 +39=-128 联动；監獄 +39=8、醫院 +39=16）——
   重写代码当前用 bit31 作出生点/道路判定（`turn_system.cpp`），需与原版核对
4. `sub_41D476` / `sub_41D7D4` 具体语义待分析（`sub_40FA61` 已明：神明槽 7/8/15 阻止购地/升级）
5. ~~事件槽 NPC（玩家 4..7）与 0x43xxxx 段（監獄/醫院/事件）的关系~~ —— **已解决**：
   槽记录 `g_miscTable80`（0x498E28，16B×5）+ `jail/hospitalFlags`；`498df0-event-slot-npc.md` 权威规格
6. ~~商業用地建设~~ —— **已解决**：建设施对象是 `corp`（4000+，**商業用地** `[HELP 22]`），非 estate；
   `off_475150` = 设施名表（公園/旅館/購物中心/加油站/研究所 = type 0..4；帮助列表段作「商場」），
   升级上限表 `g_facilityMaxLevel`（0x474940 = `{1,5,5,1,5}`）。详见 `pricing-formulas.md` §4
7. ~~公司企業 11 行业对应~~ —— **已解决**：`[HELP 20]`「公司企業」行业收费 = **行業設施點 specPt index（+26）**
   + 旅館（corp，帮助作「飯店」）；水力/電力/百貨/銀行四行业原版**未接通**收费（详见 §3.2「11 种行业归属」）。
   corp 的 `+24 type`（0..4）是设施类别（图块组 `dword_48AE64[17]` 为建筑外观）。
   持股/分红是独立 12 支股票系统（`g_playerShares`/`flt_496994`/`sub_4291D6`）。
   水費/電費/房租費/店租費在原版映射表中存在但收费分支未接通（预留）。详见 `pricing-formulas.md` §2/§5
