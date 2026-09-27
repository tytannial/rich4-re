# 0x411B53 settingsDialog / 0x4103A3 settingsWndProc 设置面板与游戏内系统菜单

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x411B53`（settingsDialog）/ `0x4103A3`（settingsWndProc） |
| 大小 | `0x33C` / `0x6E4` |
| 调用者 | `0x417D65` case1（游戏内工具条「设置」→ `settingsDialog(1)`）、`0x40257A`（主菜单设置 → `settingsDialog(0)`） |
| 被调用 | `0x4018E7` runModal、`0x453A32` confirmDialog、`0x411AA3`/`0x411AE0`/`0x411B46`、`0x4119E3` dateWndProc、`0x411122` hotkeyWndProc、`0x44E40B` helpWndProc |
| 重写符号 | `src/app/settings_dialog.cpp` `settingsDialog` / `settingsEventHandler` |
| 状态 | 已实现 |

## 功能

`dword_48BB58 = page`（0 = 设置面板，1 = 游戏内系统菜单），`data.mkf[3]` 对话框居中
`(320 - 帧0宽/2, 240 - 帧0高/2)`，`runModal(settingsWndProc)`。返回值经 `& 0x7FFF` 分发：

| 返回值 | 行为 | 原函数 |
|--------|------|--------|
| 1 | 重新遊戲：游戏日期 = 系统日期 + `g_sceneRequest=1`（回主菜单） | `sub_411AA3` |
| 2 | 認輸投降：当前玩家出局 + 交棒（`sub_41906A(1)`） | `sub_411AE0` |
| 3 | 結束遊戲：`saveConfig()` + `g_quitGame=1`（WinMain LABEL_27 退出程序） | `sub_411B46` |
| 高位 0x8000 | 布局 `byte_49715D` 变化 → `sub_41906A(1)` 刷新 | — |

## 逆向依据

- 控件命中矩形 `dword_474B92..`（16 个）：0-2 滑块（速度/音乐/音效）、3-5 页按钮、
  6 音乐列表、7 取消、8 確定、9-15 复选/单选
- **游戏内菜单（page!=0）**：点击页按钮 3/4/5 → `sub_453A32(320,200)` 确认框 → YES →
  `postModalExit(控件-2)`（1/2/3）
- **设置面板（page==0）**：case 7 取消 → `postModalExit(0)`；case 8 確定 → 应用设置 +
  `saveConfig` + `postModalExit(0)`；页按钮 3/4/5 → 日期/热键/说明子对话框
- **音效**（`sub_4542CE`）：
  - case 7 取消 → `dword_482332` = **Effect.mkf[4]**
  - case 8 確定 → `dword_48232A` = **Effect.mkf[2]**
  - 其余控件（滑块/页按钮/复选/单选）→ `dword_482322` = Effect.mkf[1]；悬停 → `dword_48231A` = Effect.mkf[0]
  - 音乐列表音量 0 → `dword_48233A` = Effect.mkf[3]
- **音乐处理**（case 8, 0x410991）：仅音量 0↔非0 时停止/重播（可选曲目 `sub_454D91(0)` /
  场景 `sub_4549CF`）；非0 之间变化只调音量（`sub_45497B`）；**取消（case 7）不改动音乐**
- 字符串：`off_474A54` 设置项标签（游戏速度/动画过程/音乐/音效/自动存档/乐曲/视窗/日月历/缩小地图/组合画面）、
  `off_474B2C` 页标签（原始設定/取消/確定）、`off_474ABC` 28 项热键功能名、`off_474A9C` 音乐列表（8 首）

## 关键结构

```c
dword_48BB58   page（0/1）
byte_48BB48..  设置副本（进入时 memcpy 自 byte_497158；確定写回 + RICH4.CFG）
dword_48BB50   = byte_48BB48+8 上次手动确认日期（dateWndProc 工作初值 dword_48BB84）
byte_49715A/5B 音乐/音效音量（0-4）
byte_49715D    布局（0/1/2；变化 → 返回值 0x8000）
dword_497160   = byte_497158+8 游戏日期（与设置区重叠，随 RICH4.CFG 一并存盘）
dword_474D74   当前按下控件索引
```

## 游戏日期链（0x497160）

开局日期 = 当前 `dword_497160`，**`newGameInit`(0x406DE7) 全程不写该值**（45 处 xref 无其一）。
写入者共四处：

| 时机 | 地址 | 值 |
|------|------|----|
| 启动 `loadConfig` 尾部 | `0x411F21`→尾跳转 `0x411A7C` | `dos_getdate` 钳位 [1998,2010]（<1998→1998-1-1；>2010→2010-1-1；否则系统日期） |
| 主菜单 设置→更改日期 确认 | `0x4119E3`（`dateChangePage`）→ `0x411A77/7C` | 用户选择日期（双写 `48BB50`+`497160`） |
| 游戏内 重新遊戲 | `0x411AA3`（`restartGame`） | 系统日期（**无钳位**） |
| 读档 | `0x402B27`（`loadGameFromSlot`） | 存档日期 |

`advanceDay`(0x41CF67) 过天 +1。存盘：`saveConfig`(0x411F80) `fwrite(byte_497158,16)` 把
`dword_497160` 作为 CFG 第 8..11 字节一并写出（但下次启动会被钳位系统日期覆盖，**日期不持久化**）。

**踩坑**：重写早期 `newGameInit` 硬编码 `gameDate=1998-1-1`，导致「设置→更改日期」所选日期
在开新局时被覆盖（与原版不符）；现按上表还原。`dateDialog` 初值传 `48BB50`
（原版语义：上次手动确认日期，会话首开为 0 → 重写回退系统日期）。

## 重写要点

- `settingsEventHandler`：悬停/拖动/点击分发；確定/取消专用音效（2/4）
- 返回值分发在 `settingsDialog`（page!=0 时 1/2/3 → 重新游戏/投降/结束游戏）
- **结束游戏**：`quitGame` → `game_loop` 检测后 `app.quit()` 直接退出（对齐原版 LABEL_27）
- **投降 ✅ 2026-09-27**：`surrenderPlayer` 0x411AE0 —— `eliminatePlayer` 淘汰（含演出/资产
  清算/拍卖）+（humanCount>1 且存活人类非 0）死神召唤对话框 0x4339D9/0x433088 选目标 →
  `createMapObject(15,0,7,target)` 死神附身复仇 → `nextPlayer` 交棒（单人战败界面 M3）
- **重新游戏**：`sceneRequest=1` → 回主菜单（原版 `_WinMain` LABEL_5 语义）；
  `newGameInit` 重置 `mapRotation`/骰子/回合/音乐计时器等残留状态
- **音乐**：取消不改动音乐；確定仅静音 0↔非0 时停/重播（避免「从头播放」）

## 验证方式

主菜单/游戏内工具条打开设置：page0 调整参数后確定/取消；page1 点重新遊戲/認輸投降/結束遊戲；
观察音效（確定=2/取消=4）、音乐不中断、结束游戏直接退出。
