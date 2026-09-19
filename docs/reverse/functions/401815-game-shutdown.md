# 0x401815 gameShutdown

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x401815` |
| 大小 | `0x67` |
| 调用者 | `0x4019DD` WM_DESTROY、`0x401B9C` WinMain 退出分支、`0x40257A` 退出按钮 |
| 被调用 | `0x4543C4` 音效关闭, `0x450404` MKF 关闭, `0x419228` 输入清理, `0x44F9B3` 调色板释放, `0x454240` 锁释放, `0x453D28` |
| 重写符号 | `src/game/game_shutdown.cpp` `gameShutdown` |
| 状态 | 未开始 |

## 功能

游戏全局清理（幂等，由 `byte_46CB05` 保证只执行一次）：关闭音频/资源/输入，
卸载键盘钩子，释放 DirectDraw 表面与接口。

## 逆向依据

- 反编译观察：
  - 入口 `if (!byte_46CB05)` 保证幂等，末尾 `byte_46CB05 = 1`
  - `sub_4543C4()` 音频关闭；`sub_456E11(dword_474938)` 释放初始化分配的缓冲区
  - `sub_4021B2()`、`sub_4548EF()` 其他子系统关闭
  - `sub_450404` 依次关闭 4 个 MKF（effect/panel/speaking/data）
  - `sub_419228()` 输入/计时清理；`sub_44F9B3()` 调色板释放
  - `UnhookWindowsHookEx(hhk)` 卸载键盘钩子
  - `sub_454240(&unk_48231A)` 锁释放；`sub_453D28()`
  - 表面释放：`dword_48A0E0`、`dword_48A0DC` 调 vtable+8（Release），`lpDD->Release()`
- 全局变量：`byte_46CB05` 已清理标志、`hhk`、`lpDD`、表面指针、MKF 句柄

## 重写要点

- `[PORT]` 释放顺序保持一致：音频 → 资源 → 输入 → 钩子 → 表面 → DD 接口
- `[PORT]` `UnhookWindowsHookEx` → SDL3 无对应（改用事件分发后无需卸载）
- 幂等标志 `byte_46CB05` 保留（SDL 侧为 `m_shutdownDone`）

## 验证方式

- 调用两次无副作用；释放后所有句柄为空
