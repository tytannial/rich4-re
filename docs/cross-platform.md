# 跨平台兼容方案（Windows x64 + Linux x86-64）

> 依据: 2026-09-30 全量静态审查（C++ 平台依赖 / Python 工具链 / 构建系统）+ 用户决策
> （docs/m3-plan.md M3-C 展开）。目标：Windows 与 Linux 行为一致、可构建可运行；
> 「窗口分辨率高清文本层」列为后续专项（见 §7）。

## 1. 环境与构建

| 平台 | 工具链 | 依赖 |
|------|--------|------|
| Windows x64 | MSVC (VS 18) + CMake + Ninja | `third_party/SDL3-*`（预编译，本地优先）+ `third_party/freetype`（源码） |
| Linux x86-64 | GCC 16 / Clang 22 + CMake + Ninja | 系统包 `sdl3`、`freetype2`（`find_package` 优先） |

```bash
# Linux（WSL EndeavourOS 验证环境）
sudo pacman -S cmake ninja sdl3 freetype2        # Arch 系
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
./build/linux/rich4 --game resources/MultiverseJourney
```

查找顺序（CMakeLists.txt）：SDL3 = Windows 本地 third_party → `find_package` → FetchContent
（tag `release-3.4.16`，与本地版本对齐）；FreeType = `find_package` → 本地源码 → FetchContent
（`FT_DISABLE_ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI` 纯 C 构建）。

字体文件 `resources/Fonts/*.ttf` **不入库**（`.gitignore`，版权+体积）；缺失时文本渲染
降级为空操作（不阻塞启动，见 §4.1）。

## 2. P0 修复（Linux 下必然失败）

| # | 问题 | 位置 | 修复 |
|---|------|------|------|
| 1 | 文本渲染全部依赖 GDI（`CreateFontW`/`DrawTextW`/DIB），非 Windows 为空桩 → 所有文字消失 | `src/render/text.cpp` | FreeType 统一两平台（§4），删 GDI 实现（366 行） |
| 2 | `big5ToUtf8` 依赖 `MultiByteToWideChar(950)`，非 Windows 返回空串 → 所有资源文本为空 | `src/core/encoding.cpp` | 内嵌 cp950 映射表查表 + 手写 UTF-8 编码（§5） |
| 3 | 资源名大小写不匹配：`Help.mkf`/`Jump.mkf`/`Map.mkf` vs 磁盘 `help.mkf`/`jump.mkf`/`map.mkf`（Linux 大小写敏感 → 地图加载失败） | `help_dialog.cpp` / `new_game.cpp` / `new_game_dialog.cpp` / `game_init.cpp` | `resolveResourcePath(dir,name)`：先按原样，失败遍历目录做 ASCII 大小写不敏感匹配（`src/core/paths.cpp`） |
| 4 | `(3*std::rand())>>15` 在 glibc（RAND_MAX=2³¹−1）下有符号溢出 UB | `shop_dialog.cpp:386` | 等价改写 `%3`（分布一致） |

## 3. P1/P2 修复

| 类别 | 问题 | 修复 |
|------|------|------|
| strict aliasing | 4 处 `*reinterpret_cast<int32_t*>(&float)`（GCC -O2 TBAA 可误编译） | `rich4::floatBits()`（`include/game/core/bit_cast.h`，memcpy 语义） |
| 随机数 | 68 处 `std::rand()`：MSVC 与 glibc 序列不同、`>>`/`%` 概率漂移 | 自带 PRNG 复刻 MSVC LCG（§6） |
| 缺失 include | 依赖 MSVC/libstdc++ 传递包含（clang+libc++ 会失败） | 补齐 `<cstdint>/<cstdio>/<cstdlib>/<cstring>/<algorithm>/<utility>` 等 20+ 处 |
| sort 不稳定 | `map_render.cpp`（绘制列表同 y → 拾取顺序）、`shop_dialog.cpp`（AI 买卡同价） | `std::stable_sort` |
| `long` 尺寸 | `number_input_dialog.cpp` LLP64/LP64 校验差 | `strtoll` + `long long` |
| 日志格式串 | `logMessage` 无格式检查 | GCC/Clang `__attribute__((format(printf,2,3)))` |
| 事件类型截断 | `application.cpp` `pushKey(…, uint8_t type)` 把 `SDL_EVENT_KEY_DOWN/UP`（0x300/0x301）截断为 0/1 → **`--game-key` 合成键盘事件从未生效**（Linux `-Woverflow` 暴露的既有 bug） | 参数改 `SDL_EventType`；已实测合成 Ctrl+Shift+G 触发调试命令 |
| 格式串不匹配 | 2 处 `%zu` 传 `int`（`bank_dialog.cpp`/`bank_stay_dialog.cpp`，x86-64 下参数宽度不匹配 UB） | 改 `%d` |
| sign-compare | 5 处 `int` 与 `static_cast<size_t>` 比较（GCC -Wsign-compare） | 双侧显式 `static_cast<size_t>` |
| 可写路径 | 存档/CFG/日志写游戏目录，Linux 标准安装只读 | `writableDataFile`/`readableDataFile`：游戏目录优先，不可写回退 `SDL_GetPrefPath`（§7） |
| Python 工具 | `run_tests.py` 硬编码 `rich4.exe`；`write_text` 缺 `newline`（CRLF/LF 漂移）；BIG5 输出崩控制台 | exe 按 `sys.platform`；`newline="\n"`；`sys.stdout.reconfigure(utf-8, replace)` |
| CI | Linux job 只 build 单测（主程序从未在 Linux 编译） | build 全部目标 + 补 SDL3 系统依赖 |

