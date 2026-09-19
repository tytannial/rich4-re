# 0x4019DD wndProc

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x4019DD` |
| 大小 | `0x107` |
| 调用者 | Windows 消息分发（`DispatchMessageA`） |
| 被调用 | `0x401815` 清理, `0x45174A`/`0x454D2C` MCI 通知处理 |
| 重写符号 | `src/game/event_stack.cpp` `dispatchEvent`（事件栈转发部分） |
| 状态 | 未开始 |

## 功能

主窗口过程。处理窗口销毁、激活状态切换（暂停/恢复 MCI 音轨），
其余消息转发给**模态栈顶**事件处理器（`dword_48A010[nIDEvent]`），
无处理器时调用 `DefWindowProcA`。

## 逆向依据

- 反编译观察：
  - `WM_DESTROY(2)` → `sub_401815()`（游戏清理）+ `PostQuitMessage(0)`
  - `WM_ACTIVATEAPP(0x1C)` → 激活时 `SetFocus`、恢复 `mciSendStringA("resume vfw"/"resume mid"/"play cdtrack notify")`；
    失活时暂停（`"pause vfw"`/`"pause mid"`/`"stop cdtrack"`），并设 `dword_46CB0B`
  - `MM_MCINOTIFY(953)` 且 `wParam==1`（MCI_NOTIFY_SUCCESSFUL）→
    `sub_45174A`（AVI 播放结束）或 `sub_454D2C`（CD 音轨结束），按 `byte_46CB02` 区分
  - 默认分支：`if (dword_48A010[nIDEvent])` 调用栈顶处理器，否则 `DefWindowProcA`
- 字符串引用：`"resume vfw"`、`"resume mid"`、`"play cdtrack notify"`、`"pause vfw"`、`"pause mid"`、`"stop cdtrack"`
- 全局变量：`dword_48A010[20]` 事件栈、`nIDEvent(0x46CAD8)` 栈顶索引、`byte_46CB01` 激活标志、`dword_48A0DC` 主表面

## 关键结构

```c
// 模态事件栈：dword_48A010 是 20 项函数指针数组，nIDEvent 为栈顶索引
int dword_48A010[20];   // 0x48A010
int nIDEvent;           // 0x46CAD8，初始 0，dword_48A010[0]=0
```

## 重写要点

- `[PORT]` `WM_DESTROY` → `SDL_EVENT_QUIT`：`gameShutdown` + 退出主循环
- `[PORT]` `WM_ACTIVATEAPP` → `SDL_EVENT_WINDOW_FOCUS_GAINED/LOST`：
  音频恢复/暂停（MCI → SDL3 音频待迁移）
- `[PORT]` `MM_MCINOTIFY` → 音频播放完成回调（SDL3 `SDL_AudioStream` 回调或轮询）
- 事件栈转发逻辑保留：`dispatchEvent` 优先交给 `eventStack.top()`，
  未处理再交默认处理（SDL 侧为无操作）
- MCI 音轨控制属平台迁移范围，按 `[PORT WINMM:mciSendStringA]` 标注

## 验证方式

- 断点/日志确认消息顺序：ACTIVATEAPP → 转发栈顶 → 默认处理
- 模态栈深度变化时的转发目标切换
