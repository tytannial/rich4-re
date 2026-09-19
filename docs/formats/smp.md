# SMP 高彩位图格式

> 16bit 直接色位图集（无调色板）。逆向自 `sub_450069` 与渲染代码。
> 资源魔数 `SMP\0` (0x00504D53)。尽管名为 "SMP"，实际是位图图像，非音频。

## 布局

```
+----------------------------+ 0
| magic : u32 = 'SMP\0'      |
+----------------------------+ 4
| count : u32                |   帧数
+----------------------------+ 8
| data_offset : u32          |   数据区偏移 = 12 + count*12
+----------------------------+ 12
| entry[count] x 12 字节     |
|   u16 width                |
|   u16 height               |
|   u16 x                    |
|   u16 y                    |
|   u32 size                 |   像素字节数 = width*height*2 (16bit)
+----------------------------+ data_offset
| 各 entry 的 16bit 像素      |   按条目顺序连续排列, 总大小 = Σsize
+----------------------------+
```

与 [SPR](spr.md) 的唯一区别：**无 512 字节调色板**，像素为 16bit 直接色。

## 关键点

- 每帧像素大小 = `width * height * 2`
- 16bit 色默认按 **RGB555** 解释（`--rgb565` 可切换）
- 色值 `0x0000` 视为透明
- `sub_450069` 的 SMP 分支基址为 `自身 + data_offset`（SPR 则额外 `+512` 跳过调色板）

## C 结构草稿

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    uint32_t size;          /* width * height * 2 */
} SmpFrame;
#pragma pack(pop)

typedef struct {
    uint32_t  magic;        /* 'SMP\0' */
    uint32_t  count;
    uint32_t  data_offset;
    /* SmpFrame frames[count]; */
    /* uint16_t pixels[];      16bit 直接色, 逐帧连续 */
} SmpFile;
```

## 工具

```bash
python tools/smp.py info    <smp.bin>
python tools/smp.py extract <smp.bin> <outdir> [--rgb565]
```

## 验证

`Data.mkf` index 0（43 帧）导出为 PNG 后为可识别的游戏图标（蓝色球体、红色 X 标记等），
证明 16bit 色解析正确。
