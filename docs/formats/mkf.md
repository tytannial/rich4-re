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
| index[0] : u32 = 4         |   资源区起始
| index[1] : u32             |   第 0 个资源头的偏移
| index[2] : u32             |   第 1 个资源头的偏移
| ...                        |
| index[N-1] : u32           |   最后一个资源的结束偏移
+----------------------------+ EOF
```

- 索引项数 `N = (filesize - index_offset) / 4`
- 资源数 `= N - 1`
- 所有整数为 **小端序 (little-endian)**
- 第 `i` 个资源头位于 `index[i]`，其数据紧跟头之后
- 第 `i` 个资源数据大小 `= compressed_size`，结束位置即 `index[i+1]`

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

| 魔数 | 类型 | 文档 |
|------|------|------|
| `SPR\0` | 8bit 索引精灵 | [spr.md](spr.md) |
| `SMP\0` | 16bit 高彩位图 | [smp.md](smp.md) |
| `RIFF` | WAV 音频 (PCM) | 标准 RIFF/WAVE |
| `GND\0` | 地图地面数据 | [map.md](map.md) |
| 其它 | 原始数据 / BIG5 文本 | [text.md](text.md) |

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
    uint32_t entry_count;      /* N - 1 */
    const uint32_t *index;     /* N 项, index[i] = 第 i-1 个资源头偏移 */
    const uint8_t  *data;
    size_t         size;
} MkfArchive;
```

## 实际文件统计

| 文件 | 资源数 | 压缩数 |
|------|--------|--------|
| Data.mkf | 601 | 310 |
| Speaking.mkf | 1373 | 0 |
| map.mkf | 297 | 23 |
| Effect.mkf | 114 | 0 |
| Panel.mkf | 112 | 91 |
| help.mkf | 99 | 0 |
| jump.mkf | 70 | 36 |
| **合计** | **2666** | **460** |

## 工具

```bash
python tools/mkf.py info    <file.mkf>          # 头部信息
python tools/mkf.py list    <file.mkf>          # 列出所有子资源
python tools/mkf.py stats   <file.mkf>          # 魔数分布
python tools/mkf.py extract <file.mkf> <dir> [--index N] [--raw]
```
