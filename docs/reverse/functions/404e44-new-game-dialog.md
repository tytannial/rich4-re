# 0x404E44 newGameDialog（选图/选人界面）

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x404E44` |
| 大小 | `0x12A5`（4773 字节窗口过程） |
| 调用者 | `0x406DE7` newGameInit（`runModal(0x404E44, lParam)`） |
| 被调用 | `0x404504` redrawNewGamePanel / `0x40423C` redrawAvatarGrid / `0x40482C` drawOptionList / `0x404D0A` selectCharacter / `0x404D82` deselectCharacter / `0x45663E` blitSpriteFrame / `0x456180` blitScrolledMap / `0x4552B7` convertImageChannels / `0x451A97` saveBackground 等 |
| 重写符号 | `src/app/new_game_dialog.cpp` `newGameDialog` |
| 状态 | 已实现（截图逐场景验证） |

## 功能

新游戏/时空之旅的选图选人模态：左侧 12 角色头像选择区（6x2 网格），
右侧参数面板（6 项下拉参数 + 4 地图列表 + 確定/取消），全屏地图预览循环滚动背景。
点击確定后自动随机补齐 AI 玩家（按下瞬間首个 tick 即补第一个、其后每 10 tick≈1s 一个），随后播放进入游戏过渡动画。

两个入口（`0x401B9C` WinMain case 0/4）：

| 入口 | 主菜单按钮 | `word_4991B6`(gameMode) | 面板背景帧 | 地图预览索引 |
|------|-----------|------------------------|-----------|-------------|
| 原版开始游戏 | 0 (190,380) | 0 | 帧 1 | `map + 0..3` |
| 时空之旅 | 4 (62,380) | 1 | 帧 21 | `map + 4..7` |

`0x406DE7` 的 `lParam == byte_46CAFC`：非 0（从游戏内重新开始）时保留上次配置、
恢复玩家 0 角色、界面 10 tick 后自动確定（`dword_48A404` 快速重开模式，忽略鼠标）。

## 三段状态机（`dword_48A3AC` g_newGameState）

| 状态 | 定时器 | 行为 |
|------|--------|------|
| 0 选择 | 100ms | 地图滚动偏移 +4（1280 环绕）；底部已选角色 SPR 动画帧推进；头像悬停/选中/取消；6 项参数下拉；4 地图切换；確定/取消 |
| 1 随机补齐 | 100ms | **按下確定时预置 `dword_48A3CC = 10`**（确定分支内），配合 WM_TIMER 的 `v19 = dword_48A3CC++; if (v19 >= 10)` 使**首个 tick 即检查/补齐**；此后每 10 tick 从未选角色随机取一个（`rand() % 未选数`）加入玩家槽并标记 AI，直到 `已选数 == 玩家数`；补完最后一人后仍需 10 tick 才检查满员（约 1s，原版同此）；完成后切 50ms。**勿把初值置 0**（会多等一整轮 10 tick ≈ 1s，即"确认后迟迟不进游戏"根因） |
| 2 进入过渡 | 50ms | 地图画布左移（`dword_48A3E4 -= 速度`）、面板右移（`dword_48A3E8 += 速度`）、4 玩家头像右飞（`dword_48A3EC[i] += 加速度`）；速度每 tick +2，加速度上限 30；**动画阻塞自动退出**：WM_PAINT 中最后槽 `blitSpriteFrame`(0x45663E) 越界（x≥640）返回 1 → 写 `dword_48A3D8[playerCountIdx]`（=`dword_48A3D4[最后槽]`）→ 下一帧 `PostMessage WM_KEYDOWN` → `postModalExit(1)`；非等用户点击（按键/抬起亦可提前退出）|

## 关键结构

```
g_newGameOptions[7]  0x46CB3C  选项索引: [0]玩家数(0=二人/1=三人/2=四人) [1]起始资金
                               [2]行进方式 [3]土地权限 [4]游戏时间 [5]胜利条件 [6]地图
g_charState[12]      0x4990F4  0=未选 1=已选(人类点击/随机补齐AI，外观同为变暗-16)
                               2=上轮通关遗留AI(灰度+红X+不可选) 4=已烘焙(原版帧内标记)
