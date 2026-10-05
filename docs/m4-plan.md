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
> - **批次 A1（画布运行期尺寸，scale 恒 1）✅ 已实施**：`Surface` 运行期宽高 + 95 处
>   `Surface::kWidth/kHeight` 全量参数化（17 文件）+ blit 家族行距/裁剪取自画布 + 新增
>   `--canvas WxH` 开发入口；L0 新增 4 用例；矩阵 94/94、save-selftest STABLE、re_map 通过，
>   **800×600 画布与 native 左上 640×480 逐像素一致（A/B diff=0）**。详见 §15。
> - **批次 A2（scale + 缩放 blit + 文本 1:1 光栅化 + preset/resize）✅ 已实施**：
>   `Surface::scale/logicalToDevice/deviceToLogical` + `SurfaceScaleGuard`；blit 家族
>   **逻辑像素→设备块展开**（`blitScaled` 导出；UI 散点叠加全设备化：区域快照走
>   `saveRegion/restoreRegion`、FLC/RAW 叠加走 `blitScaled`；**像素完美**，scale=1 恒等）；
>   文本 `设计字号×scale` 1:1 光栅化（阴影/描边/字距/边距乘 scale）；光标按 scale 放大；
>   **鼠标双坐标**（UI 逻辑 / 地图设备）；**free/wide 整幅画面放大**（worldScale=uiScale）；
>   **演出帧节拍改绝对截止时刻**（放大后动画不变慢）；`--scale <f>` / `--preset
>   native|wide|free`（free 处理窗口 resize）/ `--stats`；`run_tests.py --extra`。
>   含 4 项实机修复（§16.3）：free 界面错位 / 光标 SMP / 放大拖慢（17→9.8ms）/ 动画节拍。
>   **下一步 = C 脏区重绘 + UI 布局表 + 命中派生**。

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

> **实施状态（2026-10-05）**：G(1/2) 生成物入 `src/gen/` + debug 归 `src/debug/`（`5bd7c8b`）；
> G(2/2) `.cpp` 目录细分**完成**——`src/app/` 60 文件分为 `dialogs/`（33，`*_dialog.cpp`）、
> `ai/`（2）、`ui/`（5：game_panel/object_tip/float_message/main_menu/ui_layout）、
> `game/`（20：流程/渲染/经济/存档/事件等）（`b919f51` + 本批，纯 `git mv`，GLOB 零改动、
> 无相对 include）。**剩余**：大文件拆分（`turn_system.cpp` 4.2k / `debug.cpp` 3.0k 行、
> application 调试截图 API）留后续专项。

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
A1 画布运行期尺寸（scale 恒 1）→ 全量矩阵回归   ✅ 2026-09-30（见 §15）
A2 引入 scale + 缩放 blit + 文本 scale + 阴影/描边乘 scale   ✅ 2026-09-30（见 §16）
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
| `312_npc_tip_name` **✅**（`50f4cef`） | 事件槽 NPC 悬停提示名（`tip show id=61444` "小偷"；press 强制渲染规避 C1 idleFrame） |
| `314_overlap_multi_player` **✅** | 同格两玩家取叠放上层：teleport 同格 → `clickplayer 1` → `tip show id=61441`（2026-10-05） |
| `316_minigame_money_doll` **✅** | 喜從天降交互版（settings.anim 1）：`minigame enter/score` trace + `move` 驱动娃娃（2026-10-05；trace 由本次新增） |
| `320_wide_layout_hit` **✅**（native + `--preset wide` 双跑） | 布局派生命中：`clickr tab.2`（panelX+176 派生）→ `tab switch 2`；`clickr top.6` → `dialog open name=query`（2026-10-05） |
| `318_go_panel_load_parachute` 🟡 待构造 | 读档残留未落地玩家 → 跳伞期无 GO（需"未落地玩家"存档，构造复杂） |
| `31x_native_pixel_equal` 🟡 待建（脚本） | native preset 渲染与冻结基准逐字节一致（A1 已以一次性 A/B 脚本验证 800×600 左上区 diff=0，见 §15.2；常驻形态建议脚本基线比对） |
| `322_unowned_parcel_pick` 🟡 待构造 | 无主地块上/下方叠对象时点选结果与原版一致（需 estate→格 传送辅助与重叠构造） |

---

## 11. 逆向档案订正清单（随对应改动一起提交）

> ✅ **2026-10-05 全部完成**（提交 `50f4cef` + 文档批）：1→`map-target-audit.md` §1（按字节
> 订正 + 无射程结论）；2→`420e9a-item-ai.md` §1.1（采集器中心澄清 + 全图化为重写决策）；
> 3→`target_select_dialog.h` + `.cpp`（0x40 分支精确 `(flags&0x80)|0x37` + 清 BYTE1）；
> 4→`ui-controls.md` §21/§39；5→`item-effects.md` §1.3（核弹口径=目标处一屏）。

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

---

## 15. 批次 A1 实施记录（2026-09-30，✅ 已落地）

### 15.1 改动清单

| # | 位置 | 改动 | 依据 |
|---|------|------|------|
| 1 | `include/game/render/surface.h`、`src/render/surface.cpp` | `Surface` 宽高运行期化（`m_width/m_height`，默认 native 640×480）；`create(renderer, w, h)`；`present/fillRect/saveBmp` 全走运行期值；create 成功后把全局裁剪窗口重置为画布全尺寸 | 0x48A08C 后台缓冲 / 0x4861B8 全局裁剪 |
| 2 | `src/render/blit.cpp` | `pressDown`/`saveRegion`/`restoreRegion` 步长与边界改画布 `width()/height()`；`blitScrolledMap` 源行距固定 640（地图预览位图）、目标行距按画布、**行数取 min(画布高, 480)**（见 §15.4）；全局 clip 初值标注为设计尺寸 | 0x455C52 / 0x456180 等 |
| 3 | `include/game/render/cursor.h`、`src/render/cursor.cpp` | `Cursor::getRect` 增 `const Surface&` 参数，裁剪到运行期画布（无调用点，改签名安全） | 0x4024C0 |
| 4 | `src/app/*.cpp`（15 文件）、`src/application.cpp`、`src/main.cpp` | 95 处 `Surface::kWidth/kHeight` → `dst.width()/height()`（像素步长/边界）；`Application::init` 增画布尺寸参数；新增 `--canvas WxH` 开发入口（默认 native） | — |
| 5 | `tests/unit/test_surface.cpp`（新增） | L0 锁定：运行期宽高/默认 native、fillRect 越界裁剪、blit 按画布裁剪（<640 画布）、saveRegion/restoreRegion 越界语义、blitScrolledMap 源 640 行距 + 画布目标行距 | — |

