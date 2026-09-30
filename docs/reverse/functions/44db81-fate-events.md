# 0x44DB81 fateEvent P2 命運格研究（case 3）

> 本文档为 `landingEvent` case 3（命運格 cellType 3）的完整逆向记录：主循环、37 项触发判定、
> 49 条事件效果（0..32 通用 + 33..48 地图专属坐牢）、幸运判定、卡片联动（生日/免罪/嫁祸）
> 与重写实现要点。判定以**反汇编**为准（Hex-Rays 曾把相位0 头像偏移/共享尾部读岔）。
> 入口/分派见 `gameplay-map-mechanics.md` §3；case 2..16 总览见 `41982d-p2-events.md`。
>
> 状态（2026-09-26）：✅ **已实现**（`src/app/fate_event.cpp` + `include/game/app/fate_event.h`；
> `fateEvent`/`fateEventCheck` 37 项判定 + 49 效果全实现；调试 `Ctrl+Shift+z`）。
> 公共辅助提取至 `event_common.{h,cpp}`（新闻/命运共用）；卡片联动见 §5。
> 语音 ✅ P4-D（2026-09-30 列勘误：id9 賣股 = `off_48085E` 列5 expr2、逃过坐牢 = 列0 expr0、
> 载具 id10 列3/4 随机（id11 列3 固定））；byte_497324/5 统计并入 `misc8A`。

## 0. 总览

```
landingEvent(cellType==3) → fateEvent() 0x44DB81（返回 0x80，不等待）
fateEvent:
    dword_48C5E0 = sub_450441(panel.mkf 句柄, 66)         // SMP 2 帧 440×480，用帧 0
    v1 = allocUiElement(388, 251, 0, 0)                   // 插画元素缓冲
    do {
        v5 = g_fateOrder[g_fatePos]                       // 37 项顺序表（洗牌）
        v2 = fateEventCheck(&v5)                          // 1 = 本次触发；可改写 v5（载具变体）
        setTextFont(28, 0xF0F0F0, 0x101010, 3, 0)
        if (v2 == 1) {
            idx = (v5 >= 33) ? 4*mapIndex + v5 : v5       // 地图专属索引 33..48
            sub_450441(data.mkf, word_475FB4[idx], v1 像素)  // 插画 388×251 RGB555
            blitBackground(panel+24, v1, 25, 44)
            funcs_44DC44[idx](0)                          // 效果阶段 0（选目标 + 文本/头像）
        }
        if (++g_fatePos == 37) g_fatePos = 0
    } while (!v2)                                          // 未触发则继续抽下一个
    blitElementFullscreen(backbuffer, panel+24, 0, 0)
    sub_4544F6(1600)                                       // 阻塞 1600ms
    funcs_44DC44[idx](1)                                   // 效果阶段 1（数值/状态/高亮）
    sub_4528B9(800)                                        // 再停 800ms
    sub_456E11(panel)                                      // 释放
```

数据表：

| 数据 | 地址 | 说明 |
|------|------|------|
| `g_fateOrder` | 0x496B38 | 37 项顺序表（`newGameInit` 0x4074C4 → `sub_44BAEA` **洗牌**；非固定顺序）|
| `g_fatePos` | 0x4990B4 | 当前索引（0..36 循环）|
| `g_fateOrder` 值域 | 0..36 | 事件 id；37 次内全部出现一次 |
| `word_475FB4` | 0x475FB4 | 插画索引 49 项 u16：0..36 通用（33..36 被 map0 复用）+ 37..48（map1..3 的 33..36）|
| `funcs_44DC44` | 0x475EF0 | 效果函数指针 49 项（0x44BE16..0x44DB53）|
| `fateEventCheck` | 0x44BB4B | 触发判定 + 载具改写（§1）|
| 插画 | data.mkf[477..516] | 388×251 RGB555 无头位图（194776B）；索引可重复（497/498/501/505/506/507/510）|
| 命运面板 | panel.mkf[66] | SMP 2 帧 440×480（与新闻共用，帧 0）|
| 文本表 | 0x465915..0x465DBF | 49 条 `#0185..#0233`（含 `%d`），`#NNNN` 前缀触发语音 |

文本与头像布局：`drawText(panel+24, text, 24, 330, 0)`；`blitElementToCanvas(panel+24,
g_pieceSprites[13*cur] + N, 390, 344)`（N=12×帧号：+24=帧2 / +36=帧3 / +48=帧4 / +60=帧5）。

