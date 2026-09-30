# M4 执行计划（渲染/UI 现代化 + 追加体验优化）

> 状态：**方案定稿（讨论稿）**。本档为交接/执行文档，含逆向依据（地址）、改造点（`文件:行号`）、
> 提交粒度与回归门禁。执行前先读 `AGENTS.md` 与 `docs/cross-platform.md`。
>
> 前置（2026-09-30）：M3 完成（终局链 ✅、演出打断/GO 层级/物件提示演出屏蔽 ✅、跨平台冻结 ✅；
> L0 67/67、L1 93 场景两平台一致、全量矩阵 91/91 绿）。
>
> 用户口径（2026-09-30）：
> 1. **当前阶段不做太多破坏原版操作方式的改动** —— 优先"修正缺陷 + 现代化呈现"，
>    操作/规则层面的改动（射程、键盘操作、面板选择）默认放到可配开关或后续阶段；
> 2. 先出本方案文档，再按批次实施；
> 3. 先落地「喜從天降判定修正」（见 §9.3，**已完成**）。

## 0. 总览与优先级

> **进度（2026-09-30）**
> - **批次 0 已入库**：`b3f2622`（喜從天降娃娃跟随判定 0x4135F4 + L0）、`1dc28a3`（单测 stdout
>   关缓冲）、`94ca907`（本档）。
> - **批次 B（GO 面板）✅ 已实施**：C1（窄谓词 `blockingPerf`）+ C2（4 个控制位入口）；C3 判定
>   不需要。验收场景 `308_go_panel_surrender`——**修前 FAIL（窗口内 75 次 `cur=0` 面板绘制）、
>   修后 PASS（0 次）**，判别力已实测。详见 §14。
> - **下一步 = A1**（画布运行期尺寸；实测替换面仅 17 文件 95 处，见 §1.1 订正）。

| 批次 | 内容 | 依赖 | 风险 | 建议顺序 |
|------|------|------|------|----------|
| **A** | 渲染核心：画布参数化（"渲染即输出"）| — | 中（触及所有 blit/文本调用点） | 1 |
| **B** | GO 面板层级 + 控制位语义（C1/C2；C3 判定不需要） **✅ 2026-09-30** | — | 低（局部） | 2 |
| **C** | 脏区重绘还原 + UI 布局表 + 命中派生 | A | 中 | 3 |
| **D** | 宽屏视口（等距表扩展 / 光栅化 / FLC 锚定） | A、C | 中高 | 4 |
| **E** | 文本高分渲染 | A | 低 | 5 |
| **F** | 配置系统（`rich4.ini`） | — | 低 | 6 |
| **G** | 源码结构重组 | — | 低（.cpp）/ 中（头文件） | 7 |
| **H** | 追加体验（射程 / 预览高亮 / 小游戏 / 同格消歧） | A–F | 中 | 8 |

**不变式（每条提交都必须满足）**：

1. **`native` 保真模式逐字节不变** —— 现有 93 场景 + `--save-selftest` + L0 是回归网；
   渲染重构期间任何"画面看起来不同"都必须能区分"重构引入"与"本来如此"。
2. **不修改 `RICH4.CFG` 的格式与语义**（72B：16B 设置 + 28×u16 键位；原版 exe 同读写）。
3. 逆向双落：源码 `[RE 0xXXXXXX]`/`[PORT ...]` 标注 + `docs/reverse/` 档案 + IDB 注释，
   提交前 `python tools/re_map.py`。
4. 每个可测功能一个 `wip:` 提交；不把 `wip:` 当里程碑。

---

## 1. M4-A 渲染核心：画布参数化（"渲染即输出"）

### 1.1 现状（实测）

| 环节 | 现状 | 位置 |
|------|------|------|
| 画布 | `Surface` 640×480 RGB555，`kWidth/kHeight` 为 `static constexpr` | `include/game/render/surface.h:18-19` |
| 上屏 | 每帧 `SDL_UpdateTexture` + `SDL_RenderTexture`，**NEAREST** | `src/render/surface.cpp:18-41` |
| 缩放 | `SDL_LOGICAL_PRESENTATION_LETTERBOX`（640×480 逻辑） | `src/platform/renderer.cpp:31-36` |
| 窗口 | 1280×960、`HIGH_PIXEL_DENSITY`、可缩放但**无 resize/全屏处理** | `src/application.cpp:43`、`src/platform/window.cpp:10` |
| 文本 | FreeType → **512×200 RGB555 临时缓冲** → blit 进画布；字号 = 设计字号 × `kFontSizeScale(1.09)` | `src/render/text.cpp:22-33,278` |
| 光标 | 软件光标，保存/恢复 32×32 背景，画在画布内 | `src/render/cursor.cpp` |

调用面（**2026-09-30 实测订正**）：`app.surface()` **100 处调用**、
`Surface::kWidth/kHeight` **95 处 / 17 个文件**（最多：`render/blit.cpp` 12、`app/turn_system.cpp` 10、
`app/bank_stay_dialog.cpp` 8、`app/stock_market_dialog.cpp` 8）——原文"100 个文件 / 70 个文件"
是**把调用次数当成了文件数**，A1 的机械替换面比原估小得多；`drawText` **336 处**，
`setFont` 数字字面量 **149 处**（16 号 59、20 号 31、12 号 15…）。

原版侧对应：`SetDisplayMode(640,480,16)` @0x401643；primary `ddsCaps=0x200`
（`DDSCAPS_PRIMARYSURFACE`，**不是 512×512 表面**）；backbuffer 640×480 @0x4016DC/E6；
`g_pickBuffer = malloc(0x5E880=387200)` @0x4017E6；blitter 内 **63 处硬编码行距 640**；
非地图 UI 走 `blitElementFullscreen` @0x4563F5 = `blitElementOpaque(640,480,…)`。

### 1.2 目标模型