**范围外（按 §1.4 分两次提交约定）**：scale 缩放 blit、文本 scale、preset 表、窗口 resize 处理——
均留待 A2；A1 只做运行期尺寸，且默认路径行为与 640×480 逐像素一致。

### 15.2 验收结果（实测）

| 项 | 结果 |
|---|---|
| L0 单测 | **121 checks / 0 fails PASS**（新增 4 用例：`surface_runtime_size_fill_clip` / `surface_blit_clips_to_runtime_canvas` / `surface_region_save_restore_uses_canvas` / `surface_scrolled_map_runtime_width`） |
| L1 场景矩阵 | **94/94 PASS**（`python tools/run_tests.py --matrix`；本次含 `84_ai_marathon` 505s 亦 PASS） |
| 存档往返 | `rich4 --save-selftest 0` → `STABLE(t1==t2)=YES` |
| 逆向标注 | `python tools/re_map.py` 校验通过 |
| **像素 A/B** | `--shot --shot-frame 300`（seed 42 quickstart）：native 640×480 与 `--canvas 800x600` 左上 640×480 区域**逐像素一致（diff=0）** |
| 稳定性 | 00_smoke ×20 + `--canvas 800x600` ×5 全部 EXIT=0 |

### 15.3 已知边界与遗留

- `--canvas` 目前只服务开发验证：画布大于 640×480 时其余区域为黑（布局仍按 native 坐标，
  宽屏布局属批次 C/D）；窗口仍固定 1280×960，逻辑呈现（LETTERBOX）随画布尺寸同步。
- `Surface::kWidth/kHeight` 保留为**设计尺寸常量**：仅余 3 处引用（create 默认参数、
  `blit.cpp` 全局 clip 初值、`blitScrolledMap` 的 640 宽预览源行距），均已注释语义。
- 开发观察（未复现）：首次全量构建后的二进制曾出现启动即反复重开主菜单（同一命令 clean
  重建后无法复现；后续 20+ 次运行全绿）。两轮均处于可能存在残留进程的窗口期，暂归因构建
  产物/运行环境瞬态；若后续复现，从 `runModal` 退出条件（`running`/`exitRequested`）入手排查。

---

## 16. 批次 A2 实施记录（2026-09-30，✅ 已落地）

**目标**：`scale` 机制 + 缩放 blit + 文本 1:1 光栅化 + preset/窗口 resize（scale=1 恒等）。

### 16.1 改动清单

| # | 位置 | 改动 | 依据 |
|---|------|------|------|
| 1 | `surface.h/cpp` | `scale/loginToDevice/logicalSpanToDevice/deviceToLogical` + `SurfaceScaleGuard`；`fillRect` 逻辑矩形设备化 | — |
| 2 | `blit.cpp/h` | **`blitScaled`**（**逻辑像素→设备块展开**；SPR/16bit/色键/不透明）导出供 UI 散点复用；四个 blit 入口 `scale != 1` 分支；`pressDown`/`scaleSurfaceChannels`/`saveRegion`/`restoreRegion` 设备化；`blitScrolledMap` 缩放 + 环绕采样；全局裁剪逻辑坐标 → 设备化 | 0x455C52/0x456180 等 |
| 3 | `text.cpp` | `px = 字号 × kFontSizeScale × scale`；缓冲按 scale 重建；折行/边距/字距/阴影/描边/落点全设备化；**blit 前 `SurfaceScaleGuard(1)` 1:1 写入**（无二次放大） | 0x44FABC |
| 4 | `cursor.cpp` | 保存区域/偏移按 scale 设备化；`blitFrameDevice` 帧放大绘制（**SMP 16bit 与 SPR 8bit 双支持**） | 0x401E59 |
| 5 | `iso_blit.cpp` | 等距四边形**逻辑域行扫描 + 每个逻辑像素展开为设备块**（像素完美；scale=1 时块=1px 与原浮点路径逐字节一致） | 0x4557A1 |
| 6 | `map_render.cpp` | `orColorMask`/`drawEstateHighlight` 逐逻辑像素 → 设备矩形；骰子 FLC 设备采样（色键范围在采样后判断）；`setClipRect` 恢复为设计逻辑坐标 | 0x456384/0x451985 |
| 7 | `game_loop.cpp`/`map_objects.cpp`/`turn_system.cpp` | 跳伞/事件 FLC 叠加改 `blitScaled`；飞行动画/飘走快照区域设备化；`playEventFlc` 保帧回卷区域设备化 | 0x45144F/0x451A97 |
| 8 | UI 散点（18 文件） | 区域快照统一走 `saveRegion/restoreRegion`（气泡/大图悬停/消息板）；FLC/RAW 叠加统一走 `blitScaled`（乐透/魔法屋/小游戏/月结/卡片获得/银行插画/大地图/小地图）；走势图逐点经 `plotLogical`；数字输入框 SPR 区域改 `blitScaled` | 各对应地址 |
| 9 | `application.h/cpp` | `DisplayConfig`（窗口/画布/uiScale/autoScale）；`--preset native|wide|free`；`SDL_EVENT_WINDOW_RESIZED → applyWindowResize`（free：画布=drawable、uiScale=高/480、逻辑呈现同步）；**鼠标双坐标**（`mouseLogicalPos`=设计逻辑、`mouseDevicePos`=设备；事件坐标统一逻辑）；方向键步长 ×scale | 0x401B9C |
| 10 | `tools/run_tests.py` | `--extra "<参数>"` 透传（`--preset`/`--scale` 回归） | — |
| 11 | `tests/unit/test_surface.cpp` | +1 用例（高画布行数钳制） | — |

### 16.2 关键语义（与 §1.2/§6 的差异说明）

- **free/wide preset 下 `worldScale = uiScale`（整幅画面放大，nearest）**：免费/宽屏 preset 的
  直觉行为即"把 640×480 画面放大 N 倍"（free 1280×960 → 恰好铺满；像素放大保锐利）。
  §1.2 的"worldScale 默认 1:1"实为 **D 阶段"宽屏视野扩展"**（地图区变大而非放大 UI）的语义；
  当前地图层无独立 scale，与 UI 同倍率（`renderMap` 去 guard 后全链路跟随）。
- **鼠标双坐标**：事件/命中统一设计逻辑坐标（`transformMouseEvent` ÷scale；合成输入本为逻辑）；
  光标/地图 `mapHitRegions` 用设备坐标（A2 后 mapHitRegions 回**逻辑**，与 A1 前语义一致）。
- **`--canvas` 任意尺寸**仍为开发入口（scale 恒 1；`--preset` 与之互斥时告警忽略）。

### 16.3 验收与实机修复

