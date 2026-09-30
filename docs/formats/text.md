# 文本与字库格式 (BIG5)

> 游戏内码为 **BIG5 (cp950, 繁体中文)**。所有文本源（exe 字符串、MKF 文本、存档、配置）
> 均为 BIG5 双字节编码。重建时统一以 **UTF-8** 为内部编码，加载期由 cp950 转换。

## 文本资源

### help.mkf 帮助文本

`help.mkf` 中的非图像资源（98 个 BIN）为纯 BIG5 文本，以 `0x00` 作为**换行/分段标记**，
每行约 14 个中文字符（固定排版宽度）：

```
本遊戲的操作方法非常\0簡單，只要操作滑鼠游\0標移動，以及確定、取\0消鍵即可進行。\0
```

- 无长度前缀，`0x00` 即行分隔
- 解码：`bytes.decode('cp950')`，按 `\0` 分行
- 其余 MKF 中的 BIN 资源可能是脚本/数据，需按上下文判断

### exe 内嵌字符串

`rich4.exe` 的 DGROUP 段（文件偏移 `0x61600` 起）存放 BIG5 字符串常量，
如 `SAVE%d.DAT`、`RICH4.CFG`、`data.mkf` 等文件名与提示文本。

## 字体系统

游戏**不使用内嵌点阵字库**，而是通过 GDI 系统字体渲染（原版，Windows 独占）：

- `sub_44F9D8`（设置字体）：
  `CreateFontA(-size, 0,0,0, weight, 0,0,0, 0x88, 0,0,0,0, &pszFaceName)`
  - `charset = 0x88 = 136 = CHINESEBIG5_CHARSET`
  - 字体名 `pszFaceName`(`0x4660A0`) = BIG5 **"細明體"** (MingLiU)
  - 粗体时 `weight=700`，否则 `400`；字号由参数传入（菜单处为 16）
- `sub_44F7C7`（输出文本）：逐字符 `TextOutA`
  - 首字节 ≥ 0x80（BIG5 双字节）时一次输出 2 字节
  - 文本按行高逐字**竖排**（`y += line_height`）

### 重写实现（跨平台，2026-09-30）

GDI 为 Windows 独占 → 统一改为 **FreeType**（`src/render/text.cpp`，
本地 `third_party/freetype` 或系统 `freetype2`，见 `docs/cross-platform.md` §4）：

- 字体：`resources/Fonts/HarmonyOS_Sans_SC_{Regular,Bold}.ttf`（不入库；缺失时文本降级为空操作）
- 保持原版 512×200 RGB555 临时缓冲 + `measure` 包围盒 + align 1–7 + 阴影/描边 + 竖排语义
  —— 调用点零改动、遮挡/快照语义不变
- 字号映射 `px = round(size × 1.09)`（HarmonyOS 汉字 bbox ≈ 0.915em，对齐原版点阵高度）
- 已知差异：字形风格（黑体 vs 明朝体）、小字号无点阵 hinting（见 cross-platform.md §4.2）

## 转码工具

```bash
python tools/big5.py decode  <file>              # 按 BIG5 解码为 UTF-8 输出
python tools/big5.py strings <file> [--min 4]    # 提取 BIG5/ASCII 字符串
```
