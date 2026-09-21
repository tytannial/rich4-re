# 0x40829D renderMap（等距地图渲染）

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x40829D` |
| 大小 | `0x1786`（6022 字节） |
| 调用者 | `0x415E70`（地图区绘制）、`0x40E32C`、`0x40D7C4` 等 |
| 被调用 | `0x407A2C`、`0x407A8C`、`0x4557A1`、`0x456770`、`0x4564C1`、`0x457E6C` 等 |
| 重写符号 | `src/app/map_render.cpp` `renderMap` |
| 状态 | 已实现（地面/cellEnts/玩家棋子/特殊点/事件格/地产/公司 + y 排序） |

## 功能

以当前玩家像素坐标为视口中心，绘制等距地图：地面地块（四边形纹理映射）、
cellEnts 物件、玩家棋子，以及地产/公司/特殊点/事件格（按 y 排序后绘制）。

## 逆向依据

- 反编译观察：
  - 视口中心 = `dword_48B2AC/B0`（首次时取当前玩家坐标）
  - 坐标换算 `sub_407A2C`：`outX = (xo*c0 + yo*c2)>>5`（`byte_474910..913` 8 方向系数）
  - 等距表 `word_46CCF0`：8 方向 × 29×29 格 × 4 字节（`CCF0=y, CCF2=x, CCF4/CCF6=相邻列`）
  - 遍历序 `byte_473610`：8 方向 × 296 项 × `{i8 row, i8 col}`
  - 地块贴图 `sub_4557A1(dst, tile32x32, quad)`：逐行扫描 + u/v 插值
  - 绘制顺序：地面 → cellEnts → 玩家 → 地产 → 公司 → 特殊点 → 事件格（后段按 y 排序）
  - 首次进入随机出生点 `sub_40AA0F`（未被占用且有连接道路的 cellEnt）
- 全局变量：`g_gndCellIndex`/`g_gndBitmaps`/`g_gndPalette`、`g_cellEnts`、
  `g_playerSpriteX/Y`、`dword_499088`（旋转方向）
- 交叉验证：等距表 26912B + 遍历序 4864B + 系数 32B 在 DGROUP 内连续衔接
  （`0x46CCF0 → 0x473610 → 0x474910 → 0x474930`）

## 关键结构

```c
// 等距表索引（字节偏移）: idx = 3364*dir + 116*(row+14) + 4*(col+14)
//   CCF0=idx+0(y0) CCF2=idx+2(x0) CCF4=idx+4(y1) CCF6=idx+6(x1)
// 四边形顶点: (col,row)→(col+1,row)→(col+1,row+1)→(col,row+1)
// 纹理坐标: (0,0)→(32,0)→(32,32)→(0,32)
// 重写 quad[4][2] = {x,y}；isoBlitQuad 扫描线仿射映射
```

## 重写要点

- **索引易错点**：原版 `word_46CCF2 + 字节偏移` 是 **char* 运算**；
  重写用 `uint8_t*` 视图 + `reinterpret_cast<const int16_t*>` 读取，
  切勿把字节偏移直接用于 `int16_t*`（曾因此导致地块水平撕裂）。
- **坐标语义**：`x = CCF2(HIWORD)`、`y = CCF0(LOWORD)`（与 sub_40829D 的 int 打包一致）。
- **等距贴图**：原版 `sub_45596A` 边光栅化（16.16 定点 span 缓冲）等价实现为
  `isoBlitQuad` 标准扫描线 + u/v 线性插值（等距地块为平行四边形，仿射等价）。
- **差异**：地产/公司/特殊点/事件格已收集并按 y 排序绘制（`SpriteDraw` 列表，
  等价 `dword_48A44C/48A84C/word_48A850/854/856`）；玩家棋子行走动画/朝向帧已接入
  （帧 = `playerMoveFrame + dir*perDir`，`dir=(8-mapRotation+p.dir)&7`；**移动中即朝本步方向**，
  见 `40d7c4-game-state-machine.md`）；调色板染色（原版 `*(WORD*)(palette+510)`）阶段 3 接入。
- 原版事件槽（玩家 4-8）NPC + `g_miscTable80` 段未绘制（本作 M1 无事件槽 NPC）。
- 骰子滚动/点数绘制见 `game_panel.cpp`/`turn_system.cpp`（屏幕固定落点，见 `audio-system.md` 同段）。

## 验证方式

`--shot` 截图：可见草地/道路/NEWS 建筑/监狱/玩家棋子，与原版画面逐区比对。
