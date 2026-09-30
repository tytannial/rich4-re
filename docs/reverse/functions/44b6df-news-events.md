# 0x44B6DF newsEvent P2 新聞格研究（case 2）

> 本文档为 `landingEvent` case 2（新聞格 cellType 2）的完整逆向记录：主循环、36 条触发判定、
> 36 条事件效果函数逐条笔记、资源与重写实现要点。判定表以**反汇编**为准（Hex-Rays 分支曾整体读岔）。
> 入口/分派见 `gameplay-map-mechanics.md` §3；case 2..16 总览见 `41982d-p2-events.md`。
>
> 状态（2026-09-26）：✅ **已实现**（`src/app/news_dialog.cpp` + `include/game/app/news_dialog.h`；
> `newsEvent`/`newsEventCheck` 36 条/`g_newsFuncs` 36 效果全实现；调试 `Ctrl+Shift+A`）。
> 语音 ✅ P4-D（业主倒霉台词列3/4、奖励入账金额档）；浮动数字 `sub_44F354` 仍 `TODO`。
> **拍卖 idx 7 ✅ 2026-09-26**（`auction_dialog.cpp` `runAuction`，价款入公库）。
> 高亮（estate+corp）已按原版 `g_pickMask` 形状修复（2026-09-26，见 §7）。

## 0. 总览

```
landingEvent(cellType==2) → newsEvent() 0x44B6DF（返回 0x80，不等待）
newsEvent:
    dword_48C5AC = sub_450441(panel.mkf 句柄, 66)         // SMP 2 帧 440×480，用帧 0
    v0 = allocUiElement(388, 251, 0, 0)                   // 插画元素缓冲
    do {
        v1 = g_newsOrder[g_newsPos]                       // 36 项顺序表（洗牌，0..35）
        v2 = newsEventCheck(v1)                           // 1 = 本次触发
        setTextFont(28, 0xF0F0F0, 0x101010, 3, 0)
        if (v2 == 1) {
            sub_450441(data.mkf, v1 + 441, v0 像素)        // 插画 388×251 RGB555
            blitBackground(panel+12, v0, 25, 44)
            drawText(panel+12, g_newsTitles[g_newsTitleClass[v1]], 24, 8)
            v5 = v1
            g_newsFuncs[v1](0)                            // 效果阶段 0（绘制 + 部分立即生效）
        }
        if (++g_newsPos == 36) g_newsPos = 0
    } while (!v2)                                          // 未触发则继续抽下一个
    blitElementFullscreen(backbuffer, panel+12, 0, 0)      // 全屏呈现
    sub_4544F6(2400)                                       // 阻塞 2400ms
    g_newsFuncs[v5](1)                                     // 效果阶段 1（世界变化）
    sub_456E11(panel)                                      // 释放
```

数据表：

| 数据 | 地址 | 说明 |
|------|------|------|
| `g_newsOrder` | 0x499090 | 36 项顺序表（`newGameInit` 洗牌；重写旧名 `cardShuffle` 应改 `newsOrder`）|
| `g_newsPos` | 0x4990E0 | 当前索引（0..35 循环；重写旧名 `cardShufflePos`）|
| `g_newsFuncs` | 0x475E24 | 36 个效果函数指针（idx → 0x448ECA..0x44B5F5）|
| `g_newsTitleClass` | 0x475EB4 | 标题类 36B：0×6, 1×8, 2×2, 3×2, 4×4, 5×14 |
| `g_newsTitles` | 0x475ED8 | 6 个标题指针：0x4653EC/0x4653F7/0x465400/0x465409/0x465412/0x46541B |
| 插画 | data.mkf[441..476] | 388×251 RGB555 无头位图（194776B）|
| 新闻面板 | panel.mkf[66] | SMP 2 帧 440×480 8bit 索引（`data_offset`=36）；只用帧 0 |
| 文本表 | 0x465424.. | 每条以 `#NNNN`（语音编号前缀）开头，共 #0149..#0184 |

标题（BIG5→UTF-8）：無責任新聞 / 政府公告 / 社會新聞 / 路況報導 / 氣象報導 / 財經新聞。

新闻层临时全局（依函数复用，语义以调用点为准）：

