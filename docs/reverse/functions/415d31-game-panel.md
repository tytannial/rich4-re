# 0x415D31 / 0x415F69 / 0x4166F8 / 0x4169BC / 0x416E6D 游戏内面板

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x415D31`（工具条）/ `0x415F69`（玩家信息）/ `0x4166F8`（玩家条）/ `0x4169BC`（日历）/ `0x416E6D`（小地图） |
| 大小 | 0x13F / 0x78F / 0x2C4 / 0x4B1 / 0x314 |
| 调用者 | `0x417E26` gameWndProc WM_PAINT 分支 |
| 被调用 | `0x4563F5` blitElementFullscreen、`0x456418` blitElement、`0x44F9D8` setFont、`0x44FABC` drawText、`0x452793` 金额格式化 |
| 重写符号 | `src/app/game_panel.cpp` `renderGamePanel`（drawTopBar/drawPlayerBar/drawPlayerInfoPanel/drawMiniMap/drawCalendar） |
| 状态 | 已实现（布局 0/1/2 完整；物價指數、4 页签统计与切换、页签点击均已还原） |

## 功能

游戏内右侧面板与顶部工具条绘制，按 `byte_49715D` 选择布局：

| 布局 | 组成 |
|------|------|
| 0 | `sub_415F69`(440,0-280) + `sub_4169BC`(440,280-480) |
| 1 | `sub_415F69`(440,0-280) + `sub_416E6D`(小地图 y=280) |
| 2 | `sub_4166F8`(440,0-80) + `sub_416E6D`(小地图 y=80) + `sub_4169BC`(日历) |

`byte_49715D` 来自 `RICH4.CFG`（默认 2）。`dword_4752AA[布局]` 给出小地图 y（0/280/80）。

## 逆向依据

- 反编译观察：
  - `sub_415D31`：panel.mkf[1] 帧 0（439x40）画到 (0,0)；11 个按钮帧 1+i 画在 (40*i+20,20)；
    高亮帧 12+i（`dword_48BDE4`）；原版 `(u16*)dword_475118 + 6*i + 12` = 帧 i+1（+12 主头）
  - `sub_4166F8`：panel.mkf[0] 帧 4（= `+60` → 200x80）画到 (440,0)；颜色条 + 棋子图标 + 名字/现金/存款
  - `sub_4169BC`：`byte_497164`（日历模式）二分支——0=大数字（背景 `byte_475218[月-1]`；`sub_4521F0`
    命中节日 → `data.mkf[word_475208[4*mode+map]+节日idx]` 专属背景；星期名 + 60 号大「日」）；
    ≠0=月历网格（背景帧 `+4`；1..N 数字，`sub_4523D5` 星期日/节日红字，当日画框）；
    两模式共有年(580,288)/月(500,328)；点击日历顶栏 x∈[448,474]→mode0、[478,504]→mode1（`0x417E26`）
  - `sub_416E6D`：小地图 `dword_48BADC`（`sub_40A4E1` 工作副本）画到 (440, `kMiniMapY`)，玩家标记
    `g_pieceSprites[13*i]+84` = **帧 6**（缩放 5696/65536）；旋转箭头 data.mkf[517] 帧 20/21 画到
    (443/468, y+3)（按下态帧 18/19，见 `0x417E26`）；当前玩家框白色 `(v8-15, v9-15, 30, 30)`；
    `dword_48BE18` 时视野框红色 `(v5+425, y+v6-15, 30, 30)`
  - `sub_40A4E1(a1)`：`blitBackground(BADC, BAD0)` 拷原图后叠加 `estates/corps/specPts` 中 owner≠0 的
    玩家色块（帧 `(dir&1)+22/24`，缩放 5696；a1=1 时帧 26/28、缩放 11392 供大地图）；原版在物件变化时
    （买地/过天/玩家行动等 22 处）反复调用
  - `sub_40A801`（大地图对话框）：背景 `dword_48BADC+24`（帧1，400x400）画到 (20,60)；玩家标记
    `g_pieceSprites[13*i]+72` = **帧 5**（缩放 11392；与小地图帧 6 不同）
- 字符串/资源：panel.mkf[0..8]、`kTabText`(off_475274)、`kMonthName`(off_47511C)、
  `kMonthFrame`(byte_475218)、`kCalendarRes`(word_475208)
- 全局变量：`dword_48BE0C/475118/48BE10/48BE14/48BE04/48BE08/48BDF8`（panel.mkf 帧）

## 关键结构

```c
// 布局 2（CFG 默认）:
//   (440,0)-(640,80)    玩家条   panel.mkf[0] 帧 5
//   (440,80)-(640,280)  小地图   map.mkf[base+16]
//   (440,280)-(640,480) 日历     panel.mkf[2]
// 金额格式 sub_452793: 千分位（'$' 前缀由 byte_415D1D 模板提供）
```

## 重写要点

- **颜色**：原版 `1052688`=0x101010（深灰）、`16711680`=0xFF0000（红）。
- **文本坐标**：drawText 的 align 0 左上 / 1 右上 / 2 居中（与原版一致）。
- **差异**：
  - ~~`sub_415F69` 的 4 页签（資金/地產/股票/其他）统计与切换阶段 2 接入~~ →
    ✅ 已完整（4 页签统计 @(600,102/166/230) + 页签点击 0x4182FA，见 `query_dialog.cpp`/
    `ui-controls.md` §27）。
  - 事件槽回合（`currentPlayer` 4..7）NPC 面板分支 **✅ 2026-09-26**（原版帧 5 背景 +
    `dword_47ED5A` NPC 名 @(582,40) + bailer 棋子帧 2 @(524,64)；`game_panel.cpp`
    `drawPlayerInfoPanel` NPC 分支）；`currentPlayer==8` 機器娃娃沿用**槽 8 bailer**
    （`byte_498E70` = `npcSlots[4].bailer`，0x416118/0x4167F4；旧 `workerPlayer` 字段已并入槽记录）。
  - `sub_416E6D` 小地图已使用 `sub_40A4E1` 叠加标记后的工作副本
    （`dword_48BADC`，见 map_render.cpp `buildMiniMapMarks`）；**每帧重建**以随物件变化更新
    （原版在 22 处变化点调用）；玩家标记帧 6、大地图帧 5 已对齐。
  - 小地图旋转箭头/玩家框（白）/视野框（红）见 game_panel.cpp `drawMiniMap`；
    箭头按下态复用 `pendingAction`（对应原版 `byte_48BE28`）。
  - 小地图右键取消手动视角（`dword_48BE18=0`）见 game_loop.cpp `handleRightButtonUp`；
    左键抬起无条件结束拖拽（`byte_48BE29=0`）见 `handleLeftButtonUp`。
  - 原版 `dword_475110` 脏区标志 → 重写每 tick 全量重绘。位语义（`sub_4192F7` 逐位局部 blit
    后清位，`0x1F` 全屏；`sub_41906A(1)` 全屏重绘并清零）：bit0 工具条 (0,0)-(440,40)、
    bit1 地图 (0,40)-(440,480)、bit2 玩家条 (440,0)-(640,80)、bit3 小地图 (440,80)-(640,280)、
    bit4 日历 (440,280)-(640,480)；`drawPlayerInfoPanelFull/Slim` 入口条件分别查 `&0xC`/`&4`。
  - 日历：三显示效果（大数字普通 / 大数字节日背景 / 月历网格）已实现；节日表 `kHoliday[8][24][5]`
    由 `tools/gen_map_tables.py` 从 `byte_47FF4A..` 提取（type 0=固定月日、2=第N个星期X；
    type 1 浮动节日依赖 `dword_47639C` 暂未实现）；节日背景为 RAW 200x200（`decodeRawBitmap`）。
  - ⚠️ 帮助 `[HELP 2]` 提到的"日期闪烁=预定事件 / 点日期看内容 / 点月份翻阅其他月份"
    **原版引擎未实现**（`sub_4169BC` 无 blink、`gameWndProc` 月历网格数字区不响应点击、
    exe 内无 per-date 事件表）——属帮助宣传设想，非重写缺口；原版真实行为（双模式切换
    x∈[448,474]/[478,504]、当日红框、节日红字、节日插画、年/月显示）已全部覆盖。详见 `help-checklist.md` §9-2。
  - 大地图对话框（`sub_40A801`）暂未叠加物件标记（`sub_40A4E1(1)`）。

## 物價指數显示核对（2026-09-25）

> 帮助 `[HELP 5]`；右侧完整面板（布局 0/1）的物價指數文字，重写 `game_panel.cpp`
> `drawPlayerInfoPanel`。核对结论：**与原版逐项一致**。

- **绘制链（0x415F69）**：`blitElementFullscreen(背景帧 = panel.mkf[0] 帧 tab @440,0)` →
  `setTextFont(12, 0x101010, 0, style=2(粗体), spacing=1)` @0x416165 →
  `sprintf("物價指數  %d", g_moneyMul)` @0x41617e → `drawText(450, 260, align=0)` @0x416199。
- **格式串池 0x4638F5**（BIG5 实测，共享）：`物價指數  %d` / `%d天` / `%d月` / `連鎖店` /
  `×%d` / `餘股:%d` / `現  金` / `存  款` / `總資產`。
- **显示条件**：入口 `g_panelLayout != 2 && (dword_475110 & 0xC) != 0xC`，且在
  `currentPlayer < g_playerCount || currentPlayer == 8` 分支内；**任何 tab 都绘制**（位于
  页签 switch 之前）。布局 2 的玩家条（`drawPlayerInfoPanelSlim` 0x4166F8）**不含**此文字。
- **`currentPlayer == 8`（機器娃娃虚拟槽）**：原版三个函数均取
  `byte_498E70`（**槽 8 记录 bailer = `npcSlots[4].bailer`**）作为实际玩家——完整面板 0x416118、
  玩家条 0x4167F4、`nextPlayer` 0x418EDA；重写已还原（`drawPlayerInfoPanel` / `drawPlayerBar`）。
- **刷新时机**：原版由 `dword_475110` 脏区位驱动（bit2 玩家条区 + bit3 小地图区 = `0xC`），
  `sub_4192F7` 局部 blit 后清位；重写逐帧全量重绘，数值每日 `updateMoneyIndex`
  （0x423ACF，`advanceDay` 内）更新后立即生效，无可见差异。

## 验证方式

`--shot` 截图布局 2：顶部 11 按钮、玩家条（头像/名字/现金/存款）、
小地图（台湾 + 玩家标记 + 白色当前框 + 左右旋转箭头）、日历（1998 年 1 月，1 号红色）。
`--game-drag` 拖拽小地图后显示红色视野框；`--game-rclick` 小地图右键回到当前玩家视角。

物價指數核对（2026-09-25）：设为布局 0/1（设置 → 显示单选，`settings[5]`）后右侧完整面板
(450,260) 应显示「物價指數  N」；`Ctrl+7` 加现金 / `Ctrl+Shift+D` 过天后数字上升（只升不降）。
機器娃娃：`Ctrl+Shift+F` 发全道具 → 道具栏使用機器娃娃（虚拟槽 8）期间，布局 0/1 完整面板
与布局 2 玩家条均应显示**发起玩家**资料（槽 8 bailer），面板不消失、玩家不错位。