g_aiUsed[12]         跨局保留的遗留标记（原版 0x406DE7 结尾清理: 4→2 / 其余→0）
g_playerCharId/frame/anim  0x48A35C 起 12 字节/槽交错: charId(bit31=AI) / 动画帧 / SPR 资源
g_selectedCount      0x48A40D  已选角色数
g_pressedCtrl        0x48A40E  按下中的控件 (-1=无)
g_openList           0x48A40F  展开的下拉列表 (-1=无)
g_listHover          0x48A40C  列表高亮项
g_avatarHover        0x48A410  头像悬停索引
g_scrollOffset       0x48A3C8  地图滚动偏移 0..1279
g_mapPreview/Raw     0x48A354/0x48A358  暗化(-16)滚动源 / JUMP.MKF 原图
```

控件矩形 `g_ctrlRects[13]`（0x46CC18，屏幕坐标）：

| 索引 | 矩形 | 语义 |
|------|------|------|
| 0 | {8,15,440,159} | 头像区（命中 `(x-8)/72 + 6*((y-15)/72)`） |
| 1 | {456,176,535,215} | 確定（按下态帧 2，80x40） |
| 2 | {544,176,623,215} | 取消（按下态帧 3，80x40） |
| 3-8 | {602,226+36i,625,250+36i} | 6 选项按钮（按下态帧 4，24x25；抬起展开列表 i-3） |
| 9-12 | {457,31+32i,625,62+32i} | 4 地图项（点击切换） |

下拉列表 `g_listRects[6]`（0x46CC88）/ 背景帧 `g_listBgFrame[6]`（0x46CCB8 = {5,6,5,6,6,7}）：

| 列表 | 项数 | 内容 | 特殊行为 |
|------|------|------|----------|
| 0 玩家数 | 3 | `kPlayerCountText` 二人/三人/四人 | 减少时移除槽位 >= 玩家数的角色 |
| 1 起始资金 | 6 | `kStartMoney` {300000,200000,100000,50000,30000,10000} | |
| 2 行进方式 | 3 | 步行/機車/汽車 | 变化时重载所有已选角色 SPR |
| 3 土地权限 | 6 | 無限期/二年/一年/六個月/三個月/一個月 | |
| 4 游戏时间 | 6 | 同上（`kGameDays` {0,730,365,182,91,30}） | |
| 5 胜利条件 | 6 | `kWinMoneyMul` {0,100,50,10,5,3} × 起始资金，0 显示"無限" | |

列表项高亮：`fillRect(rect.left+2, rect.top+2+23i, 宽-3, 20, 0xAA0000)` + 白字。

## 资源

| 资源 | 内容 |
|------|------|
| Jump.mkf[8] SMP 22 帧 | 0=头像区背景(440x155) 1/21=面板(192x461，模式 0/1) 2/3=確定/取消按下态(80x40) 4=选项按下态(24x25) 5/6/7=列表背景(42x71/67x140/87x140) 8=地图选中红勾(27x25) 9=AI 红X(50x52 off 25,26) 10=蓝星(27x27 off 14,14) |
| Jump.mkf[0..7] SMP | 4 地图 × 2 模式全屏预览（640x480，`mapIndex + 4*gameMode`） |
| Jump.mkf[9 + 3*角色 + 行进方式] SPR | 12 角色 × 3 行进方式动画（8bit 索引 + 256 色 RGB555 调色板，0 透明） |
| Data.mkf[2] SMP | 12 帧 72x72 角色头像（帧号 = 角色 ID） |

> 帧头偏移注意：原版 `g_jumpUi + 108` 是**帧 8**（12 + 12*8 = 108）、`+120` 是**帧 9**，
> 不是"帧 9/帧 10"（曾因误读导致地图标记画成红 X 且错位）。

## 绘制顺序

状态 0/1（原版 WM_TIMER）：地图滚动全屏 → 底部已选角色动画 → 头像区画布(4,10) → 面板(445,10) → 按下态叠加 → 下拉列表。
状态 2（原版 WM_PAINT）：地图滚动（**未暗化** `g_mapPreviewRaw`）→ 头像区画布(transMapX) → 面板(transPanelX) → 底部头像(transSlotX)。

头像区细节：
- 未选：普通头像；悬停未选：头像斜移 (4,-4) + 名字条（`-20` 暗化 70x20 + 16 号白字居中，第二行在头像上方 / 第一行在下方）
- 已选（状态 1，含随机补齐的本轮 AI）：头像 + `-16` 变暗（人类与 AI 外观相同，AI 标志在 `charId` bit31）
- 上轮通关遗留 AI（状态 2）：灰度化头像（`(R+G+B+16)>>2`）+ 红 X（帧 9 画在格子内 (11,11)），不可选
- 原版状态 2 渲染后会烘焙进资源帧和帧 0 背景并置 4；重写为每次重绘临时生成（不改只读资源）
- `0x406DE7` 结尾清理：本轮 AI（状态 4）→ 2、其余 → 0，跨局保留"上轮 AI"标记
  （回主菜单再进会清空；游戏内"重新开始"保留 → 遗留角色灰度+红X 不可选）

## 重写要点

- **定时器**：原版 `SetTimer(hWnd, g_modalDepth, 100/50ms)` + `WM_TIMER` →
  `EventStack::setTimer` + `runModal(..., tickMs)` 循环按 `SDL_GetTicks` 合成
  `kModalTimerEvent`（SDL3 无内置定时器事件，用 `SDL_EVENT_USER`）派发栈顶。
- **消息映射**：`WM_MOUSEMOVE`→`SDL_EVENT_MOUSE_MOTION`；`WM_LBUTTONDOWN`→
  `SDL_EVENT_MOUSE_BUTTON_DOWN(LEFT)`；`WM_LBUTTONUP`→`UP(LEFT)`；
  `WM_RBUTTONUP(0x205)`→`UP(RIGHT)`（关闭列表或退出）。
- **全量重绘**：原版用 `saveBackground` 保存按下态背景逐区域恢复 + 画布离屏元素；
  重写统一"状态变化 → redrawAll 全量重绘"，不需要保存区域。
- **音效**（`sub_4542CE`，已实现）：
  - 头像选择/取消、地图切换 → `dword_482322` = Effect.mkf[1]（点击）
  - 头像悬停 → `dword_48231A` = Effect.mkf[0]
  - 选项下拉（控件 3-8）→ `dword_48231A` = Effect.mkf[0]
  - **確定（控件1）→ `dword_48232A` = Effect.mkf[2]**；**取消（控件2）→ `dword_482332` = Effect.mkf[4]**
    （按下音效 = `sub_4542CE(&dword_48231A + 8*控件 + 8, 0)`）
  - 进入阶段（补齐完成）→ `unk_46CCD0` = Effect.mkf[5]（0x405D03）
- **颜色变换**：`convertImageChannels`（-16 暗化，32 字节查表 `kChannelHalf`）、
  `scaleSurfaceChannels`（-20 `kChannelThird`）、`grayscaleImage`。
- **状态 2 动画阻塞退出机制**（2026-09-29 IDA 复核订正）：旧记载"`dword_48A3D8` 恒 0、无写入者、
  实际依赖用户输入退出"**有误**。xrefs 确认 `0x48A3D8` 唯一写入点为 `0x4060CD`（状态 2 WM_PAINT），
  其值来自 `dword_48A3D4[j] = blitSpriteFrame(...)`——`blitSpriteFrame`(0x45663E) 在目标 x≥640
  越界时**返回 1**。故退出判据 `dword_48A3D8[playerCountIdx]`（地址即 `dword_48A3D4[最后玩家槽]`）
  在最后一个头像飞出右边界后置非 0 → 投 `WM_KEYDOWN` 自动 `postModalExit(1)`，属**动画播完自动进游戏**。
  重写 `handleTimer` 状态 2 以 `transSlotX[playerCount-1] >= 640 → requestExit(1)` 等价复现（曾误用
  `transMapX <= -640`，时机偏差已修正）；按键/鼠标抬起仍可提前退出（同原版 `handleEnterEvent`）。

## 验证方式

`--shot` 截图逐场景比对（原版 DxWnd 运行画面）：

| 场景 | 命令要点 | 结果 |
|------|----------|------|
| 初始布局 | 点击新游戏 | 12 头像 + 地图列表 + 6 参数 + 背景滚动 |
| 选择角色 | 点头像 | 头像变暗 + 底部出现 SPR 动画 |
| 下拉列表 | 点选项按钮 | 列表背景帧 + 项文本 + 高亮 |
| 地图切换 | 点地图项 | 标记移动 + 预览重载（map=1 日志） |
| 悬停名字 | `--hover` 头像 | 头像斜移 + 名字条 |
| 確定→补齐 | 点 OK | AI 灰度头像 + 图标 + 随机补齐日志 |
| 过渡动画 | 等待 | 地图变亮 + 面板移出 + 头像飞动 |
| 進入游戏 | 状态 2 按 ESC | `result=1` → loadMap → enterGameLoop（阶段 1：完整游戏画面） |
| 时空之旅 | 点击主菜单按钮 4 | 面板帧 21 + 關卡五~八 + 太空预览 |