| 全局 | 用法 |
|------|------|
| `dword_48C5AC` | 新闻面板画布（仅 newsEvent 内使用）|
| `dword_48C59C` | **当前事件目标**：多数为 objId（2000+/4000+）；idx 15/18 为 estate index；idx 8/9/10/29 为玩家索引；idx 11-13 为每玩家金额数组 |
| `dword_48C5A0` | **事件参数**：idx 5/19/21 为业主+1（0=无主）；idx 7 为坐标打包 x\|(y<<16)；idx 8/9/10 为奖金金额 |

## 1. newsEventCheck 触发判定（0x448BE2，36 条）

`int newsEventCheck(int idx)`，返回 1 = 本次触发。判定（反汇编核对版）：

| idx | 条件（返回 1）| 说明 |
|-----|--------------|------|
| 0, 1 | `g_jailFlags != 0` | 任一玩家在押 |
| 2, 3 | `g_hospitalFlags != 0` | 任一玩家住院 |
| 4, 5, 15 | 存在 estate level(+26)!=0 或 corp sub(+26)!=0 | 有已建设地块 |
| 6 | 无条件 | |
| 7 | 存在 estate/corp owner(+25)==0 | 有无主地块 |
| 8, 9, 12 | 存在 estate/corp owner(+25)!=0 | 有主地块 |
| 10, 13 | 存在存活玩家且 `g_playerShares[24*玩家 + 2*股] != 0` | 有持股 |
| 11 | 无条件 | |
| 14 | 无条件 | |
| 16 | 存在存活玩家且 `travel(+17)==0` | 行人 |
| 17 | 存在存活玩家且 `travel(+17)!=0` | 开车 |
| 18-27 | 无条件（17 除外）| |
| 28 | 存在 `byte_496986[36*股] != 0` | 有停牌股 |
| 29 | 存在 specPt owner(+24)!=0 且该玩家存活且无状态（`sub_40D73F`）| 有经营中的公司 |
| 30-34 | 无条件 | |
| 35 | 存在 specPt fund(+40) > 10000 | 公司获利 >1 万 |

> `sub_40D73F(p) = g_playerAlive[104*p] && dword_496B9A[26*p]==0`（存活且无任何状态）。

## 2. 36 条事件表与效果函数

事件文本从 0x465424 起（BIG5，下列为解码后原文）；`g_newsFuncs` 顺序即下表地址。

### 無責任新聞（titleClass 0）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 0 | 0149 | 獄中囚犯無罪開釋 | `sub_448ECA` | 文本(y=310)；遍历玩家：`g_jailFlags[i]!=0` → 头像(帧+60, 390, y=328+42n) + `stateFlags` BYTE2=0x80 + 清 `g_jailFlags[i]` | 无 |
| 1 | 0150 | 獄中囚犯延長刑期%d天（3）| `sub_448F45` | 同上但头像帧+48；`stateFlags` BYTE2=(3+旧)&0x7F | 无 |
| 2 | 0151 | 住院中病患提前出院 | `sub_449006` | 文本；`g_hospitalFlags` → BYTE3=0x80 + 清标志 + 头像+60 | 无 |
| 3 | 0152 | 住院中病患延長住院%d天（3）| `sub_449081` | 头像+48；BYTE3=(3+旧)&0x7F | 无 |
| 4 | 0153 | 外星人攻打地球 | `sub_44913D` | 文本 | 收集有建筑地块 → 随机 → `getObjectPosition` + `refreshGameUi(x,y,2)` → `expireAssets(100, 38, 1, -1)` → FLC 531 音效 86 → 遍历 `(alive & 0x40)!=0` 玩家 `hospitalizePlayer(j, 3)` → `sub_41D546()` |
| 5 | 0154 | 外星怪獸襲擊%s\n摧毀建築一棟 | `sub_4492A0` | 收集有建筑 → 随机 → `g_newsTarget=objId`、`g_newsParam=owner+1` → 文本(名称) | `getObjectPosition` + refresh → `demolishEstate(objId, 1)` → FLC 539 音效 84 → 业主台词 `sub_44EF41(owner-1, 2, off_480856[char*27+(rand&1)])` ✅ P4-D（列3/4 随机） |