## 4. 文本渲染（GDI → FreeType）

### 4.1 设计

- 保持原版 512×200 RGB555 临时缓冲 + `measure` 包围盒扫描（0x44F70C）+ align 1–7 定位
  + 竖排（0x44F7C7）+ 阴影/描边多次偏移 + `#NNNN` 语音前缀 + `blitElementRegion` 色键合成
  —— **327 处 `drawText` / 149 处 `setFont` 调用点零改动**，遮挡/快照/变暗语义 100% 不变。
- FreeType 渲染：字形缓存（键 `face|px|码点`，LRU 65536）→ 8bit coverage 位图
  → RGB555 通道插值混合（`dst += (src−dst)·cov/255`）→ 后续 measure/blit/清零沿用。
- 字体：`resources/Fonts/HarmonyOS_Sans_SC_Regular.ttf`（weight 400）/
  `_Bold.ttf`（700，`style & kTextStyleBold`）；**路径写死，TODO 改配置**。
  覆盖验证：cp950 双字节 13750/13752（仅缺 `╴`/`ˍ`）；`fsType=8` 允许嵌入分发。
- 字号映射：`px = round(size × 1.09)`（HarmonyOS 汉字 bbox ≈ 0.915em，对齐原版
  16px 細明體点阵高度）；`kFontSizeScale` 可调，实机对照用 `--shot` + `tools/frame_align.py`。
- 行高 = FreeType ascender+descender；折行 = `\n` 显式 + 512 宽字符级自动换行
  （等价原版 `DrawTextW DT_CALCRECT` 无 DT_SINGLELINE）。

### 4.2 已知差异（实机对照时关注）

- 字形风格：HarmonyOS Sans（黑体）vs 原版 細明體（明朝体）——用户已确认接受。
- 小字号（12/14/15px）无内嵌点阵、无 TrueType hinting → 依赖 FreeType autohinter，
  观感偏平滑；后续可切 1bpp mono / stem darkening（保留优化位）。
- 多行文本行距按 FreeType 自然度量（比 GDI tmHeight 略大），实机可微调。

## 5. BIG5(cp950) 内嵌映射表

- 生成：`python tools/gen_big5_tables.py` → `src/core/big5_tables.cpp`（13752 项，
  `uint16_t[126×256]`，索引 `(lead−0x81)×256+tail`，生成物勿手改）。
- 解码：ASCII 透传；双字节查表；非法序列 → U+FFFD（等价系统替换行为）；`length` 显式时
  嵌入 0 字节照转（与 `MultiByteToWideChar` 按字节数转换一致）。
- **与 Windows `MultiByteToWideChar(950)` 的已知差异**（全量对比实测）：

  | 类别 | Windows 行为 | 本实现（Python cp950 表） | 影响 |
  |------|--------------|---------------------------|------|
  | 造字区 0x81–0xA0（5968 项） | 映射 PUA U+EEB8..（无字形） | 未映射 → U+FFFD | 观感同为豆腐块；游戏不使用造字区 |
  | 重复字符（假名/西里尔/制表符，249 项） | 映射 PUA U+F6B1..（无字形） | 标准 Unicode（有字形，可显示） | 本实现更优（游戏几乎不用） |
  | 未定义序列（4346 项） | `?`(U+003F) | U+FFFD | 无效序列不出现 |

  结论：保持内嵌表（两平台一致 + 标准 Unicode）；如实机发现具体文本差异，可再切完整复刻表。

