# 大富翁4 逆向重建（Rich4 Recomp）

《大富翁4》（1999 年，i386 PE，DirectDraw/DirectSound）的引擎逆向重建工程。
原版已无法在现代系统上良好运行且无法跨平台，本项目通过逆向分析原版 `rich4.exe`，
以 **C++17 + SDL3** 重写引擎，实现 **Windows / Linux 跨平台**运行，并加入宽屏、
分辨率无关缩放、设置界面与快捷键等现代化体验。

> 仅用于学习与研究。本项目**不包含**原版游戏程序、图像、音频等任何受版权保护的资源，
> 运行需要自备合法取得的原版游戏文件。

## 特性

**核心玩法**

- 完整回合循环：掷骰动画 → 移动 → 过天；玩家 / AI / 托管自动行动
- 住宅用地买地、升级、他人收费（同路段联合租金 / 连锁 / 查封翻倍 / 同盟分账）
- 商業用地（建设施、升级、收费、旅館住宿、连锁店）、行業設施點、地标与事件格
- 股市（行情 / 认购 / 买卖 / 走势图 / 休市）、银行（柜员机 / 停留 / 週轉 / 催收）、
  月初结息与 15 号分红、交易市场「公佈欄」
- 30 种卡片 + 13 种道具 + 神明（附身 / 显灵 / 持续效果）+ 事件槽 NPC 与四大恶人
- 事件格 case 2..16 全部实现：新闻 / 命运 / 魔法屋 / 拍卖 / 樂透 / 迷你游戏 /
  百货公司 / 监狱 / 医院 / 银行 / 得點券 / 卡片格等
- 存档 / 读档 / 自动存档 / 时光机快照，1:1 原版 `SAVE*.DAT` 字段格式

**现代化**

- 宽屏地图区（右栏贴边）、`native / wide / free` 显示预设、任意 `--scale` 缩放
- FreeType 统一文本渲染（两平台一致）、`rich4.ini` 配置文件
- 调试模式：Ctrl+1..9 构造 / 诊断热键（`--debug` 启用，键位见 `docs/debug-keys.md`）

## 构建

### 依赖

- CMake ≥ 3.24 + Ninja，支持 C++17 的编译器（MSVC / GCC / Clang）
- [SDL3](https://github.com/libsdl-org/SDL/releases)（≥ 3.4，Windows 预编译包放
  `third_party/SDL3-*/`；缺失时 CMake 自动 FetchContent 拉取源码构建）
- FreeType（文本渲染；系统包 / `third_party/freetype` / FetchContent 自动回退）
- 字体文件 `resources/Fonts/HarmonyOS_Sans_SC_{Regular,Bold}.ttf`
  （不入库，需自行准备；缺失时文本渲染降级为空操作，不影响逻辑）

### Windows（MSVC）

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> 注意：Strawberry Perl 自带的 cmake/ninja 在本机可能损坏（缺 DLL），
> 请使用 VS 自带或独立安装的 CMake/Ninja。

### Linux

```bash
# Arch: sudo pacman -S cmake ninja sdl3 freetype2
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
```

## 运行

将合法取得的原版游戏文件放入任意目录（如 `resources/MultiverseJourney/`，
需包含 `Data.mkf`、`Panel.mkf`、`Speaking.mkf`、`rich4.exe` 等），然后：

```powershell
# Windows
./build/rich4.exe --game resources/MultiverseJourney
```

```bash
# Linux
./build/linux/rich4 --game resources/MultiverseJourney
```

常用参数：

| 参数 | 说明 |
|------|------|
| `--game <dir>` | 原版资源目录 |
| `--preset native\|wide\|free` | 显示预设（宽屏 / 自由缩放窗口） |
| `--scale <f>` / `--canvas <WxH>` | 自定义缩放与逻辑画布尺寸 |
| `--stats` | 输出帧率 / 帧耗时诊断 |
| `--debug` | 启用调试热键 |
| `--set section.key=value` | 覆盖 `rich4.ini` 配置项 |
| `--headless --seed <n> --quickstart <map[,players[,humans]]>` | 无头测试开局 |
| `--script <file>` / `--exec "<cmd; cmd>"` | 执行测试脚本 / 命令 |
| `--save-selftest <slot>` | 存档 save∘parse∘save 往返校验 |

## 测试

自动化测试为「状态 / 演出 Trace / 字节-数据」三支柱，画面观感以人工验收为准
（总纲见 `docs/testing.md`）。

```powershell
# L0 单元测试（收租公式 / 存档往返 / MKF-LZHUF 等，无资源自动 SKIP）
ctest --test-dir build -R unit --output-on-failure

# L0 + L1 全量：单元测试 + 全场景矩阵（需要原版资源）
python tools/run_tests.py --matrix
```

L1 场景位于 `tests/scenarios/`（99 个 headless 场景，覆盖收租 / 购地 / 卡牌 /
道具 / 事件格 / 存档等），支持 `--filter` 过滤与 `--repeat` 稳定性重跑。
CI（GitHub Actions）在无资源环境下跑两平台单元测试。

## 项目结构

| 路径 | 说明 |
|------|------|
| `src/core/` | 游戏状态、回合、经济、存档、配置 |
| `src/app/` | 对话框（`dialogs/`）、AI（`ai/`）、UI 辅助（`ui/`）、游戏流程（`game/`） |
| `src/render/` | 画面合成、文本、精灵 / FLC 解码 |
| `src/resource/` | MKF / LZHUF / SPR / SMP 等格式读取 |
| `src/platform/` | SDL3 平台层（窗口 / 输入 / 音频 / 光标） |
| `src/debug/` | 调试命令注册表与测试钩子 |
| `src/gen/` | 由工具生成的查表数据（勿手改） |
| `include/game/` | C++ 头文件 |
| `tools/` | Python 逆向与资源解析 / 测试编排工具 |
| `docs/` | 逆向档案、格式规范、开发计划 |
| `tests/` | L0 单元测试（`unit/`）与 L1 场景（`scenarios/`） |

## 文档

- `docs/help-checklist.md` — 游戏内帮助 99 条 → 功能实现状态（机制权威索引）
- `docs/cross-platform.md` — 跨平台方案与验证记录
- `docs/testing.md` — 测试体系总纲
- `docs/formats/README.md` — 资源格式总览与解析工具
- `docs/reverse/functions/` — 逐功能逆向档案
- `docs/reverse/ui-controls.md` — UI 控件指南
- `docs/m2-plan.md` / `docs/m3-plan.md` / `docs/m4-plan.md` — 里程碑计划与结项记录

开发约定与逆向标注规范见 `AGENTS.md` 与 `docs/reverse/README.md`。

## 免责声明

- 本项目为个人学习与研究性质的兼容性重实现，与原版权方无任何关联。
- 仓库内**不包含**原版游戏程序、图像、音频、字体等受版权保护的资源；
  运行所需资源请自行通过合法途径取得（如购买 Steam 版《大富翁4》）。
- 《大富翁4》及相关素材的版权归原权利人所有。如版权方对本项目有异议，
  请通过 Issue 联系，我们将配合处理。

## 许可

本项目源代码以 [MIT 许可证](LICENSE) 发布。
原版游戏资源不在许可范围内，其版权归原权利人所有。
