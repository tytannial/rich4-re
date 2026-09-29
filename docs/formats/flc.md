# FLC 帧动画格式

> Autodesk **FLC** 动画。逆向自 `sub_450CED`(头解析) / `sub_450F04`(帧与 chunk 解码) /
> `sub_450555`~`sub_450C79`(各 chunk 处理器)。资源魔数 `0xAF12`（位于数据偏移 4）。
> 全库共 **105** 个 FLC 动画（见下文分布）。

## 在加载管线中的位置

`sub_450441`（读 MKF 子资源）对 FLC **不做** `relocateFrames`（那仅处理 `SPR`/`SMP` 魔数），
仅按资源头 `data_offset/data_length` 调 `convertPixelFormat` 转换调色板区。
FLC 的实际解码由独立的动画播放器 `sub_450CED`+`sub_450F04` 完成，逐帧输出到 16bit 后台表面。

## 文件头（128 字节，标准 FLIC header，小端）

```
+----------------------------+ 0
| size   : u32               |   解压后动画总字节数
+----------------------------+ 4
| magic  : u16 = 0xAF12      |   FLC 魔数（sub_450CED 校验；不认 0xAF11/FLI）
+----------------------------+ 6
| nFrames: u16               |   帧数   → dword_48C86C
+----------------------------+ 8
| width  : u16               |   宽     → dword_48C878
+----------------------------+ 10
| height : u16               |   高     → dword_48C87C
+----------------------------+ 12
| depth  : u16 (=8)          |
+----------------------------+ 14
| flags  : u16               |
+----------------------------+ 16
| speed  : u32               |   帧间隔 → dword_48C870（可被播放参数覆盖，见下）
+----------------------------+ 20
| ... 保留 ...               |
+----------------------------+ 128   帧数据从这里开始
```

> `sub_450CED` 只读取 `+4/+6/+8/+10/+16`。播放参数 `a4` 的 bit4-7 若置位，则
> `speed = 10 * ((a4>>4)&0xF)` 毫秒覆盖头中的 `speed`。

## 帧结构

帧数据从偏移 128 起，逐帧以 `[u32 frameSize]` 串联。`sub_450F04` 的帧定位循环：

```c
do {
    v3 = cur; cur += *(u32*)v3;          // 前进一帧
} while (*(u16*)(v3 + 4) != 0xF1FA);     // 跳过非 FLC 帧
```

即**首帧若为 `0xF100`（FLI 占位/信息帧）会被跳过**，解码从首个 `0xF1FA` 帧开始。
（抽查 `Data.mkf[554]` 等确认：frame0 magic=`0xF100`，frame1 起为 `0xF1FA`。）

帧头（16 字节）：

```
+0  u32 frameSize        本帧总字节数
+4  u16 magic = 0xF1FA   FLC 帧魔数
+6  u16 nChunks          子 chunk 数
+8  u16 maxExtra
+10 u16 reserved
+12 u32 nextFrame (=0)
+16 子 chunk 列表
```

子 chunk 头（6 字节）：`+0 u32 chunkSize`(含头) / `+4 u16 type`，payload 长 `chunkSize-6`。

## chunk 类型（`sub_450F04` 分派表）

原版**只处理下列 5 种**，其余 type（如本库出现的 `0x0012`）直接忽略。
所有像素为 8bit 索引，经调色板 `word_48C630` 展开为 RGB555 写入表面（stride 1280 = 640px×2）。