## 1. fateEventCheck 触发判定（0x44BB4B）

`int fateEventCheck(int* v)`，返回 1 = 本次触发，可原地改写 `*v`（载具变体）。判定（反汇编核对版）：

| v | 条件（返回 1）| 改写 |
|---|--------------|------|
| 0 | 存在自有 `owner==cur+1 && level(+26)!=0` 的 estate | — |
| 1 | 存在自有 `owner==cur+1 && level(+26)==0` 的 estate | — |
| 2/3/4 | 无条件 | — |
| 5 | 其他玩家 `cardBagCount > 0` 之和非 0 | — |
| 6/7 | 无条件 | — |
| 8/9 | 自己有股票持仓（12 支任一非 0）| — |
| 10 | `travel==1(机车)` 或 `2(汽车)` | 汽车 → 11 |
| 11 | `travel==1` 或 `2` | 机车 → 10 |
| 12 | `travel<=1` | 机车 → 13 |
| 13 | `travel<=1` | 步行 → 12 |
| 14 | `travel<=2` | 机车 → 15、汽车 → 16 |
| 15 | `travel<=2` | 步行 → 14、汽车 → 16 |
| 16 | `travel<=2` | 步行 → 14、机车 → 15 |
| 17..32 | 无条件 | — |
| 33..36 | 无条件 + `gameMode(word_4991B6)==0` | — |
| >=37 | 无条件（实际不存在）| — |

> 载具语义：`travel` 0=步行 / 1=机车 / 2=汽车（`Player.travel`，原 `g_playerVehicle`）。
> 33..36 为地图专属事件，仅在基础 4 图（gameMode==0）触发；时空之旅 4 图无专属事件。

## 2. 49 条事件表与效果

金额均 ×`g_moneyMul`；`ph0` = 阶段 0（目标/文本/头像），`ph1` = 阶段 1（结算）。
幸运判定列 `(a3,a4)` 为原版 `sub_44B896` 参数（§3）。

### 通用（0..32）

