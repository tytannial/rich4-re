# 交易市場「公佈欄」（`sub_4284BE` 0x4284BE + `sub_427C21` 0x427C21 + `sub_4255DA` 0x4255DA）

> 2026-09-27 专项逆向 + 实现。帮助 `[HELP 11]`「交易：公佈欄：玩家間公開交易房產/股票/卡片/道具」；
> 工具条 case 9（第 10 个按钮）。IDB 已重命名：`queueTradeOrder` 0x4246C5；
> 重写：`src/app/trade_market.cpp` + `include/game/app/trade_market.h`
> （✅ 2026-09-27 实现，待实机验收）。
>
> ⚠️ 旧文档（`stock-system.md` §1.5/§2.2）把类型 3/4 及道具/卡片数组写反——
> **正确：type 3 = 道具（`g_itemStock` 0x49915B/5C），type 4 = 卡片（`g_cardState60` 0x499120）**。

## 0. 调用者与资源

| 调用者 | 场景 |
|---|---|
| `sub_417D65` 工具条 case 9（0x417DEE） | 人类点工具条第 10 按钮 → `sub_4284BE` |
| `sub_418C55` case 2/5（0x418E13） | AI/托管回合自动：清挂单 → 挂卖/改价/买入 → 随机用卡或道具 → 移动 |
| `advanceDay`（0x41CFC4） | `sub_428475` 每日挂单 age++ |

资源：`panel.mkf[73]`（SMP 20 帧）+ `panel.mkf[74]`（13 帧 = 13 道具图标）。

panel[73] 帧表（实测）：

| 帧 | 尺寸 | 用途 |
|---|---|---|
| 0 | 596×348 | 主面板（blit @(22,66)） |
| 1 | 336×416 | 股票挂单对话框（@(152,32)） |
| 2 | 416×416 | 地产挂单对话框（@(112,32)） |
| 3 | 360×128 | 道具挂单对话框（@(140,160)） |
| 4 | 360×128 | 卡片挂单对话框（@(140,160)） |
| 5 | 184×88 | 提示框（`sub_424502` @(227,42/196)） |
| 6 | 192×224 | 详情背景：道具/卡片（标签 類型/市價/賣價） |
| 7 | 192×256 | 详情背景：股票（類型/張數/市價/賣價） |
| 8 | 192×288 | 详情背景：地产（類型/地點/等級/市價/賣價） |
| 9..16 | 72×72 | 挂单格子图标：帧 = `4*颜色 + type + 8`（type 1..4 × 2 色） |
| 17 | 144×96 | 投标 2×2 子按钮底板（@(464,116)） |
| 18 | 21×21 | EXIT 按钮（股票对话框 @(463,38)） |
| 19 | 80×32 | 地产对话框页签高亮（@(112+80i,32)） |

## 1. 挂单表 `g_miscTable336`（0x4967E0；84B/玩家 = 7 槽 × 12B）

| 偏移 | 字段 | 说明 |
|---|---|---|
| +0 | type | 1=股票 2=地产 **3=道具 4=卡片**（0=空槽） |
| +1 | age | 每日 ++（`sub_428475`）；**无读取点**（死字段，照抄） |
| +2 | objId | 股票号(0..11) / estate+2000 / corp+4000 / 道具 id 1..13 / 卡片 id 1..30 |
| +4 | price | 售价（现金） |
| +8 | count | 数量（仅股票） |
| +10/+11 | snapType/snapLevel | 地产快照（estate +24/+26 或 corp +24/+26；失效检测） |

其它全局：`word_48BE6E[?]`（地产挂单列表缓冲，翻页 11 项）、`dword_4754BA`（起始索引）、
`dword_4754BE`（本页项数）、`dword_4754C2`（列表玩家）、`dword_48C2B4`（总数）、
`g_itemVisibleMap` 0x48C548（道具对话框映射 16B）、`byte_496828[84*p]`（= 玩家 p 槽 6 类型，
"公佈欄已滿"判定）。

## 2. 数据层

