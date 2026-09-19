# AGENTS.md

## 项目简介

《大富翁4》(Rich4) 逆向重建工程。原始 `rich4.exe`（1999 年，i386 PE，DirectDraw/DirectSound）
已无法在现代系统运行且无法跨平台。本项目通过逆向分析，用 **C++17 + SDL3** 重写引擎，
目标是 Windows/Linux 跨平台运行，并支持宽屏、快捷键等现代化操作。

游戏内码为 **BIG5（繁体）**；引擎内部统一使用 UTF-8。

## 目录结构

| 路径 | 说明 |
|------|------|
| `resources/game/` | 原始游戏文件（exe + mkf + dat + cfg），**只读，不提交** |
| `docs/formats/` | 资源格式规范（已完整解析，见 `README.md`） |
| `docs/reverse/` | 函数/全局/字符串逆向资料 |
| `tools/` | Python 逆向与资源解析工具 |
| `include/game/` | C++ 头文件 |
| `src/` | C++ 源码 |
| `build/` | 构建输出（已忽略） |

## 构建

工具链：**MSVC (VS 18 Community) + CMake + Ninja**。首次配置会通过 CMake FetchContent 拉取 SDL3。

```powershell
# 若 cmake 不在 PATH, 先加载 MSVC 环境:
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

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
```

## 代码约定

- **C++17**，根命名空间 `rich4`
- 文件/目录 `snake_case`；类型 `PascalCase`；函数 `camelCase`；私有成员 `m_`
- 头文件用 `#pragma once`
- 平台层不抛异常，错误以 `bool` / `std::optional` 返回
- 内部字符串 UTF-8；BIG5 仅在资源加载期转换
- **不要添加无关注释**；格式相关说明可引用 `docs/formats/*.md`

## 重要参考

- `docs/formats/README.md` — 资源格式总览与工具索引
- `docs/formats/lzhuf.md` — 压缩算法（含易错点说明）
- `include/game/formats.h` — 全部格式的 C 结构定义

## 注意事项

- `resources/game/` 下的文件**不要修改或提交**（版权 + 体积）
- `src/resource/lzhuf_tables.cpp` 由 `tools/gen_lzhuf_tables.py` 从 exe 生成，**勿手改**
- LZHUF 解压器每次解压需重置工作区（自适应树有状态）
