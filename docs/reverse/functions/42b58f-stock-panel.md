# 0x42B58F 股市面板（第 11 工具条按钮）

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x42B58F`（面板主入口）/ `0x42AAFF`（交互 WndProc）/ `0x4297F7`（数据列重绘）/ `0x4296C1`（列标题+选框）/ `0x429D65`（走勢圖对话框）/ `0x42B2EC`（休市模态）/ `0x428D01`（休市判定） |
| 大小 | 0x508 / 0x7ED / 0x56E / 0x136 / 0xD70 / 0x100 / 0x29 |
| 调用者 | `0x417D65`（工具条 case 10）、`0x444F25`、`0x44503F`（卡片/事件查股价） |
| 被调用 | `sub_450441`(载入 panel.mkf[75])、`blitElementFullscreen`、`drawText`、`setTextFont`、`sub_452793`(金额千分位)、`runModal`、`sub_428D2A`(买)、`sub_428E23`(卖)、`sub_453544`(数字输入)、`showMessage` |
| 重写符号 | `src/app/stock_panel.cpp` `stockPanelDialog`；逻辑 `src/app/stock_system.cpp` |
| 状态 | 已实现（待实机验证） |

## 功能

全屏（640×480）股市面板。背景为 `panel.mkf[75]`（SMP，29 帧，每帧 640×480）：
帧 0 = 股價表视图、帧 1 = 持有股數表视图、帧 2 = 走勢圖视图。12 支股票成行显示，
顶部 5 按钮（持有股數表/買進/賣出/上市公司資訊/離開），右侧玩家栏 + 银行余额。
休市（周末/节日或事件设 `dword_4990DC`）时仅显示"本日休市"大字。

## 数据字段映射（原版 g_stocks @0x496980，每支 36 字节 = 9×u32）

```c
// [0] +0x00  char*   name        股票名 BIG5 指针（(&g_stocks)[9*i]）
// [1] +0x04  u16     specPtIdx   word_496984[18*i]：上市公司=specPt 数组索引，非上市=0
//     +0x06  u8      halted      byte_496986[36*i]：停牌剩余天数
//     +0x07  u8      news        byte_496987[36*i]：新闻（高4位=涨标记基数, 低4位=剩余）
// [2] +0x08  u16     reserved    word_496988[18*i]：保留股份（loadMapData 初值，买减卖加）
//     +0x0A  u16     volume      word_49698A[18*i]：交易量（sub_42915A 每日生成，买减卖加）
// [3] +0x0C  float   basePrice   flt_49698C：基准价（非上市动量缩放基准）
// [4] +0x10  float   prevClose   flt_496990：昨收
// [5] +0x14  float   curPrice    flt_496994：现价
// [6] +0x18  float   volatility  flt_496998：波动率
// [7] +0x1C  float   momentum    flt_49699C：动量（clamp ±10）
// [8] +0x20  float   noise       flt_4969A0：当日个股扰动
// 持股：g_playerShares @0x4971A0，[24*player + 2*stock] int32（重写 playerShares[4][12]）
// 均价：flt_4971A4 @0x4971A4，[24*player + 2*stock] float（重写 playerAvgCost[4][12]）
// 历史：g_stockHistory @0x497328，[144*stock + turnCounter] int32（存 float 位模式）
// 大盘：dword_499078 = Σ现价 × 10；总市值 dword_49907C = ΣbasePrice × 10
// 休市：dword_4990DC（低字节=剩余休市天数, bit7=到期标志）；分红期数 dword_499084
```

> 重写 `GameState.stocks[12][9]`（float）字段 [0..8] 与上表 [0..8] 一一对应；
> `specPtIdx/halted/news/reserved/volume` 在原版是同一 36B 结构内的 u16/u8，
> 重写拆为 `stocks[s][1]`(specPtIdx) + 独立数组 `stockHalted/stockNews/stockReserved/stockVolume`。

## 面板布局（全屏坐标，align：0 左上 / 1 右上 / 2 居中 / 6 右对齐千分位）

### 顶部按钮（`Rect[5] @0x4754C8`，stride 16 = {L,T,R,B}）
| # | 矩形 | 功能（`sub_42AAFF` switch） |
|---|------|------|
| 1 切換 | (16,9,124,39) | 股價表↔持有股數表（`byte_48C2EC^=1`，重绘 `sub_4297F7`） |
| 2 買進 | (128,9,198,39) | 涨停→"漲停無法買進！"；否则 `sub_453544(可买)`→`sub_428D2A` |
| 3 賣出 | (202,9,272,39) | 无持仓跳过；跌停→"跌停無法賣出！"；否则 `sub_453544(持股)`→`sub_428E23` |
| 4 走勢圖 | (276,8,410,40) | PostMessage 0x40B → `runModal(sub_429D65, sel-1)` |
| 5 離開 | (≈414,8,623,40) | PostMessage 0x205 → 取消退出 |

按钮标签由 `sub_4296C1` 画：買進@163,24 / 賣出@237,24 / 上市公司資訊@343,24；
按钮 1 文字 = 目标视图名（股價表视图显示"持有股數表"，反之"股 價 表"）。

### 股價表视图（帧 0，`sub_4297F7(0)`）
列头 y=64：股票名稱@76 / 成交價@188 / 漲跌@280 / 交易量@368 / 持有股數@468 / 平均成本@572。
行 i（i=0..11）y=96+32i：
- 股票名 @76（align2）：`word_496984`≠0 → 青 0x00F0F0，否则浅灰 0xF0F0F0
- 现价 @225（align6）：格式档 `sub_429691`（<15 `%.2f` / <150 `%.1f` / else `%.0f`）
- 涨跌 @305（align6）：`%+.nf`，色随状态
- 停牌 @368 "暫停交易" 或 交易量 @401（`word_49698A`，色随状态）
- 持股 @504（当前玩家 `g_playerShares`，非空才画）
- 均价 @609（`%.2f` `flt_4971A4`）
- 银行余额 @540,24：`'$' + sub_452793(g_playerBank)`

### 涨跌五态（`stockUpDownStatus 0x4295EA`：prev=昨收, cur=现价, up=EC5(prev,+10), down=EC5(prev,-10)）
> 返回值：`cur>prev ? (cur>=up ? 1 : 0) : (cur==prev ? 4 : (cur<=down ? 3 : 2))`
> 即 **0涨 / 1涨停 / 2跌 / 3跌停 / 4平**（`return cur>=up` 为真=1）。买拒 status1、卖拒 status3。

| 状态 | 条件 | 现价字色 | 现价底色 | 涨跌/交易量色 |
|------|------|----------|----------|----------------|
| 0 上涨 | prev<cur<up | 红 0xFF0000 | 无底 | 红 |
| 1 涨停 | cur≥up | 白 0xF0F0F0 | 红底 0xD00100（fillRect 144,rowTop-10,89,20）| 红 |
| 2 下跌 | down<cur<prev | 绿 0x00FF00 | 无底 | 绿 |
| 3 跌停 | cur≤down | 黑 0x101010 | 绿底 0x00D000 | 绿 |
| 4 平盘 | cur==prev | 浅灰 0xF0F0F0 | 无底 | 灰 |

### 持有股數表视图（帧 1，`sub_4297F7(≠0)`）
列头 y=64：股票名稱@71 / 玩家名@168+80p / 保留股份@492 / 累積盈餘@580。
行 i y=96+32i：各玩家持股 `sub_452793(g_playerShares[p][i])` @192+80p；
第一大股东（`specPts+24==p+1`）该行 fillRect 高亮 + 黄字 0xF0F000；
上市股票（`word_496984`≠0）右侧画 保留股份 `specPts+48`@520、累積盈餘 `specPts+40`@609。

### 选框（`sub_4296C1`/`sub_42AAFF`）
`drawRectBorder(surface, 15, 32*row+48, 610, 32, 色)`——悬停/选中行画框（色 = `push 0FFFFFFh` 白；
清除用 `push 0` 黑），旧框擦除；⚠ `word_46CAEC` 只是 640×480 surface 描述符，非颜色。
行命中：x∈[16,625]、y∈[80,464]，row=(y-80)/32+1。

## 走勢圖对话框（`sub_429D65`，帧 2，子模态区 (26,52,613,427)）

- 股票名 @320,82（20 号）
- 上市公司：公司图标 blit `panel.mkf[off_47552C[mode*48+map*12+dir]+..]`@50,107；
  公库 `sub_452793(specPts+40)`@309,123；累積盈餘 `specPts+44 / dword_499084`@309,162；
  經營者 `g_players[specPts+24-1].name`@269,203
- 现价 @453,123 / 涨跌 @453,162 / 交易量 `word_49698A` @589,123 / 漲跌幅 `%.2f`@589,162（=涨跌/昨收×100）
- 5 期均 @453,203（最近 6 个历史点均值）/ 24 期均 @589,203
- 历史高 @589,243 / 历史低 @589,283（遍历 144 期）
- 饼图（持股比例）：圆心(502,365) rx=88 ry=29，`Pie` 比例 = `g_playerShares / 10000`；
  红 0xD00000 / 蓝 0xD0 双色；无持股→整圆
- 折线图（半年内走势）：白笔 0xFFFFFF，x 起 92 步 2，y = `324 - (price-mid)*scale`，
  scale = 109/((max+min)*0.5*0.6) 或 6.48e17/(max-min)（按振幅选档）；从 `turnCounter` 环形遍历 144 点
- 任意点击退出（`sub_451EDB` 还原 + `postModalExit(0)`）

## 休市（`sub_428D01` / `sub_42B2EC`）

`sub_428D01() = dword_4990DC || isSpecialDate(dword_497160)`。命中时 `sub_42B58F` 只画
帧 0 背景 + 列标题 + "本日休市"双层 72 号字（深灰 0x101010@(324,244) 阴影 → 浅灰 0xF0F0F0@(320,240)），
`runModal(sub_42B2EC)` 任意点击退出；不画股票数据/余额/持股，按钮不响应。

## 每日维护（`advanceDay @0x41CF67`，stockTick 前后）

1. `dword_4990DC` 倒计时：bit7 未置且有值→--，==1 时置 bit7；bit7 已置→清 0（事件设的休市天数递减）。
2. 停牌 `byte_496986[36*i]` 递减（>0 则 --）。
3. 新闻 `byte_496987[36*i]`：高 4 位、低 4 位分别递减到 0。
4. `srand(GetTickCount())` → `stockTick()`（0x4291D6，见 pricing-formulas.md）。
5. 交易量 `sub_42915A`：`reserved<=1000`→`volume=reserved`；否则 `volume=reserved×(rand()%2000+1000)/10000`。
6. 月末（日==15）公司结算 `sub_42BA97/sub_431712`；月初 `sub_439BFA` + `dword_499084++`（分红期数）。

## 买卖逻辑（`sub_428D2A` 买 / `sub_428E23` 卖）

- 买（viaBank=a4）：银行→`amount=count×现价`、`bank-=amount`、`volume-=count`、`reserved-=count`；
  现金→`amount=specPts.capital/10000×count`、`sharesLeft-=count`、`cash-=amount`；
  共同：`shares+=count`、均价加权、`updateSpecPtControl`。
- 卖：`shares-=count`（归零则均价清 0）、`amount=count×现价`、`volume+=count`、`reserved+=count`、
  入 bank（a4≠0）或 `dword_499080`（公库）；`updateSpecPtControl`。

## 常量表

| 地址 | 值 | 用途 |
|------|-----|------|
| flt_463F88 | 100.0 | 动量→比例 |
| flt_463F8C/90/94/98 | 5/15/50/150 | 价格档位阈值 |
| dbl_463FB4/FAC/FA4/F9C | 0.01/0.05/0.1/0.5 | 价格 tick（+1.0 顶档） |
| flt_463FBC | 9999.0 | 价格上限 |
| flt_463FC4 | 4097.0 | 全局扰动除数 |
| flt_463FC8 | 1171.0 | 个股扰动除数 |
| flt_463FCC/FD0 | 0.5/2.0 | 动量缩放（远离基准） |
| flt_463FD4 | 8.0 | 非上市上界倍数 |
| flt_463FD8 | 10000.0 | 上市基准=股本/10000 |
| dbl_463FDC | 0.85 | 上市下界倍数 |
| flt_463FE4 | 3.0 | 上市上界倍数 |
| flt_463FE8 | -10.0 | 动量下限 |
| flt_463FEC | 10.0 | 大盘指数系数 |
| flt_463FC0 | 10000.0 | 交易量比例除数 |
| flt_463FF0/FF4 | 15/150 | 价格格式档 |
| flt_46401C | 100.0 | 涨跌幅百分比 |
| flt_464020 | 10000.0 | 饼图分母（总股本） |
| dbl_464024/2C | 2π/π/2 | 饼图角度 |
| dbl_464034/3C/44/4C | 88/502/29/365 | 饼图 rx/cx/ry/cy |
| flt_46405C | 0.5 | 折线中线系数 |
| flt_464080/84 | 324/2.0 | 折线 y 基准 / x 步进 |

## 股票名表

`kStockNames[8][12]`（UTF-8，由 `tools/gen_map_tables.py` 从 `off_47F072` 各组 field0 VA 提取，
8 组 = 4 地图 × 2 模式；主题：台/陆/日/美/科幻/武侠/恐龙/度假）。

## 重写要点

- 平台替换：Win32 GDI（CreatePen/Pie/Ellipse/FloodFill/BeginPaint）→ SDL Surface + 自绘
  折线/饼图（`stock_panel.cpp` 内 Bresenham 线 + 角度扇形填充）；`runModal` → 重写 `runModal` 阻塞事件循环。
- 底图：`panel.mkf[75]` 帧 0/1/2 用 `UiImage::load` + `frame(0/1/2)` + `blitElementOpaque`。
- 每帧全量重绘（不预渲染文字到底图，参照 `help_dialog.cpp`）。
- 颜色：原版 0xRRGGBB 经 `convertColor`→RGB555；本表已给 0xRRGGBB，绘制前转 555。
- 差异：
- 休市事件源 ✅ 2026-09-27：`newsEvt26_stockHaltAll`（0x44B0A0）设 `dword_4990DC = 10` →
  重写 `stockMarketClosed`（`news_dialog.cpp`）；`advanceDay` 递减/bit7（`turn_system.cpp`）；
  `stockTick`/面板判定已消费。`sub_44808A/448544` 时光机快照含该字段（存档专项）。
  - ~~公司分红/月末结算（`sub_42BA97/431712/439BFA`）未实现，走势图"累積盈餘/本月盈餘"暂用 specPts 现值。~~
    **已订正（2026-09-27）**：三者均已实现（分红 `dividend_dialog.cpp` 含亏损分担 / 乐透
    `lottery_dialog.cpp` / 月初结息 `month_settle_dialog.cpp`）；走势图"累積盈餘"= `specPts+40`、
    "本月盈餘"= `specPts+44 / dword_499084` 已按原版口径读取（`stock_market_dialog.cpp`）。
  - 走勢圖公司图标表 `off_47552C` 与 `dword_499084` 除数语义待实机核对。

## 实机校正（第 2 轮）

- **数字列 align**：原版现价/涨跌/交易量/持股/均价/余额用 `drawText align 6` = 右对齐 **且垂直居中**（`sy-=boxH/2`）；表头/名用 `align 2`（居中）。用 align1（仅右对齐、顶对齐）会下移半字。
- **字体阴影**：原版 `setTextFont` 第4参=3（含阴影位1）；有底色现价用2；休市大字用2。重写 `setFont` 第4参直传。
- **按钮下沉**：`highlightRect 0x451B9E` = 按钮矩形内容 memmove 右下平移 + 顶行/左列 `scaleSurfaceChannels` 半暗（凹陷浮雕），**仅按下时**（`pressedBtn`）绘制，悬停无效果，抬起复原。本面板大按钮用 2px 平移 + `kChannelThird`（比热键小按钮的 1px/`kChannelHalf` 更夸张）。通用实现与经验见 `docs/reverse/ui-controls.md` §1 浮雕按钮。
- **走势图折线 scale**：`scale = ((hi-lo)/mid<=0.3) ? 109/(mid*0.6) : 109/(hi-lo)`，`y=324-(price-mid)*scale`，`x=92+2k`；折线框左缘标 hi/lo（drawText 88, y±8, 12号）。
- **走势图饼图**：底椭圆(414,343,591,401)+顶椭圆(414,336,591,394) 立体；持股扇=蓝(COLORREF 0xD00000)、其余=红(0xD0)；无持股全蓝。原版 `CreatePen(0,1,0)` 黑笔 → 两椭圆均有**黑色轮廓**，另有左右刻度短线 (414/590, 365→372) 与持股分界半径线（顶→角度点），重写须在像素填充后补描边（否则缺黑框）。**绘制顺序**须按 GDI：底椭圆(填充+轮廓) 先画 → 顶椭圆(填充+轮廓) 后画覆盖其上半，底椭圆下半露出 = 圆柱侧壁；若先填两个再画两个轮廓，底轮廓上半会污染顶面、侧壁消失（变扁平）。
- **数字框右键取消**：`numberInputDialog` 须处理 `SDL_BUTTON_RIGHT`→requestExit(-1)（原版子模态右键取消）。
- **選框**：行选中/悬停用 `drawRectBorder`（白）。
- **numberInputDialog**：原版子模态保留父画面；重写须捕获进入时 surface 快照叠加，**不可** `renderGameFrame` 重绘地图（否则股市面板被擦）。
- **走势图 blit**：帧2 = **587×375**，`blitElementFullscreen(canvas+36, 26, 52)` 画到全屏 (26,52)；标签预渲染在帧2（运行时全屏坐标=帧内+26,+52），数据值 sub_429D65 全屏坐标。
- **公司图标**：`off_47552C[2][4][12]`（mode/map/costType→帧号，值15/24=股票号特殊）；blit `panel.mkf[75]` 帧 v5 @ (50,107)，图标帧 80×112。帧号映射为推测（表值直用 + 尺寸保护），若显示错图需校正偏移。

## 验证方式

`--shot` 截图四态：股價表（红涨绿跌、上市名青色、现价底色五态）、持有股數表、本日休市、走勢圖（饼+折线）。
实机：点行选股票→買/賣弹数字框→成交后持股/均价/交易量刷新；涨停买失败、跌停卖失败；
周末开面板→"本日休市"；走勢圖按钮→折线随历史推进。