### 2.1 `sub_42483E` clearInvalidTradeOrders
每玩家 7 槽（移除后原地重检）：

| type | 失效条件 |
|---|---|
| 1 股票 | `count > g_playerShares[p][objId]` |
| 2 地产 | owner≠p+1 或 type/level 与快照不等（estate +24/+26；corp +24/+26） |
| 3 道具 | `g_playerCards[15p+objId]==0`（道具库存为 0） |
| 4 卡片 | `sub_4413AD(p,objId)==0`（卡片袋无此卡） |

### 2.2 `queueTradeOrder(p, type, objId, price, count)` 0x4246C5
找空槽或同 `(type,objId)` 槽覆盖；7 槽满 → **不写**。写 type/age=0/objId/price；
type==1 写 count；type==2 记 estate/corp 快照（corp 用 56 字节/项、estate 52）。

### 2.3 `sub_4247D5(p, slot)` 移除
`memcpy(12*(6-slot))` 前移 + 末槽清零。

### 2.4 `sub_428475` ageTradeOrders
存活玩家每个非空挂单 `+1`（`advanceDay` 0x41CFC4 调用）。

### 2.5 `sub_4255DA(seller, slot)` executeTrade（买方 = 当前玩家）
1. `g_playerCash[buyer] < price` → 人类提示 `byte_463E50`「您的現金不足！」+ 阻塞 1500ms，返回 0。
2. 按类型转移：
   - **case 1 股票**：买方持股 += count；卖方持股 -= count（归零清均价）；
     买方均价 = `(price + (int)(旧股数×旧均价)) / 新股数`；
     `updateSpecPtControl(buyer, 股票号)` 变化且买方人类 → 提示 `byte_463E5F`「恭喜您獲得經營權！」。
   - **case 2 地产**：owner = buyer+1；`rebuildMiniMap(0)`。
   - **case 3 道具**：买方库存 <9 → `takePlayerCard(seller,id)` + `givePlayerCard(buyer,id)`；
     否则人类提示 `byte_463E72`「道具欄已滿\n\n無法購買！」返回 0。
   - **case 4 卡片**：买方卡数 <15 → `sub_441343(seller,id)` + `sub_4412E4(buyer,id)`；
     否则人类提示 `byte_463E89`「卡片欄已滿\n\n無法購買！」返回 0。
3. `sub_41D2C6(buyer, seller, price, 0)` 付款 → 返回 1。

## 3. 入口与 AI（`sub_4284BE`）

```
sub_42483E();                        // 两分支共同：清理失效挂单
if (g_playerAlive[104*cur] == 1) {   // 人类 → UI
    dword_48C298 = 载入 panel[73]; dword_48C2A8 = 载入 panel[74];
    预绘帧文本（见 §4.1）; blit 玩家棋子头像到帧0; runModal(sub_427C21);
} else {                             // AI
    见下 AI 逻辑（0x42887D..0x428CAA）
    // 之后 sub_418C55 继续：rand()&1 → useCardFlow/itemPanelFlow → 移动
}
```

AI 逻辑（概率触发，每回合）：

| 概率 | 行为 |
|---|---|
| `rand()%15==0` | 挂卖：卡数 >12 → 收集**重复卡**（扫槽 0..count-1 两两比较，取重复值）随机一张；否则道具（库存 ≥3 或 `kItemAiPersona[id]-aiPersonality==2`）随机一个。挂卖前若**槽 6 非空则先移除槽 0**。价格 = 原价（点券）×100×M |
| `rand()%3==0` | 自己 type 3/4 挂单价格重置为原价×100×M |
| `rand()%4==0` | 扫其他玩家挂单买入（每次最多一笔）：**股票** 单价 `price/count` < 现价；**地产** 报价 `< 估值/3` 且 余额 `> 2×报价`。成交后移除该挂单 |

## 4. 主面板（`sub_427C21` + `sub_4249C2`）

### 4.1 绘制
- 帧 0 @(22,66)；玩家棋子帧0 @(66, 102+72i)（原版预绘到帧0 的 (44,36+72i)）。
- 挂单格子 72×72 @(104+72c, 114+72r)，帧 = `4*byte_496B7C[104*p] + type + 8`
  （`byte_496B7C` = 玩家结构 +20 = 角色模板值 0/1；无写点）。
