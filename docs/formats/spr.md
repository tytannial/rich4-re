# SPR 精灵格式

> 8bit 调色板索引精灵集。逆向自 `sub_450069`(指针重定位) 与渲染代码。
> 资源魔数 `SPR\0` (0x00525053)。

## 布局

```
+----------------------------+ 0
| magic : u32 = 'SPR\0'      |
+----------------------------+ 4
| count : u32                |   帧数
+----------------------------+ 8
| data_offset : u32          |   数据区偏移 = 12 + count*12
+----------------------------+ 12
| entry[count] x 12 字节     |
|   u16 width                |
|   u16 height               |
|   u16 x                    |   屏幕放置坐标
|   u16 y                    |
|   u32 size                 |   像素字节数 = width*height (8bit)
+----------------------------+ data_offset
| palette : 256 x u16 (512B) |   调色板, 默认 RGB555
+----------------------------+ data_offset + 512
| 各 entry 的 8bit 索引像素   |   按条目顺序连续排列, 总大小 = Σsize
+----------------------------+
```

## 关键点

- 每帧像素大小恒为 `width * height`（8bit 索引）
- 像素索引 `0` 为透明色
- 调色板 16bit 色：默认按 **RGB555** 解释（`r=(v>>10)&0x1F, g=(v>>5)&0x1F, b=v&0x1F`）
- 运行时 `relocateFrames`(`sub_450069`) 把每帧 entry 的 `size` 字段**就地改写为像素绝对指针**：
  帧 0 = `基址 + data_offset + 512`（跳过调色板），帧 i = `帧(i-1)指针 + 帧(i-1) size`。
  故像素在内存中逐帧连续排布，SMP 同理但帧 0 = `基址 + data_offset`（无调色板）。

## C 结构草稿

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    uint32_t size;          /* width * height */
} SprFrame;
#pragma pack(pop)

typedef struct {
    uint32_t  magic;        /* 'SPR\0' */
    uint32_t  count;
    uint32_t  data_offset;
    /* SprFrame frames[count]; */
    /* uint16_t  palette[256]; */
    /* uint8_t   pixels[];     8bit 索引, 逐帧连续 */
} SprFile;
```

## 工具

```bash
python tools/spr.py info    <spr.bin>
python tools/spr.py extract <spr.bin> <outdir> [--rgb565]
```

## 验证

`Data.mkf` index 128（8 帧）导出为 PNG 后为可识别的角色精灵（帽子、粉色服装），
证明调色板与像素布局解析正确。
