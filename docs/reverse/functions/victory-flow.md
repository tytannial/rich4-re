# 通关流程（胜利判定 → 结算 → 地图选择 → 回到选人界面）

## 总览

```
游戏内每回合（sub_41CF67 过天）
 └─ sub_41D89E 胜利判定（资产最高者/时间到）
     ├─ 单人游玩（dword_499104==1）且人类赢：
     │   其他玩家（AI）charState[角色] = 2（下局灰度+红X不可选）
     │   byte_46CAF8 = 2（场景切换请求）
     ├─ 多人游玩且人类赢：byte_46CAF8 = 3
     ├─ AI 赢（玩家输）：byte_46CAF8 = sub_407842(1)（失败界面 → 读档/主菜单）
     └─ 未结束：返回 0，继续回合

WinMain 处理 byte_46CAF8:
  2 → v8=0; sub_4075C1()（通关结算+地图选择）; goto LABEL_6（新游戏，byte_46CAFC=1）
  3 → sub_4075C1(); goto LABEL_5（回主菜单）
  1 → goto LABEL_5（回主菜单）
  4 → loadDialog() 成功则 v8=1（读档）
```

## 胜利判定 `sub_41D89E`（0x41D89E）

调用者 `sub_41CF67`（0x41CF67，游戏内过天函数：`++dword_4990E4` 天数后调用）。

- 前置：`dword_49911C`（游戏时间限制）与 `dword_499108`（胜利资金）均为 0 时不判定（无限模式）
- 找出资产最高（`sub_4239B9`）的存活玩家 `a1`
- 胜利条件：`dword_49911C && dword_49911C <= dword_4990E4`（时间到）
  或 `dword_499108 && 资产 >= dword_499108`（资金达标）
- 胜利处理：`*dword_49910C = a1`（赢家），其他玩家 `byte_496B7D[104*i] = 0`（淘汰标记）
- 分支：
  - **单人游玩（`dword_499104 == 1`，人类玩家数）且赢家是人类**：
    `g_charState[byte_496B7B[104*AI]] = 2`（所有 AI 角色 → 遗留标记），`byte_46CAF8 = 2`
  - 单人且赢家是 AI：`byte_46CAF8 = sub_407842(1)`（失败界面）
  - 多人且赢家是人类：`byte_46CAF8 = 3`（结算后回主菜单）
  - 多人且赢家是 AI：`byte_46CAF8 = 1`（回主菜单）

## 通关结算 + 地图选择 `sub_4075C1`（0x4075C1）

- `dword_4990F0[word_4991B8] = 1`：**标记当前地图已通关**（4 字节进度数组）
- 读 JUMP.MKF 当前地图预览 + 帧集；读 panel.mkf 角色通关动画（`byte_496B7B[i] + 100`，i = 首个未淘汰玩家）
- 在 g_jumpUi 帧上标记所有已通关地图，统计已通关数 `v4`
- **`v4 >= 4`（4 张全通关）**：
  - 时空之旅：播放 `END%02d.AVI` + `THANKS.AVI`；原版：`END.AVI` + `OVER.AVI`
  - `byte_46CAF9 = 1`（退出游戏）
- **否则**：`runModal(sub_4060E9, 0)`（地图选择界面，见下）

## 地图选择界面 `sub_4060E9`（0x4060E9）

- 50ms 定时器：地图预览滚动 + 角色动画（`dword_48A3BC`）+ 装饰（`dword_48A38C`）；
  地图选择面板（帧 15/20，257x177）从 y=-80 滑入到 y=360
- 4 个地图项（帧 11-14 / 16-19，257x45）画在 (320, 300+40*(item-1))
- **已通关地图不可选**：检查 `dword_4990F0[item-1]`（`dword_4990EC + item + 3`）
- 点击未通关项：把红勾（帧 8）画进该项（214,8）→ `word_4991B8 = item-1`（**选定下一张地图**）
  → 10 tick 后 `postModalExit(0)` 退出
- 多人游玩（`dword_499104 != 1`）时显示"恭喜勝利過關！！"（`byte_463176`，字号 48）
- 退出后 `sub_4075C1` 释放资源返回，WinMain `goto LABEL_6` → `newGameInit(byte_46CAFC=1)`

## 失败处理 `sub_407842`（0x407842）+ `sub_406B14`（0x406B14）

- `sub_407842(1)`：显示失败界面（`runModal(sub_406B14)`）
- `sub_406B14`：1 秒/tick 的 10 秒倒计时；按键（`word_497178` 確定键）或左键抬起 → `postModalExit(1)`；超时 → `postModalExit(0)`
- 用户确认（返回非 0）：清空其他玩家 `charState[角色] = 0`、`byte_496B7D[0] = 1`，返回 4
  → WinMain case 4：打开读档对话框（读档继续）；取消则回主菜单