- 预绘文本（原版写入帧内存；重写绘制时叠加）：
  - 帧1（股票）：`股票名稱`@(48,16)、`持有張數`@(144,16)、`總 市價`@(250,16)
  - 帧2（地产）：页签 `kQuerySubPageNames[5]`@(40+80i,16)；列头 `kQueryLandCols[5]`
    @(word_4754B0[i]−112,48)，`word_4754B0`={147,231,319,395,471}
  - 帧6/7/8（详情）：标签 @(31,92..)；`EXIT`@(140,203/236/265)
- 投标按钮带 (464..536, 74..114) / EXIT (536..608, 74..114)；帧17 子按钮底板 @(464,116)；
  2×2 子按钮 (473+63c, 125+39r, 62×38)：**左上=股票、左下=地产、右上=道具、右下=卡片**
  （`v8 = (y−125)/39 + 2*(x−473)/63`；`byte_48C2CB = v8+1`；`PostMessage(1038+byte)` →
  1039/1040/1041/1042 = sub_4258C1/42608F/4267A4/426C2E）。

### 4.2 交互（原版"按下-滑动-松开"）
| 消息 | 行为 |
|---|---|
| `WM_LBUTTONDOWN`(0x201) | 格子（有挂单）→ `byte_48C2CA = grid+100`；EXIT → 2；投标区 → 槽6 非空则提示 `byte_463F03`「公佈欄已滿\n\n請先撤件！」+ 阻塞 1500，否则 `=1` + 高亮 |
| `WM_MOUSEMOVE`(0x200) | **仅 `byte_48C2CA==1` 时**：按 (473..599,125..202) 更新子按钮 `byte_48C2CB` + `scaleElementChannels(-12)` 高亮 |
| `WM_LBUTTONUP`(0x202) | `==1` → 重绘格子 + `byte_48C2CB` 非 0 则打开对应子对话框；`==2` → 退出；`>=100` → 详情（`PostMessage(0x413=1043)`） |
| `WM_RBUTTONUP`(0x205) / `WM_USER+1`(0x401) | 退出 / 初始化重绘 |

高亮 = `scaleElementChannels(surface, x,y,w,h, -12)`（重写 `kChannelDim` 表）。

## 5. 详情（`sub_42704E`；`byte_48C2C2`=卖方行、`byte_48C2C3`=槽）

- 入参 `lParam = byte_48C2CA`（100 + 7*行 + 列）→ seller/slot。
- 背景帧：`byte_4754A8[type+3]` → 股票 7 / 地产 8 / 道具·卡片 6；
  落点 `((640−w)/2, (480−h)/2)`（帧6 224,128；帧7 224,112；帧8 224,96）。
- 图标帧 `4*byte_496B7C[104*seller] + type + 8` @(c4+112, c6+6)。
- 玩家名 @(c4+58, c6+40) 居中（0xF0F0F0/0x101010）。
- 数据（0xF0F0F0/0x101010；**价格值带「元」**——格式串 `aS_43` 0x463EE0 的 IDA 显示 "%s"
  实为 "%s元"，BIG5 显示坑）：
  - 股票：名@(120,92)、張數@(178,124,align6)、市價(張數×現價)@(178,156)、賣價@(178,188)
  - 地产：類型(住宅用地 0x463EE5 / 商業用地 0x463EEE / 设施名)@(120,92)、
    地點@(120,124)、等級(住宅 `kBuildingNames[level]` / 连锁「連鎖店」0x463E30 /
    corp `kBuildingNames[11+sub]`)@(120,156)、市價@(178,188)、賣價@(178,220)
  - 道具/卡片：名@(120,92)、市價(原價×100×M)@(178,122)、賣價@(178,152)
