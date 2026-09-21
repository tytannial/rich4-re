# MKF 资源包格式

> 大宇 MASSIVE 资源容器。`rich4.exe` 通过 `sub_4502FE`(打开) / `sub_450441`(读取子文件) 访问。
> 本规范由逆向 + 7 个实际 `.mkf` 文件交叉验证得出。

## 总体布局

```
+----------------------------+ 0
| index_offset : u32         |   索引表在文件中的绝对偏移
+----------------------------+ 4
| 资源区                      |
|   [资源头 16B][数据...]     |
|   [资源头 16B][数据...]     |
|   ...                      |
+----------------------------+ index_offset
| index[0] : u32 = 4         |   资源 0 头偏移（资源区起始）
| index[1] : u32             |   资源 1 头偏移
| index[2] : u32             |   资源 2 头偏移
| ...                        |
| index[N-1] : u32           |   资源 N-1 头偏移（也是最后一个资源）
+----------------------------+ EOF
```

- 索引项数 `N = (filesize - index_offset) / 4`
- 资源数 `= N`（**索引表每一项都是资源头偏移**，没有结束哨兵项）
  - 依据: Data.mkf 索引 602 项，`index[601]` 指向 614400 字节资源，
    原版 `0x401543` 中 `sub_450441(handle, 601)` 正是读取该资源；
    7 个 MKF 全部验证 `index[N-1] + 16 + compressed_size == index_offset`
- 所有整数为 **小端序 (little-endian)**
- 第 `i` 个资源头位于 `index[i]`，其数据紧跟头之后
- 第 `i` 个资源数据大小 `= compressed_size`

## 资源头 (16 字节)

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| 0 | u32 | `decompressed_size` | 解压后字节数 |
| 4 | u32 | `compressed_size` | 存储字节数 |
| 8 | u32 | `data_offset` | 解压后数据内需做像素格式转换的起始偏移 (v10) |
| 12 | u32 | `data_length` | 需转换的长度 (v11) |

- 若 `compressed_size == decompressed_size`：**未压缩**，数据原样存储
- 否则：数据为 [LZHUF 压缩](lzhuf.md)，解压到 `decompressed_size`
- `data_offset` / `data_length` 用于 `sub_451801`：把 16bit 像素从资源内部格式转换为当前显卡格式（`data_length` 通常为 512，即 256 色调色板）

## 子文件类型 (按数据魔数)

| 魔数 | 位置 | 类型 | 文档 |
|------|------|------|------|
| `SPR\0` | +0 | 8bit 索引精灵 | [spr.md](spr.md) |
| `SMP\0` | +0 | 16bit 高彩位图 | [smp.md](smp.md) |
| `RIFF` | +0 | WAV 音频 (PCM) | 标准 RIFF/WAVE |
| `GND\0` | +0 | 地图地面数据 | [map.md](map.md) |
| `0xAF12` | +4 | FLC 帧动画 | [flc.md](flc.md) |
| 无（16bit 像素） | — | RAW 裸位图（尺寸按 index 硬编码） | [raw_bitmap.md](raw_bitmap.md) |
| 其它 | — | BIG5 文本 / 结构·数据表 / 占位 | [text.md](text.md) |

> `sub_450441` 读资源后：解压 → 若 `data_length!=0` 调 `convertPixelFormat` 转 16bit 像素 →
> **仅当**前 3 字节为 `SPR`/`SMP` 才调 `relocateFrames`。FLC 与 RAW 均不走 `relocateFrames`；
> FLC 由独立播放器 `sub_450CED`/`sub_450F04` 解码，RAW 由调用方按 index 已知尺寸直接使用。

## C 结构草稿

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t decompressed_size;
    uint32_t compressed_size;
    uint32_t data_offset;
    uint32_t data_length;
} MkfEntryHeader;
#pragma pack(pop)

typedef struct {
    uint32_t index_offset;
    uint32_t entry_count;      /* N */
    const uint32_t *index;     /* N 项, index[i] = 第 i 个资源头偏移 */
    const uint8_t  *data;
    size_t         size;
} MkfArchive;
```

## 实际文件统计

| 文件 | 资源数 | 压缩数 |
|------|--------|--------|
| Data.mkf | 602 | 310 |
| Speaking.mkf | 1374 | 0 |
| map.mkf | 298 | 23 |
| Effect.mkf | 115 | 0 |
| Panel.mkf | 113 | 92 |
| help.mkf | 100 | 0 |
| jump.mkf | 71 | 36 |
| **合计** | **2673** | **461** |

## 工具

```bash
python tools/mkf.py info    <file.mkf>          # 头部信息
python tools/mkf.py list    <file.mkf>          # 列出所有子资源
python tools/mkf.py stats   <file.mkf>          # 魔数分布
python tools/mkf.py extract <file.mkf> <dir> [--index N] [--raw]
```
