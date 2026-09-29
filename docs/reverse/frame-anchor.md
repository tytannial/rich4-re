# 帧锚点（offset）与落点对齐

> 横切主题档案：解释 SMP/SPR 帧头 `x/y` 的语义、原版与重写的绘制落点规则，
> 以及**像素对齐校验流程**（工具 `tools/frame_align.py`）。
> 凡面板/精灵出现"元素整体偏几像素~几十像素"或"原版实参可疑"类问题，先查本文件。

## 1. 结论（TL;DR）

- SMP/SPR 帧头 `(w, h, x, y, size)` 的 `x/y` 是 **锚点偏移**（i16，可为负），不是裁剪坐标。
- **原版与重写的元素 blit 落点规则一致**：`dst = (实参 − offset)`
  （原版 `blitElementOpaque` 0x455B3A / `blitElement` 0x455C52 / `blitElementRegion*` 0x455E24；
  重写 `clipBlit`，`src/render/blit.cpp` 开头 `dx = x - src.offsetX`）。
- **移植规则：直接用原版实参，不要自行加减 offset**（`blitElement*`/`blitSpriteFrame*` 内部已处理）。
- offset 非 0 的帧**无需补偿**；像素对齐工具的用途是**验证原版实参是否存在笔误**。

> 教训（2026-09-23）：曾误判"重写 blit 不应用 offset"，对監獄/医院头像与 NPC 标记做了
> `x - f.offsetX` 补偿 → **双重补偿**，元素偏移达 offset 两倍（強盜标记偏 (-116,-213)）。
> 原因：只读了 `blitElement`→`blitElementRegion` 主体，**没有读到底层 `clipBlit`**。
> 回退后恢复。**读绘制函数务必读到实际写像素的那层。**

## 2. 原版证据

### 2.1 blit 落点公式

`blitElementOpaque`（0x455B3A）反编译开头（另两个 blit 同构）：

```c
int __cdecl blitElementOpaque(int w, int h, dst, unsigned __int16 *elem, int x, int y) {
  x -= (__int16)elem[2];      // elem[0]=w, elem[1]=h, elem[2]=offX, elem[3]=offY
  y -= (__int16)elem[3];
  ...  // 以 (x, y) 为左上角绘制
}
```

`blitElementFullscreen`（0x4563F5）、`sub_456418`（0x456418）、`blitOpaqueToBackbuffer`
（0x45643D）分别只是上述三函数的 640×480 包装。

### 2.2 offset 的来源（加载链）

- 资源帧头即"元素头"：`w/h` 两个 u16 + `x/y` 两个 i16（+ 第 3 个 dword 为 size/数据指针）。
- 加载 `sub_450441`（0x450441）对 SPR/SMP 只调用 `relocateFrames`（0x450069），
  后者**只把每帧第 3 个 dword（size 字段）改写为像素数据指针**（SPR 还要跳过 512 字节调色板），
  **`x/y` 原样保留**（Python 侧 `tools/frame_align.py offsets` 读到的就是运行时的 offset）。
- offset 是**美术锚点**：例如 panel[63] 头像帧 17 offset=(-8,-25)，绘制时按实参 -offset 偏移，
  正好落在格框内；医院 panel[65] 护士帧全部为 0（实参即落点）。

## 3. 移植与排查规则

1. 反编译原版调用点，抄下**实参**（如 `blitElementFullscreen(帧7, 139, 180)`）→ 直接使用。
2. 元素位置异常时，用像素对齐验证**原版实参**（不是重写代码的坐标）：
   `python tools/frame_align.py align` 求最佳落点，与原版实参比对：

| 对比结果 | 含义 | 处理 |
|----------|------|------|
| 最佳落点 ≈ 实参 − offset（或 offset=0 时 = 实参） | 原版实参正确 | 重写照抄实参 |
| 最佳落点与实参 − offset 明显不符（误差差 5~15 倍） | **疑原版笔误** | 记录证据后按最佳落点修正，注释注明差异 |

3. 不要全局改 `blitElement*` 的 offset 语义（已与原版一致，无需改）。

## 4. 像素对齐校验流程（实战方法）

```bash
# ① 导出资源帧为 PNG（MKF → bin → 帧 PNG）
python tools/mkf.py extract resources/MultiverseJourney/Panel.mkf <dir> --index 65
python tools/smp.py extract <dir>/Panel_0065.bin <dir>/frames

# ② 查帧表 offset（SPR/SMP 均支持，*.mkf 需 --index）
python tools/frame_align.py offsets resources/MultiverseJourney/Panel.mkf --index 65

# ③ 全范围搜索最佳落点：以"大图/静止帧"为基准，待校准帧为候选
#    --base-at = 基准帧在屏幕上的实参（如 104,110）；err 最小处即最佳落点
python tools/frame_align.py align \
  --base frames/Panel_0065_004.png --base-at 104,110 \
  --cand frames/Panel_0065_008.png --range 100,260,130,260 --top 3
#    (139,180) err=19.3   ← 正确
#    (139,179) err=77.2 / (139,181) err=82.7
```

## 5. 实战案例

### 5.1 医院护士（panel[65]，offset 全 0）

帧内容（PNG 实测）：

