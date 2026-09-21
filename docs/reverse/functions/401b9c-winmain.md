# 0x401B9C WinMain

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x401B9C` |
| 大小 | `0x23F` |
| 调用者 | `0x458CED`（CRT 启动，传入 `GetModuleHandleA(0)`、`lpCmdLine`、`nShowCmd=10`） |
| 被调用 | `0x4015D6` 初始化, `0x451677` 播放 START.AVI, `0x4029FD` 主菜单, `0x406DE7` 新游戏, `0x4190CF` 读档流程, `0x401981` 进入游戏, `0x401815` 清理 |
| 重写符号 | `src/application.cpp` `Application::init/run` |
| 状态 | 已实现（主循环/场景分发/取消回主菜单；游戏内菜单经 `g_sceneRequest`/`g_quitGame` 接入） |

## 功能

注册窗口类、创建全屏主窗口、初始化游戏，随后进入 Win32 消息循环；
消息循环中按场景返回值分发到新游戏/读档/退出等分支，并将消息转发给
模态栈顶处理函数。

## 逆向依据

- 反编译观察：
  - `RegisterClassA`（类名 `"Rich4"`，`lpfnWndProc = sub_4019DD`，图标 `RICH4_ICON`）
  - `CreateWindowExA(0, "Rich4", "Rich4", 0x80000000, 0,0, GetSystemMetrics(0), GetSystemMetrics(1), ...)`
    —— `0x80000000` = `WS_POPUP`，尺寸取屏幕，全屏无边框
  - `sub_4015D6()` 返回非 0 才继续（初始化成功）
  - `ShowWindow(hWnd, 5)`（SW_SHOW）+ `UpdateWindow`
  - `sub_451677("START.AVI", &dword_46CADC, 1)` 播放开场动画
  - 主循环：`PeekMessageA(&Msg, 0, 0, 0, PM_REMOVE)`，`WM_QUIT(18)` 时返回
- 字符串引用：`"START.AVI"`、`"Rich4"`、`"RICH4_ICON"`
- 导入 API：`RegisterClassA`/`CreateWindowExA`/`ShowWindow`/`PeekMessageA`/`TranslateMessage`/`DispatchMessageA`
- 全局变量：`hInstance(0x48A064)`、`hWnd(0x48A0D4)`、场景标志 `byte_46CAF8/byte_46CAF9/byte_46CAFC/byte_46CB01`

## 主循环结构

```
sub_4029FD() -> v8          // 主菜单场景，返回按钮索引
switch (v8):
  0 -> 新游戏: sub_406DE7(byte_46CAFC) 失败则 continue，成功后
       sub_401543(); sub_407AD2(); sub_4190CF(...); sub_4291D6(); sub_415872();
       byte_46CAFC = 1; sub_401981(0); 进入内层循环
  1 -> 读档: sub_4190CF(...); byte_46CAFC=1; sub_401981(1)
  3 -> 退出: sub_401815(); DestroyWindow(hWnd)
  4 -> word_4991B6=1; 同 case 0 分支（新游戏变体，带模式选择）
内层循环（游戏运行中）:
  PeekMessage 空转时:
    byte_46CB01(窗口激活) -> sub_4192F7() 等定时任务
    byte_46CAFA -> sub_40D7C4()
     byte_46CAF8(场景切换请求) -> switch:
         1 -> LABEL_5（重置 word_4991B6/4991B8/g_clearedMaps/byte_46CAFC + 回主菜单）
         2 -> gameClearFlow(); 若 g_quitGame 退出否则回主菜单
         3 -> gameClearFlow(); 回主菜单
         4 -> loadDialog(0) 后进入游戏
     byte_46CAF9(g_quitGame) -> sub_451B36(); sub_401815(); DestroyWindow（退出程序）
  WM_QUIT(18) -> return
```

## 关键结构

```c
// 全局
HINSTANCE hInstance;    // 0x48A064
HWND      hWnd;         // 0x48A0D4
BYTE byte_46CAF8;       // 场景切换请求
BYTE byte_46CAF9;       // 退出请求
BYTE byte_46CAFC;       // 是否已开始过游戏（影响新游戏初始化路径）
BYTE byte_46CB01;       // 窗口是否激活
WORD word_4991B6;       // 新游戏模式（case 4 设置）
```

## 重写要点

- `[PORT]` 窗口创建：`RegisterClassA`+`CreateWindowExA(WS_POPUP)` →
  `SDL_CreateWindow`（原版全屏 640x480x16，SDL 侧窗口化 + 逻辑分辨率）
- `[PORT]` 消息循环：`PeekMessageA`/`TranslateMessage`/`DispatchMessageA` →
  `SDL_PollEvent` + 事件栈分发（见 `4018e7-run-modal.md`）
- `[PORT]` 窗口激活：`WM_ACTIVATEAPP` → `SDL_EVENT_WINDOW_FOCUS_GAINED/LOST`
- `START.AVI` 播放依赖 VFW/MCI（`mciSendStringA "play vfw"`），
  现代化方案待定（FFmpeg 或跳过），需单独设计并在映射表登记
- **主循环（`Application::run`）**：`while` 循环对应原版 `LABEL_5`——
  每次回主菜单前重置 `word_4991B6/4991B8`、`byte_46CAFC`；
  `case 0/4` 新游戏取消（`newGameInit` 返回 0）时 `continue` 回主菜单
  （原版 `if (!newGameInit(...)) continue`），修复了此前"取消即退出程序"的问题
- 游戏内菜单事件已接入（`game_loop.cpp` 16ms timer 检测）：
  - `GameState.sceneRequest=1`（重新遊戲）→ `requestExit(0)` 回主菜单
    （`Application::run` 循环 continue，重置 gameMode/mapIndex/gameInited）
  - `GameState.quitGame`（結束遊戲）→ `requestExit(0)` + `app.quit()` **直接退出程序**
    （对齐原版 `sub_411B46` + LABEL_27）
  - 重新遊戲时 `newGameInit` 重置 `mapRotation`/骰子/回合/音乐计时器等残留状态

## 验证方式

- 对照 `PeekMessageA` 循环行为：模态栈深度、场景切换标志位的状态机流转
- 运行时日志对比：初始化成功路径、退出路径的调用序列
