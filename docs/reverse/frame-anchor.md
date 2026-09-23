# 帧锚点（offset）与落点对齐

> 横切主题档案：解释 SMP/SPR 帧头 `x/y` 的语义、原版元素绘制的落点公式、重写的差异，
> 以及**像素对齐校验流程**（工具 `tools/frame_align.py`）。
> 凡面板/精灵出现"元素整体偏几像素~几十像素"类错位，先查本文件再改代码。

## 1. 结论（TL;DR）

- SMP/SPR 帧头 `(w, h, x, y, size)` 的 `x/y` 是 **锚点偏移**（i16，可为负），不是裁剪坐标。
- 原版所有元素 blit 的落点 = **实参 − offset**：`dst = (x - offX, y - offY)`
  （`blitElementOpaque` 0x455B3A / `blitElement` 0x455C52 / `blitElementRegion*` 0x455E24 三者一致）。
- 重写 `blitElement*` / `blitSpriteFrame*` 以**参数为左上角**（不应用 offset）。
  因此移植原版调用时：**落点 = 原版实参 − offset**；offset = 0 时实参即落点。
- **offset 非 0 的帧必须校准**；原版实参偶尔有笔误（见 §5 案例）。
  不要凭肉眼，用 `tools/frame_align.py align` 做像素差扫描。

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
- offset 是**美术锚点**：例如 panel[63] 头像帧 17 offset=(-8,-25)，绘制时让头像相对格坐标
  向右下 8/25 像素，正好落在格框内；医院 panel[65] 护士帧全部为 0。

## 3. 重写移植规则

1. 反编译原版调用点，抄下**实参**（如 `blitElementFullscreen(帧7, 139, 180)`）。
2. `python tools/frame_align.py offsets <mkf> --index <n>` 查该帧 offset。
3. 若 offset = 0：重写直接用实参（如医院护士帧 5~8）。
4. 若 offset ≠ 0：重写落点 = 实参 − offset，即调用处写
   `blitElement(dst, f, x - f.offsetX, y - f.offsetY, ...)`（如面板头像类）。
5. 对"元素叠加在另一帧上"的场景（覆盖层动画），先做像素对齐验证：
   **错误落点的像素差会是正确落点的 5~15 倍**，可据此判断原版实参是否有笔误。

> 不建议全局改 `blitElement*` 去应用 offset：重写现存调用点语义已统一为"左上角"，
> 部分点位的参数是实测/校准过的；全局改动需要全量回归，风险大于收益。

## 4. 像素对齐校验流程（实战方法）

```bash
# ① 导出资源帧为 PNG（MKF → bin → 帧 PNG）
python tools/mkf.py extract resources/game/Panel.mkf <dir> --index 65
python tools/smp.py extract <dir>/Panel_0065.bin <dir>/frames

# ② 查帧表 offset（SPR/SMP 均支持，*.mkf 需 --index）
python tools/frame_align.py offsets resources/game/Panel.mkf --index 65

# ③ 全范围搜索最佳落点：以"大图/静止帧"为基准，待校准帧为候选
#    --base-at = 基准帧在屏幕上的落点；err 最小处即正确落点
python tools/frame_align.py align \
  --base frames/Panel_0065_004.png --base-at 104,110 \
  --cand frames/Panel_0065_008.png --range 100,260,130,260 --top 3
#    (139,180) err=19.3   ← 正确
#    (139,179) err=77.2 / (139,181) err=82.7
```

判定：

| 对比结果 | 含义 |
|----------|------|
| 最佳落点 = 原版实参 | 原版实参正确，重写照抄（offset=0） |
| 最佳落点 = 实参 − offset | 正常锚点，重写需减 offset |
| 最佳落点与实参/offset 都对不上 | **疑原版笔误**（记录证据后按最佳落点修正，注释注明差异） |

## 5. 实战案例：医院护士（panel[65]，2026-09-23）

帧内容（PNG 实测，修正了旧的错误命名）：