| id | 文本 | kind | 金额/天数 | 判定 | 效果（ph1）|
|----|------|------|-----------|------|------|
| 0 | 強制拆除房屋一棟 | HouseDemolish | 补偿 `升级价(+30)×level` | — | 随机自有有房地块：补偿入现金、level=0、type=0、`focusView`+单块高亮 |
| 1 | 強制徵收土地一處 | LandRequisition | 补偿 `购地价(+28)` | — | 随机自有无房地块：补偿、owner=0、到期日=0、小地图刷新+高亮 |
| 2 | 人頭被盜用冒貸%d元 | FakeLoan | 10000 | (0,1) | `loan += amt` + `insurancePayout`（code 2 加倍、code 1 免）|
| 3 | 支票跳票\n銀行拒絕往來一個月 | CheckBounce | — | (0,1) | code 1 免；否则 `bankRefuseDays += 30`（code 2 无加倍）|
| 4 | 侵入銀行電腦\n挪用其他人存款%d％ | BankHack | — | — | 每个其他存活玩家：银行余额 10% → 我（`transferMoney(i,cur,amt,4)`）|
| 5 | 今天是你生日\n向每人收取一張卡片 | Birthday | — | — | 逐个有卡者：人类弹选卡/道具面板；AI 随机取卡（无卡取道具）|
| 6 | 強迫出國觀光%d天 | Travel | 3 | (1,1) | code 1 免；code 2 天×2；`resolvePenaltyTarget` → 状态 BYTE1 + FLC 558 |
| 7 | 被外星人綁架%d天 | Abducted | 3 | (1,1) | 同上，FLC 533、BYTE1 bit6 置位 |
| 8 | 股票違約交割損失股票%d％ | SellStockPct | 10% | (0,1) | code 1 免；否则每支卖 10%（`sellStock(...,false)`；code 2 无加倍）|
| 9 | 變賣所有股票求現 | SellAllStock | — | (0,1) | code 1 免；否则全部 `sellStock(...,true)` |
| 10 | 機車被偷遺失 | VehicleLost | — | (1,1) | code 1 免；否则 `travel=0` + 回收计数 + 重载行走 |
| 11 | 汽車撞電線桿全毀 | VehicleLost | — | (1,1) | 同 10 |
| 12 | 掉進水溝就醫%d天 | Hospitalize | 3 | (1,1) | code 1 免；code 2 ×2；`resolvePenaltyTarget` → damage + 住院 |
| 13 | 騎機車摔傷住院%d天 | Hospitalize | 3 | (1,1) | 同 12（共享代码）|
| 14 | 行人闖越馬路罰款%d元 | Fine | 3000 | (0,1) | 罚款（§3 通用罚款流程）|
| 15 | 騎機車未戴安全帽\n罰款%d元 | Fine | 3000 | (0,1) | 同 14 |
| 16 | 汽車超速罰款%d元 | Fine | 3000 | (0,1) | 同 14 |
| 17 | 請所有人吃大餐\n花費%d元 | Fine | 6000 | (0,1) | 同 14 |
| 18 | 亂丟垃圾罰款%d元 | Fine | 600 | (0,1) | 同 14 |
| 19 | 你家小狗亂大小便\n罰款%d元 | Fine | 1500 | (0,1) | 同 14 |
| 20 | 在路邊撿到%d元 | Gain | 1000 | (0,0) | 入现金（code 1 作废、code 2 加倍）|
| 21 | 在路邊撿到%d元 | Gain | 2000 | (0,0) | 同 20 |
| 22 | 在路邊撿到%d元 | Gain | 3000 | (0,0) | 同 20 |
| 23 | 遺失錢包損失%d元 | Fine | 1000 | (0,1) | 同 14 |
| 24 | 遺失錢包損失%d元 | Fine | 2000 | (0,1) | 同 14 |
| 25 | 意外獲得遺產%d元 | Gain | 10000 | (0,0) | 同 20 |
| 26 | 被倒會損失%d元 | Fine | 8000 | (0,1) | 同 14 |
| 27 | 發票中獎%d元 | Gain | 4000 | (0,0) | 同 20 |
| 28 | 發票中獎%d元 | Gain | 6000 | (0,0) | 同 20 |
| 29 | 發票中獎%d元 | Gain | 8000 | (0,0) | 同 20 |
| 30 | 付保險金%d元 | Fine | 5000 | (0,1) | 同 14 |
| 31 | 領取保險金%d元 | Gain | 5000 | (0,0) | 同 20 |
| 32 | 變賣所有卡片道具 | Confiscate | 按价格表 | (1,1) | code 1 免；否则 `confiscateItems+confiscateCards` → 点券 |

### 地图专属（33..48，全部坐牢）

| tableIdx | 地图 | 天数 | 文本 |
|----------|------|------|------|
| 33 | map0 | 3 | #0218 酒醉大鬧警局坐牢%d天 |
| 34 | map0 | 5 | #0219 防礙風化坐牢%d天 |
| 35 | map0 | 7 | #0220 走私毒品坐牢%d天 |
| 36 | map0 | 9 | #0221 販賣大補帖坐牢%d天 |
| 37 | map1 | 3 | #0222 酒醉大鬧警局坐牢%d天 |
| 38 | map1 | 5 | #0223 違法聚眾示威坐牢%d天 |
| 39 | map1 | 7 | #0224 獵捕保育動物坐牢%d天 |
| 40 | map1 | 9 | #0225 盜賣國寶坐牢%d天 |
| 41 | map2 | 3 | #0226 誘騙未成年少女拘役%d天 |
| 42 | map2 | 5 | #0227 防礙風化坐牢%d天 |
| 43 | map2 | 7 | #0228 走私毒品坐牢%d天 |
| 44 | map2 | 9 | #0229 施放毒氣坐牢%d天 |
| 45 | map3 | 3 | #0230 非法持有槍械坐牢%d天 |
| 46 | map3 | 5 | #0231 毆打警員坐牢%d天 |
| 47 | map3 | 7 | #0232 獵捕保育動物坐牢%d天 |
| 48 | map3 | 9 | #0233 盜賣國家機密坐牢%d天 |

判定统一 (1,1)：code 1 免（消息 + 台词 `off_48084A` 列0 expr0，0x44D873 共享块）；code 2 天数 ×2；
`resolvePenaltyTarget` → `jailPlayer(days)`。

## 3. 幸运判定 sub_44B896（0x44B896）