- 按钮 y = `word_48C2C8` = 股票 236 / 地产 265 / 道具·卡片 203；
  左按钮 (c4+16..88) 文案 自己=`撤 件`(0x463EF7) 他人=`購 買`(0x463EFD)（0xFFFFFF/0x800000）；
  右按钮 (c4+104..176) = `EXIT`；`WM_MOUSEMOVE` → `highlightRect` 下沉。
- `WM_LBUTTONUP`：左按钮 → 自己：`sub_4247D5` 撤件；他人：`yesNoDialog(320,240)` 确认 +
  `sub_4255DA` 成交 → 移除；**取消/失败停留**；其余（右按钮/空白）→ 退出。

## 6. 子对话框

### 6.1 股票 `sub_4258C1`（帧1 @(152,32)）
- 列表 = 持股 > 0 的股票（最多 12）：名称@200、張數@332(align6)、市值@478(align6)，
  行 y=80+32i；行区 64+32i 高亮（drawRectBorder **白** 0xFFFFFF，调用点 0x425C23 push）；EXIT 帧18 @(463,38)。
- 点击行 → `sub_453544(持股)` 张数 → `sub_453544(10×市值)` 价格 → `queueTradeOrder(cur,1,股票号,价,张数)` → 退出。
- 提示串：`byte_463EA0`「請輸入欲賣出的張數」/ `byte_463EB3`「請輸入欲拍賣的價格\n\n（市價：%d元）」
  （`sub_424502` 提示框 @(227,42)，数字框前显示、取消后整层重绘清除）。

### 6.2 地产 `sub_42608F` + 列表 `sub_424AEA`（帧2 @(112,32)）
- 5 子页签（`kQuerySubPageNames`）@(112+80i,32)，DOWN 切换（`sub_424AEA(tab,0)` 重收集）。
- 列表 = `collectOwnedAssetIds(cur, tab)`（同 0x423B3B；0=全部 1=住宅區 2=商業區 3=房屋 4=連鎖店），
  11 行/页，行 y=112+32i；列 x={147,231,319,395,471}：
  地點 / 開發狀況 / 價格 / 收費 / 租期。
  - 價格：estate `(priceAdd + priceBase×level)×M`；corp `(buildPrice + feeTable[0]×sub)×M`
  - 收費：estate 普通 `estateRouteRent(owner, es)`；连锁 = 同 owner 连锁总租；corp `M×feeTable[sub]`
  - 租期：`"%02d/%d/%d"`（expireDate 年%100/月/日）或「無限期」(0x463E42)
- 右侧 3 按钮 x 512..527：y 33..50 EXIT、65..95 上一页（pageStart−11）、97..127 下一页（+11）。
- 点击行 → `sub_453544(10×估值)` 价格 → `queueTradeOrder(cur,2,objId,价,0)`（行悬停白框，0x4262CC push）。
- 键盘首字搜索（`word_48C2BD`）未复刻（差异）。

### 6.3 道具 `sub_4267A4`（帧3 @(140,160)，**色键 blit**）
- 列表 = `g_itemStock[15cur+i] != 0`（i 0..12）：**5 列**网格（`v45>464` 换行 → 176+72c，c=0..4），
  图标 panel[74] 帧 `id−1` @(160+72c, 208+32r)（色键）、数量 `×N`(0x463ED6) @(206+72c, 208+32r, align6)。
- 点击 → `sub_453544(10×原价×100×M)` → `queueTradeOrder(cur,3,id,价,0)`。
- 原版命中网格 5 列与绘制一致（`col=(x-140)/72` 0..4、`row=(y-192)/32` 0..2）——重写对齐；
  命中挂 `WM_LBUTTONDBLCLK`（需双击），重写单击（差异）。

### 6.4 卡片 `sub_426C2E`（帧4 @(140,160)，**色键 blit**）
- 15 槽（5 列 × 3 行）：`g_cardState60[15cur+slot]` 非空 → 卡名 @(176+72c, 208+32r)。
- 点击 → `sub_453544(10×卡价×100×M)` → `queueTradeOrder(cur,4,id,价,0)`。
- 原版绘制按紧凑序、命中按槽位网格（不一致 bug）；重写统一 5×3 网格（差异）。