| 帧 | 内容 | 原版实参 | 验证 | 重写 |
|----|------|----------|------|------|
| 4 | 左护士全身（基准 @104,110） | — | — | 同 |
| 5/6 | 左眼 睁/闭（眨眼） | (139,158) | 最佳落点即此 | ✅ |
| 7 | 左嘴 张 | (139,180) | err 15.8 | ✅ |
| 8 | 左嘴 闭（驻留复位） | **(179,198)** | err 300.9；最佳 (139,180) err 19.3 | 修正为 (139,180) |
| 9 | 右护士全身（退场 @91,112） | — | — | — |
| 10/11 | 右眼 | (165,158) | ✅ | ✅ |
| 12 | 右嘴 张 | (165,183) | ✅ | ✅ |
| 13 | 右嘴 闭（复位） | **(205,198)** | 最佳 (165,183) | 修正为 (165,183) |

原版帧 8/13 传的是 `Rect.right/bottom`（矩形右下角，疑复制粘贴笔误），效果是把嘴部
复位帧贴到护士右手上；像素差 300 vs 19 是决定性证据。旧注释曾把帧 4/5/7 误标为
"标题板/行走帧/小像"，已订正为"护士全身/眼/嘴"。

### 5.2 監獄/医院面板头像与 NPC 标记（offset 非 0）

panel[63] 头像帧 5..20、panel[65] 头像帧 14..29、panel[64] 标记帧 0..3 的 offset 均非 0；
**直接用格坐标实参**（`blitElement(dst, f, x, y)`），blit 内部按 offset 定位。
曾误加 `- f.offsetX/offsetY` 造成双重补偿，已回退（2026-09-23）。

### 5.3 收租联动高亮整体上移 40px（pickBuffer 坐标系陷阱，2026-09-26）

**症状**：过路费（收租联合租金）闪烁的高亮菱形比建筑整体偏上约 40px（≈1 格）；新闻破坏类、
命运 id0/1 的单块高亮同路径。

**原版证据链**：
- 写入端 `rebuildPickBuffer 0x409B18` 物件高亮：
  `writePickBuffer(g_pickBuffer, g_pickMask+12+12*帧, g_drawListX, g_drawListY - 40, id)`；
  `writePickBuffer 0x456A1C` 落点 = `(x - offX, y - offY)`。
- 回贴端 `highlightBlink 0x451985`：
  `dst = backbuffer + 40 行`、`blitHighlightedMap(dst, backbuffer+51200, g_pickBuffer, 440, 440,
  lut)` → **pickBuffer (0,0) = 屏幕 (0,40)**（地图区 440×440 = 480−40 工具条高度）。
- 所以写入的 `-40` 是"屏幕 → pickBuffer 局部坐标"换算，回贴 `+40` 抵消；
  **最终屏幕落点 = `(drawListX - offX, drawListY - offY)`**，与建筑绘制实参完全一致
  （建筑 blit 同用 `g_drawListX/Y`，见 0x40988E 绘制循环）。

**重写 bug**：`captureHighlightShapes` 直接生成屏幕坐标给 `drawEstateHighlight` 逐像素绘制，
却沿用了 `it.y - 40` → 高亮整体上移 40px。
**修复**：`map_render.cpp` 的 `hlAnchorY = it.y`（去掉 -40；x 无需换算，pickBuffer 与屏幕
x 相同）。

**教训**：从原版抄坐标常量前先确认**坐标系**（pickBuffer 局部 / 屏幕 / 世界投影）。
`-40`/`+40` 这类常数很可能是坐标系换算而非美术偏移。

## 6. 已扫描清单：Panel.mkf 非零 offset 帧（2026-09-23）

> 用途：了解哪些帧是"锚点帧"（实参 ≠ 最终落点），排查时优先核对；
> **不需要在调用点补偿**。扫描命令见 §4。

| panel | 帧（offset） | 重写调用点 | 状态 |
|-------|--------------|------------|------|
| 0 / 7 / 21 / 22 / 72 / 75 | 全部 0 | 工具条/日历/数字框/股市 | — |
| 1 | 帧 1..22（(9,17)~(21,20)，中心锚点小图标） | 待确认用途（save_dialog 面板来源存疑） | 待核对 |
| 2 | 帧 8/9 (12,11)、10/11 (10,10) | `game_panel.cpp:403/404`（日历左右箭头 @462,300/492,301） | blit 内部已处理 ✓ |
| 3 | 帧 0..5 (84,-98)、6..11 (-11,-103)、12..17 (-68,-99) | `map_render.cpp` `blitSpriteFrameClipped(dx+85, dy+145)`（灰度头像） | blit 内部已处理 ✓ |
| 63 | 帧 5..20 头像（(0,-10)~(-26,-30)） | `jail_dialog.cpp` `drawPanel` | ✓（补偿已回退） |
| 64 | 帧 0..3（(79,181) 等） | NPC 释放标记（監獄/医院） | ✓（补偿已回退） |
| 65 | 帧 14..29 头像（(-13,-30)~(0,-5)） | `jail_dialog.cpp` `drawPanelHosp` | ✓（补偿已回退） |
| 68~71 | 帧 2..13（(82,82) 中心锚点） | `roulette_dialog.cpp:155`（旅館/購物转盘） | blit 内部已处理 ✓ |
| 77 | 帧 6..17（(42,34) 等） | `ai_dialog.cpp:398`（AI 头像网格） | blit 内部已处理 ✓ |

尚未扫描：`data.mkf` 的 SPR 精灵（玩家棋子/骰子/物件/卡片）、`jump.mkf` 选人资源、
`map.mkf` 图块——出现错位时按 §4 流程核对。

## 7. 待办

- [ ] 扫描 `data.mkf` 高频 SPR（`blitSpriteFrameClipped` 路径）的 offset 分布。
- [ ] 元素错位类问题统一流程：先查本文件 → 像素对齐 → 再改代码（禁止凭肉眼补 offset）。
