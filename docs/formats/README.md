# 《大富翁4》资源格式规范

本目录记录从 `rich4.exe` 逆向并经验证的全部资源格式，作为现代引擎重写（C++/SDL3）
的数据层依据。

## 已解析格式

| 文档 | 格式 | 状态 | 验证方式 |
|------|------|------|----------|
| [mkf.md](mkf.md) | MKF 资源容器 | ✅ 已验证 | 7 个文件全部解析，索引/偏移自洽 |
| [lzhuf.md](lzhuf.md) | LZHUF 压缩 | ✅ 已验证 | 460 个压缩资源长度精确匹配 |
| [spr.md](spr.md) | SPR 8bit 精灵 | ✅ 已验证 | 导出 PNG 为可识别角色 |
| [smp.md](smp.md) | SMP 16bit 位图 | ✅ 已验证 | 导出 PNG 为可识别图标 |
| [flc.md](flc.md) | FLC 帧动画 (0xAF12) | ✅ 已验证 | 抽查解码 Panel[20] 首帧为可识别画面 |
| [raw_bitmap.md](raw_bitmap.md) | RAW 无头 16bit 位图 | ✅ 已验证 | 抽查解码 200×200/388×251/640×480 均为可识别图 |
| [text.md](text.md) | BIG5 文本 | ✅ 文本已验证 / 字库待逆向 | cp950 解码正确 |
| [map.md](map.md) | GND 地图数据 | ✅ 已验证 | 8 个 GND 结构自洽（72x72 格 x 1026B） |
| [mapdat.md](mapdat.md) | MAPDAT 地图物件表 | ✅ 已验证 | 8 个资源 `evtCellOff+28*count+28==filesize` |
| [save.md](save.md) | SAVE%d.DAT 存档 | ⚠️ 头部/玩家表已解析 | 版本校验 + 界面加载验证 |
| [cfg.md](cfg.md) | RICH4.CFG 配置 | ✅ 已验证 | 键位表与 `fn` 钩子逐项对应 |

## 资源清单

- [`resource-manifest.csv`](resource-manifest.csv) — 全部 **2673** 个子资源
  （mkf, index, type, size, note）

> 注：MKF 索引表每一项都是资源头偏移（含最后一项），资源数 = 索引项数。
> 见 [mkf.md](mkf.md)。

### 类型分布

> 原 `BIN` 类（无 `SPR/SMP/GND/RIFF` 魔数者）已按实测细分为 **FLC**（数据偏移 4 处
> `0xAF12`）、**RAW 图**（无头 16bit 位图，非零）、**其它**（BIG5 文本 / 结构·数据表 /
> 全零占位 / 空资源）。见 [flc.md](flc.md)、[raw_bitmap.md](raw_bitmap.md)。

| MKF | SPR | SMP | WAV | GND | FLC | RAW图 | 其它 | 合计 |
|-----|----:|----:|----:|----:|----:|------:|-----:|-----:|
| Data.mkf | 290 | 9 | 0 | 0 | 72 | 199 | 32 | 602 |
| Speaking.mkf | 0 | 0 | 1374 | 0 | 0 | 0 | 0 | 1374 |
| map.mkf | 261 | 21 | 0 | 8 | 0 | 0 | 8 | 298 |
| Effect.mkf | 0 | 0 | 99 | 0 | 0 | 0 | 16 | 115 |
| Panel.mkf | 66 | 34 | 0 | 0 | 8 | 1 | 4 | 113 |
| help.mkf | 0 | 1 | 0 | 0 | 0 | 0 | 99 | 100 |
| jump.mkf | 36 | 2 | 0 | 0 | 25 | 7 | 1 | 71 |
| **合计** | **653** | **67** | **1473** | **8** | **105** | **207** | **160** | **2673** |

> 数字由 `tools/extract_all.py` 按魔数/结构自动分类实测（含 `resource-manifest.csv`）。

- **FLC 105**：帧动画（Data 416–439/523–570、jump 46–70、Panel 4/5/6/14/16/17/20/78）。
- **RAW图 207**：无头 16bit RGB555 位图（Data 4–127=200×200 卡、441–516=388×251 插画、
  jump 0–7=640×480 背景、Panel 92=640×480 背景）。
- **其它 160**：help 99 BIG5 文本 + 35 全零占位(PAD) + Effect 16 空资源 + map 8 结构表 +
  Panel 2 数据表。

## 工具 (`tools/`)

| 工具 | 用途 |
|------|------|
| `mkf.py` | MKF 容器解析（info/list/stats/extract） |
| `lzhuf.py` | LZHUF 解压（含从 exe 提取的静态表） |
| `spr.py` | SPR 解析与 PNG 导出 |
| `smp.py` | SMP 解析与 PNG 导出 |
| `flc.py` | FLC 帧动画解析与 PNG 帧序列导出 |
| `big5.py` | BIG5 ↔ UTF-8 转码与字符串提取 |
| `extract_all.py` | 一键解析/导出所有 MKF + 生成 manifest |

### 快速开始

```bash
# 生成资源索引
python tools/extract_all.py resources/MultiverseJourney --out ./assets --manifest docs/formats/resource-manifest.csv

# 导出全部图像与音频（含 FLC 帧动画、RAW 位图）
python tools/extract_all.py resources/MultiverseJourney --out ./assets --extract --flc-frames 8

# 单独解析/导出某个 FLC
python tools/mkf.py extract resources/MultiverseJourney/Panel.mkf ./out --index 20
python tools/flc.py info    ./out/0020.bin
python tools/flc.py extract ./out/0020.bin ./out/panel20 --all
```

## 待办

- [ ] BIG5 点阵字库格式（位置/字模尺寸/索引）
- [ ] GND 格内 2 字节属性语义（`cellIndex` 之外的字段）
- [ ] MAPDAT 各表剩余未标注字段（cellEnt +39 状态、estate/corp 类型枚举）
- [x] Data.mkf BIN 资源细分：FLC 动画、RAW 位图、全零占位已分类（见 flc.md/raw_bitmap.md）
- [ ] RAW 位图各族 index ↔ 具体事件/关卡/界面 的逐项对应（需追 `sub_450441` 调用点 index 常量）
- [ ] SAVE%d.DAT 存档结构
- [ ] RICH4.CFG 配置与键位表