```
输出画布 = drawable 像素（窗口像素；HiDPI 下含 DPI 缩放）
 ├─ 场景层：世界位图按 worldScale 绘制（默认 1:1，保像素画锐利）
 ├─ UI 层：UI 位图按 uiScale 绘制（可配 nearest/linear）
 └─ 文本：FreeType 直接以 设计字号 × scale 光栅化（1:1 输出，无二次放大）
```

- **文本不引入"独立文本层"**：`docs/cross-platform.md` §9 已论证独立文本层会破坏 8 类遮挡语义
  （13 处快照回写擦字、9 处 `pressDown`/变暗含文本变换、FLC 双 z 序、`showGodNarration` 冻结帧、
  `saveBmp` 截图链）。**画布即输出**则文本仍在同一像素缓冲、同一 z 序，遮挡语义零改动。
- `scale` 由 `Surface` 统一吸收：位图 blit 按 scale 采样，`fillRect`/clip/文本按 scale 变换
  → 338 处 `drawText`、100 处 `surface()` 调用点基本无需改动。
- 唯一必须机械替换的：`Surface::kWidth/kHeight`（70 个文件，作为像素步长）→ `dst.width()/height()`。

### 1.3 改造点清单

1. `Surface`：`kWidth/kHeight` → 运行期 `m_width/m_height`；新增 `scale`、`logicalToDevice()`；
   保留 640×480 构造入口（native 模式）。
2. blitter 家族（`src/render/blit.cpp`、`iso_blit.cpp`）：行距由画布宽度取；新增
   **缩放 blit**（定点采样；nearest/linear 可配；UI 位图可按 `(资源,scale)` 缓存缩放副本，
   之后仍 1:1 拷贝）。
3. `blitElementFullscreen` 等价物：640×480 硬编码 → 布局派生（对应 63 处原版 640）。
4. 文本：`drawText` 内 `px = fontSize * kFontSizeScale * scale`；阴影 `(1,1)` 与描边
   `kDx/kDy{1,1,0,2}`（`text.cpp:450-456`）**乘 scale**（否则高分下阴影不可见）；
   建议改用 `FT_Set_Char_Size` 26.6 定点 + 亚像素 advance，避免非整数倍下字距抖动。
5. 软件光标：32×32 保存/恢复按 scale 处理；或评估演出外改用硬件光标。
6. 显示 preset（`native` 为默认）：

| preset | 画布 | scale | 滤镜 | 用途 |
|---|---|---|---|---|
| `native` | 640×480 | 1.0 | nearest | **回归基线**（逐字节），玩家可选"原汁原味" |
| `wide` | 1024×768 / 1280×720 / 1280×800 / 窗口 | `uiScale = H/480`、`worldScale` 可配 | nearest/linear | 宽屏体验 |
| `free` | drawable 像素 | 自动 | 可配 | 自由缩放窗口（需新增 resize 处理） |

7. 新增窗口事件处理：`SDL_EVENT_WINDOW_RESIZED` → 重建画布与布局（当前全库无此处理）。

### 1.4 风险与回退

- 最大工作量：blitter 行距 + 70 个文件的步长替换；建议**先只做"运行期尺寸"**（scale 恒 1），
  跑通全量矩阵后再引入 scale，分两次提交。
- 回退：`native` 路径保持旧行为，任何阶段可直接切回。

---

## 2. M4-B GO 面板层级 + 控制位语义（先做，独立可验收）

### 2.1 根因（IDA 实证）

| 事实 | 地址 |
|------|------|
| 前进面板是**一次性增量绘制**：`sub_417191(a1)`，a1≠0 重画 / **a1==0 只擦除**（唯一 a1=0 在 `showObjectTip`） | 0x417191、0x4175D5 |
| `disablePlayerControl` **连带擦除面板**：`byte_46CAFD=0; jmp 0x4196FA` | 0x419703 |
| disable 的 5 个调用点后**都紧跟缓存面恢复**（`sub_415D31(1)` 恢复地图区 / `sub_41D546`） | 0x415E4C、0x4182E6 等 |
| 任何与面板矩形相交的重绘都会**自动补画**面板 | `sub_4174CD` 0x4174CD（10 处调用） |
| 阻塞演出**不派发消息**（全程序无 `DispatchMessage`）→ 动画期间 WM_PAINT/WM_TIMER 被丢弃 | `sub_45144F` 0x4514CF、`sub_4528B9`、`sub_4544F6` |
| `beginPlayerTurn` 里跳伞 FLC 在 `enablePlayerControl` **之前**，enable 是函数末尾动作 | 0x418CA9 → 0x418D99 |

→ 原版 GO 面板只存在于"纯等输入"窗口；演出期间不可能重画。

### 2.2 重写根因

| # | 根因 | 位置 |
|---|------|------|
| **B1 主因** | 工具条收尾只要 `playerActionState==0 && sceneRequest==0` 就 `gamePlayerControl=true`（**不看 pending 演出**）；`beginPlayerTurn` 的 AI 分支**从不清** control → 一旦置 true，此后整局 AI 回合/掷骰/移动/新闻·命运 FLC 都带 GO。最小复现：设置→認輸投降 | `src/app/game_loop.cpp:234-236`、`src/app/turn_system.cpp:3115-3158` |
| **B2** | 工具条读档后同样置 true；档内当前玩家未落地时下一 tick 走 `spawnPlayerAt+updateParachute` 并 return（`beginPlayerTurn` 不跑，无人关 control）→ 跳伞期间 GO 可见 | `src/app/new_game.cpp:190`、`turn_system.cpp:4153-4161`、`map_render.cpp:173-175` |
| **B3** | `disablePlayerControl` 只翻 bool **不擦像素**；快照型演出（`snapBg`/`playHighlightBlink(preRedraw=false)`/`flyObjectSprite`）可能把面板烘进背景 | `turn_system.cpp:3175-3177/1182-1198/1091`、`map_objects.cpp:1040-1050` |
| **B4** | M3-B 的层级重排只能"盖"：`drawEventFlcFrame` 只跳 colorKey，**大面积透明处露出 GO**；且 524 住院是 440×74 横带 @(0,210)，GO@(180,120) 在其上方必然可见 | `game_panel.cpp:589/753`、`game_loop.cpp:657-667` |