| 项 | 结果 |
|---|---|
| 编译 | 干净重建零错误（MSVC Debug） |
| scale=1 恒等 | native 截图与 A2 中途基线 **diff=343px，bbox (0,0)-(22,22)**——恰为光标帧回归（见下） |
| **free 整幅放大** | free(1280×960, scale=2) 截图**降采样 2× 与 native diff=0.8%**（文本 1:1 光栅化/图标边缘），画面即 native 的 2 倍放大 |
| preset 冒烟 | native/wide/free 完整「主菜单→选人→确定→进入游戏」×各 1 + native smoke 全 EXIT=0 |
| 选人界面 A/B | 见 §15.4（native vs 800×600 逐像素 diff=0） |

**实机问题修复（2026-09-30 用户反馈，本批内）**：
1. **free 模式界面错乱**：初版把 free 做成"UI 放大 + 地图 1:1"，两层坐标错位（顶栏盖地图、
   中部残 Loading 画面）。**修正为 worldScale=uiScale 整幅放大**（§16.2），free 画面即
   native 的 2 倍（降采样 diff 0.8% 验证）。
2. **主菜单/游戏内看不到鼠标**：`Cursor::blitFrameDevice` 初版假设 SPR（8bit+调色板），
   而光标资源是 **SMP（16bit 直取）** → 提前 return。修复为 SMP/SPR 双支持。
3. **窗口放大后整体拖慢 + 放大画面有采样细纹**：
   - 初版放大是"逐设备像素反查源像素"（nearest 采样）——非整数/大倍数下采样边界抖动
     （实测 free 地图 2×2 块内部不一致率 **59%**，视觉细纹）；每设备像素一次除法/浮点
     插值也慢（free 1280×960 纯 CPU 绘制 **17 ms/帧**）。
   - **修正为"逻辑像素 → 设备块展开"**（每个源像素填 `[logicalToDevice(lx), logicalToDevice(lx+1))`
     设备块）：`blitScaled`/`isoBlitQuad`/`blitScrolledMap`/光标/骰子/orColorMask 全部统一该模型。
   - 结果：**free 地图块不一致率 59% → 0%（像素完美）**；**native 截图逐字节 diff=0**
     （scale=1 时块=1px，与统一前完全一致）；**性能 free 17 → 9.8 ms/帧、wide 12 → 8.2 ms**
     （`--stats` 实测，headless 纯 CPU；native 5.5 ms）。实机验证命令：`--preset free --stats`。
   - `--stats` 新增（每 120 帧输出 FPS/帧耗时；性能诊断用）。
4. **窗口放大后动画变慢**（实机反馈"动画效果还有点变慢"）：所有逐帧演出循环的帧间隔
   原为"绘制完成后再等固定间隔"（绘制耗时**外加**在帧间隔上）——原版绘制≈瞬时无感，
   放大后每帧多 5~10ms → 动画整体放慢。**修正为绝对节拍**（`frameWaitStep`，
   `deadline += interval`，绘制耗时**计入**帧间隔）：`playEventFlc` / `playHighlightBlink`
   / `playSettleFlc`（月结）/ `attachEnd`（飘走）/ `flyObjectSprite`（物件飞行）五处统一；
   虚拟时钟下 delayMs 仅推进虚拟时间，测试语义与时长断言不变。静态停留
   （`eventAudioWait`/`showGodNarration`/holdMs）与原版 `sub_45285E` 对齐，保持固定等待。

### 16.4 已知边界与遗留

- **wide(1280×720) 右侧黑边**（内容 960×720）：宽屏视野扩展（地图区铺满）属批次 D
  （等距表 R=32 + 地面去表化）。
- linear 滤镜与 UI 位图缩放缓存未做（实时 nearest 采样，UI 位图量小已够用）。
- 调试注入（`--shot-cursor`/`--game-click` 的窗口坐标换算）在 scale>1 下按设备近似
  （native 不受影响；正式交互不受影响）。
- 命中派生/布局表仍按计划在批次 C 收口（当前为"逻辑坐标统一"的最小方案，`--canvas`
  任意尺寸的布局错位仍在）。

### 15.4 实机问题修复：选人界面高画布崩溃（2026-09-30）

**现象**：`--canvas 800x600` + 主菜单「新遊戲」进入选人界面 → **访问冲突崩溃**
（0xC0000005，BMP 未生成）。

**根因**：`blitScrolledMap` 的目标拷贝循环按 `dst.height()` 遍历，但源是 640×480 的
地图预览位图（`jump.mkf`，固定 480 行 × 1280B）。画布高 > 480 时按目标高多遍历出 120 行
（800×600/1024×768/1280×720 均触发），`src += 1280` 越界读源缓冲（15 万字节量级）→
访问冲突。native 640×480 与画布高 ≤ 480 时不触发（故矩阵/截图 A/B 未暴露——矩阵全部
quickstart 跳过选人界面，`blitScrolledMap` 仅用于选人界面与通关地图选择）。

**修复**：行数取 `min(dst.height(), 480)`（源没有的行不写）；`blitScrolledMap` 的源语义
注释明确（640×480、480 行）。L0 新增 `surface_scrolled_map_high_canvas_rows_clamped`
（320×520 画布：前 480 行正确写入、480 行以下保持原值）。

**验证**：
- 选人界面 `--canvas 800x600` 崩溃消除、`--canvas 1024x768` / `1280x720` 完整开局流程
  （选人→确定→补 AI→过渡→进入游戏）EXIT=0；
- 选人界面 native vs `--canvas 800x600` 左上 640×480 **逐像素一致（diff=0）**；
- L0 **126 checks / 0 fails**；全量矩阵 **94/94**；`--extra "--canvas 1024x768"` 全量
  （94/94，含 84_ai_marathon）——大画布全路径无越界；
- `--save-selftest 0` → STABLE=YES。

**工具**：`tools/run_tests.py` 新增 `--extra "<rich4 参数>"`（透传给每个场景，
宽屏/画布尺寸回归用）。


---

## 17. 批次 C1 + D1 实施记录（2026-09-30，✅ 已落地）

### 17.1 C1 静止帧跳过（脏区重绘第一部分）

| # | 位置 | 改动 | 依据 |
|---|------|------|------|
| 1 | `game_loop.cpp` | `idleFrame(app)`：`gamePlayerControl && !viewScrolling && !blockingPerf && !diceAnimActive && !showDice && remainingSteps==0 && 全部 playerActionState==0`；timer 分支 `if (goBlinkTurn || !idleFrame(app)) renderGameFrame(app);` | 原版该窗口只按 `dword_475110` 脏位补画增量层，重写全量重绘的保守等价 |