```
sub_44B896(a3, a4)：以 luck 决定 0（无事）/1（幸运）/2（倒霉），并写消息 byte_48C5B8
  a3=1 → luckC  文本：1=「%s保佑\n\n逃過此劫！」(0x4658e7) / 2=「%s作祟\n\n倒霉加倍！」(0x4658d4)
  a4=1 → luckB  文本：1=「%s保佑\n\n免付罰金！」(0x4658c1) / 2=「%s作祟\n\n罰金加倍！」(0x4658ae)
  默认 → luckB  文本：1=「%s作祟\n\n獎金作廢！」(0x46589b) / 2=「%s保佑\n\n獎金加倍！」(0x465888)
分档：luck>100 → 固定；0..50 → 0；50..100 → rand&1 后取 1 或 2（默认路径取 2 或 0）；<0 → 反档
%s = off_47ED76[g_playerAttachedObj]（携带神明名，重写 kObjectNames[cellTableIdx]）
```

- 惩罚类（罚款/住院/坐牢）用 a4=1 或 a3=1：code 1 = 免灾；code 2 = 加倍。
- 奖金类（捡到/中奖）用默认：code 1 = 作废；code 2 = 加倍（语义相反，勿混）。
- 通用罚款流程（Fine）：code 1 → 消息 + 免；code 2 → 消息 + ×2；
  `transferMoney(cur, -1, amt, 0)`（付公庫）→ `insurancePayout(cur, amt)`（保险期内由保险公司理赔）。

### 3.1 幸运值来源 = 携带神明（IDA 证据，2026-09-26 复核）

```
attachObject 0x40EAD7（踩中神明附身）：
    g_playerAttachedObj[104*p] = type（1..15）
    g_luckA[52*p] += g_luckTblA[type-1]   // 表 0x4749E2（18×i16）
    g_luckB[52*p] += g_luckTblB[type-1]   // 表 0x474A06
    g_luckC[52*p] += g_luckTblC[type-1]   // 表 0x474A2A
deleteMapObject 0x40E14D：拥有者属性回退时 -= 同表
```

表数值（get_bytes 0x4749E2 核对，与重写 `kLuckA/B/C` 一致）：

| type | 名称 | luckA | luckB | luckC | 命运获奖（luckB，默认判定） | 命运受罚（luckC，(1,1)） |
|------|------|-------|-------|-------|------------------------------|--------------------------|
| 1 | 小財神 | -100 | **+100** | 0 | 50% 翻倍（50..100 随机） | — |
| 2 | 大財神 | -200 | **+150** | 0 | **必翻倍**（>100） | — |
| 3 | 小福神 | -100 | 0 | **+100** | — | 50% 逃过 |
| 4 | 大福神 | -200 | 0 | **+150** | — | **必逃过** |
| 5 | 小窮神 | +100 | **-60** | 0 | **必作废**（<0） | — |
| 6 | 大窮神 | +200 | **-100** | 0 | 必作废 | — |
| 7 | 小衰神 | +100 | 0 | **-60** | — | **必加倍**（<0） |
| 8 | 大衰神 | +200 | 0 | **-100** | — | 必加倍 |
| 9 | 天使 | -100 | +60 | +60 | 50% 翻倍 | 50% 逃过 |
| 10 | 惡魔 | +100 | -60 | -60 | 必作废 | 必加倍 |
| 12 | 土地公 | -500 | 0 | 0 | —（luckA 用于月度排名评分） | — |
| 15 | 死神 | +1000 | -200 | -200 | 必作废 | 必加倍（另有拆房/记债效果） |
| 其余（11/13/14/16/17） | 惡犬/禮物/寶箱/路障/地雷/炸弹 | 0 | 0 | 0 | — | — |

> 与收租神明修正（`applyGodRentModifier` 0x41D709：槽1 小財神=租金减半 / 槽2 大財神=免付 /
> 槽5 小窮神=+50% / 槽6 大窮神=加倍）是**两套独立系统**：收租按神明槽直判，命运/事件按
> luck 值分档（sub_44B896）。**新闻事件无神明修正**：`sub_44B896` 的 15 个调用点全部位于
> 命运效果函数（0x44C0E8..0x44D80F，xref 核对），且 `addMoney` 0x41D3F4 无幸运逻辑。

### 3.2 sub_44B896 调用点参数（xref 0x44B896，15 处）