### 2.3 方案

| 方案 | 内容 | 代价 |
|------|------|------|
| **C2（治本，先做）** | 收尾 enable 增加条件 `pendingSpawnPlayer==0 && !eventFlcActive && !surrenderRequest && playerActionWait[cur]==0`；`beginPlayerTurn` case2/5、`surrenderPlayer`/`nextPlayer` 入口显式 `disablePlayerControl`（对齐 0x417D65/0x4182E6/0x418C55 均保证 `byte_46CAFD=0`） | 小 |
| **C1（同时做，止血）** | `drawAdvancePanel` 守卫用**窄谓词** `blockingPerf`（仅事件 FLC/跳伞/待入场；**不含**视口滚动与模态——原版这两类窗口面板本来就在画面上，直接复用 `tipPerfBlocked` 会误伤） | 1 行 |
| **C3（防御）——判定不需要** | `disablePlayerControl` 后补表面恢复（等价 `sub_415D31(1)`）：C1+C2 已覆盖全部入口——控制位为 0 后 `drawAdvancePanel` 首个守卫即返回；快照型演出（`snapBg`/`flyObjectSprite`/`highlightBlink(preRedraw=false)`）都发生在控制位已为 0 的移动/结算段 | — |
| **C4（可选）** | 面板改为"一次性增量层 + 有效标记"，实现 `sub_4174CD` 语义（矩形相交且 control 才补画） | 视实机验收决定 |

### 2.4 验收（✅ 2026-09-30 实施）

- ✅ `tests/scenarios/308_go_panel_surrender`：`surrender; trace.clear` **同帧并列**（`execLine`
  分号）——投降请求置位后立刻清零，此后任何 `go panel draw cur=0` 都属演出期
  （p0 淘汰后回合只会交给 1 号人类 = `cur=1`）。实测**修前 FAIL（75 次）/修后 PASS（0 次）**。
  配套：`drawAdvancePanel` 加 `trace go panel draw cur=%d`；新增调试命令 `trace.clear`
  （脚本断言"某段之后不再发生 X"的必要前置）。
- ⬜ 读档残留未落地玩家 → 跳伞期间无 GO（`canResume` 已加 `!blockingPerf`，场景待补：需先造出
  "档内当前玩家未落地"的档）。
- ⬜ 人工：默认开局全流程无"动画上出现 GO"（已知三入口已消除，待实机复核）。

---

## 3. M4-C 脏区重绘 + UI 布局表 + 命中派生

### 3.1 脏区重绘还原

原版 `dword_475110` 五位（`docs/reverse/functions/415d31-game-panel.md`）：

| bit | 区域 | 现代对应 |
|---|---|---|
| 0 | 工具条 (0,0)-(440,40) | 顶栏 |
| 1 | 地图 (0,40)-(440,480) | 场景视口 |
| 2 | 玩家条 (440,0)-(640,80) | 右栏上 |
| 3 | 小地图 (440,80)-(640,280) | 右栏中 |
| 4 | 日历 (440,280)-(640,480) | 右栏下 |

`sub_4192F7` 逐位局部 blit 后清位（`0x1F` = 全量）；`sub_41906A(1)` = 全量重绘并清零；
`drawPlayerInfoPanelFull/Slim` 入口分别查 `&0xC`/`&4`。重写现状是**每 16ms 无条件全量重绘**
（`src/app/game_loop.cpp:531-533`）。

- 收益：① 从架构上消除"演出期面板/提示复活"一类 bug；② 宽屏性能的前提。
- 改造点：引入 `dirtyFlags` + `compose()`；100+ 处 `renderGameFrame` 调用点语义化为"标脏 X"。
- 注意：区域矩形需与布局表（§3.2）统一，宽屏后由布局派生而非硬编码。

### 3.2 UI 布局表 + 命中派生

```cpp
enum class Anchor { TopLeft, TopCenter, Right, Center, BottomCenter };
struct UiSlot { Anchor anchor; int dx, dy; float scale; int z; UiVisibility when; };
```

- 需要收口的布局常量分布（实测）：地图区裁剪 `(0,40)-(440,480)`（`map_render.cpp:203`、
  `map_objects.cpp:562`）、视口中心 `+220/+260`（`map_render.cpp:27-28`）、右栏起点 440
  （`game_panel.cpp` 11 处 + `game_loop.cpp` 9 处 + `bank_stay_dialog.cpp:67`）、顶栏高 40
  （`game_loop.cpp:39`）、对话框居中 `320-w/2, 240-h/2`（8 个文件）、`kMiniMapW/H=200`、
  `advancePanelX/Y=180/120`。
- **命中区必须从同一张表派生**（现为手写常量：`game_loop.cpp:368-460` 工具条/页签/日历/前进面板、
  `game_panel.cpp` 440 系、8 个对话框的 `originX/Y`；见 `docs/reverse/ui-controls.md` §2 的三套坐标基准）。
  否则宽屏后所有点击坐标都会错位。
- 顺带实现两个**死配置**（原版有此设置但重写无消费点）：`settings[0]`（遊戲速度 → tick 间隔/演出
  `delayMs` 倍率）、`settings[6..8]`（日曆/縮小地圖/組合畫面 → 布局表）。

---

## 4. M4-D 宽屏视口

### 4.1 硬约束（IDA 实测）

| 约束 | 证据 |
|---|---|
| 地图裁剪矩形**只有一个写入点**（常量 `(0,40,440,480)`） | `sub_45577C` 0x45577C，唯一调用 0x418111 |
| 非地图 UI 绕过该裁剪（`blitElementFullscreen` = 640×480 硬编码） | 0x4563F5 |
| FLC 落点 = 后缓冲绝对 (x,y) + 素材自身 w×h，行距 1280 | `flcOpen` 0x450CED（`unk_48C864=1280`） |
| 场景切换快照面 = **440×440**、pitch 880、偏移 `2*(x+440*(y-40))`、`malloc(0x5E880)` | 0x450E3C、0x4017E6 |
| 地图区基址 `51200 = 40×1280` | 0x450E82、`highlightBlink` 0x451985 反证 |
| blitter 内 **63 处**硬编码 640 行距 | `find immediate 640` |
| **事件动画素材全部 440 宽**（523/525/527/529/…/555/556/559 皆 440×440；524=440×74；526=110×110；536=28×40；537=31×39） | 解压 Data.mkf 读头 + 调用点实参 |

