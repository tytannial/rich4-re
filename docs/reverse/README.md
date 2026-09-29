# 逆向工作规范

本目录是《大富翁4》重写工程与原版 `rich4.exe` 之间的**对应关系档案**。
所有重写代码必须可追溯到原始函数/API，禁止"凭感觉"实现。

## 核心原则

1. **逐函数对应**：每个游戏逻辑函数在源码中标注原始入口地址与逆向依据。
2. **单一事实来源**：地址映射表由 `tools/re_map.py` 从源码注释自动生成，勿手改。
3. **平台替换放宽**：DirectDraw/DirectSound/Win32 → SDL3 允许架构重组
   （一对多/多对一），但必须写明原 API 调用点与替换依据。
4. **逆向过程留痕**：关键函数的伪代码要点、全局变量、字符串证据记录在
   `docs/reverse/functions/` 档案中，IDA 数据库内同步重命名与注释。

## 源码注释规范

> **定义 vs 引用**：每个地址的 `依据:` / `替换依据:` 行只写一次
> （**定义**，通常位于实现文件 `.cpp`），其他出现位置只写 `[RE 0x...]` 简注
> （**引用**，不登记映射表）。`tools/re_map.py` 只登记定义，同一地址重复定义报错。

### 游戏逻辑函数（与 exe 函数一一对应）

```cpp
// [RE 0x4015D6] gameInit
// 依据: IDA 反编译 0x4015D6; 字符串 "DirectDraw Initial Error!"/"data.mkf" 引用;
//       SetWindowsHookExA(WH_KEYBOARD) 安装键盘钩子; 由 WinMain 0x401B9C 调用
// 迁移: DirectDrawCreate/SetDisplayMode(640x480x16) → SDL_CreateWindow+SDL_CreateRenderer
bool gameInit(const std::string& gameDir);
```

字段说明：

| 字段 | 必需 | 内容 |
|------|------|------|
| `[RE 0xXXXXXX]` | 是 | 原始函数入口地址（大写十六进制，`0x` 前缀） |
| 名称 | 是 | 原版符号名（IDA 命名或 `sub_XXXXXX`），非 C++ 函数名 |
| `依据:` | 是 | 逆向证据，至少一项：反编译观察 / 字符串引用 / 导入 API / 调用链 / 全局变量访问 |
| `迁移:` | 条件 | 涉及平台替换时必需：原 API → 新 API 及理由 |

### 平台替换函数（DDraw/DirectSound/Win32 → SDL3）

原版调用点分散、无独立函数时，按**功能点**标注：

```cpp
// [PORT DDraw:CreateSurface] 640x480x16 后台缓冲
// 替换依据: 0x4015D6 中 CreateSurface(DDSD 0x6C/0x840) → SDL_CreateTexture(RGBA32)
//           原版 16bit 调色板由 sub_44F935 设置，SDL 侧用调色板纹理还原
```

格式：`[PORT <子系统>:<原API或调用点>]`，必须给出 `替换依据:` 行
（原地址/API + 语义等价说明）。

### 全局变量与常量

```cpp
// [RE 0x48A0D8] lpDD
IDirectDraw* g_lpDD;   // DirectDraw 接口指针
```

```cpp
// [RE 0x47EDC2] unk_47EDC2 默认键位表（28 x u16）
```

### 纯新增代码

```cpp
// [NEW] SDL3 无对应物；<用途说明>
```

### 文件名与符号命名

- 文件名不强制带地址；一个源文件可承载同一子系统的多个原函数。
- 重写函数名用语义化 `camelCase`（如 `gameInit`），不用 `sub_` 前缀。
- 重写代码结构尽量保持原函数边界：一个原函数 → 一个重写函数。
  确需拆分/合并时，在 `依据:` 中说明原因。

## 目录结构

```
docs/reverse/
├── README.md            本规范
├── address-map.md       地址 ↔ 重写符号映射表（tools/re_map.py 生成）
├── globals.md           全局变量/数据表档案（按需生成/维护）
├── frame-anchor.md      帧锚点 offset 与落点对齐（横切主题；像素对齐工具用法）
└── functions/
    ├── 401b9c-winmain.md
    ├── 4015d6-game-init.md
    └── ...
```

函数档案命名：`<地址小写>-<语义名>.md`。档案模板见
`functions/_template.md`，关键函数（主流程、状态机、核心算法）必须建档。

## 工作流

1. **IDA 分析**：在 `rich4.exe.i64` 中重命名函数/变量、加注释（保存入库）。
2. **建档**：在 `docs/reverse/functions/` 写逆向笔记（伪代码要点、证据）。
3. **实现**：写 C++ 代码，按规范加注释；平台迁移写 `[PORT]`。
4. **校验**：运行 `python tools/re_map.py` 生成映射表并检查覆盖率与格式。
5. **验证**：优先用资源数据（manifest）或运行时对比验证行为一致性。

## 校验命令

```bash
# 生成映射表并校验注释格式（返回非 0 表示有错误）
python tools/re_map.py

# 查看覆盖率报告
python tools/re_map.py --report

# 帧锚点 offset 查询 / 像素对齐（元素错位类问题；方法见 frame-anchor.md）
python tools/frame_align.py offsets resources/MultiverseJourney/Panel.mkf --index 65
python tools/frame_align.py align --base <大图帧.png> --base-at 104,110 --cand <待校准帧.png>
```

## 进度统计

映射表与覆盖率见 [`address-map.md`](address-map.md)。