| 调用地址 | 事件（owner 函数） | (a3,a4) | 语义 |
|----------|-------------------|---------|------|
| 0x44C184 | id2 人頭被盜用冒貸（0x44C0E8） | (0,1) | 罰金：免付/加倍 |
| 0x44C280 | id3 支票跳票（0x44C229） | (0,1) | 免/照罚（code2 无加倍） |
| 0x44C65C | id6 強迫出國（0x44C5D8） | (1,1) | 逃过/加倍 |
| 0x44C771 | id7 被外星人綁架（0x44C6ED） | (1,1) | 逃过/加倍 |
| 0x44C874 | id8 股票違約交割（0x44C7EF） | (0,1) | 免/照卖（code2 无加倍） |
| 0x44C97C | id9 變賣所有股票（0x44C91F） | (0,1) | 免/照卖 |
| 0x44CAA3 | id10 機車被偷（0x44CA46） | (1,1) | 逃过/加倍 |
| 0x44CBB0 | id11 汽車撞毀（0x44CB53） | (1,1) | 逃过/加倍 |
| 0x44CCD8 | id12 掉進水溝就醫（0x44CC53） | (1,1) | 逃过/加倍 |
| 0x44CE39 | id14 行人闖越馬路（0x44CD99） | (0,1) | 免付/加倍（disasm 0x44CE35 push1/CE37 push0） |
| 0x44CFE3 | id15 未戴安全帽（0x44CF1E） | (0,1) | 免付/加倍（16/19/23/24/26/30 共享） |
| 0x44D176 | id17 請所有人吃大餐（0x44D0D6） | (0,1) | 免付/加倍（18 共享） |
| 0x44D2AD | id20 路邊撿到（0x44D224） | (0,0) | 獎金：作废/加倍（21/22/25/27/28/29/31 共享） |
| 0x44D6D4 | id32 變賣所有卡片道具（0x44D677） | (1,1) | 逃过/加倍 |
| 0x44D80F | id33 坐牢（0x44D783） | (1,1) | 逃过/加倍（disasm 0x44D80B push1/D80D push1；34..48 共享） |

无判定事件：id0/1（拆房/徵收，0x44BE16/0x44BFB1）、id4（银行黑客）、id5（生日）、id13（跳
id12 代码）、id16（跳 id15 代码）。重写 `kFateDefs[].luck` 与本表一一对应。

## 4. 出国/绑架状态（0x40D375）

```
fateStartTravelState(p, days, abducted)：
  v9 = (abducted<<6) | days
  已有 BYTE1 → BYTE1 = (旧 & 0x3F) + v9（天数累加）
  否则：清監獄/醫院标志 + stateFlags → 清当前格占用 → travel=0/diceCount=1 + loadWalkResources
       → 保险理赔 2000×M×days → byte66 累计 → BYTE1 = v9
       → FLC 出國 558 @(0,40) sound 96 switchFrame 20 / 綁架 533 sound 84 switchFrame 28
       → sprite 坐标 = 玩家当前格
```

递减/释放已由 `updatePlayerStates` 处理（BYTE1：低 6 位天数、`(n-1)&0x3F==0` → bit7；到期清位 +
恢复格占用）。

## 5. 卡片联动（本轮完整实现）

### 5.1 免罪/嫁祸（0x441210 / 0x444BB2 / 0x44476A）

```
resolvePenaltyTarget(p) [RE 0x441210]：
  有免罪卡(21) → showCardGet(21, "玩家名\n\n免罪卡生效！") + 移除卡 → 返回 -1（惩罚取消）
  否则有嫁祸卡(19) → passOnCardDialog(p) [RE 0x44476A(p,0,0)] → 返回转移目标（-1 = 取消则返回自己）
  否则返回 p
passOnCardDialog(p)：
  先 showCardGet(19, "玩家名\n\n嫁禍卡生效！")
  人类：候选 = 其他存活玩家；1 人 → confirmDialog「是否嫁禍給%s？」(0x46534E)；
        多人 → selectPlayerDialog「請選擇嫁禍對象...」(0x46535D)
  AI：findMaxCreditor [RE 0x40D2D3]（矩阵行 p 欠款最大者）→ 无则 pickRandomActiveTarget
      [RE 0x40D31C]（随机存活无状态玩家）→ showMessage「嫁禍給%s！」(0x46536F)
  选定 → cardBagRemove(p, 19)；返回目标
```

### 5.2 生日收卡（0x44192A / 0x4413EC）