- 超时（返回 0）：返回 1 → 回主菜单

## 通关进度 `dword_4990F0`（4 字节）

| 位置 | 语义 |
|------|------|
| `0x4015D6` gameInit | 初始化 |
| `0x401B9C` WinMain LABEL_5 | **回主菜单时清零**（连续通关才累计） |
| `0x402AC5`/`0x402FD1` | 读档/存档（通关进度进存档） |
| `0x4075C1` | 标记当前地图已通关 |
| `0x4060E9` | 检查地图是否可选 |

## 与用户印象的对照

| 印象 | 实际 |
|------|------|
| 单人游玩点开始按总人数自动补齐 AI | ✓（选人界面状态 1，charState=1） |
| 胜利后自动回到选人界面 | ✓（`byte_46CAF8=2` → newGameInit(lParam=1)） |
| 上一轮 AI 置灰且标记 X | ✓（`sub_41D89E` 设 charState=2，选人界面灰度+红X 不可选） |
| 随机挑选剩余 AI 中的 3 个 | ✓（选人界面状态 1 随机补齐） |
| 地图自动选择下一个场景 | **不完全**：弹出地图选择界面（`sub_4060E9`），**手动点击**下一张未通关地图；已通关的不可选；4 张全通关则播放 END.AVI 并退出游戏 |

## 重写现状

- 已实现：选人界面（charState 状态 1/2 语义、`aiUsed` 跨局保留）、回主菜单循环
- 已实现（阶段 1，进入游戏画面）：`0x406DE7` 确认后的玩家/经济/股票初始化、
  `0x407AD2` loadMapData（GND/MAPDAT/图块/棋子）、`0x40829D` 等距地图渲染、
  `0x417E26` 游戏内主循环（每 tick 全量重绘）、`0x4190CF` loadPanelUi、
  `0x415D31/4166F8/4169BC/416E6D` 面板绘制
- 已实现（阶段 2，视角与交互）：`0x417E26` 小地图拖拽/箭头旋转（箭头帧 20/21、按下 18/19）/
  右键取消手动视角/左键抬起结束拖拽、工具条悬停高亮与按钮分发（`0x417D65`，帮助/设置/读档）、
  `0x40829D` 住宅用地/商業用地/行業設施點/事件格 y 排序绘制、`0x40A4E1` 小地图标记叠加
  （玩家框白 `0xFFFFFF`、手动视野框红 `0xFF0000`）
- 已实现（后续阶段补记）：`sub_41CF67` 回合推进（advanceDay/nextPlayer）、`sub_417559` 物件信息框、
  工具条存檔/地圖/卡片/道具/股票按钮；**`eliminatePlayer` 破产全流程 ✅ 2026-09-27**（含资产
  收集/清仓持股/没收道具卡片/乐透作废/资产 >3 随机拍 3 件/淘汰演出 FLC555/棋子灰度；
  单人投降见 `surrenderPlayer` 0x411AE0）
- **破产/投降终局判定 ✅ 2026-09-27（实机验证通过）**（`sub_40CD87` 尾段）：单人局人类破产 → `defeatFlow`
  （重写近似 sceneRequest=1，失败界面 M3）；无存活人类 → scene 1；**仅剩 1 存活 → 该玩家胜利
  `sceneRequest = 2（单人）/3（多人）`，不做资产清算**；>1 → 资产清算 + 拍卖。配套 `g_playerAlive`
  时序对齐：未入场玩家 alive=0（**跳伞落地才置位** 0x418C55/0x418D07），原版 231 处目标/统计/
  排行引用（投降候选/AI 用卡/物价指数/拍卖名单…）自动排除未入场者；出生点分配抽为
  `spawnPlayerAt` 并在 `beginPlayerTurn` 前触发（重写 updateGameState 时序保证）
- **✅ 2026-09-29（M3-A2/A4）已实现**（`src/app/victory.cpp`）：`sub_41D89E` 胜利判定
  `checkVictory`（时间上限 `g_gameDaysLimit`/资产达标 `g_winMoney`，接 `advanceDay 0x41CF67`
  ++dayCount 之后；赢家台词 `kMoneyLines[charIndex][21]` expr3、其余 alive=0、单人人类赢→
  其余 AI `aiUsed=2` + scene2 / 单人 AI 赢→defeatFlow / 多人人类赢→scene3 / AI 赢→scene1）+
  `sub_407842`+`sub_406B14` 失败界面 `defeatFlow`（panel.mkf[112] 10s 倒计时模态，确认→4 读档、
  超时→1 主菜单，破产路径播 FLC556）；调试注入 `victory <days> <money>`；测试 280/282/286。