- **语义**：空闲期场景不变（光标由 renderFrame 独立合成/恢复；GO 闪烁翻转点仍重绘）。
- **性能**（`--stats`，headless，等待输入期）：native 5.5→**1.5ms**、free 9.8→**3.5ms**。
- **验证**：free 空闲截图与全量重绘版**逐字节 diff=0**（无残影）；00_smoke/20_rent_route/52_event_dianquan 全 PASS。

### 17.2 D1 宽地图区

| # | 位置 | 改动 | 依据 |
|---|------|------|------|
| 1 | `tools/gen_map_tables.py`、`map_tables.h/cpp` | 等距投影表 **29×29 → 65×65**（半窗口 14→32）：表内原表**逐项保真**、外圈用边界步长线性外推；索引 `16900*dir + 260*(row+32) + 4*(col+32)`；生成器补 idx12 无效名称指针特判 | §4.2 R=32 建议；表内回归 6728 项 0 差异 |
| 2 | `ui_layout.h/cpp`（新） | 逻辑画布宽 = 设备宽/scale；地图区宽 = 逻辑画布宽 − 右栏 200；右栏 x/偏移派生；`kTopBarH=40`/`kPanelW=200` | §3.2 布局表第一批 |
| 3 | `map_render.cpp` | 视口中心 x = mapW/2；裁剪 `(0,mapTop)-(mapW,480)`（宽屏 mapTop=0 / native 40）；**宽屏去表化**：与视口相交格点双循环（地面互不重叠 → 顺序无关；格点左上角粗筛 + 表 ±32 上限），native 保留原 296 项遍历序 | §4.2"宽屏可完全去表化" |
| 4 | `surface.h`、`blit/surface/text/iso` 等 | **绘制原点** `origin/deviceX/deviceY/spanX/spanY` + `SurfaceOriginGuard`；全部坐标变换点统一 deviceX/Y（native origin=0 恒等） | 右栏整体平移 |
| 5 | `game_panel.cpp` | 右栏 5 函数（玩家条/信息面板/小地图/节日/日历）加 origin guard（坐标保持 440 基准不动） | 同上 |
| 6 | `game_loop.cpp`、`object_tip.cpp` | 命中派生：小地图/箭头（panelX+3..+53）/页签（+176..+200）/日历（+8..+34、+38..+64）/地图区（x<mapW，顶栏仅 x<440 段）；物件提示方向判定按 mapW；`miniMapHitRegion` 增 Surface 参数 | §3.2"命中必须从同一张表派生" |
| 7 | `game_loop.cpp` | FLC/跳伞素材在宽地图区水平居中（`(mapW-440)/2`；native 偏移 0） | §4.1 |

### 17.3 验收（实测）

| 项 | 结果 |
|---|---|
| native 逐字节 | 截图与基线 **diff=0**（含投影表扩展 + mapTop 裁剪 + origin 机制） |
| free | 降采样 2× 与 native **diff 0.7%**（= native 干净 2 倍） |
| wide | 非黑像素 **74% → 97%**（右侧 320px 黑边被地图视野填满） |
| 冒烟 | 00_smoke / 20_rent_route / 52_event_dianquan（FLC 演出）/ 114_magic_smoke + wide 冒烟 全 PASS |
| 性能（静止期） | native **1.47ms** / free **3.52ms** / wide **2.68ms**（`--stats`） |

### 17.4 已知边界与遗留

- **宽屏顶栏右侧**：顶栏素材 440 宽，`(440,mapW)×(0,40)` 区域露出地图（观感增强而非缺陷）；
  顶栏宽屏装饰/拉伸未做（D 收尾）。
- **区域级脏区（5 位 dword_475110）未做**：当前只有"全量/跳过"两档；计划中的按区补画
  （GO 只重绘地图区等）留 C2。
- **布局表收口未完成**：`advancePanelX/Y`、对话框居中（`320-w/2`）、`kMiniMapY` 等仍是
  散落常量；`settings[6..8]`（日曆/縮小地圖/組合畫面）未接布局。**C2**。
- `--canvas WxH` 任意尺寸（scale=1）下地图区按 440（布局派生只认 scale 推导的画布），
  命中仍按左上 640×480；正式 preset（native/wide/free）不受影响。

### 17.4.1 S4 收口审计（2026-10-05，✅ 结论：三项为设计内/已派生，无代码遗留）

1. **`advancePanelX/Y`**：IDA 复核 `qword_475284` 为**静态常量 (180,120)**（18 处 xref
   全为读取、无写入点；`beginPlayerTurn` 仅读它 `SetCursorPos(+46,+34)` 移鼠标到 GO 中心，
   重写不劫持光标）→ 重写常量一致，无需动态化；宽屏已随 `uiMapCenterShiftX` 平移
   （绘制与命中同源）。`game_panel.cpp` 旧注释"原版动态确定"已订正。
2. **对话框居中 `320-w/2`**：是 **640 基准模态框架的设计内坐标**（`dispatchModalAware`
   统一 `uiModalBaseX` 平移+事件 -base），非散落缺陷 → 不改；任意逻辑宽下自动居中。
3. **宽屏顶栏右侧 `(440,mapW)×(0,40)`**：保持**地图视野延展**（原版无此区域；D1 验收
   "非黑像素 74%→97%"即此项），不另加装饰素材。
4. **`--canvas WxH` 任意尺寸**：D 收尾后布局/命中全派生（`uiMapLogicalWidth` 等），
   已由 `--extra "--canvas 1024x768"` 全量矩阵验收（§15.4）；无遗留。

### 17.5 settings 消费点复核 + settings[0] 接入（2026-10-01）

- **订正 §3.2 "两个死配置"**：IDA xrefs 复核——`settings[6]/[7]`（0x49715E/15F）**无任何
  引用**（原版未消费），旧称"日曆/縮小地圖/組合畫面"不成立；`settings[8]`（0x497160）
  是**与日期等游戏全局重叠的字段**（45 处引用），不是设置。
- **`settings[1]~[5]` 重写已消费**（音效开关/音乐音量/音效音量/自动存档/布局）；
  `settings[0]`（游戏速度）本轮接入**骰子 FLC 帧速**：`kDiceFlcSpeed`={5,3,2}×10ms
  （慢 50 / 默认 30 / 快 20 ms，原版 flags bit4-7 覆盖 `unk_48C870`）。
- **已知差异（记录，不阻塞）**：原版状态机按 `settings[0]` 刻度（`byte_46CB20`={6,4,2}
  ×20ms = 120/80/40ms，`cursorTimer` 置 `byte_46CAFA` → WinMain 步进）驱动；重写为固定
  16ms tick + 内部时间门控（行走动画 60ms/帧、位移按 `kMoveSpeed` 折算），与原版默认档
  节奏已独立校准。逐档状态机对齐（快/慢档全链）如需另立专项。