→ 宽屏下动画**铺不满**更宽的地图区：保持 1:1 与锚点语义，动画期用"快照冻结 + 叠加"，
允许把 440 宽动画在宽地图区内**水平居中**；**不要缩放/拉伸像素画**。

### 4.2 等距投影表扩展（本次数据验证）

- 表 = `0x46CCF0`，`int16[8][29][29][2]`，索引 `3364*dir + 116*(row+14) + 4*(col+14)`；
  是**格点表**（地块四边形 = `T[r][c],T[r][c+1],T[r+1][c],T[r+1][c+1]`）。
- `kIsoCoeff`（0x474910）**只被 `sub_407A2C` 使用**，仅负责**格内亚像素相位**
  （`outX=(xo*c0+yo*c2)>>5`）；**格间位移完全来自预计算表** →
  **不能用 coeff 反推整表**（实测外推误差达 42px，量级错）。
- 表的真实性质：**仿射 + 舍入**（8 方向最小二乘残差 1.68–2.02px）→ 可用实测步长外推，
  但因四边形取自**共享格点**，只要格点函数自洽即天然无接缝。
- 半径需求（半窗口）：640×440 → **13**（现有 14 刚好）；1024×768 → 21；1280×720 → 25；
  1280×800 → 26。**建议 R=32（65×65）**，用 `tools/gen_map_tables.py` 扩参重生成，
  并以**原 29×29 窗口逐项回归校验**。注意四边形需 `row+1/col+1` → **遍历半径比表半径小 1**。
- 遍历序 `0x473610`（8×296 项）是"覆盖带"而非排序结果（可见集超集，行主序）；地面菱形互不重叠
  → **宽屏模式可完全去表化**，直接在"与视口相交的 (r,c) 区间"双循环；native 保真模式保留原表。

### 4.3 性能预算与优化

| 项 | 现在 | 1280×800 |
|---|---|---|
| 地砖绘制数 | 216 | ~1000–1300（**5–6×**） |
| 每帧填充像素 | 19.4 万 | ~100 万（**5.3×**） |

- 第一瓶颈：`isoBlitQuad`（`src/render/iso_blit.cpp`）**逐像素浮点**仿射贴图。
  优化：定点化 + 每方向预计算行 LUT；或每方向预旋转瓦片位图（8 份缓存）→ 退化为整行拷贝。
- 配合 §3.1 脏区重绘（世界不动时不重画）。
- 建议先加**帧耗时统计**（`--stats`/FPS 覆盖层），宽屏改造前先有基线。

---

## 5. M4-E 文本高分渲染

- 画布即输出（§1.2）后，文本天然 1:1 → 无需独立文本层。
- 必做细节：阴影/描边偏移乘 scale；`kFontSizeScale` 与整数字号取整 → 定点 + 亚像素 advance；
  字形缓存键已含 `px`（`glyphKey`）↔ 多缩放天然兼容。
- 字体路径配置化 + 缺字回退链（HarmonyOS Sans SC → 系统字体）随 §6 一起做。
- 现状见 `docs/cross-platform.md` §4/§9。

---

## 6. M4-F 配置系统（`rich4.ini`）

**红线：不往 `RICH4.CFG` 塞新字段**（72B 定长、原版 exe 同读写、`settings[8..11]` 与日期字段重叠）。

```ini
# rich4.ini（置于 writableDataDir；游戏目录可写则用游戏目录）
[display]
preset = native|wide|free ; canvasPreset = 1280x720 ; uiScale = 1.5 ; worldScale = 1.0
filter = nearest|linear ; vsync = 1 ; fullscreen = 0 ; integerScale = 1
[text]
fontRegular = resources/Fonts/HarmonyOS_Sans_SC_Regular.ttf
fontBold    = resources/Fonts/HarmonyOS_Sans_SC_Bold.ttf
sizeScale   = 1.09 ; hinting = normal
[paths]
mediaDir = <gameDir>/Media ; logFile = rich4.log ; screenshotDir = .
[input] / [audio] / [debug] ...
```

- 优先级：**CLI 覆盖 > `rich4.ini` > `RICH4.CFG` > 内置默认**；新增通用 `--set k=v`（保留现有
  `--game/--seed/...`）。
- 实现：`src/core/config.{h,cpp}`（默认值 + 注释自生成 + 版本迁移）；`src/core/paths.cpp` 语义不动。
- 路径字面量收口点（实测 25 处，默认值 = 当前硬编码值）：字体 `src/render/text.cpp:32-33`、
  媒体目录 `src/application.cpp:26-28`、日志 `src/core/log.cpp:64/80`、`--game` 默认
  `src/main.cpp:35`、截图/`load_shot_s*.bmp`/selftest 工作目录 `src/main.cpp:346/278-300` 等。
- 布局类候选键 ≈ 30 处裸字面量（440/40/640/480/160/200…），随 §3.2 的布局表一起接入。

---

## 7. M4-G 源码结构重组

**影响面结论**：`CMakeLists.txt:79` 为 `GLOB_RECURSE src/*.cpp` → **.cpp 搬目录零成本**；
头文件在 `include/`（include root）且以 `game/app/x.h` 引用 → **头文件搬目录需全仓改 63 处 include**。
建议**先只动 .cpp**，头文件同步搬迁单独立项。

现状：`src/` 92 文件 / 47,453 行，`src/app/` **64 文件 / 40,482 行（85%）**；
最大单档 `turn_system.cpp` 4,248 行、`debug/debug.cpp` 2,813 行、`big5_tables.cpp` 2,024 行（生成）。
`src/game/`、`include/game/game/` 为**空目录残留**（可删）。

目标结构（.cpp 先行）：