### 政府公告（titleClass 1）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 6 | 0155 | %s公告地價調漲３０％ | `sub_4494E0` | 随机地块（全部 estate+corp）→ 文本(名称) | estate：同路段（同名）每块 `+28 priceAdd = u16*1.3`；corp：`+34 buildPrice = *1.3`；`rebuildPickBuffer(1)` + 标记 + `highlightBlink()` |
| 7 | 0156 | 公開拍賣%s\n公有土地一處 | `sub_449735` | 收集**无主**地块 → 随机 → `g_newsTarget=objId`、`g_newsParam=x\|(y<<16)` → 文本 | `sub_43BDE5(-1, objId, 1)` **拍卖 UI**（见 §5）|
| 8 | 0157 | 公開表揚第一大地主\n%s獲得%d元獎勵 | `sub_4498B3` | 统计各玩家有主地块数 → 最多者 `g_newsTarget=玩家`；名字去空格 `sub_452946`；`g_newsParam=10000*M`；文本 + 该玩家头像(帧+60, 390, 328) | `refreshGameUi(玩家 sprite)` → `addMoney(玩家, 奖金, 现金=1)` → `sub_41D433(玩家)`（信息面板刷新）→ `sub_44F354(玩家, 金额)` 浮动数字 [TODO P4] |
| 9 | 0158 | 公開補助土地最少者\n%s獲得%d元補助 | `sub_449A8A` | 同上统计 → **最少**者（初值 10000）；`g_newsParam=5000*M`；复用 idx 8 的文本+头像尾（`0x4499C1`）| 同 idx 8 尾 |
| 10 | 0159 | 公開表揚股市第一大戶\n%s獲得%d元獎勵 | `sub_449B9C` | 统计持股总数 → 最多者；`g_newsParam=10000*M`；复用 idx 8 尾 | 同 idx 8 尾 |
| 11 | 0160 | 所有人繳交所得稅５％ | `sub_449C7C` | 文本；setTextFont(24)；每玩家 `g_newsTarget[i] = cash*0.05` → 非 0 时 `"%s繳交%d元"` 逐行(y=346+32n) + 头像+48 | 遍历玩家 `transferMoney(i, -1, 金额, 0)`（入银行）|
| 12 | 0161 | 所有人繳交地價稅５％ | `sub_449DE6` | 每玩家地产估值：estate `priceAdd + priceBase*level`、corp `buildPrice + feeTable[0]*sub` → `×M×0.05` 逐行显示 | 同 idx 11 转账 |
| 13 | 0162 | 所有人繳交證交稅５％ | `sub_44A029` | 每玩家 `Σ(持股数 × flt_496994 现价) × M × 0.05` 逐行显示 | 同 idx 11 转账 |

### 社會新聞（titleClass 2）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 14 | 0163 | %s房屋鬧鬼\n地價下跌３０％ | `sub_44A220` | 随机地块 → 文本 | estate：同路段 `+28 = u16*0.7`；corp：`+34 = *0.7`（共享 `0x44970E`）；`highlightBlink()` |
| 15 | 0164 | %s一處民宅瓦斯爆炸\n房屋失火 | `sub_44A453` | 收集**有建筑 estate**（不含 corp）→ 随机 → `g_newsTarget=estate index`（阶段 1 用 `+2000`）→ 文本 | `refreshGameUi` → `demolishEstate(index+2000, 0)`（降级）→ FLC 527 音效 87 → `sub_4528B9(300)` → 业主台词 |

### 路況報導（titleClass 3）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 16 | 0165 | 豪雨特報\n行人休息一回合 | `sub_44A5D6` | 文本；遍历存活 `travel==0` 玩家 → 头像(帧+36, 390, y=346+12+32n) + `skipMove(+56)=1` | 无 |
| 17 | 0166 | 交通阻塞\n汽車停止一回合 | `sub_44A657` | 同上但 `travel!=0` | 无 |

