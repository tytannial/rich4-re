# MAPDAT 地图数据格式

> 状态：**已解析**（`0x407AD2` loadMapData 反编译 + `map.mkf` 8 个资源实测验证）。
> 本作**无独立 `MAPDAT.MKF`**：`0x407AD2` 中 `sub_4502FE("MAPDAT.MKF")` 返回 -1，
> 走 fallback 分支从 `MAP.MKF` 的**奇数索引**读取。

## 资源位置（MAP.MKF）

设 `base = 4 * gameMode + mapIndex`（gameMode 0=原版 / 1=时空之旅，mapIndex 0-3）：

| 索引 | 内容 |
|------|------|
| `2*base` | GND 地图数据（见 [map.md](map.md)） |
| `2*base + 1` | **MAPDAT**（本文件） |
| `base + 16` | 小地图（200x200 SMP） |
| `base + 79` | 地产标志图块 |
| `24` / `25` / `26` | cellEnt 图块 / 玩家棋子图块 / 旗帜图块 |
| `n + 5*base + 39`（n=0..4） | 特殊点图块 |
| mode 0：`i + 87` / mode 1：`17*mapIndex + 104 + i`（i=0..16） | 公司图块 |

## 文件布局

```
+----------------------------+ 0
| u32 cellEntCount           |     g_cellEntCount   0x498E9C
| u32 cellEntOffset          |     = 40（表紧接头）
+----------------------------+ 8
| u32 estateCount            |     g_estateCount    0x498E98
| u32 estateOffset           |
+----------------------------+ 16
| u32 corpCount              |     g_corpCount      0x498E8C
| u32 corpOffset             |
+----------------------------+ 24
| u32 specPtCount            |     g_specPtCount    0x498E90
| u32 specPtOffset           |
+----------------------------+ 32
| u32 evtCellCount           |     g_evtCellCount   0x499074
| u32 evtCellOffset          |
+----------------------------+ 40
| cellEnt[cellEntCount+1]    |     40 字节/项
| estate[estateCount+1]      |     52 字节/项
| corp[corpCount+1]          |     56 字节/项
| specPt[specPtCount+1]      |     52 字节/项
| evtCell[evtCellCount+1]    |     28 字节/项
+----------------------------+
```

- **每表 index 0 保留**（原版循环从 1 开始），故实际拷贝 `count+1` 项。
- 原版校验：`evtCellOffset + 28*evtCellCount + 28 == filesize`（8 个资源全部成立）。
- 实测（map.mkf[1]）：cellEnt 103 / estate 50 / corp 4 / specPt 3 / evtCell 21，7956 字节。

## 表项字段（按原版字节偏移）

### cellEnt（40 字节，`g_cellEnts` 0x498E80）

| 偏移 | 类型 | 语义 |
|------|------|------|
| +0 | i16 | 像素坐标 x（`>>5` = 格子 x） |
| +2 | i16 | 像素坐标 y |
| +24..+30 | u16[4] | 连接道路的相邻 cellEnt（`exits`，出生点候选判定） |
| +32 | u16 | 特殊标记（8001/8002，`word_48BAE2/E0`） |
| +34 | u16 | 图块索引（`dword_474949 + 12*(idx-1)`） |
| +36 | u32 | 占用位掩码（`|= 256 << player`） |
| +39 | i8 | 状态（`>= 0` 判定） |

### estate（52 字节，`g_estates` 0x498E84）

| 偏移 | 类型 | 语义 |
|------|------|------|
| +0 / +2 | i16 | 像素坐标 |
| +23 | u8 | 旗帜（`&1`） |
| +24 | u8 | 类型（决定商業用地图块组） |
| +25 | u8 | 拥有者（玩家号 + 1，0=无） |
| +26 | u8 | 等级 |
| +27 | u8 | 朝向（`&1`） |
| +48 | i32 | 价格 |

### corp（56 字节，`g_corps` 0x498E88）

| 偏移 | 类型 | 语义 |
|------|------|------|
| +0 / +2 | i16 | 像素坐标 |
| +24 | u8 | 类型（0-4，决定图块组） |
| +25 | u8 | 拥有者 |
| +26 | u8 | 子索引 |
| +27 | u8 | 朝向 |
| +28 | u8 | 旗帜（`&1`） |

### specPt（52 字节，`g_specPts` 0x498E7C）

| 偏移 | 类型 | 语义 |
|------|------|------|
| +0 / +2 | i16 | 像素坐标 |
| +24 | u8 | 拥有者 |
| +25 | u8 | `word_496988[18*owner]` 索引（价格计算） |
| +27 | u8 | 朝向 |
| +32 | u16 | 图块索引（`dword_48AE4C[idx]`） |
| +48 | i32 | 价格（`10000 - word_496988[...]`） |

### evtCell（28 字节，`g_evtCells` 0x498E78）

| 偏移 | 类型 | 语义 |
|------|------|------|
| +0 / +2 | i16 | 像素坐标 |
| +24 | u8 | 朝向 |
| +26 | u16 | 图块索引（`dword_48AE4C[idx]`） |

## 相关资源与索引（`0x407AD2`）

- `word_496988`（0x496988）：特殊点价格表（`.bss`，运行时初始化）。
- `dword_474949` = MAP.MKF[24]（cellEnt 图块集）；`dword_47494D` = MAP.MKF[26]（旗帜）。
- `dword_48AE4C[5]` = MAP.MKF[n + 5*base + 39]（特殊点图块组）。
- `dword_48AE60` = MAP.MKF[base + 79]（地产标志）。
- `dword_48AE64[17]` = 商業用地图块组（mode 0/1 索引不同）。
- `dword_49692C[cellType]` = data.mkf[cellType + 395]（cellType 1-20；`dword_496930 = &dword_49692C[1]`）。
- `dword_48BAD8` = data.mkf[517]（住宅用地/商業用地标记图块集）；`dword_48BAD4` = data.mkf[519]。

## 待逆向

- `word_496988` 的初始化位置与取值。
- cellEnt `+39` 状态字段语义。
- 各表剩余未标注字段。