```
src/
  main.cpp
  app/            application.cpp（拆分：外壳/消息泵 | 调试注入钩子）
  core/           log clock paths rng encoding trace config(new) dirty(new)
  platform/       window renderer(SDL) input audio
  render/         surface(画布+scale) blit scale(new) text cursor fli ui_image layout(new)
  resource/       mkf spr smp lzhuf
  game/           turn/ landing/ rent/ god/ npc_slot/ card_bag/ date/ map/ economy/ ai/
  ui/             hud(game_panel) dialogs/(40+ *_dialog) widgets/
  debug/          debug（按命令组拆分）+ debug_keys
  gen/            *_tables.cpp（生成物集中，标注生成脚本）
```

拆分优先级：① `turn_system.cpp`（12 类职责）② `debug/debug.cpp` ③ `application.cpp`（55% 是调试
截图 API）④ `game_loop.cpp` ⑤ 生成物入 `gen/`。

---

## 8. M4-H 追加体验（四项）

### 8.1 道具/卡片使用范围（射程）

**原版真相（IDA 逐字节复核）**：人类选目标的**唯一空间判据 = 目标锚点像素落在当前 440×440
拾取缓冲内**（`sub_409DE7` 0x409EA3 / `rebuildPickBuffer` 0x409B18）；32 个调用点里**只有 3 个
mode 带 `0x80` 可滚屏**：飛彈 `0x300C0`、核子飛彈 `0x400C0`、**房地產公司 `0x2090086`**；
**无半径/距离/同路段射程**（BYTE1 只做类型语义）。

订正（并入逆向档案，见 §11）：
- `mode` 字节：byte0=flags、byte1=BYTE1 过滤、**byte2 = `cursorSelect` index**、
  **byte3 = frameCount-1**（`0x445ED8`）；
- `0x40` 是宏：`flags = (原 flags & 0x80) | 0x37` 且**清 BYTE1** → `0x40` 单独 = `0x37`（**不能滚屏**）；
  **滚屏只来自 `0x80`**；重写 `target_select_dialog.cpp:358-361` 对 `0x40` 恒置 `0x80|0x37` 略偏
  （当前无调用点命中）；
- 半径版 `sub_40A45C(a1)` 的**中心是视口中心**（0x40A48F `441*(220-a1)`）；以任意世界坐标为中心的
  是 **`sub_40A0B1`**（仅 2 个 AI 调用者）→ 重写 `collectAround`/`nearIso` 对应的是后者（正确）；
- 飛彈 = `expireAssets(100,38,0,cur)`（±100 屏幕像素方块）；核彈 = `expireAssets(-1,38,1,cur)`
  = **"视口对准目标后的一屏 440×440"**（不是全图）。

**AI 侧**：视口实体 / 视口格 / **视口∩路径** 三种候选域；**唯一全图的是 `aiTgtNuke`**。
→ 若玩家限制 = 视口，AI **天然一致**；若改成欧氏半径，**必须同步改 AI**，且核弹 AI 保留全图、
地雷/炸彈/路障保留"视口∩路径"。注意**视口在世界空间是菱形，与圆不等价**——"半径 R"属新规则。

**三档方案**：**(a)** 严格原版（视口 + 3 个滚屏 mode；并把重写已"全图化"的飛彈/核彈 AI 判据回退）；
**(b)** 自定语义射程表（玩家与 AI 共用，逐项定标）；**(c)** 可配（默认 a，开关切 b）。

配套：超射程目标给"禁止光标 + 提示"；射程 > 一屏的目标提供快速定位（复用大地图
`mapDialog` 的小地图点击跳转）。

### 8.2 目标区域实时高亮（预览）

**已有原语**：`orColorMask`（OR 蒙膜、**遵守全局 clip rect**，`map_render.cpp:85`）、
`drawEstateHighlight`（`kHighlightLut[16]` → `kHlTable[9][32]`，dump 自 0x485D68，`:1012`）、
`mapHitRegions[].shape/shapeFrame/anchorX/Y`、`flagTiles` = **`map.mkf[26]` 5 帧菱形**
（71×47 / 71×51 / 143×103 / 143×103 / 51×51 → 单格/地块/路段**零新素材**）、
`collectSameRouteEstates`（`card_effects.cpp:449`，**未声明**）、`expireAssets` 屏幕空间范围口径
（`economy.cpp:377-393`）。

**缺口**：① 非阻塞逐帧高亮驱动器（**模态不会自动重绘**：`runModal` 只在入口画一次，
`event_stack.cpp:54-103`）② 半径/区域指示原语（全库无 >143×103 的菱形素材，须纯代码）
③ 亮化 LUT 复用导出 ④ `collectSameRouteEstates` 头声明。

**约束**：预览与效果**必须共用同一范围谓词**（抽 `hitInRange()` 自 `economy.cpp:381-393`，
WYSIWYG）；`drawRectBorder`/`scaleSurfaceChannels`/`drawEstateHighlight` **都不读全局 clip rect**
→ 必须手动 clamp 到地图区（宽屏后改为布局派生）；全局 `highlightFrame/…` 是单份，悬停预览
只在模态内写、退出即清。

**顺带保真缺口**：`target_select_dialog` 未实现原版 `(flags&8)==0` 的取消守卫。

### 8.3 小游戏（平滑 + 喜從天降）

- 原版 tick（IDA `SetTimer` 复核）：挖寶 `0x4148e3=100ms`、氣球 `0x414c42=100ms`、
  接錢 `0x415034=50ms`；重写一致（`minigame_dialog.cpp:663/1358/1075`）。
- **不要改 `tickMs`**：HUD 倒计时按 tick 直显、结算按 tick 数、**rng 是"每 tick 一次"**
  （气球生成 `:1141`、袋型 `:742`、云触发 `:846/872/901`）、headless 迭代数 = 时长/tick
  → 改 tick 会破坏同 seed 序列与跨平台 PRNG 基线。
