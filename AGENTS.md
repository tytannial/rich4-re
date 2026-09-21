# AGENTS.md

## 项目简介

《大富翁4》(Rich4) 逆向重建工程。原始 `rich4.exe`（1999 年，i386 PE，DirectDraw/DirectSound）
已无法在现代系统运行且无法跨平台。本项目通过逆向分析，用 **C++17 + SDL3** 重写引擎，
目标是 Windows/Linux 跨平台运行，并支持宽屏、快捷键等现代化操作。

游戏内码为 **BIG5（繁体）**；引擎内部统一使用 UTF-8。

## 当前进度

**M1（基本交互）已完成**：主菜单 → 选人/选地图 → 开局（跳伞入场，期间锁定交互）→
回合循环（掷骰三阶段：角色扔骰/骰子 FLC/点数停留 → 移动 → 过天）→
工具条（帮助/设置/大地图/读档/存档/托管AI）→ 右侧面板（玩家信息/日历/小地图/前进面板）→
地图物件（神明事件点/地标/地产/公司/特殊点/事件格）与**左键按住查看物件信息**。
非玩家控制期（AI 回合/掷骰/移动/结算）锁定交互（`g_playerControl`）。

**M2 待办**（详见 `docs/reverse/functions/` 各档案「差异」段）：

- 落地事件分发：`onPlayerActionPhase`（0x41B42D）格子类型 11..18 收租/公司分红/神明等
- 经济系统：买卖地/银行/贷款/股票行情（`sub_4291D6` 等）、卡片与道具使用
- AI 决策：`alive & 6`（电脑/托管）自动行动（`sub_41982D` 回合主流程）
- 存档完整恢复：`loadGameFromSlot` 目前仅恢复地图/模式/玩家数
- 事件槽 NPC（玩家 4-7：流氓/间谍/小偷等）

## 目录结构

| 路径 | 说明 |
|------|------|
| `resources/game/` | 原始游戏文件（exe + mkf + dat + cfg），**只读，不提交** |
| `docs/formats/` | 资源格式规范（已完整解析，见 `README.md`） |
| `docs/reverse/` | 逆向规范与函数档案（注释格式、地址映射表、逐函数笔记） |
| `tools/` | Python 逆向与资源解析工具 |
| `include/game/` | C++ 头文件 |
| `src/` | C++ 源码 |
| `build/` | 构建输出（已忽略） |

## 构建

工具链：**MSVC (VS 18 Community) + CMake + Ninja**。

### 准备 SDL3

从 https://github.com/libsdl-org/SDL/releases 下载 `SDL3-devel-<ver>-VC.zip`，
解压到 `third_party/`（如 `third_party/SDL3-3.4.16/`）。CMake 会自动查找该目录。
若未提供，则回退到 FetchContent 从源码构建（较慢）。

### 编译

```powershell
# 加载 MSVC 环境
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> 注意：Strawberry Perl 自带的 cmake/ninja 在本机已损坏（缺 DLL），
> 请使用 VS 自带或独立安装的 CMake/Ninja。

## 运行

```powershell
./build/rich4.exe --game resources/game
```

## 测试

当前骨架阶段以手动运行为主。后续加入单元测试（格式解析往返）后在此补充命令。

## Python 工具

```bash
# 一键解包全部资源 + 生成索引
python tools/extract_all.py resources/game --out ./assets --manifest docs/formats/resource-manifest.csv

python tools/mkf.py list resources/game/Data.mkf
python tools/spr.py extract <spr.bin> <outdir> [--rgb565]
python tools/smp.py extract <smp.bin> <outdir> [--rgb565]

# 逆向标注校验 + 生成地址映射表
python tools/re_map.py
```

## 代码约定

- **C++17**，根命名空间 `rich4`
- 文件/目录 `snake_case`；类型 `PascalCase`；函数 `camelCase`；私有成员 `m_`
- 头文件用 `#pragma once`
- 平台层不抛异常，错误以 `bool` / `std::optional` 返回
- 内部字符串 UTF-8；BIG5 仅在资源加载期转换
- **不要添加无关注释**；格式相关说明可引用 `docs/formats/*.md`
- **逆向标注强制**：每个重写函数/全局都要标注原始地址与依据
  （`// [RE 0xXXXXXX] 原符号名` + `依据:` 行；平台迁移用 `[PORT ...]` + `替换依据:`），
  格式见 `docs/reverse/README.md`，提交前跑 `python tools/re_map.py` 校验

