# 0x4015D6 gameInit

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x4015D6` |
| 大小 | `0x23F` |
| 调用者 | `0x401B9C` WinMain |
| 被调用 | `0x45011A` 环境检查, `0x453B55` 音频初始化, `0x4502FE` MKF 打开, `0x411E8F` 读配置, `0x45175D` 视频/AVI 初始化, `0x4020FA`/`0x4545BA`/`0x4021F8` 其他初始化 |
| 重写符号 | `src/game/game_init.cpp` `gameInit` |
| 状态 | 未开始 |

## 功能

游戏全局初始化：创建 DirectDraw 全屏表面（640x480x16），初始化随机数、
音频、资源归档（4 个 MKF）、配置与键位、键盘钩子，重置全局状态。
返回 1 表示成功；失败时弹 `MessageBoxA` 并返回 0。

## 逆向依据

- 反编译观察：
  - `sub_45011A()` 返回 0 则整体失败（环境检查，疑似已运行实例检测）
  - `sub_453B55(0)` 音频初始化
  - `DirectDrawCreate(0, &lpDD, 0)`；失败弹 `"DirectDraw Initial Error!"`
  - `lpDD->SetCooperativeLevel(hWnd, 17)`：`17 = DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE|DDSCL_ALLOWREBOOT` 之组合
  - `lpDD->SetDisplayMode(640, 480, 16)`；失败弹 `"DirectDraw SetMode Error!"`
  - 主表面 `dword_48A0DC`：`dword_48A068`(DDSD=108=0x6C, 512 宽) / `dword_48A0D0`(512 高)，
    创建后 `Blt` 填色；`dword_48A060 = dword_48A078`（调色板指针）
  - 后台表面 `dword_48A0E0`：`dword_48A06C=7`(DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH) / `2112`(DDSCAPS_VIDEOMEMORY 等) / 640x480
  - `GetTickCount` + `srand`（随机种子）
  - `sub_44F935` 调色板初始化、`sub_45175D` 视频初始化
  - `sub_4502FE` 打开 4 个 MKF：`data.mkf`、`speaking.mkf`、`panel.mkf`、`effect.mkf`
  - `sub_411E8F` 读取 `RICH4.CFG`（见 docs/formats/cfg.md）；尾部 `dos_getdate` → 钳位年
    ∈[1998,2010]（越界→该端点 1-1）→ `dword_497160` 游戏日期（尾跳转 0x411A7C，**不**写
    `dword_48BB50`；CFG 第 8..11 字节虽被读入 `byte_497158`，随即被该钳位值覆盖，即启动日期**不持久化**）
  - `SetWindowsHookExA(2, fn, hInstance, 0)`：`WH_KEYBOARD` 全局键盘钩子（`fn = 0x401010`）
  - `sub_4021F8(41, 1, 0)`、`sub_456F60(&dword_4990F0, 0, 4)`（清全局）
  - `dword_474938 = sub_456F80(0x5E880)`：分配 387200 字节缓冲区
  - 清 `byte_46CAF8/byte_46CAF9/byte_46CAFD/byte_46CB05`、`dword_48A010[0]=0`、`nIDEvent=0`
- 字符串引用：`"DirectDraw Initial Error!"`、`"DirectDraw SetMode Error!"`、`"data.mkf"`、`"speaking.mkf"`、`"panel.mkf"`、`"effect.mkf"`
- 导入 API：`DirectDrawCreate`、`SetWindowsHookExA`、`srand`、`GetTickCount`、`MessageBoxA`
- 全局变量：`lpDD(0x48A0D8)`、主/后台表面 `0x48A0DC`/`0x48A0E0`、MKF 句柄 `0x48A0E4/0x48A054/0x48A05C/0x48A058`、`hhk(0x48A050)`

## 关键结构

```c
// DDSURFACEDESC 复用的全局区（0x48A068 起）
DWORD dword_48A068;   // dwSize/DDSD 标志区
...
DWORD dword_48A0D0;   // 主表面高度 512（512x512 工作缓冲）
DWORD dword_48A074;   // 后台表面宽 640
DWORD dword_48A070;   // 后台表面高 480
```

## 重写要点

- `[PORT]` DirectDraw 初始化 → SDL3：
  - `DirectDrawCreate`+`SetCooperativeLevel(FULLSCREEN|EXCLUSIVE)`+`SetDisplayMode(640,480,16)`
    → `SDL_CreateWindow` + `SDL_CreateRenderer`（逻辑分辨率 640x480，16bit 调色板在 SDL 侧展开为 RGBA）
  - 替换依据：原版 16bit 高彩 + 调色板由 `sub_44F935` 管理（见 docs/formats/spr.md 调色板），
    SDL3 无硬件调色板，需在纹理上传时应用调色板（保留原色值）
  - 主表面 512x512 工作缓冲 → SDL_Texture(512x512) 或 CPU 缓冲 + 上传
- `[PORT]` `SetWindowsHookExA(WH_KEYBOARD)` → SDL3 无全局钩子；
  原钩子实现快捷键（见 docs/formats/cfg.md），SDL 侧改用 `SDL_EVENT_KEY_DOWN` 分发
- `[PORT]` MKF 打开：`sub_4502FE` → `MkfArchive::load`（资源层重构为与原版句柄表对应，待办）
- `RICH4.CFG` 读取 `sub_411E8F` → `Input::loadConfig`（已实现，键位表已核对）

## 验证方式

- 初始化后全局状态与原始一致：4 个 MKF 句柄有效、键位表内容一致
- 失败路径消息（可用日志替代 MessageBox）