- **状态机帧数等待的时间换算（2026-10-05）**：`playerActionWait` 低 7 位是**原版步数**
  （每步 = settings[0] 刻度，默认 80ms）——重写固定 16ms tick 需 ×5 折算：
  `landingEvent` 落地等待 0x88（8 步）→ **40 帧（≈640ms）**（实机反馈"买地/加盖后
  来不及看清效果"）；**待专项**：`-125`（0x83，无事件交棒 5 步 ≈400ms）等其它
  等待值仍按原帧数（5 帧 ≈80ms）偏短，统一折算未做（影响 AI 全局长跑节奏，需单独回归）。
- 详见 `docs/formats/cfg.md`「设置字节消费点复核」表。

## 18. 批次 C2 + D 收尾实施记录（2026-10-01，✅ 已落地）

**提交线**：`b7410b7`(settings 消费点) → `9ad4270`(对话框派生) → `1d6e678`(640 基准框架)
→ `1c3a3a0`(原点全路径) → `77ab5c8`(区域脏区) → `d2bb9af`(date/合成输入收口)。

### 18.1 640 基准 UI 宽屏居中框架（1d6e678）

- 原版全部 UI 坐标基于 640×480；宽画布（wide 逻辑 853 / `--canvas`）下模态/主菜单
  贴左、右侧露旧画面（银行实机暴露）。**框架**：`Application::dispatchModalAware(event)`——
  栈顶为 centerBase 层（`EventStack::Frame.centerBase`，`runModal(..., centerBase, fillBars)`）
  且逻辑宽 > 640 时：进入时**两侧填黑一次**（仅全屏剧场模态 `fillBars=true`；叠加式保留
  游戏画面）、事件鼠标坐标 -base、绘制原点 +base（SurfaceOriginGuard 包住 handler）。
- `uiModalBaseX = (逻辑宽-640)/2`（native/free=0；wide=106）；游戏内循环
  `runModal(..., false, false)` 直通。叠加式模态 `fillBars=false`
  （设置/帮助/热键/日期/询问框/数字框/消息/台词/选骰/交易市场/AI/卡片道具/住院保释）。
  ~~2026-10-05 实机补：银行 4 处 fillBars=false~~ **【已订正，见 §20】**
  9434b10 曾把银行 4 处（柜员机/停留/週轉/催收）误判为叠加式——银行界面为 640×480
  全屏铺底，应为全屏剧场 `fillBars=true`（用户 2026-10-05 反馈要求两侧黑边内容居中）。
- `renderGameFrame` 入口强制 OriginGuard(0,0)：模态内重绘游戏画面保持宽屏布局，
  模态自身面板居中（两者混叠场景正确）。
- **C2 坐标回退**：confirm 14 处/4 主对话框架/trade 详情回 640 基准常数（框架统一平移，
  避免双重偏移）；uiScreenCenterX/uiMapCenterX helper 移除。

### 18.2 绘制原点全路径修正（1c3a3a0）

`scale==1` 快速路径此前未应用 origin（`--canvas` 宽画布命中/绘制不一致、面板不居中）：
- `blitElementRegion`/`Opaque`、`blitSpriteFrameClipped`、`blitScrolledMap`、
  `scaleSurfaceChannels`、`saveRegion`/`restoreRegion`、`Surface::fillRect`、
  `minigame blitSprScaled` —— origin≠0 时统一走设备化路径（1px 块与 native 等价）。
- `text.cpp` 落点已 deviceX/deviceY（含 origin）→ blit 侧显式 origin=0 防二次应用。
- `settingsDialog` 删除 runModal 前预绘制（模态进入 0x401 已在 origin 内重绘同内容；
  提前无 origin 绘制在宽画布左侧留残影——`--canvas`/wide 实拍定位）；`dateDialog`
  同类预绘制改包 SurfaceOriginGuard。
- **世界锚定坐标派生**：showGodNarration 旁白居中地图区、attachEnd 飘走兜底/裁剪、
  flyObjectSprite 快照区=地图区全宽、renderMap 恢复裁剪=逻辑画布、playEventFlc
  preserve 回卷钳制=逻辑画布。

### 18.3 区域级脏区（77ab5c8）

- `game_panel.h` 脏区位（bit0 工具条/bit1 地图场景层/bit2 玩家条/bit3 小地图/bit4 日历；
  `kDirtyAll`）；`renderGamePanel(app, dirty)` 按位绘制；`renderGameFrameWith(app, dirty)`
  分区重绘（bit1 含地图+物件提示+GO+跳伞/FLC 叠加+高亮，层级与全量一致）；
  `renderGameFrame` = 全量（100+ 调用点不变）。
- timer 分支**保守标记**：视口平滑滚动、掷骰(1)/移动(2) → 地图层+小地图；宽屏 mapTop=0
  下地图覆盖顶栏素材 → 地图层补画恒伴随顶栏；GO 闪烁 → 地图层；未标脏非空闲帧 → 全量。
- 验证：native SHA 逐字节全等；移动期"局部 vs 强制全量"同帧截图逐字节 IDENTICAL
  （trace 确认 0x0B 连续 135 帧局部路径）。

### 18.4 调试合成输入收口（d2bb9af）

- debug 合成鼠标（click/clickr/move/press + synthesizeClick）改经 dispatchModalAware：
  脚本坐标 = 640 基准 → 栈顶模态时 +base 转画布坐标（事件 -base 命中 + 绘制 origin）。
  修 headless 截图中"模态内合成点击触发重绘无 origin → 面板错位重影"（实机鼠标无此问题）。

### 18.5 验证与边界

- **native 逐字节**：top/bank/set 三张 SHA 全等（全程保持）；`--canvas 1024×480` 设置面板
  居中 + "確定"点击命中；wide 银行居中+对称黑边、叠加式模态保留完整游戏画面、主菜单居中。
- 22 场景 native 全 PASS（含 266/268/270/308 正确 quickstart 参数）。
- **边界（设计决策/记录）**：宽屏顶栏右侧 (440..mapW)×(0,40) 为地图延展（观感增强，
  不做装饰）；GO 面板保持原版固定 (180,120)（地图区内，忠实原版）；区域脏区为保守子集
  （更多路径可细分，收益有限）；advancePanelX/Y 仍为 state 固定值；`--canvas` 任意尺寸
  为开发入口（布局/命中已派生，非正式 preset）。

### 18.6 实机四问题修复（2026-10-01，提交 `2b2a3cc`）