### 氣象報導（titleClass 4）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 18 | 0167 | %s強烈地震房屋倒塌 | `sub_44A6E0` | 随机地块 → 文本 | estate：同路段 `level--`（连锁 `type!=0` → level/type 清 0）；corp：`sub--`（1→0 时 `type=0` + `sub_40DFFA()` 强制退房）；`highlightBlink` + refresh(0,0,1) + 延时 500 |
| 19 | 0168 | %s山洪爆發土地流失 | `sub_44A91E` | 随机地块 → `g_newsTarget=objId`、`g_newsParam=owner+1` → 文本 | `demolishEstate(objId, 1)`（夷平归公）+ 标记 + `highlightBlink` + refresh + 延时 300 + 业主台词 |
| 20 | 0169 | 超級颱風侵襲%s\n多處房屋受損 | `sub_44AB2C` | 随机地块 → 文本 | `expireAssets(100, 6, 0, -1)` → FLC 534 音效 89 → 延时 500 |
| 21 | 0170 | 龍捲風侵襲%s\n摧毀房屋一棟 | `sub_44AC99` | 随机地块 → `g_newsParam=owner+1` → 文本 | `demolishEstate(objId, 0)` → FLC 535 音效 88 → 延时 300 → 业主台词（共享 `0x44AB10`）|

### 財經新聞（titleClass 5）

| idx | # | 文本 | 函数 | 阶段 0 | 阶段 1 |
|-----|---|------|------|--------|--------|
| 22 | 0171 | 銀行擠兌停止放款１５天 | `sub_44AE89` | 文本；存活玩家 `bankFinanceFlags(+60)=15` | 无 |
| 23 | 0172 | 銀行加發１０％儲金紅利 | `sub_44AEDB` | 文本；每玩家 **无贷款**（`loan==0`）时 `addMoney(bank*0.1, 入银行)` + `"%s得到%d元"` 逐行 | 无 |
| 24 | 0173 | 股市低迷不振重挫崩盤 | `sub_44B00A` | 文本；`stockNews[12] = 1`（跌）→ `stockNewsApply(0)`（0x429040 全市场）| 无 |
| 25 | 0174 | 股市氣勢如虹全面上漲 | `sub_44B055` | 文本；`stockNews[12] = 16`（涨）→ `stockNewsApply(0)` | 无 |
| 26 | 0175 | 股市暫停交易１０天 | `sub_44B0A0` | 文本；`dword_4990DC = 10`（全市场停市天数）| 无 |
| 27 | 0176 | %s股票暫停交易１０天 | `sub_44B0D1` | 随机股 `rand%12` → 名称/文本；`byte_496986[36*股]=15`；现价=昨收（`flt_496994 = flt_496990`）；`stockHistory[144*股+turn]` 记录 | 无 |
| 28 | 0177 | %s股票恢復上市交易 | `sub_44B1A3` | 收集停牌股（`byte_496986!=0`）→ 随机 → 名称/文本 → `byte_496986=0` | 无 |
| 29 | 0178 | %s違法超貸\n經營者%s坐牢５天 | `sub_44B25B` | 收集有主 specPt → 随机 → 公司名+业主名 → `g_newsTarget=owner-1` | `refreshGameUi(sprite)` → `resolvePenaltyTarget`（`sub_441210`：免罪卡21→取消返回-1；嫁祸卡19→换目标）→ `jailPlayer(玩家, 5)` **✅ 2026-09-26 接入**（含淘汰业主防御） |
| 30 | 0179 | %s工廠排放污水\n罰款10000元 | `sub_44B374` | 随机 specPt → 文本；公司 `fund(+40) -= 10000`、`(+44) -= 10000`；`stockNo(+25)<12` → `stockNews=3`（跌）→ `stockNewsApply(stockNo+1)` | 无 |
| 31 | 0180 | %s海外投資\n獲利20000元 | `sub_44B419` | 随机 specPt → 文本；fund `+40 += 20000`、`+44 += 20000`；`stockNews=48`（涨）→ apply | 无 |
| 32 | 0181 | %s海外投資\n虧損20000元 | `sub_44B4A8` | 同 31 但 `-20000`；`stockNews=4`（跌）→ apply | 无 |
| 33 | 0182 | %s違規開發山坡地\n罰款10000元 | `sub_44B53F` | 自选 specPt + 文本 #0182 压栈后 JUMP `0x44B3AD`（与 idx 30 共享罚款段 -10000 + 跌 3）| 无 |
| 34 | 0183 | %s製造噪音公害\n罰款5000元 | `sub_44B57D` | 随机 specPt → 文本；fund `-5000` → JUMP `0x44B3E7`（共享 idx 30 的股价跌 3 尾）| 无 |
| 35 | 0184 | %s獲利調高一倍 | `sub_44B5F5` | 收集 `fund(+40)>10000` 公司 → 随机 → 文本；`+40 = 2*旧`、`+44 += 旧`；`stockNews = 16*(旧/10000)`（涨）→ apply | 无 |