- 资源 `panel.mkf[11]`：卡包帧 0 @(14,70)（命中 x∈[19,419)、y∈[75,243)）、道具包帧 1 @(14,270)
  （命中 y∈[275,443)，仅有道具时绘制）；5×3 网格；按下高亮 `highlightRect`（l=80i+20、
  t=56j+76/+276、78×54）；抬起确认、右键取消。
- 返回值：卡 = 卡 id；道具 = `0x8000|道具 id`（原版 `dword_48C538`）。
- 收卡落包：卡 `cardBagRemove → giveCardToBag`；道具 `takePlayerItem → givePlayerItem`。

### 5.3 选人对话框（0x440E1A / 0x43FF56）

- 资源 `data.mkf[518]`（候选数 2..8 → 帧 `count-2` 横排框）+ `data.mkf[2]`（角色头像帧 charIndex）；
  框 @(220,320)、头像 @框内 (12+80i,12)；命中 x∈[220-offX+12, +80n)、y∈[320-offY+12, +72)；
  hover 播 `g_uiSoundHover` + 三层黄框（`push 0FFFF00h`；`word_46CAEC` 只是 640×480 surface）80×78；
  点击返回槽对应玩家、右键 -1。
- 提示框 = `g_tipFrame`（data.mkf[517]）帧 5 @(220,140) + drawText align 4。

## 6. 重写实现与差异

| 项 | 原版 | 重写 |
|----|------|------|
| 公共辅助 | 各效果函数内联 | `event_common.{h,cpp}`：`blitOpaqueRgb/drawEventText/drawPieceFrame/` `focusView/resetView/eventAudioWait/blinkHighlight/blinkSingleObj/` `collect*/pickRandom/estateNameUtf8/.../playerNameNoSpace`（新闻同步改调用）|
| 效果组织 | 49 个独立函数 + 共享尾部 | `kFateDefs[49]` 数据表（kind/amount/luck/piece）+ 16 个 kind 分派函数 |
| 头像帧 | 实参 +12×帧号 | `def.piece`（反汇编逐函数核对：0/1/6/9/14/15/16/19/23/30/32=帧3、2/3/7/8/10/11/12/13/17/18/24/26/33..48=帧4、5/21/22/25/28/29/31=帧5、4/20/27=帧2）|
| 高亮 | `markPickBuffer + highlightBlink` | `blinkSingleObj(objId)`（复用收租高亮管线）|
| 出国/绑架 | `sub_40D375` | `fateStartTravelState`（状态设置）+ `updatePlayerStates` BYTE1（已有）|
| 统计 | `byte_497324/5` | 并入 `misc8A[4]/[5]`（与 `damagePlayer` 同源）|
| 语音 | `sub_44EF41`/`sub_44F354` | ✅ P4-D（金钱档/列5 卖股/列3/4 坏运/列0 逃过；`play*Line` 家族） |
| 生日逐人演出 | 顶部行「向%s收一張卡片」逐人刷新 | 简化为直接弹选卡面板 |

## 7. 实机反馈修复（2026-09-26）

- **id2 冒贷补贷款到期日**：原版 0x44C0E8 `g_playerLoan += amt` 后调 `sub_433B7E`（= 重写
  `setLoanDate` 0x433B7E：loanDate 为 0 时设为今天+90 天并跳过特殊日期）。重写此前遗漏，
  已补（`bank_stay_dialog.h` 导出 `setLoanDate`，命运与银行贷款共用）。
- **神明修正证据复核**：kLuckA/B/C 表（0x4749E2/0x474A06/0x474A2A）、attachObject 0x40EAD7
  加成、sub_44B896 分档、15 个调用点参数全部经 IDA 反编译/disasm 核对（§3.1/§3.2）。
- **单块高亮上偏 40px**：`captureHighlightShapes` 沿用原版 pickBuffer 局部坐标 `it.y-40`，
  但重写直接画屏幕（无 highlightBlink 的 `+40` 回贴换算）→ 整体上移 40px。修复：锚点用
  `it.y`（`map_render.cpp`；证据与通用说明见 `frame-anchor.md` §5.3）。命运 id0/1 高亮受益。