- **推荐：新增 `kModalFrameEvent` 做渲染插值**（`event_stack.h:15` 旁；循环 `event_stack.cpp:74-94`
  每轮派发，`user.code` = 本 tick 已过毫秒；**虚拟时钟下不派发** → headless 行为零变化）。
  插值落点：挖寶 `:468-469`、氣球 `:1134`、喜從天降 `:801-806`/`:822-839`（旋转保持离散）。
  插值必须**渲染层只读**、绝不在 render 阶段调 `rng`。
- **喜從天降判定（本批已落地，见 §9.3）**：保持鼠标操作（原版操作方式不变）；键盘化留作后续
  可配项（需在 `moneyHandler` 自跟踪按键——**不能用 `Input::isDown`**，并抑制方向键全局钩子
  推光标；速度建议 240 px/s = 财神巡游 `richX±12`/tick）。
- 测试：现有 `tests/scenarios/232_minigame_smoke.txt` 走非交互分支，不受影响；
  交互小游戏**目前零场景覆盖**，建议补 trace `minigame enter <case>` / `minigame score <n>`。

### 8.4 同格多目标（消歧）

**原版两个缓冲、两种语义**：

| 缓冲 | 写入者 | 语义 | 同格多玩家 |
|---|---|---|---|
| 鼠标拾取缓冲（形状） | `rebuildPickBuffer` 0x409B18 → `writePickBuffer` 0x456A1C | **赋值**（`mov [edi],dx` @0x456B24） | **后写覆盖 = 叠放上层**（单比特） |
| 范围/候选缓冲（每对象锚点 1 像素） | `sub_409DE7` | **OR**（`|= v0` @0x409EDE） | **位掩码累积** |

- 玩家 id = **`0x8000 | (1<<p)`**（汇编 0x4087E6-0x408800 实证）；
- 单体目标解码 = **`sub_40D293` 取最低置位 → 玩家索引 0..7**（15 个调用点：模态自身、
  `showObjectTip`、`onPlayerActionPhase`×3、10 张单体玩家卡/道具）；**原版无选择面板**；
- 模态玩家过滤：`<4` 查 `alive`（`[0x496B7D+104*p]`），**`≥4`（事件槽 NPC）直接放行**
  （0x4462B3-0x4462E2）→ 重写 `target_select_dialog.cpp:55-60` 的"NPC 恒可选"**有依据**；
- 范围类效果按位**全体命中**（`expireAssets`），单体类取最低位。

**重写现状与差异**：
- 鼠标路径 = `pickMapObject` 逆序遍历取最后命中（`object_tip.cpp:147`）→ **语义一致**
  （前提：`mapHitRegions` 顺序与原版 drawList 同序）；
- **已发现顺序差异（低风险）**：`mapHitRegions` 入列顺序为「cellEnt 格 → **无主地块（未参与 y 排序）**
  → y 排序精灵」；原版全部在**同一个 y 排序 drawList** 里。无主地块菱形 143×103，其上方约 2–3 格的
  精灵会落入不一致区间 → 建议把无主地块也走 `items` 通道（加"只登记不绘制"标记）或排序后合并推送；
- **AI/范围路径差异**：重写若用 `pickMapObject`（最后绘制者）而原版此处取**最低位**（玩家号最小），
  需按原版口径对齐。

**素材（候选面板可行性）**：四大恶人**有头像** —— `panel[63]` 帧 17..20（89×105 / 119×112 /
103×124 / 83×114）、`panel[65]` 帧 26..29（117×64 / 125×55 / 125×66 / 125×55）、全身像
`panel[64]` 帧 0..3；玩家 `Data.mkf[2]` 12×72×72（**不含 NPC**）；棋子表情头像
`pieceSprites[p]` = `map.mkf[charIndex+27]` 帧 1..4。**现成控件**：`selectPlayerDialog`
（`0x440E1A`，`data.mkf[518]` = 177×97/257×97/337×97 的 2/3/4 格框）可直接复用。
缺口：NPC 无 72×72 方图（需缩放）、NPC 无专属颜色（需自定 4 色）、框只到 4 格、2 字名对齐。

**建议**：默认保持原版（叠放上层 + 掩码取最低位）；面板作为**可配增强**，只在"同一像素 ≥2 个合法
候选"时弹出。

---

## 9. 实施批次与提交粒度

### 9.1 建议顺序

```
A1 画布运行期尺寸（scale 恒 1）→ 全量矩阵回归   ← 下一批
A2 引入 scale + 缩放 blit + 文本 scale + 阴影/描边乘 scale
B  GO 面板 C2+C1（+C3）                          ✅ 2026-09-30（C3 判定不需要，见 §14）
C  脏区重绘还原 → UI 布局表 → 命中派生
E  文本定点化/亚像素（可与 C 并行）
F  配置系统（rich4.ini）
D  宽屏视口（等距表 R=32 → 地面去表化 → 光栅化优化 → FLC 锚定）
G  结构重组（最后做，避免与前面并行冲突）
H  追加四项（射程 → 预览高亮 → 小游戏插值/键盘 → 同格消歧）
```

### 9.2 回归门禁（每次提交）

| 门禁 | 命令/判据 |
|---|---|
| L0 单测 | `ctest`（`rich4_tests`）——**需可写游戏目录**（存档往返用例写 SAVE900/901） |
| L1 场景矩阵 | 全场景 headless（当前 **94 个**）→ 期望全绿；**`84_ai_marathon` 不纳入常规矩阵**（120k 帧全 AI 长跑，按用户约定 2026-09-30 仅主动要求时单跑） |
| 存档往返 | `rich4 --save-selftest 0` → `STABLE(t1==t2)=YES` |
| native 画面 | `--shot` + `tools/frame_align.py`（新增/preset 切换后重点核对） |
| 逆向标注 | `python tools/re_map.py` |

> ⚠️ 跑场景会在**游戏目录**写 `SAVE0.DAT`(AUTO)/`SAVE*.DAT` 与 `rich4.log`（原版语义），
> 会覆盖该目录已有存档；沙箱下 `build/`、`tests/`、`src/`、`resources/` 子目录写入需放行。

### 9.3 已落地：喜從天降判定修正（本批）