### 实机反馈修复（2026-09-26）

- **相位1 未切回场景**：各效果函数原版先 `refreshGameUi` 重绘地图再播 FLC；重写单 Surface 需
  显式 `renderGameFrame + renderFrame`（`newsHandler` phase1 前）。否则外星人/坐牢动画画在
  事件面板上（与 fateEvent 同步修复）。
- **高亮前重建命中区**：`playHighlightBlink` 在 `captureHighlightShapes` 前 `renderGameFrame`，
  避免部分事件路径未重绘导致形状快照为空 → 地块不闪。
- **idx 29 坐牢**：接入 `resolvePenaltyTarget`（免罪/嫁祸卡，见 §5.1 fate 文档）+ 淘汰业主
  防御（`alive==0` 跳过，避免永不递减的 stateFlags）。
- **资金日志**：`applySpecPtChange`（idx 30-34 公司 fund）、`rewardPlayerPhase1`（8/9/10）、
  `taxPhase1`（11-13）、`evt23`（银行红利）均输出前后余额——**注意 idx 30-34 改动的是公司
  `fund(+40/+44)` 与股价，不是玩家资金**（原版 0x44B419 反编译确认）。
- **新闻无神明修正（IDA 证据）**：`sub_44B896`（命运/事件的幸运判定）15 个调用点全部位于
  命运效果函数（0x44C0E8..0x44D80F）；新闻的 `addMoney`（0x41D3F4）与 `transferMoney`
  无幸运逻辑 → 新闻资金事件不受携带神明影响（与命运不同，勿套用）。
  神明修正机制详见 `44db81-fate-events.md` §3.1/§3.2。
- **高亮上偏 40px（2026-09-26 修复）**：`captureHighlightShapes` 沿用原版 pickBuffer 局部坐标
  `it.y-40`，但重写直接画屏幕（缺少 `highlightBlink 0x451985` 的 `+40` 回贴换算）→ 整体上移。
  修复：锚点用 `it.y`；证据链见 `frame-anchor.md` §5.3（写入端 0x409B18、回贴端 0x451985）。

## 3. 股票新闻字段（提交 5 依据）

| 符号 | 重写字段 | 说明 |
|------|---------|------|
| `byte_496986[36*股]` | `GameState.stockHalted[股]` | 停牌剩余（idx 27 置 15、28 清 0；`advanceDay` 每日递减）|
| `byte_496987[36*股]` | `GameState.stockNews[股]` | 新闻冲击：**高 4 位 != 0 → 涨；否则跌**；`stockNewsApply` 应用后取 `flt_49699C=+10.0/-10.0` 计算新价 |
| `flt_496990 + 9*股` | 昨收价 | |
| `flt_496994 + 9*股` | 现价 | |
| `flt_49699C + 9*股` | 动量（±10）| |
| `g_stockHistory[144*股+turn]` | `stockHistory` | 历史记录 |
| `dword_4990DC` | 待确认 | 全市场暂停交易天数（idx 26 = 10）|

`sub_429040(a1)` = **stockNewsApply**：a1==0 应用全部 12 支、否则第 a1-1 支；
高 4 位 != 0 → `+10.0`，否则 `-10.0` → `stockPriceUpdate(昨收, 动量)` → 现价 + 历史。

## 4. 资源与绘制

| 项 | 细节 |
|----|------|
| panel.mkf[66] | SMP：`data_offset=36`，count=2，两帧均 440×480（8bit 索引+局部调色板）。`newsEvent` 只用帧 0 作画布（`+12` 处为帧头/像素）|
| data.mkf[441+idx] | 388×251 RGB555 无头位图（194776B），blit 到 panel (25,44) |
| 标题 | `setTextFont(28, 0xF0F0F0, 0x101010, 3, 0)` @ (24,8) |
| 效果文本 | 默认字体 @ (24,310)；逐玩家行 y 起点 346、行距 32 |
| 玩家头像 | `g_pieceSprites[13*玩家] + 帧`（+36=帧3 豪雨/交通、+48=帧4 各类列表、+60=帧5 释放/奖励）@ x=390 |
| 阻塞时长 | 2400ms（`sub_4544F6`）；部分函数内部另有 `sub_4528B9(300/500)` 阶段 1 延时 |