| 帧 | 内容 | 原版实参 | 解析 | 重写 |
|----|------|----------|------|------|
| 4 | 左护士全身（@104,110 的基准） | — | offset=0 | 同 |
| 5/6 | 左眼 睁/闭（眨眼） | (139,158) | err 最小即此 | ✅ |
| 7 | 左嘴 张 | (139,180) | err 15.8 | ✅ |
| 8 | 左嘴 闭（驻留复位） | **(179,198)** | **err 300.9；最佳 (139,180) err 19.3** | 修正为 (139,180) |
| 9 | 右护士全身（退场 @91,112） | — | offset=0 | — |
| 10/11 | 右眼 | (165,158) | ✅ | ✅ |
| 12 | 右嘴 张 | (165,183) | ✅ | ✅ |
| 13 | 右嘴 闭（复位） | **(205,198)** | 同帧 8 笔误；最佳 (165,183) | 修正为 (165,183) |

- 原版帧 8/13 传的是 `Rect.right/bottom`（矩形右下角，疑复制粘贴笔误），效果是把嘴部
  复位帧贴到护士右手上；像素差 300 vs 19 是决定性证据。
- 旧注释曾把帧 4/5/7 误标为"标题板/行走帧/小像"，已订正为"护士全身/眼/嘴"。

## 6. 已扫描清单：Panel.mkf 非零 offset 帧（2026-09-23）

> 扫描命令：`tools/frame_align.py offsets`（或 §4 的内联脚本）。
> "重写调用点"需按 §3 规则核对，✅=已校准，⚠️=待核对。

| panel | 帧（offset） | 重写调用点 | 状态 |
|-------|--------------|------------|------|
| 0 / 7 / 21 / 22 / 72 / 75 | 全部 0 | 工具条/日历/数字框/股市 | ✅ 无 offset 问题 |
| 1 | 帧 1..22（(9,17)~(21,20)，中心锚点小图标） | 待确认用途（save_dialog 面板来源存疑） | ⚠️ |
| 2 | 帧 8/9 (12,11)、10/11 (10,10) | `game_panel.cpp:403/404`（日历左右箭头 @462,300/492,301） | ⚠️ 未减 offset |
| 3 | 帧 0..5 (84,-98)、6..11 (-11,-103)、12..17 (-68,-99) | `map_render.cpp:508` `blitSpriteFrameClipped(dx+85, dy+145)`（灰度头像） | ⚠️ |
| 63 | 帧 5..16 玩家头像、17..20 NPC 头像（(0,-10)~(-26,-30)） | `jail_dialog.cpp` `drawPanel` | ✅ 已补偿 |
| 64 | 帧 0..3（(79,181) 等） | NPC 出狱标记（P5） | P5 时处理 |
| 65 | 帧 14..25 玩家头像、26..29 NPC 头像（(-13,-30)~(0,-5)） | `jail_dialog.cpp` `drawPanelHosp` | ✅ 已补偿 |
| 65 | 帧 5..13 护士动画（全 0） | `nurseTick` | ✅（含帧 8/13 原版笔误修正） |
| 68~71 | 帧 2..13（(82,82) 中心锚点） | `roulette_dialog.cpp:155`（旅館/購物转盘） | ⚠️ 绘制落点待核对 |
| 77 | 帧 6..17（(42,34) 等） | `ai_dialog.cpp:398`（AI 头像网格） | ⚠️ |

尚未扫描：`data.mkf` 的 SPR 精灵（玩家棋子/骰子/物件/卡片）、`jump.mkf` 选人资源、
`map.mkf` 图块——出现错位时按 §4 流程单独校准。

## 7. 待办

- [ ] 按清单逐个核对 ⚠️ 项（转盘/AI 头像/日历箭头/灰度头像最优先）。
- [ ] 扫描 `data.mkf` 高频 SPR（`blitSpriteFrameClipped` 路径）的 offset 分布。
- [ ] 新面板实现流程固化：写完绘制后用 §4 跑一遍基准帧对齐（大图基准 + 覆盖帧候选）。