**问题**：`moneyTick` 取鼠标用裸 `SDL_GetMouseState`（**窗口像素**）与逻辑像素 `dollX` 比较
（`src/app/minigame_dialog.cpp:824`）。默认窗口 1280×960（2×）下娃娃追的目标偏右一倍 →
一路贴右边缘不跟手；且绕过 `m_mouseOv`，headless/调试 `move` 无法驱动娃娃。

**依据（IDA 复核）**：
- 原版跟随 `0x4135F4` `GetCursorPos` → 死区 `|diff| <= 8`（0x413661）→ `diff>0` 左行 10px
  （0x413673）/ `diff<0` 右行 10px（0x413688）；行走帧 `(walk+1) % half`（0x41369B）；
  **死区内 dir 不清零、行走帧不推进**（0x413664 直接跳过 = 原版 quirk）；
- 原版用 `SetCursorPos` 把光标夹在 640×480 内（0x41361C/0x41363C）——全屏下光标不可能越界；
- 原版接取判定（0x413332 起）与重写**逐条一致**（开区间、摆动后 `bx`、`!boomActive`、`dollDir!=0`）；
- 订正：`moneyWndProc`（0x414FCD）**没有任何 `cursorSelect` 调用**（0x4021F8 的 xref 仅
  `digWndProc`/`balloonWndProc`）→ 此前"case 8 缺光标设置"的判断**不成立**，不新增光标调用。

**改动**：
1. `include/game/app/minigame.h`：新增纯函数声明 `moneyDollFollowStep(dollX, cursorLogicalX,
   dir&, walk&, walkHalf)`；
2. `src/app/minigame_dialog.cpp`：定义该函数（匿名命名空间之外，供 L0 链接）；
   `moneyTick` 改用 `Application::mouseLogicalPos`（窗口→逻辑换算 + `m_mouseOv` 合成输入），
   并把采样值夹取到 `0..639`（等价原版全屏约束，不劫持系统光标）；
3. `tests/unit/test_minigame.cpp`（新增）：锁定 ①死区 ±8（含等于）②死区内 dir/walk 不变
   ③移动方向与步长 ④行走帧回绕 ⑤逐 tick 逼近后停在死区。

**未做（按"不破坏原版操作"口径推迟）**：键盘操作、边界强化、光标处理（原版本无）。

**验证状态（2026-09-30）**：
- 干净重建（148 目标）通过，`minigame_dialog.cpp`/`test_minigame.cpp` 均重新编译链接；
- 新增 3 个 L0 用例（`money_doll_deadzone_keeps_dir_and_walk` / `money_doll_step_left_and_right` /
  `money_doll_walk_wraps_and_converges`）**全部 PASS**；
- `rich4 --save-selftest 0` → `STABLE(t1==t2)=YES`（二进制/存档链正常）；
- ⚠️（**2026-09-30 订正**）沙箱限制已定位：本会话可正常跑 `--headless --script`（含交互小游戏
  之外的端到端场景），但 `build/`、`tests/`、`src/`、`resources/` **子目录在受限沙箱下拒绝写入**
  （只有工作区根可写）→ 编译/矩阵/提交需单命令放行 `danger-full-access`。
- ✅ **既有问题已修复（2026-09-30）**：干净重建后 `mkf_data_read_stable`
  （`tests/unit/test_mkf.cpp`）在提供真实 `Data.mkf` 时**确定性 abort（exit 3）**——
  根因 = LZHUF 位流读取 UB：`uint32_t bits |= src[byteOff+k] << (8*k)` 中 k=4 的 `<<32`
  在 x86 按 mod 32 变为 `<<0`，把第 5 字节 OR 进低字节、污染 match offset 低字节 →
  `out` 下标 size_t 下溢（Debug 断言 vector subscript out of range）；
  修复 = `uint64_t` 组装 40 位、右移后取低 32 位（对齐 `tools/lzhuf.py`）。
  验证：L0 86/86、全矩阵 93/93 绿、GUI 启动冒烟正常。

---

## 10. 新增测试场景清单（建议）

| 场景 | 断言 |
|---|---|
| `308_go_panel_surrender` **✅** | 認輸 + 死神选人窗口无 `go panel draw cur=0`（修前 75 次 / 修后 0 次） |
| `30x_go_panel_load_parachute` | 读档残留未落地玩家 → 跳伞期无 GO |
| `31x_native_pixel_equal` | native preset 渲染与冻结基准逐字节一致 |
| `31x_wide_layout_hit` | 宽屏下顶栏/右栏/GO 命中区正确（`clickr` named region） |
| `32x_minigame_money_doll` | `move` 驱动娃娃 → 娃娃位置/判定按死区语义变化（需补 trace） |
| `32x_overlap_multi_player` | 同格两玩家：默认取叠放上层；候选数=2（若做面板） |
| `32x_unowned_parcel_pick` | 无主地块上/下方叠对象时点选结果与原版一致 |

---

## 11. 逆向档案订正清单（随对应改动一起提交）

1. `docs/reverse/functions/map-target-audit.md` §1：mode 字节语义（byte2=`cursorSelect` index、
   byte3=frameCount-1）、`0x40` 精确语义（`(flags&0x80)|0x37` 且清 BYTE1）、补
   **房地產公司 `0x2090086` 可滚屏**。
2. `docs/reverse/functions/420e9a-item-ai.md` §1.1：注明"半径版 `sub_40A45C` 中心 = 视口中心；
   以任意世界坐标为中心的是 `sub_40A0B1`"；飛彈/核彈的"全图化"标注为**重写决策**（非原版）。
3. `include/game/app/target_select_dialog.h`：mode 注释订正。
4. `docs/reverse/ui-controls.md` §39 / §21：补充"事件 FLC 素材 440 宽"与"`moneyWndProc` 无光标设置"
   两条实测结论。
5. `docs/reverse/functions/item-effects.md` §1.3：核彈范围口径（对准后一屏，非全图）。

---

## 12. 待拍板（R1–R8）