## 5. 拍卖（idx 7，**✅ 2026-09-26 已实现**）

`runAuction(a1=-1, a2=objId, a3=1)` 0x43BDE5：土地拍卖 UI（重写 `src/app/auction_dialog.cpp`）。
- 计算底价：`M × (int)(base × (1 + level×0.5))`（estate+28 / corp+34 基价，+26 等级）——
  **订正**：非旧记 `(priceAdd+priceBase*level)`，实测公式为基价×(1+等级×0.5)；
- panel.mkf[26]（184 帧）、逐玩家头像/现金、`runModal(0x43A2DD)` 竞价（7 按钮 PASS/+100..+10000/
  Give up + AI 出价公式 0x439F0D）、获胜者 `owner=胜者` + `transferMoney(胜者-1, a1, 成交价, 0)`
  （a1=-1 → 入公库）+ 地权期限；
- 同时被魔法屋惩罚 11（seller=玩家）调用（破产清算 `sub_40CD87`、卡片 `sub_443225` 复用待查）。
- 全部细节/帧表/状态机见 **`43bde5-auction.md`**。

## 6. 通用辅助 / 共享尾部

| 符号 | 语义 | 重写 |
|------|------|------|
| `sub_452946(dst, src)` | 复制字符串并**去掉空格**（用于 sprintf 名字）| 以 `std::string` 等价实现 |
| `demolishEstate(objId, mode)` | 0=降一级 / 1=夷平归公 / 2=拆建筑留地；corp 归零时 `sub_40DFFA` 强制退房 | `demolishAtObjId` ✅ |
| `sub_40DFFA` | 所有住宿中玩家 `stateFlags` BYTE0=0x80（强制退房）| `forceHotelCheckout`（提交 3 接入）|
| `expireAssets(radius, flags, dump, payer)` | 视野内破坏（flags&2 estate / &4 corp / &0x20 事件槽）| ✅（`economy.cpp`）|
| `stockNewsApply(a1)` | 股价新闻应用 | 重写 `stockNews` 字段 + 接入 `stockTick`（提交 5）|
| `sub_41D433(玩家)` | 刷新右侧玩家信息面板 | `drawPlayerInfoPanel` 等价 ✅ |
| `sub_41D546()` | 面板/视口复位 | `TODO`（提交 3 核对）|
| `sub_44EF41` / `sub_44F354` | 角色台词语音 / 浮动数字 | 语音 ✅ P4-D；浮动数字 `TODO` |
| `sub_441210` | 坐牢目标判定（免罪卡/嫁祸卡）| 本轮简化为返回原玩家，卡片分支 `TODO(P4)` |

`g_newsFuncs` 共享尾部：`0x44970E`（corp 价格调整 + highlight）、`0x449725`（highlightBlink）、
`0x44972A`（epilogue）、`0x4499C1`（idx 8 奖励显示行）、`0x44A01E`（税费返回）、
`0x44B3AD`（specPt 罚款 sprintf 段）、`0x44B3E7`（股价跌 3 段）、`0x44B402`（stockNewsApply 调用）。

## 7. 重写实现要点（已实现 2026-09-26）

- `src/app/news_dialog.cpp` + `include/game/app/news_dialog.h`：
  - `newsEvent(Application&)`：加载 panel[66] → 抽事件（`newsOrder`/`newsPos`，未触发 ++pos 继续，
    36 次防御）→ `runModal` 绘制（panel 帧 0 不透明 + 插画 (25,44) + 标题 (24,8)）→ 效果(0)
    → 2400ms → 效果(1) → `requestExit`（返回 0x80）；
  - `newsEventCheck(const GameState&, int idx)` 36 条（§1）；
  - `kNewsTexts[36]` / `kNewsTitles[6]` / `kNewsTitleClass[36]` / `kNewsFuncs[36]`；
  - 事件私有目标存 `NewsCtx.targetObjId`/`targetParam`（对应原版 `g_newsTarget`/`g_newsParam`）。