- **id1 强制徵收完全不闪烁（2026-09-26 二修）**：原版 0x44BFB1 顺序为
  `refreshGameUi(x,y,2)→rebuildPickBuffer(1)→markPickBuffer(…, id, 0xFFFF)→addMoney→
  owner=0/expireDate=0→rebuildMiniMap→highlightBlink`；`0x409B18` 对**无主 estate 同样**
  写 `g_pickMask` 帧 `(8-(rot+dir))&1`（drawList 中 tile=0 但 id 已登记）。重写
  `map_render.cpp` 无主分支只填拾取字段、`hlShape` 留空 → 形状快照为空 → 无闪烁。
  修复：两处无主分支（estate/corp）补齐 `hlShape/hlFrame/hlAnchor`（帧 0/1、+2）；
  拾取矩形保持帧 4 体感魔改。详见 `40e033-map-objects.md` 同名修正段。

- **相位1 未切回场景**：实机反馈"坐牢时屏幕还没切回场景就开始播警车动画"。原版各效果函数
  phase1 开头先 `refreshGameUi(0,0,3)` 重绘地图（覆盖全屏事件面板）再播 FLC；重写单 Surface
  需显式 `renderGameFrame + renderFrame`。修复：`fateHandler` phase1 前全量重绘（警车 538/
  护士 524/飞碟 533/出国 558 同理）；**新闻 `newsHandler` 同步修复**（外星人/收监类 FLC）。
- **侵入銀行電腦显示 0%**：`kFateDefs[4]` 的 amount 误为 0（文本 `%d％` 取 `dword_48C5B0=10`）。
  修复：amount=10，`fateApplyBankHack` 按 `def.amount％` 计算并加日志。
- **罚款明细日志**：`fateApplyFine` 输出 idx/code/金额/转账前后 cash/bank/保险期，便于核对
  "未扣款"反馈（免罚/保险理赔为原版设计）。
- **坐牢「X 天实际 X+1 天」= 原版语义，非 bug（2026-09-26 IDA 复核）**：
  `jailPlayer` 0x43D593 写 BYTE2=days（累加 `&0x7F`）；`updatePlayerStates` 0x41C84F
  每回合开始 `n→n-1`，`1→0x80`，**下一次**回合才 release+走出（`sub_40D6BE`）；
  `sub_40C912` 显示 `(BYTE2&0x7F)+1`。即从触发回合数起第 X+1 次回合开始恢复行动
  （入狱当回合余下时间计入）。重写逐行一致，debug 直调补跳当回合同语义。
  见 `turn_system.cpp` jailPlayer 注释；**不做行为修改**。
- **扣钱/加钱后右侧资金面板不刷新（2026-09-26）**：原版命运各效果函数 phase1 普遍调用
  `refreshGameUi`（罚款 0x44CD99 甚至开头 `refreshGameUi(0,0,3)`），且 `transferMoney
  0x41D2C6` / `addMoney 0x41D3F4` 尾对当前玩家调 `refreshPlayerPanelFor 0x41D433`
  （`g_modalDepth<=1` 立即重绘面板）。重写缺少这两层刷新，相位1 转账后 800ms 尾部与
  保险消息期间停留在旧值。修复：① `game_panel.cpp` 导出 `refreshPlayerPanelFor`
  （depth 守卫 + 临时切 currentPlayer）；② 转账/加钱函数尾调用；
  ③ `fateHandler` 相位1 效果执行后补 `renderGameFrame + renderFrame`（`newsHandler` 同步）。

## 8. 验收清单

- [ ] `Ctrl+Shift+z` 连续抽取：文本/插画/头像帧/金额与本文档一致，37 项不重复
- [ ] `Ctrl+Shift+v` 切载具（步行/机车/汽车）→ 10..16 判定改写（偷车/撞毁/水沟/摔伤/闯马路/安全帽/超速）
- [ ] `Ctrl+Shift+G` 瞬移命运格走真实 `landingEvent` 链路（返回 0x80）
- [ ] 惩罚类（14/17/33..48）：免罪卡 21 消耗取消、嫁祸卡 19 转移目标、状态/金额正确
- [ ] 相位1 切场景：面板 1600ms 后先回地图，再播警车/护士/飞碟 FLC（不得画在面板上）
- [ ] 扣款日志核对：`fate fine: ... amt=... cash/bank` 与 `after` 差值（免罚/保险除外）
- [ ] 奖金类（20..31）：神明 luck 加成（加倍/作废）与 `insurancePayout` 分支
- [ ] 生日（id 5）：人类选卡/道具面板（双页、高亮、右键取消）；AI 随机取卡
- [ ] 存档往返：`fateOrder[37]` + `fatePos` 位置正确
- [ ] 新闻回归 `Ctrl+Shift+A`（event_common 提取后行为不变）