| # | 问题 | 选项 / 建议 |
|---|---|---|
| **R1** | 射程口径 | (a) 严格原版视口+3 滚屏 mode（**建议先做**）(b) 自定语义射程表 (c) 可配（默认 a） |
| **R2** | 半径单位定标 | `expireAssets(100)` = ±100 屏幕像素 ≈ ±3 格（按 `kIsoCoeff` ≈35px/格）vs 文档 ±6 格；需实机定标 |
| **R3** | 超射程目标交互 | 禁止光标+提示 / 允许但警告 / 允许滚屏去够 |
| **R4** | 预览高亮形态 | 纯蒙膜 / 蒙膜+描边 / 交替闪烁（可逐类定）；且预览=效果同口径 |
| **R5** | 小游戏平滑 | 新增帧事件插值（**建议**，headless 零变化）/ 改 tickMs（不推荐） |
| **R6** | 喜從天降操作 | **本阶段保持鼠标**（已按此落地判定修正）；键盘化留后续可配项 |
| **R7** | 同格消歧 | 默认对齐原版（叠放上层 + 掩码取最低位）；候选面板作可配增强 |
| **R8** | 四大恶人目标 | 原版允许（掩码 ≥4 放行）；若做面板需自定 4 色 + 头像缩放 |

---

## 13. 交叉引用

- `docs/m3-plan.md`（前序里程碑）、`docs/cross-platform.md`（跨平台与文本层调研）
- `docs/testing.md`（三支柱测试）、`docs/debug-keys.md`（调试命令/热键）
- `docs/reverse/functions/`: `415d31-game-panel.md`（面板与脏区）、`map-target-audit.md`（目标判定矩阵）、
  `420e9a-item-ai.md`（AI 候选域）、`441baa-card-effects.md`、`item-effects.md`、
  `41982d-p2-events.md` §6（小游戏）、`498df0-event-slot-npc.md`（事件槽 NPC）
- `docs/reverse/ui-controls.md`（控件沉淀；**发现新控件必须更新**）、`frame-anchor.md`（落点对齐）

---

## 14. 批次 B 实施记录（2026-09-30，✅ 已落地）

### 14.1 改动清单

| # | 位置 | 改动 | 依据 |
|---|------|------|------|
| 1 | `include/game/app/game_loop.h`、`src/app/game_loop.cpp` | 新增 **`blockingPerf(app)`** = `eventFlcActive \|\| parachuteActive \|\| pendingSpawnPlayer`（定义在**匿名命名空间之外**，供 `game_panel` 调用）；`tipPerfBlocked` 改为 `blockingPerf \|\| viewScrolling \|\| 棋子行动态 \|\| depth>1` | 0x45144F / 0x418C55：演出期消息泵只取不派发 |
| 2 | `game_panel.cpp:drawAdvancePanel` | 守卫加 `\|\| blockingPerf(app)`；通过后 `trace go panel draw cur=%d` | 0x417191 增量层 |
| 3 | `game_loop.cpp:topBarFinish` | `canResume` 加 `&& !blockingPerf(app)` | 0x4196F1 enable 只在纯等输入窗口（B2） |
| 4 | `turn_system.cpp:beginPlayerTurn` | 入口 `disablePlayerControl(app)` | 0x418C55 起手保证 `byte_46CAFD=0`（case 1 再 0x4196F1 置位） |
| 5 | `turn_system.cpp:nextPlayer` | 入口 `disablePlayerControl(app)` | 0x418EBD 交棒 |
| 6 | `economy.cpp:surrenderPlayer` | 入口 `disablePlayerControl(app)` | 0x411AE0 認輸演出（本批测试的判别点） |
| 7 | `debug/debug.cpp` | 新增命令 `trace.clear` | 测试辅助，无原版对应 |

**C1 采用窄谓词的实测理由**：原版**视口滚动**（`sub_415D31(1)` 之后 `sub_4174CD` 会按矩形相交
补画面板）与**任意模态**（面板画在模态之下）都**仍然画前进面板**；若直接复用 `tipPerfBlocked`
（含 `viewScrolling`、`playerActionState!=0`、`depth>1`）会新增两类偏差——"小地图滚动期 GO 闪烁
消失""工具条对话框后面板缺一块"。故拆出窄谓词，只屏蔽原版消息泵真正吞掉重绘的三类窗口。

**C3 不做的实测理由**：控制位为 0 后 `drawAdvancePanel` 首个守卫即返回；快照型演出
（`snapBg`/`flyObjectSprite`/`playHighlightBlink(preRedraw=false)`）全部发生在控制位已为 0 的
移动/结算段 → 不存在"把面板烘进背景"的路径，不需要额外表面恢复。

### 14.2 验收结果（实测）

| 项 | 结果 |
|---|---|
| `308_go_panel_surrender`（修后） | `asserts=3 failures=0` **PASS**；`--trace-out` 显示投降后至 p1 回合之间 `go panel draw cur=0` = **0 次**（合法的 `cur=1` 自 p1 回合开始） |
| 同场景**修前基线**（临时回退 C1+C2 后重建） | `asserts=2 failures=1` **FAIL**，窗口内 `cur=0` = **75 次** → 场景确有判别力 |
| L0 单测 | **86/86 PASS**（`rich4_tests.exe resources/MultiverseJourney`；存档往返用例需可写游戏目录） |
| L1 场景矩阵 | **93/93 PASS**——94 个场景中 `84_ai_marathon` 按用户约定不跑（120k 帧长跑）；逐个 headless 单跑（`--seed 42` + 各场景 `# quickstart:`） |
| `re_map` | **校验通过**（RE 246 / PORT 10 / NEW 176；`address-map.md` 仅行号位移 + `0x417191` 备注自动收录本批注释） |

**窗口构造关键**：`surrender; trace.clear` 写在同一脚本行——`execLine` 以 `;` 并列，两命令在
**同一帧**生效（投降请求置位后立刻清 trace）。若分成两个 step，则"清 trace 与投降之间"那一帧的
**合法**面板绘制会落进窗口造成误判（首版即如此误判）。
