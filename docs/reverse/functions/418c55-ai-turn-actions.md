# 0x418C55 beginPlayerTurn AI 回合动作链（留档，后续行为补完专项）

> 2026-09-28 逆向留档。本轮只修复了「道具/卡片 AI 的候选域与视口刷新」，本文登记
> **原版回合链中存在、重写缺失/未接入的 AI 行为**，供后续统一规划。
> 依据：`sub_418C55`（beginPlayerTurn）case 2/5 反编译 + 各目标函数逐行核实。

## 1. 原版 case 2/5（AI/托管回合）动作顺序

```
0x418DE6  sub_42BF03(cur)     AI 买股票（详见 §2）
0x418DF4  sub_42C79F(cur)     AI 卖股票（详见 §3）
0x418DFE  sub_436B0A(0)       週轉资金准备结算（重写 bankAdvanceSettle 已有，未接回合）
0x418E06  if sceneRequest → 跳过后续（场景切换保护）        【重写已有】
0x418E13  sub_4284BE()        交易市场 AI（挂单清理/挂卖/改价/低价买入） 【重写 tradeAiTurn ✅】
0x418E18  rand()&1 ? 0x441BAA(用卡) : 0x447D97(用道具)       【重写 ✅，候选域 2026-09-28 修正】
0x418E31  if stateFlags(0x496B9A) || state37(0x496B9F) || byte54(0x496B9E)
              byte_498EA0 |= 0x80        → 状态玩家：置「原地待命」标记，本回合不移动
          else if playerActionState != 1
0x418E70      sub_4221C0()               AI 骰子数动态调整（详见 §4）
0x418E75      sub_40DD1F()               开始移动                        【重写 ✅】
```

- 重写现状：`turn_system.cpp beginPlayerTurn` case 2/5 = 视口刷新（2026-09-28 新增）→
  tradeAiTurn → rand&1 卡/道具 → startPlayerMove。**缺**：§2/§3/§4 + 0x436B0A(0) 接入 +
  0x418E31 状态守卫（出国/绑架/陷害状态 AI 本应跳过本回合行动，现直接移动）。

## 2. `sub_42BF03` AI 买股票（0x42BF03，size 2204）

- 前置：`rand()%3 != 0` 不用；`aiStockPct==0` 不用；休市不用；
  贷款未结且到期 <15 天不用（`sub_4521AA(dword_497160, loanDate)<15`）。
- 预算：`v30 = aiStockPct% × (cash + bank + Σ持股市值)`；已有市值 ≥ 预算 → 不买；
  缺口 `v30 -= 市值`，且不超过 bank（**只用存款买**）。
- 选股（12 种）：跳过停牌（`byte_496986`）、涨停（`stockUpDownStatus==1`）、无价格
  （word_49698A）；按 **24 日历史均价**（`g_stockHistory`，跳过 0 值）与现价比较择优
  （低价股 30000×M 以下要求 bank 充足）→ `buyStock` + showMessage + refreshPlayerPanel。

## 3. `sub_42C79F` AI 卖股票（0x42C79F，size 2384）

- 前置：贷款到期 ≤6 天 → **强制卖**（v30=1）；否则 `rand()%3 != 0` 不卖；休市跳过。
- 评估（每种持股）：市值/成本比、做市商持股占比（`v40 = specPt(+44)/dword_499084`）、
  全玩家持股集中度（`Σ他人持股/自己持股`）、跌势（`stockUpDownStatus==3`）、停牌跳过；
  多项加分（如公库支出 ≤ -10000×M ∧ 集中度高 ∧ 月中 → +3）→ 分数最高股 `sellStock`。

## 4. `sub_4221C0` AI 骰子数动态调整（0x4221C0，被 0x418E70 调）

`byte_496B7A = players[p].diceCount`；travel(+17)：