| type | 名称 | 处理器 | 格式 |
|------|------|--------|------|
| `0x0004` | 调色板 delta | `sub_450555` | `u16 nPairs`，随后每对 `u8 skip, u8 count`(0→256)，再 \`count × 3\` 字节 8bit RGB；索引累加 |
| `0x0007` | 全帧 BRUN | `sub_45059A` | `u16 nRows`；行首 `u16` 若高 2 位=`0xC000` 表示跳行；否则为该包数。每包 `u8 skipPx, u8 cmd`：`cmd>=0x80`→run(`-cmd` 个，随后 1 索引)；`cmd<0x80`→literal(`cmd` 个索引) |
| `0x000C` | 局部 BRUN | `sub_450894` | `u16 firstRow, u16 nRows`；每行 `u8 nPackets`，包同 `0x0007`（仅覆盖指定行） |
| `0x000F` | 全帧 RLE | `sub_450A9D` | 遍历 `height` 行；每行 **1 前导字节**；`while x<width`：`u8 cmd`：`cmd<=0x80`→run(`cmd` 个，随后 1 索引)；`cmd>0x80`→literal(`256-cmd` 个索引) |
| `0x0010` | 全帧字面 | `sub_450C35` | `width × height` 字节 8bit 索引，逐行 |

> 注意 `0x0007` 与 `0x000F` 的 run/literal 判定符号相反：`0x0007` 用 `cmd>=0x80` 作 run，
> `0x000F` 用 `cmd<=0x80` 作 run（各自见对应反编译）。

### 缩放路径

当播放标志 `byte_48C882` 置位（缩放到离屏），改用 `sub_4506C7`(0x07)/`sub_45096A`(0x0C)/
`sub_450B3A`(0x0F)/`sub_450C79`(0x10) 一组处理器，语义相同，但目标为 `sub_450CED` 分配的
`0x5E880`(=387200=440×440×2) 离屏缓冲，再缩放 blit 到主表面。用于把非全屏动画贴到指定区域。

## 调色板与像素格式

- 调色板 256 项，每项 **3 字节 8bit RGB 直接值**（实测 `0x04` chunk 字节范围 0–255，
  **并非** FLIC 标准的 0–63 6bit，勿做 `*4`/`<<2` 扩展）。`sub_45520D` 按当前显示格式把
  8bit RGB 打包为 16bit。
- 输出像素为 **RGB555**（与 `dword_48A08C` 后台表面格式一致）。
- FLC 为**全帧不透明**动画，逐帧累积（delta 帧保留上一帧像素），无透明色概念。

## 资源分布（105 个 FLC）

| MKF | index | 数量 | 典型尺寸 | 用途 |
|-----|-------|-----:|---------|------|
| Data.mkf | 416–439 | 24 | 100×100 等 | 卡片/小动画 |
| Data.mkf | 523–570 | 48 | 440×440 | 结局/过场长动画 |
| jump.mkf | 46–70 | 25 | 640×480 | 跳伞小游戏动画 |
| Panel.mkf | 4,5,6,14,16,17,20,78 | 8 | 640×480 | 面板动画 |

## 播放入口与使用场景

FLC 经 `sub_450CED`（初始化，校验 `0xAF12`）装载、`sub_450F04`（逐帧解码）播放，
帧循环封装为 `sub_45144F`。按 `sub_450CED` / `sub_45144F` 的调用入口归类的使用场景：

| 播放入口 | 触发者 | 场景 | 典型资源 |
|----------|--------|------|----------|
| `sub_40D7C4` | `_WinMain` | 开场/标题动画 | — |
| `playIntro` (`0x415872`) | 启动流程 | 片头 | — |
| `sub_43010C`（模态窗口，计时器逐帧 + `drawText`） | 游戏流程 | 结局/过场长动画 | Data 523–570 (440×440) |
| `sub_40EAD7` | `onPlayerActionPhase` | 回合事件卡动画 | Data 416–439 (100×100) |
| `sub_413248` / `sub_414FCD`（带 timer 的动画子窗口） | 菜单/面板 | 面板插播动画 | Panel 4–78 |
| `defeatFlow` (`0x407842`) | 破产/失败 | 失败动画 | — |
| 跳伞小游戏 | 小游戏 | 人物动画层（叠加 `jump[0-7]` 照片背景） | jump 46–70 (640×480) |

> 逐 index ↔ 具体事件/关卡的精确对应需追各调用点读取的 MKF index（多为运行时变量
> `dword_48C3xx` 等），列为待办。

## 工具

`tools/flc.py` 提供 `info` / `extract`（导出帧序列 PNG）。`tools/extract_all.py` 的
`classify()` 已识别 FLC（`struct.unpack_from("<H", blob, 4)[0] == 0xAF12`）与 RAW 位图，
`--extract` 时自动导出 FLC 帧（`--flc-frames N`，`0`=全部）与 RAW PNG，并在 manifest 的
`note` 列标注 `帧数 宽x高`。

```bash
python tools/flc.py info    <flc.bin>
python tools/flc.py extract <flc.bin> <outdir> [--all | --frames N]
python tools/extract_all.py resources/MultiverseJourney --out ./assets --extract --flc-frames 8
```

## 验证（抽查解码成图）

按本文档语义实现解码器，多个动画逐帧解码为**清晰可辨识**画面，证明文件头字段、
帧链（`0xF100` 占位帧跳过）、chunk 链、调色板与像素格式全部正确：

- `Panel.mkf[20]`（25 帧 640×480）：1998/MAGIC 魔法阵、红发占卜师双手扶水晶球、
  周围塔罗牌/床/存钱罐/火箭/挖掘机等图标、四角天使像。
- `Data.mkf[416]`（10 帧 100×100）：戴棕帽、持左轮的牛仔角色。
- `jump.mkf[59]`（37 帧 640×480）：跳伞人物动画层（叠加在 `jump[0]` 照片背景上）。

> 关键教训：调色板**必须按 8bit RGB 直接值**解释。若误当 FLIC 标准 6bit 做 `&0x3F`/`*4`，
> 会导致全画面颜色错乱（花屏），而 `0x0F`/`0x07` 的 RLE 本身字节与像素数完全吻合。

## C 结构草稿

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t size;          /* 动画总字节数 */
    uint16_t magic;         /* 0xAF12 */
    uint16_t n_frames;
    uint16_t width;
    uint16_t height;
    uint16_t depth;         /* 8 */
    uint16_t flags;
    uint32_t speed;         /* 帧间隔 */
    /* ... 保留至 128B ... */
} FlicHeader;

typedef struct {            /* 帧头 */
    uint32_t frame_size;
    uint16_t magic;         /* 0xF1FA（首帧可能为 0xF100 占位，被跳过） */
    uint16_t n_chunks;
    uint16_t max_extra;
    uint16_t reserved;
    uint32_t next_frame;
} FlicFrameHeader;

typedef struct {            /* 子 chunk 头 */
    uint32_t chunk_size;    /* 含本头 6 字节 */
    uint16_t type;          /* 0x0004/0x0007/0x000C/0x000F/0x0010 */
} FlicChunkHeader;
#pragma pack(pop)
```
