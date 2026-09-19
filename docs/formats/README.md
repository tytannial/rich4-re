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

| MKF | SPR | SMP | WAV | GND | BIN/文本 | 合计 |
|-----|----:|----:|----:|----:|--------:|-----:|
| Data.mkf | 290 | 9 | 0 | 0 | 303 | 602 |
| Speaking.mkf | 0 | 0 | 1374 | 0 | 0 | 1374 |
| map.mkf | 260 | 21 | 0 | 8 | 9 | 298 |
| Effect.mkf | 0 | 0 | 98 | 0 | 17 | 115 |
| Panel.mkf | 66 | 33 | 0 | 0 | 14 | 113 |
| help.mkf | 0 | 1 | 0 | 0 | 99 (BIG5 文本) | 100 |
| jump.mkf | 36 | 2 | 0 | 0 | 33 | 71 |
| **合计** | **652** | **66** | **1472** | **8** | **475** | **2673** |

## 工具 (`tools/`)

| 工具 | 用途 |
|------|------|
| `mkf.py` | MKF 容器解析（info/list/stats/extract） |
| `lzhuf.py` | LZHUF 解压（含从 exe 提取的静态表） |
| `spr.py` | SPR 解析与 PNG 导出 |
| `smp.py` | SMP 解析与 PNG 导出 |
| `big5.py` | BIG5 ↔ UTF-8 转码与字符串提取 |
| `extract_all.py` | 一键解析/导出所有 MKF + 生成 manifest |

### 快速开始

```bash
# 生成资源索引
python tools/extract_all.py resources/game --out ./assets --manifest docs/formats/resource-manifest.csv

# 导出全部图像与音频
python tools/extract_all.py resources/game --out ./assets --extract
```

## 待办

- [ ] BIG5 点阵字库格式（位置/字模尺寸/索引）
- [ ] GND 格内 2 字节属性语义（`cellIndex` 之外的字段）
- [ ] MAPDAT 各表剩余未标注字段（cellEnt +39 状态、estate/corp 类型枚举）
- [ ] Data.mkf 的 302 个 BIN 资源细分（脚本/数据/调色板）
- [ ] SAVE%d.DAT 存档结构
- [ ] RICH4.CFG 配置与键位表