| # | 现象 | 根因 | 修复 |
|---|------|------|------|
| 1 | 主菜单左右黑边留残影 | resize 重建画布后 tickMs=0 的层（主菜单/多数模态）不主动重绘（无 timer），画面停在旧内容 | `renderFrame` 在 `applyWindowResize` 后强制 `dispatchModalAware(nullptr)` 重绘栈顶一次（`m_forceRepaint`） |
| 2 | 掷骰/行走等动画放大窗口后拖慢 | `runModal` tick 派发 `nextTick = now + interval` 把绘制耗时计入 tick 周期（20ms 绘制→20ms tick→按 tick 推进的动画慢 25%） | 改按 interval 网格累加 `nextTick += interval` + 欠账连发补拍（上限 4/轮防雪崩）；虚拟时钟分支不变；native SHA 仍全等 |
| 3 | 右上玩家条（layout 2）花屏+金额残影 | `drawPlayerBar` 背景帧误用 `uiPanelLogicalX`（已含面板偏移）又被 `SurfaceOriginGuard` 二次 +offset → 背景帧画到屏外（露游戏画面=花屏；金额更新时旧文字残留=残影） | 恢复函数内 440 基准坐标（宽屏右移只由 guard 统一完成） |
| 4 | 大地图打开后两侧被裁剪 | 0x40A801 是"叠加在游戏画面上"（底图 @(20,60) 不清屏），`runModal` 默认 fillBars=true 填黑两侧 | mapDialog → fillBars=false；同类审计：roulette/facility/lab（叠加面板）→ fillBars=false；target_select/dicePick（命中为画布坐标）→ centerBase=false |

附带：debug 新命令 `settings.layout <0|1|2>`（运行期切右栏布局）；`player.cash/bank` 改值后
`refreshPlayerPanelFor` 立即刷新右栏（脚本测试避免旧值残留）。

### 18.7 实机二轮修复（2026-10-01，提交 `9d00d9f`）

- **掷骰面板（GO）与骰子动画居中**：新增 `uiMapCenterShiftX`（= 地图区宽/2 − 220；
  native 0）= 宽屏视口中心相对原版 220 的位移；`drawAdvancePanel`/命中（同源）与骰子
  FLC 落点统一 +shift（骰子跟随视口中心）。
- **模态绘制边界 `paintClip`（选人界面过场残影根因）**：确认→补 AI 后角色右移/面板
  收起，元素"经过"左右黑边时残留（`blitElementOpaque`/`blitSpriteFrame`/`blitScrolledMap`/
  `fillRect` 的 `useClipRect=false` 路径不查任何边界）。新增 `Surface::paintClip`
  （画布逻辑边界）+ `SurfacePaintClipGuard`：`dispatchModalAware` 在 centerBase 模态
  期间设 `(base, base+640)`，所有 UI 绘制统一与边界取交（原快速路径在边界启用时转
  设备化路径）；`renderGameFrameWith` 重绘游戏画面时关闭（宽屏游戏画面不被裁）。
  native 下 base=0 → 等价原版 640 屏幕边界，SHA 全等。
- **主菜单预绘制残影**：`mainMenuScene` 进入前 `redrawMenu` 无 origin；非首次进入
  （主菜单→选人→取消→回主菜单，`m_modalBaseFilled` 已 true 不再填黑）时左侧残留
  无偏移按钮。预绘制包 `SurfaceOriginGuard`。
- **main_menu named region** `menu.0..4`（脚本 clickr menu.0 进选人）。
- 验证：native SHA 全等；wide 选人过场（帧 700..730）两侧采样全黑、取消回退单份按钮、
  GO 面板 286 逻辑 ≈ 地图区中心；17 场景全 PASS。

### 18.8 实机三轮修复（2026-10-01，提交 `bc23a19`）

- **文字被裁根因（选人配置项/系统设置/买地提示没字）**：paintClip 存"画布逻辑值"，
  但 text 落点经 `SurfaceScaleGuard(1)` 后是"设备像素"（×真实 scale）→ clip 与文本落点
  不同域（宽屏右侧文本全被裁）。改为**设置时以真实画布 scale 一次设备化**（存设备坐标，
  `logicalToDevice(base)`），blit/fillRect 侧直接用值；native 下等价不变。
- **存/读档保画面**：`saveDialog`/`loadDialog` → `fillBars=false`（宽画布两侧保留游戏
  画面，与大地图一致）。
- **掷骰提速**：帧速 `10×档位值`（50/30/20ms）→ `8×档位值`（40/24/16ms，×0.8 略微
  加速）；节拍改追帧累加（`last += frameMs`）保证绘制偏慢时节奏精确。
- **小地图视野框**：白/红方框由固定 30×30 改为**游戏内视野尺寸**（5696/65536 尺度；
  native 38×38、宽屏随地图区宽 56×38）。
- 验证：18 场景全 PASS；wide 选人/设置文字恢复、存档两侧游戏画面、native 小地图
  视野框生效。

### 18.9 实机五轮修复（2026-10-01，提交 `9ad41a5`）

- **放大窗口后帧率低（一帧一帧）根因**：autoScale 原架构"设备画布 + 逻辑像素块展开"，
  每帧绘制与纹理上传 ∝ 设备像素（窗口越大越慢）。改为**逻辑画布 + SDL logical
  presentation 呈现放大**：画布 = 设备/缩放比（wide 853×480 / free 640×480）、绘制
  scale 恒 1 —— 成本与 native 同级且**与窗口分辨率无关**。实测移动期：wide
  3-4.5ms→1.5-1.7ms、free 3.6-3.9ms→1.4-1.6ms（native 1.6-2.4ms）。等比缩放仅同步
  logical presentation；非 autoScale（native/`--canvas`/`--scale`）不变。
  代价：文本按设计字号光栅化后呈现层放大（大窗口略像素化；"窗口分辨率文本层"后续专项）。
- **入狱/住院押解动画残影**：`playEventFlc` 保帧回卷区用 FLC 实参 x，叠帧落点含
  "440 素材在宽地图区居中偏移"（+106）→ 宽屏下回卷区缺右侧 106 逻辑像素，透明区透出
  上一帧像素（警车/救护车拖影）。回卷区改为与 `drawEventFlcFrame` 同源。
- 验证：21 场景全 PASS（18 native + 3 wide，含 54_jail）。

### 18.10 线性放大过滤（2026-10-01，提交 `2e91ae8`）

- 用户决策：字体不做高分专项，接受"低分辨率（逻辑画布）输出 + 窗口拉伸"；要求
  呈现放大用**线性过滤**减少马赛克感。
- `Surface::setLinearFilter`（默认线性）：逻辑画布 → 窗口的呈现放大用
  `SDL_SCALEMODE_LINEAR`（1:1 呈现时两模式等价，native 不受影响）。