| travel | 基础 | 挂道具（cellNo=g_playerCellNo，寿命 `g_cellLife[槽]`） | 前方 5 格分析（`aiPathForward(cur,5)`） |
|--------|------|--------------------------------------------------------|------------------------------------------|
| 2 汽车 | **3** | life<15 → 1；life>20 → 3；否则进入分析 | v5=自家/无主格数，v6=他人格数：`v5==0∧v6>2` → **2+(rand&1)**；`v5≥2∧v6≤1` → **1** |
| 1 机车 | **2** | life<15 → 1 | `v12==0∧v13>2` → 保持 2；`v12≥2∧v13≤1` → **1**（其余 return） |
| 0 步行 | 1 | — | **不调整**（函数对 v1∉{1,2} 直接返回） |

语义：AI 在自己地产密集段收骰子数（1 骰守段收租），他人产业段多则放开骰子。
**重写现状**：仅在 useItemFoot/Car 时固定 2/3，无回合动态调整 → AI 节奏与原版的差异来源。

## 5. `sub_436B0A(0)` 週轉结算接入

重写 `bank_dialog.cpp bankAdvanceSettle(app, one)` 已实现（0x436B0A），但只在柜员机
路径（one=1）调用；原版每 AI/托管回合开始 one=0 调用（资金准备不足提示/垫付）。
**待接入**：beginPlayerTurn case 2/5（0x418DFE 位序）。

## 6. `0x418E31` 状态守卫缺失

`stateFlags(+50) ∨ state37(+55) ∨ byte54(+54)` 任一非 0（出国/被绑架/冬眠/陷害等）→
置 `byte_498EA0 |= 0x80`（= `playerActionFlags |= 0x80`，下 tick 重新进 beginPlayerTurn）
并**跳过移动与骰子调整**。重写当前直接 `startPlayerMove` → 状态期 AI 仍会掷骰移动。

## 7. 后续规划建议（优先级序）

1. ~~`0x418E31` 状态守卫~~ ✅ **2026-09-28 已实现**（`turn_system.cpp beginPlayerTurn`，
   场景 `262_ai_turn_guard`：p1 置态后全程无 land/landmove）。
2. ~~`sub_4221C0` 骰子调整~~ ✅ **2026-09-28 已实现**（`ai_item.cpp aiDiceAdjustPerTurn`，
   场景 `264_ai_dice_adjust`：实机日志 `dice=1 ownFree=4 other=0` 验证"守段收租"语义）。
3. ~~`bankAdvanceSettle(app, 0)` 接入~~ ✅ **2026-09-28 已实现**（0x418DFE 位序；场景
   `266_ai_bank_advance`。注意 one=0 循环**豁免银行经营者本人**（i==owner skip），
   开局 bank 常有初始经营者——多玩家负债才稳态触发）。
4. ~~`sub_42BF03`/`sub_42C79F` AI 股票买卖~~ ✅ **2026-09-28 已实现**（`stock_system.cpp
   aiStockBuy/aiStockSell`，beginPlayerTurn 0x418DE6/0x418DF4 接线；场景 `268_ai_stock_buy`
   实测 `aiStockBuy: p1 stock=4 n=1647`、`270_ai_stock_sell` 实测 p0 初始配股卖出。
   **字段订正**：`dword_499084` = `stockDivPeriod`（分红期数，非 publicFund）；评分中
   `fundR = specPt.fundPaid(+44)/stockDivPeriod`（每期支出均值）、`+40 = fund`（公库累计）。
5. 与「AI 行为整体规划」合并评审（用户 2026-09-28 决策：本轮先道具/卡片，行为链后续补完）。

## 8. 补充核实（2026-09-28 实现时）

- `g_playerCellNo`(0x496BA8,+64) = 挂道具槽号（1-based），寿命 = `g_cellLife[24*v10-24]`
  = `cellTable[24*(cellNo-1)+4]`（`g_cellLife 0x496D0C = g_cellTable 0x496D08+4` 别名）。
- `g_playerVehicle[104*cur]`(0x496B79) 在 `aiDiceAdjustPerTurn/aiTgtScooter/aiTgtCar` 语境
  = **travel 行进方式**（+17）；与同盟字段 `0x496BA9 ally` 是两个不同全局（IDA 曾混淆，
  以引用地址为准）。
- 状态守卫 break 后 `playerActionFlags[p] |= 0x80` → 下一 tick `updateGameState` 重新
  进 `beginPlayerTurn`（重入安全：卡/道具/週轉/交易每 tick 都会再跑，原版同形）。