## 7. 文本 / 价格 / 表

| 地址 | 内容 |
|---|---|
| 0x463E30 | 連鎖店 |
| 0x463E42 | 無限期 |
| 0x463E49 | 空  地 |
| 0x463E50 | 您的現金不足！ |
| 0x463E5F | 恭喜您獲得經營權！ |
| 0x463E72 | 道具欄已滿\n\n無法購買！ |
| 0x463E89 | 卡片欄已滿\n\n無法購買！ |
| 0x463EA0 | 請輸入欲賣出的張數 |
| 0x463EB3 | 請輸入欲拍賣的價格\n\n（市價：%d元） |
| 0x463ED6 | ×%d |
| 0x463EE5 / 0x463EEE | 住宅用地 / 商業用地 |
| 0x463EF7 / 0x463EFD | 撤 件 / 購 買 |
| 0x463F03 | 公佈欄已滿\n\n請先撤件！ |
| 0x463F1A/23/2C | 股票名稱 / 持有張數 / 總 市 價 |
| 0x463F35/3C/43/4A/51/58 | 類型： / 市價： / 賣價： / 張數： / 地點： / 等級： |
| 0x47FDEF (卡) / 0x47FEDF (道具) | 价格字节 = 点券价（重写 `kCardPrices` / `kItemPrice`）；挂单价 = ×100×M |
| 0x47FEE9 | 道具 AI 性格字节（重写 `kItemAiPersona`；`-aiPersonality==2` → AI 挂卖） |
| 0x4753D4 / 0x4753E8 | 地產 5 页签 / 5 列头（`kQuerySubPageNames` / `kQueryLandCols`） |
| 0x4754A8 | 详情背景帧号（byte[type+3]）+ `word_4754B0` 列 x 坐标 |

## 8. 重写映射与差异

| 原版 | 重写 |
|---|---|
| 0x4284BE 入口 | `tradeMarketDialog` / `tradeAiTurn`（`src/app/trade_market.cpp`） |
| 0x42483E/0x4246C5/0x4247D5/0x428475/0x4255DA | `clearInvalidTradeOrders` / `queueTradeOrder` / `removeTradeOrder` / `ageTradeOrders` / `executeTrade` |
| 0x427C21 / 0x4249C2 / 0x42704E | `tradeMainEvent`+`drawTradeMain` / `tradeDetailEvent`+`drawTradeDetail` |
| 0x4258C1 / 0x42608F / 0x4267A4 / 0x426C2E | `StockOrderUi` / `LandOrderUi` / `ItemOrderUi` / `CardOrderUi` |
| 0x453544 数字框 | `numberInputDialog` |
| 0x453A32 yesNoDialog | `confirmDialog` |
| 0x41D2C6 付款 | `transferMoney` |
| 0x4294D5 经营権 | `updateSpecPtControl` |
| 0x4412E4/441343/445A4D/445AA2 | `giveCardToBag`/`cardBagRemove`/`givePlayerItem`/`takePlayerItem` |
| 存档 0x403146 / 0x402CB8 | `save_data.cpp`（336B @ 偏移 9513）+ `loadGameFromSlot` |
| 时光机 0x44808A/0x448544 | `TurnSnapshot::tradeSlots` + `saveTurnSnapshot`/`restoreTurnSnapshot` |

差异（重写自定）：
1. 股票/道具/卡片子对话框 EXIT 与格子：原版需 `WM_LBUTTONDBLCLK`（双击，命中时画按下态）
   或右键；重写**单击**——按下态改在 `DOWN` 显示（股票 EXIT 帧18 / 其余 `pressDown`），`UP` 执行。
2. **按下态 vs hover（2026-09-27 实机修正）**：股票 EXIT（原 DBLCLK 画帧18）、地产 3 按钮、
   详情 按钮（原 `WM_LBUTTONDOWN` + `highlightRect`）、道具/卡片格与 EXIT（原 DBLCLK + `highlightRect`
   矩形 (141+72c, 193+32r, 70, 30)）均**无 hover**；仅股票/地产**列表行**有 MOVE hover
   **白框**（`push 0FFFFFFh`，0x425C23/0x4262CC）。首版把这些做成 hover 效果（悬停即亮/黄框）已修正。