- `DisplayConfig.linearFilter` + `--filter nearest|linear`（默认 linear；需要像素
  完美时可回退 nearest）。模拟 1.5x 对比：linear 明显平滑。

## 19. 批次 E~H 推进记录（2026-10-01/05）

- **E 文本高分定点化**：按用户决策**跳过**（"暂不考虑字体，低分辨率输出 + 窗口拉伸；
  线性过滤减少马赛克感"，见 §18.9/§18.10）。
- **F 配置系统 `rich4.ini` ✅**（提交 `ef21ec9`）：`core/config.{h,cpp}`——INI 解析 +
  缺失生成带注释默认模板 + `setConfigValue`（section.key/裸键）；接入 main（预扫 --game →
  loadConfig → CLI 覆盖 + 通用 `--set k=v`）/text（字体/字号参数化）/log（路径）/
  application（mediaDir/vsync/fullscreen/字体传入）；RICH4.CFG 红线保持。见 §6。
- **G 源码结构重组（1/2）✅**（提交 `5bd7c8b`）：5 个生成物集中 `src/gen/`
  （生成脚本路径同步）、`debug.cpp`/`debug_keys.cpp` 归 `src/debug/`、删空目录。
  **G(2/2) ✅（`b919f51` + 本批）**：`src/app/` 60 文件 `.cpp` 目录细分完成——
  `dialogs/`（33）/`ai/`（2）/`ui/`（5）/`game/`（20）（纯 git mv；GLOB 自动覆盖、
  无相对 include；构建 + 00/20/312/320/90 全 PASS、re_map 通过）。剩余：大文件拆分
  （turn_system 4.2k / debug 3.0k 行）留后续专项（§7 实施状态）。
- **H 追加体验（1/4）✅**（提交 `a715bde`）：小游戏渲染插值——新增 `kModalFrameEvent`
  （真实时钟每轮派发、虚拟时钟零变化），挖寶/氣球/喜從天降 tick 间位置插值（渲染层只读）。
  H 剩余：8.1 射程（含三档方案，待决策）/8.2 预览高亮/8.4 同格消歧。

### 19.1 后续批次（2026-10-05 续）

- **H(2/4) 悬停预览高亮 ✅**（提交 `0aad3bc`）：选目标模态悬停时把目标拾取形状写入
  `highlightShapes`（与 captureHighlightShapes 同源换算），`drawEstateHighlight` 逐帧
  闪烁（50ms/帧、16 帧 + 400ms 停留，由 `kModalFrameEvent` 驱动）；目标变化才重置相位；
  未命中/退出即清（全局快照单份）。场景 210/212/140/200/260 等全 PASS。
- **H(1/4) 小游戏渲染插值 ✅**（提交 `a715bde`）：kModalFrameEvent（真实时钟每轮派发、
  虚拟时钟零变化）；挖寶/氣球/喜從天降 tick 间位置插值。
- **H(3/4) 同格多目标消歧 ✅**（提交 `80f45a0`）：无主空地改走 `addItem` 通道
  （spr=nullptr 仅登记不绘制）与有主地块同参 `stable_sort(y)`，拾取顺序与原版
  "同一 y 排序 drawList" 对齐；绘制循环加 `it.spr` 守卫，绘制输出不变；14 场景 PASS。
- **H(4/4) 射程限制：撤回（2026-10-05 用户确认）**——此前对本项的理解有误，**M4 不做
  射程限制**（保持原版语义：人类选目标仅视口限制；AI 判据不动），相关提议（自定义
  射程表 b）作废，留待后续专项处理。**无任何代码改动**（仅有本节记录，已订正）。
- **H 小尾巴 ✅（提交 `50f4cef`）**：喜从天降财神巡游 `richX` 插值（`prevRichX` 回插，
  与袋子同构——本函数"先画后更新"）；事件槽 NPC 悬停提示名修复（`buildTipText`
  0xF000|4..7 → `kNpcNames`，原版 0x417B9F `dword_47ED5A` 分支；此前返回空=悬停无提示）；
  `sub_40D293` 15 调用点全量复核（showObjectTip/onPlayerActionPhase×3/模态自身/10 卡道具
  全部对齐，范围路径 = 位掩码全体命中等价；详见 §11 与 `map-target-audit.md` §1）；
  调试命令增强 `dump hits` / `npc.dump` 坐标槽位；场景 `312_npc_tip_name`
  （release→teleport→press 强制渲染→clickplayer 4 → `tip show id=61444 "小偷"`）。
- **M4 并入 main ✅（2026-10-05，merge `27bc265`）**：`feat/m4-render-ui` 36 提交并入
  main（3 处冲突解决：map_render gray+空 spr 复合、target_select cursorFrames+高亮并存、
  address-map 重生成）；合并后构建通过、16 场景冒烟全 PASS、ctest/L0 通过。

## 20. 宽屏全屏模态修复（2026-10-05 实机，并入 main 后）

**实机反馈**：① ATM 面板重影；② 银行/百货/医院/监狱/魔法屋等地图路标 UI 大界面
两侧仍是游戏画面且按钮错位；要求"原版全屏 UI 画面保持比例置中、两侧黑色填充"。

### 20.1 根因

| # | 现象 | 根因 |
|---|------|------|
| 1 | ATM 重影 | `bankVisitDialog` 在 `runModal` **之前**预绘制 `redrawVisit`（无 origin，画在画布 (60,71)），模态内每帧重绘在 `base+(60,71)` → 左侧残留未平移"幽灵面板"；`saveRegion/restoreRegion` 也用 640 坐标 |
| 2 | 按钮错位 | 模态命中用 `Application::mouseLogicalPos`（读 SDL 实时鼠标）——宽屏返回**画布坐标**，未减模态 origin=base → 与 640 基准命中区整体差 106px（银行停留/保释/百货/数字框/小游戏娃娃）|
| 3 | 两侧游戏画面 | 9434b10 将银行 4 处误设 `fillBars=false`（银行实为全屏铺底）|
| 4 | 右栏被切半 | 仅填黑两侧而保留宽屏世界画面 → 640 区里宽屏右栏（653..853）被黑边裁掉；原版语义背景应为完整 640 布局画面 |
| 5 | 退出后黑边残留 | 填黑后静止帧跳过重绘（`idleFrame`）不会自愈 |

### 20.2 修复