## 6. 随机数（跨平台一致）

- `src/core/rng.{h,cpp}`：复刻 MSVC CRT LCG `state = state×214013 + 2531011;
  return (state>>16) & 0x7FFF`（`kRandMax = 32767`）。
- `game_init` 播种接 `rng::seed(dbg::seed() 或 nowMs())`；71 处 `std::rand/srand` 全部收口。
- 收益：`--seed` 两平台序列 **bit 级一致**（`80_autoplay_60d` 两平台同耗时同结果）；
  17 处 `rand()>>N` 概率写法不再漂移；既有 91 场景基线跨平台复用，无需逐点注入改写。

## 7. 可写路径回退

- `writableDataDir(gameDir)`：游戏目录可写（探针文件）→ 用游戏目录（原版便携语义）；
  否则回退 `SDL_GetPrefPath("rich4","rich4")`（Linux `~/.local/share/rich4/`、
  Windows `%APPDATA%/rich4/rich4/`）；进程内缓存。
- `readableDataFile`：先写入目录，再另一处（兼容原版档/旧档）。
- 接入：`SAVE*.DAT`（save_data/load_dialog/save_dialog/new_game/main）、`RICH4.CFG`
  （game_init/settings/hotkey）、`rich4.log`（log）。

## 8. 验证记录（2026-09-30）

| 项 | Windows (MSVC) | Linux (GCC 16.2.1, WSL EndeavourOS) |
|----|----------------|--------------------------------------|
| 构建 | ✅ rich4 + rich4_tests | ✅ **零警告零错误**（首轮暴露 30+ 项：2 真 bug 已修〔`pushKey` 截断/`%zu`〕、其余 sign-compare 已清、format-truncation/class-memaccess 为良性假阳性） |
| L0 单测 | 67/67 PASS | 67/67 PASS（同 checks 数） |
| L1 场景矩阵 | 93 场景 → 92 PASS + 1 TIMEOUT（`84_ai_marathon` >90s wall-cap，按约定跳过） | **完全相同**：92 PASS + 同场景 TIMEOUT |
| 随机序列一致性 | — | `80_autoplay_60d` 两平台同 PASS 同耗时（15.6s） |
| BIG5 单测 | 全表 13752 项逐项校验 + 与 Python cp950 期望值对照 | 同 |
| 文本渲染 | 主菜单/游戏内/帮助界面截图正常（`shots/font_*.png`） | 同 headless 场景断言通过 |
| `--game-key` 修复 | 合成 Ctrl+Shift+G 触发调试命令（修复前 type 截断为 0 完全失效） | 同（事件合成路径平台无关） |

## 9. 后续专项：窗口分辨率高清文本层（未做）

用户需求：文字跟随窗口物理分辨率渲染（1:1 输出，避免被 letterbox 放大模糊）。
调研结论（2026-09-30）：独立文本层会破坏大量遮挡语义，需配套机制：

- **8 处阻塞冲突**：13 处快照回写擦字（FloatMessage `m_bg`/bank_stay `captureStatic`/
  dividend/victory/jail/auction/magic_house…）、9 处 `pressDown`/变暗含文本变换、
  FLC 双 z 序（面板文本在动画下 / `showGodNarration` 在冻结帧上）、
  `saveBmp` 截图链（`tools/frame_align.py` 工作流依赖）。
- **方案骨架**（备查）：主层 640×480 不变 + `TextLayer`（物理分辨率 RGBA8888，FreeType
  物理字号）+ 四类同步钩子：A 不透明写入擦除 / B 变换配对（pressDown/变暗）/ 
  C 快照配对（13 处）/ D FLC 逐像素擦除；光标升为文本层顶部；截图合成后保存。
- 先行条件：本档 §4 的 FreeType 渲染已就绪（字形/度量/对齐验证完成）。

## 10. 交叉引用

- `docs/m3-plan.md` M3-C（本档对应执行）
- `docs/formats/text.md` 字体系统章节（已更新为 FreeType）
- `docs/testing.md`（测试策略；跨平台随机数约束已由 §6 解除）
- 逆向档案：`src/render/text.cpp` 内 `[PORT Win32:MultiByteToWideChar(950)]` 等标注