- 辅助：`focusView`/`resetView`（视口对准/复位，`manualView`）、`newsWait`（音频续喂延时）、
  `forceHotelCheckout`（0x40DFFA）、`blinkHighlight`（estate/corp 列表 → `playHighlightBlink`）、
  `blinkSingleObj`（单块，原版 markPickBuffer(单块)）。
- `GameState.cardShuffle/cardShufflePos` → `newsOrder/newsPos`（存档偏移不变）；
  `landingEvent` case 2 → `newsEvent(app); return 0x80;`
- `stockNewsApply`（0x429040）实现于 `stock_system.cpp`：`stockNews` 高 4 位 = 涨（+10）、
  否则跌（-10）→ `stockPriceUpdate(昨收, 动量)` → 现价 + 历史（`turnCounter-1` 环形槽）。
- 差异：语音 ✅ P4-D、浮动数字 TODO；idx 7 拍卖桩；`sub_41D433` 面板刷新省略
  （重写每帧全量重绘）。
- **实机修正（2026-09-26）**：
  ① 高亮恢复原版形状/落点——原版 `rebuildPickBuffer` 0x409B18 用 `g_pickMask`（=map.mkf[26]）
  **estate 帧 `(8-rot-dir)&1`（0/1）、corp 帧 `+2`（2/3）**，落点 `(drawListX, drawListY-40)`；
  重写拾取为体感改用帧 4（51×51 圆）且漏了 -40 → 亮斑偏下压道路、形状过小；
  现 `mapHitRegions` 另存 `hl*`（原版形状/锚点），`captureHighlightShapes` 提取
  `highlightShapes` 快照（原版 pickBuffer 语义：启动时固定，拆除后仍亮——新闻 idx 19/21）；
  corp 单块高亮同步支持（`highlightCorps`）。
  **2026-09-26 二修**：idx19 山洪/idx18 地震命中**无主地块**（或归公后变无主）时完全不闪——
  重写无主分支只填拾取字段、`hlShape` 留空；原版 0x409B18 对无主 estate/corp 同样写缓冲。
  已补两处 `hlShape/hlFrame/hlAnchor`（帧 0/1、+2），详见 `40e033-map-objects.md` 修正段。
  ② 银行停留「暫停放款」提示 8000→**1500ms**（原版 0x800005DC = 1500+左移 100px，
  旧文档误读 8000；重写 showMessage 不支持负值左移，故传正 1500）。
  ③ 扣钱类新闻（三税/罚款）**不走**神明调整：原版 `sub_41D2C6` 纯转账、`newsEvt11/12/13/30-35`
  调用链无 `sub_41D709`；重写 `transferMoney` 同样无神明逻辑（神明只作用于过路费/设施收费）。
  ④ **金额入账后右侧资金面板不刷新（2026-09-26）**：原版 `transferMoney 0x41D2C6` /
  `addMoney 0x41D3F4` 尾对当前玩家调 `refreshPlayerPanelFor 0x41D433`（`g_modalDepth<=1`
  立即重绘面板），且各效果函数尾部普遍有 `refreshGameUi`；重写只等模态退出后主循环重绘，
  消息/动画叠加期间停留在旧值。修复：① `game_panel.cpp` 导出 `refreshPlayerPanelFor`
  （depth 守卫+临时切 currentPlayer）；② 转账/加钱函数尾调用；③ `newsHandler`/`fateHandler`
  相位1 效果执行后补 `renderGameFrame + renderFrame`。

## 8. 待深入清单

- [x] `sub_43BDE5` / `sub_43A2DD` 拍卖 UI —— **2026-09-26 完成**（`auction_dialog.cpp`，
      见 `43bde5-auction.md`）
- [ ] `sub_441210` 卡片分支（`sub_4413AD`/`sub_444BB2`/`sub_44476A`）→ E 批逆向中
- [x] `dword_4990DC`（idx 26 暂停交易天数）消费点与 `advanceDay` 递减 —— **已实现**
      （`newsEvt26 → st.stockMarketClosed = 10`；`advanceDay` 递减/bit7：`turn_system.cpp`；
      `stockTick`/面板判定消费）
- [x] `stockNewsApply` 与重写 `stockTick` 的精确对齐 —— 已实现（`st.stocks[i][7]` 动量字段）
- [x] 角色台词表 `off_480856`（业主倒霉台词，idx 5/15/19/21）✅ P4-D（`playUnluckyLine` 列3/4 随机）
