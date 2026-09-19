# 0x407AD2 loadMapData（地图数据加载）

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x407AD2` |
| 大小 | `0x41E`（1054 字节） |
| 调用者 | `0x401B9C` WinMain case 0/1/4 |
| 被调用 | `0x4080F5`、`0x4502FE`、`0x450441`、`0x40AA6C`、`0x40E033`、`0x40B93B`、`0x40A4E1`、`0x428CAF` 等 |
| 重写符号 | `src/app/new_game.cpp:277` `loadMap` + `src/app/map_render.cpp` `initMapEntities` |
| 状态 | 已实现（资源/五表/棋子动画；随机物件与小地图标记待阶段 2） |

## 功能

从 `MAP.MKF` 加载当前地图（GND + MAPDAT + 图块/棋子精灵），解析 MAPDAT 五表，
初始化格子表与玩家棋子动画资源。

## 逆向依据

- 反编译观察：
  - `sub_450441(MAP.MKF, 2*(4*mode+map))` → GND；`+1` → MAPDAT
  - `sub_4502FE("MAPDAT.MKF") == -1` → fallback 走 MAP.MKF 奇数索引
  - GND 布局：`+16` 调色板 512B、`+528` 格索引 10368B、`+10896` 图块 8bit
  - 资源索引见 [mapdat.md](../../formats/mapdat.md)
- 字符串引用：`"MAP.MKF"`(0x4631C0)、`"MAPDAT.MKF"`(0x4631C8)
- 全局变量：`g_gnd`(0x474945)、`g_mapDat`(0x47493C)、`g_cellEnts`(0x498E80)、
  `g_cellTable`(0x496D08)、`g_pieceSprites`(0x498EB0)、`dword_48BAD8`(0x48BAD8)
- 交叉验证：map.mkf 8 个 MAPDAT 的 `evtCellOffset+28*evtCellCount+28 == filesize` 全部成立

## 关键结构

```c
// MAPDAT 头部 10 个 u32（count/offset 对），各表 index 0 保留
// cellEnt 40B / estate 52B / corp 56B / specPt 52B / evtCell 28B
// 重写: GameState::cellEnts/estates/corps/specPts/evtCells（vector，含保留槽）
```

## 重写要点

- **GND**：`state.gnd` 保存原始字节，`gndPalette/gndCellIndex/gndBitmaps` 为指针视图。
- **MAPDAT**：`parseMapDat` 按头部 offset/count 拷贝五表（`count+1` 项）。
- **图块资源**：`loadUiImage` 把 MKF 资源指针化为 `UiImage`（对应 `sub_450069`）。
- **棋子动画**：`data.mkf[21*charIndex + 128 + 3*travel]`（`initMapEntities`）。
- **差异**：
  - 玩家地图数据（10008B/玩家，`g_playerMapBlocks`）与 `sub_40B93B` 棋子动画状态机
    简化为静态帧 0（阶段 2 接入朝向/动画）。
  - `sub_40E033` 随机物件放置、`sub_40A4E1` 小地图标记叠加、`sub_42915A` 待阶段 2。
  - `sub_428CAF`/`sub_4080F5` 未实现。

## 验证方式

`--shot` 截图：主菜单→新游戏→选人→OK→游戏画面；日志确认
`map data loaded: GND 5319312 B, cellEnts=104 estates=51 corps=5 specPts=4 evtCells=22`。