## 重要参考

- `docs/formats/README.md` — 资源格式总览与工具索引
- `docs/formats/lzhuf.md` — 压缩算法（含易错点说明）
- `docs/reverse/README.md` — 逆向注释规范与工作流
- `docs/reverse/address-map.md` — 地址↔重写符号映射表（自动生成）
- `include/game/formats.h` — 全部格式的 C 结构定义

## 工作经验（踩坑与约定）

### 逆向方法

- **不猜测，查 IDA**：音效索引、帧号、结构偏移、表内容一律先用 IDA 验证
  （`get_bytes` 读原始表、`decompile` 看调用链与常量），再写代码。
  凭猜测写 id/帧号是「音效对不上 / 贴图错帧」类问题的首要原因。
- **帧头公式**：精灵/元素帧头 = `资源基址 + 12 + 12*帧号`
  （如 `+84`=帧6、`+72`=帧5、`+24`=帧1）；结构数组内偏移如
  `&dword_48231A + 8*控件 + 8`。误读会画错帧或错位。
- **时序精确到调用点**：同一功能在不同阶段表现不同（例：掷骰——扔骰动画无音效 →
  骰子 FLC 第30帧与结束各播一次落地槽 → 转移动才播载具槽）；必须看反编译中
  调用发生的**具体位置**，不能想当然。
- **区分资源格式**：`UiImage.load` 仅支持 SPR/SMP；RAW 无头位图用
  `decodeRawBitmap`（按字节数推断尺寸）；FLC 用 `FliDecoder`。
- **单 buffer 语义**：原版每个音效槽是单 DirectSound buffer
  （`sub_4542E9` 停 + `sub_4542CE` 播）→ 重写用同槽替换，避免多声部叠加。

### 常见坑

- **FLC 解码越界**：部分 FLC 数据尾部含超出声明帧数的 chunk，必须按
  `m_frameCount` 封顶，否则会多播一帧（回绕成第0帧，表现为「闪回首帧」）。
- **`rewind()` 只重置偏移不清像素**；播完应冻结末帧（不回绕）。
- **视口**：`manualView=true` 且 `viewSmooth=0` → 场景跳左上角，并触发重选出生点/跳伞；
  小地图旋转只改 `mapRotation`，**不要**切视角模式。
- **事件槽（玩家 4-7）**：无激活 NPC 时不能进入（否则 `currentPlayer` 指向无数据槽）。
- **音乐**：取消对话框**不改动**音乐；確定仅在音量 0↔非0 时停/重播（非0 之间只调音量）。
- **重新开局**：`newGameInit` 需重置 `mapRotation`/骰子/回合/音乐计时器等残留状态。
- **音效 id 空间**：UI 音效（`g_uiSoundHover/Click/Confirm/Cancel` = Effect.mkf 0/1/2/4）
  与游戏音效槽（`g_effectSlots[slot]`）是两套，勿混用。

### 工作流

- 每项修正单独 `wip:` 提交；用户实机验证后再推进；**不要**把 `wip:` 当里程碑。
- 实机问题优先加**诊断日志**（限帧/状态变化）定位，再改代码，避免盲改。
- 编译前确保游戏已退出（否则 `LNK1104` 文件占用）；`build.cmd` 不提交。
- 逆向成果需**双落**：源码注释 + `docs/reverse/functions/` 档案 + IDB 重命名/注释
  （IDB 修改后保存）；改动后跑 `python tools/re_map.py` 校验。

## 注意事项

- `resources/game/` 下的文件**不要修改或提交**（版权 + 体积）
- `resources/game/rich4.exe.i64` 是权威逆向数据库（重命名/注释成果），**修改后需保存**
- `src/resource/lzhuf_tables.cpp` 由 `tools/gen_lzhuf_tables.py` 从 exe 生成，**勿手改**
- LZHUF 解压器每次解压需重置工作区（自适应树有状态）