- **✅ 2026-09-29（M3-A6/A1/A3 功能版）已实现**：WinMain `run()` LABEL_5/LABEL_6
  scene 分派（`src/application.cpp`，`m_runFlow` 驱动）——scene1→主菜单、scene2→`gameClearFlow`
  续新游戏（**不清通关进度**，`newGameInit(1)` 保配置+灰名单）、scene3→结算回菜单、
  scene4→`loadDialog 0x403D74` 选档续局；`gameClearFlow 0x4075C1`（`src/app/victory.cpp`）标记
  `clearedMaps[mapIndex]=1` + 4 图全通 `quitGame` + 自动选下一未通关图；LABEL_5 入口
  `memset clearedMaps`（0x401C9E）。测试 280/282 断言分派+标记。
- **✅ 2026-09-29（M3-A3 视觉主体）**：`mapSelectWndProc 0x4060E9` → `mapSelectDialog`（`victory.cpp`）
  实机弹面板交互（JUMP jumpUi 面板底帧 15/20 + 4 地图项帧 11-14/16-19 @(320,300+40·i) +
  已通关不可选 + 悬停音效 + 点击设 `word_4991B8`/mapIndex + 多人「恭喜過關」48 号 + 50ms 滑入）；
  headless `--quickstart` 自动选下一未通关图（bypass 交互，280/282 断言 auto map=1）。全通 → quitGame。
- **✅ 2026-09-29（M3-A 终局链观感修正，实机问题驱动）**：
  - **playLine 视口切换**（`item_effects.cpp`）：原版 `sub_44EF41` 首步
    `refreshGameUi(spriteX[a1], spriteY[a1], 0)`——说话玩家==当前玩家→复位跟随，否则
    `manualView` 对准说话者像素坐标；模态结束还原。修复"破产致胜卡在倒闭画面、气泡弹在
    旧视口"（2026-09-29 实机）。
  - **破产致胜**（`economy.cpp` 0x40CD87 尾段）：剩 1 存活分支 playLine 前
    `currentPlayer = winner`（原版 LABEL_70 在台词后还原，scene2/3 随即退循环无副作用）；
    **认输台词（列25 expr2）位置订正**——原版 0x40D237 在**清算之后、棋子灰度前**且仅
    总存活>1 路径播（重写曾误置于 FLC555 之前、所有路径都播）。
  - **选图界面全屏演出重做**（`victory.cpp`，消重影+蓝星+动画）：原版为双缓冲界面，每
    50ms tick 全屏重铺 → 重写即时模式每帧重画（旧版往游戏残留画面叠画滑入面板 = 重影根因）：
    ① 背景 = **刚通关地图**预览 `JUMP.MKF[map+4·mode]`（640×480 RGB555）水平环绕滚动
    （`blitScrolledMap` 0x456180，+4/tick，1280 周期）；② 胜者角色行走 `panel[charIdx+100]`
    SPR 25 帧 = 5 站立 + 2 方向×10 行走（帧=((dir^1)·(count−4)/2+walk+5)，walkX −100..740
    ±10/tick 到端随机 walkY=rand%360+100 换向重入，x=walkX±90）；③ 装饰 `panel[93]`
    19 帧，帧表 `byte_46CCC4`={0,1,2,3,5,6|12,13,14,15,17,18}，与角色对侧 ∓90；
    ④ **蓝星预合成**：`gameClearFlow` 把 jumpUi **帧10（蓝星 27×27 off(14,14)）** blit 进
    每个已通关地图的悬停项帧 @(32,22) + 面板底帧 @(220,40·i+28)（**blitElementToCanvas
    0x4562A5 语义 = (dst帧, src帧, x, y)**，目标=实参−src.offset，色键0；helper
    `UiImage::blitIntoFrame`）；⑤ 点击确认把**帧8（红勾 27×25）**合成进选中项帧 @(214,8)
    → 10 tick（500ms）后退出；单人右键**不可退**（必须选图，0x406970 仅 multiple 响应）；
    ⑥ 进入 `musicPlayScene(0x8006)`（playSceneMusic(6,false) 不压栈）、退出 `musicStop`。
  - **多人局终局语义确认**（用户实机疑问）：`humanCount≠1` 赢家=scene3 → 选图界面
    `byte_48A435=1` **仅演出背景+「恭喜勝利過關！！」大字（无面板/星星/选图）**→ 点击退出
    → LABEL_5 清通关进度回主菜单——**多人局不进下一关为原版行为**；连续通关仅单人局。
  - 新增 `player.bankrupt <p>` 调试命令（bankruptRequest 标志经游戏循环 tick 处理，
    仿 surrenderRequest 模式避免脚本 step 嵌套模态）；测试 `283_victory_last_stand`
    （3 AI 全灭→致胜台词→scene2→auto map=1→**实开 map=1 新局**，5 断言）。
- 待精修（观感/低频，实机核对）：284 四图连通关 + 灰名单跨局 headless（多次选人/选图模态应答）
  留实机验收；AVI(A5) END/THANKS/OVER 不实现（Indeo5）。