- **fillBars 全屏剧场框架增强**（`application.cpp`）：
  - 进入时先 `renderModalBackdrop(*this, base)`——`LayoutNativeGuard(base)`（布局派生临时锁定
    640：`uiLayout.h` + 世界原点；`uiPanelOffsetX` 叠加世界原点）把游戏画面以**原版 640 布局**
    重绘到居中区（完整右栏在 440..640），再两侧填黑；仅栈内有游戏循环层时执行
    （`EventStack::hasGameplayLayer`，主菜单/选人赛前模态不重绘世界）。
  - handler 返回后重填两侧（handler 内 `renderGameFrame` 可能覆盖）。
  - 从填黑模态退出（栈顶转非 centerBase）→ `m_forceRepaint=true`，下一帧游戏循环
    `nullptr` 分支全量重绘恢复宽屏画面。
- **mouseLogicalPos 减绘制 origin**（`application.cpp`）：模态 dispatch 期间 origin=base →
  返回 640 基准坐标；debug 合成输入覆盖坐标同步 `+base`（`dispatchSyntheticMouse` 统一），
  headless 与真实鼠标路径一致。
- **预绘制收口**：ATM/存读档删除模态外预绘制（`handler(nullptr)` 进入时以正确 origin 重绘）；
  拍卖静态帧移入 `auctionHandler(nullptr)`（否则被 backdrop 覆盖）；分红预绘制包
  `SurfaceOriginGuard` + 快照/恢复坐标 `+base`；confirm/defeat/mapSelect 的
  `saveRegion/restoreRegion` 坐标 `+base`。
- **银行 4 处 `fillBars` 恢复默认 true**（订正 9434b10）。

### 20.3 验证

- native：ATM/拍卖/医院/监狱/百货/魔法屋/开奖/月结等修正区与 native 截图像素级一致
  （409k 像素仅 356 差异=光标/闪烁）；退出后两侧恢复游戏画面。
- wide：截图像素验证——中间区 106..746 与 native 全图几乎逐像素一致、两侧恒黑；
  `mouseLogicalPos` 真实鼠标路径（`--game-click` warp，wide 点击 (451,345)）命中银行
  "申請貸款"（DBG 实测 mouse=(345,345) originX=106 event=(345,345) hit=1）。
- 回归：228/192/320/114/110/54/66/56/258/222/286/246/252/220/234/283/112/120
  native + wide 全 PASS（含 20.2 各项对应场景）。

### 20.4 实机二轮：竖排文本 + fillBars 分类订正（2026-10-05）

**实机反馈**：① 日历"星期"显示错误；② 托管界面"確定/取消"字符显示错误；
③ ATM 被错误收窄（应保持宽屏底色/游戏画面）；④ 医院/监狱没有将底色置黑；
⑤ 新闻/命运界面被左右压缩（同 ATM）。

**根因与修复**：

| # | 现象 | 根因 | 修复 |
|---|------|------|------|
| 1/2 | "星期五"只剩首字、"確定/取消"只剩单字 | 原版 `drawText` **align==3 = 竖排**（`drawTextVertical` 0x44F7C7）；重写 text.cpp 虽实现了竖排，但测量搜索框**宽高写反**（rcW=竖排总高、rcH=单字高）→ measure 只覆盖首字；且首字基线从缓冲区 y=0 起（字形上半被裁） | `text.cpp`：align==3 时 `rcW=单字宽+pad / rcH=竖排总高+pad`；竖排基线自 `pad` 起（5 处 align=3 调用点均为原版竖排语义：日历星期×2/托管確定取消/右栏 4 页签）|
| 3/5 | ATM/新闻/命运两侧黑 | 三者均为**叠加式**面板（ATM 320×338 悬浮、新闻/命运 440×480 报纸占地图区、右栏保留）→ 应 `fillBars=false` | `bankVisitDialog`/`newsEvent`/`fateEvent` 改 `fillBars=false`（ATM 保留 20.2 的删预绘制+save/restore +base 修复）|
| 4 | 医院/监狱两侧显示游戏地图 | panel[65]/[63] 帧 0 是**全屏 640×480 场景**（含右栏区域），应两侧黑+全屏居中 | `hospitalVisitDialog`/`jailBailDialog` 改 `fillBars=true`（订正此前叠加式分类）|

**分类基准（2026-10-05 定稿）**：
- **全屏剧场（fillBars=true，两侧黑+640 居中）**：银行停留/週轉/催收、百货、医院、监狱、
  魔法屋、拍卖、开奖、结算/月结、分红、股市、彩票投注、主菜单/选人/通关演出。
- **叠加式（fillBars=false，保留游戏画面）**：ATM 柜员机、新闻、命运、存读档、大地图、
  设置/帮助/热键/日期/询问框/数字框/消息/台词/选骰/设施/研究所/交易市场/AI/卡片道具/
  住院保释面板（选择面板）。

**附带修正（saveRegion/restoreRegion origin 语义）**：`saveRegion/restoreRegion` 内部经
`deviceX/deviceY` **自动应用绘制原点**（同 blit 家族）→ 模态 handler 内（origin=base）
调用时一律传 640 基准坐标，不得手动 +base（20.2 在 dividend handler / defeat handler
内手动 +base 会造成双重偏移——本轮已订正；runModal 外调用（bank/confirm/mapSelect/
defeatFlow 尾）origin=0 保留手动 +base）。

**三轮修正（2026-10-05 实机：拍卖嵌套未置黑 + 竖排首字仍缺半）**：

- **嵌套 fillBars 模态填黑失效**：新闻/命运/魔法屋内调用 `runAuction` 时，拍卖层进入
  `dispatchModalAware` 发生在**外层 handler 调用栈内**——外层 `SurfaceOriginGuard(=base)`
  与 `paintClip(base..base+640)` 尚未析构/恢复，填黑被平移且被裁剪带裁掉（顶层进入无此问题）。
  修复：进入分支的 backdrop/填黑与末尾事后填黑均加 `SurfaceOriginGuard(0,0)` +
  `SurfacePaintClipGuard(-1,-1)`（绝对画布坐标 + 绕过外层裁剪）。
- **竖排首字仍缺半个**：首字基线此前用 `pad=10` 起，但字形上缘在基线以上 `bearingY`
  （≈ascent 14+px）→ 改用 `ascent` 作首字基线（`vTop = ascent`）。

**验证**：wide 截图——日历竖排"星期五"完整（首字含）、托管竖排"確定/取消"完整、
新闻触发的拍卖两侧纯黑（strip max=0，嵌套路径）、ATM/新闻/命运两侧保留游戏画面、
医院/监狱两侧纯黑、defeat 倒计时+"PLAY AGAIN"横幅居中（native 背景+两侧黑）；
ATM 真实鼠标命中（`--game-click` (451,345) → hit=1 + number input）；
54/66/228/68/110/114/258/222/00/248/320 + 286/56/220/246/192/234 native + wide 全 PASS。