3. 原版把列头/标签/EXIT 文本**预绘制进 sprite 帧内存**（`sub_450441` 可写副本）；重写绘制时叠加文本，效果等价。
4. 道具/卡片对话框绘制网格与命中网格不一致（原版 bug）→ 重写统一（道具 5 列、卡片 5×3 网格）。
5. `sub_451D4E` 窗口卷动动画未复刻；提示串（請輸入…）已按原版实现：`sub_424502` = panel[73]
   帧5 @ (227,42/196) + 文本 0x101010 居中，数字框前显示、关闭后由对话框重绘清除。
6. 主面板投标区"已滿"提示：原版 `WM_MOUSEMOVE` 每次进入均阻塞 1500ms，重写仅在点击时提示。
7. 地产子对话框键盘首字搜索（`word_48C2BD`）未复刻。
8. 股票 AI 买入要求"余额 > 2×报价"（原版 `2*price < cash`），地产报价须 `< 估值/3`；AI 卡片重复收集跳过空槽值 0（原版可能记录 0）。
9. `age` 字段无读取点（原版死字段），重写照抄保留。
10. blit 方式逐帧核对：帧 3/4（道具/卡片）**色键**（原版 `sub_456418`）；帧 0/1/2/5/6/7/8/9..19/panel[74]
    **不透明**（原版 `blitElementFullscreen`/`blitOpaqueToBackbuffer`）——曾全用不透明导致帧3/4 黑底、格子黑边。
11. 子对话框列表点击：原版依赖 `WM_MOUSEMOVE` 的 hover 记录 + UP 分发；重写 UP 时**按坐标重算命中**（避免
    对话框刚打开时点击无 hover 记录而不响应——"点物品/卡片不出数字框"根因）。
12. 主面板：子按钮底板帧17 仅**按住**投标区（byte_48C2CA==1）时出现（原版常驻为误）；玩家棋子头像
    实参 (66,150+72p)（原版帧内 (44,36+72i) + 面板原点 (22,66)）；格子图标不透明 blit。
13. **嵌套模态退出残留修复**：帧1/2（336/416×416 @152/112,32）大于主面板（596×348 @22,66）——
    上探工具栏区（32..66）、下探地图区（414..448），提示框 y=42 也超面板顶；只重画主面板会残留。
    子对话框/详情/提示框退出后统一 `renderGameFrame(app)` + `drawTradeMain`（`redrawTradeMainFull`；
    原版靠 `saveBackground`/`sub_451EDB` 恢复背景）。
14. **子对话框内数字框取消残留**（2026-09-27 实机）：提示框 (227,42..130) 超出道具/卡片/股票/地产
    子对话框区域，取消后只重画子对话框会残留提示框 → `redrawSubLayer` = `renderGameFrame` +
    `drawTradeMainBase`（场景 + 主面板）+ 子对话框自身重绘。

## 9. 验收清单

- [ ] 工具条第 10 按钮（case 9）打开公佈欄面板（596×348 @(22,66)）
- [ ] 按住投标区滑到 4 个子按钮 → 松开打开对应挂单对话框
- [ ] 股票挂单：张数（≤持股）+ 价格（≤10×市值）→ 面板出现格子（按颜色/类型）
- [ ] 地产挂单：5 页签/翻页/列数据（價格/收費/租期）正确
- [ ] 道具/卡片挂单：图标/数量/卡名正确
- [ ] 点格子 → 详情（标签/图标/玩家名/市價/賣價）；他人购买（确认框→扣款→转移）；
      自己撤件；现金不足/道具卡满提示
- [ ] 7 槽满点投标 → 「公佈欄已滿」；成交股票获经营権 → 提示
- [ ] AI 回合：随机挂卖（日志 queueTradeOrder）、改价、低价买入
- [ ] `advanceDay` age++（日志/存档字段）；存档挂单恢复；时光机回滚含挂单
