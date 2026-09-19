# 0x4018E7 runModal

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x4018E7` |
| 大小 | `0x7B` |
| 调用者 | 场景函数（如 `0x4029FD` 主菜单、`0x406DE7` 新游戏、`0x403D74` 读档等） |
| 被调用 | 无（内部消息循环） |
| 重写符号 | `src/game/event_stack.cpp` `runModal` |
| 状态 | 未开始 |

## 功能

模态消息循环：将事件处理函数压入模态栈，`PostMessage(WM_USER+1)` 触发
窗口重绘/激活，然后运行局部消息泵，直到收到 `WM_USER+2(1026)` 消息为止；
返回该消息的 `lParam`（即处理函数通过 `sub_401966` 回传的结果码）。
栈顶索引减一，处理函数出栈。

## 逆向依据

- 反编译观察：
  - `dword_48A010[++nIDEvent] = a1`：压栈
  - `PostMessageA(hWnd, 0x401, 0, lParam)`：触发主窗口重新分发（`0x401 = WM_USER+1`）
  - 局部循环 `PeekMessageA` + `TranslateMessage` + `DispatchMessageA`，
    收到 `Msg.message == 1026`（`WM_USER+2`）退出
  - `--nIDEvent; return Msg.lParam`
- 配套函数：
  - `sub_401966`（`0x401966`）：`PostMessageA(hWnd, 0x402, 0, lParam)` —— 请求退出当前模态并传结果
  - `sub_401981`（`0x401981`）：压入 `sub_417E26`（游戏内事件处理器）并
    设置 `byte_498EA0[52*player] |= 0x80`（标记当前玩家），随后
    `PostMessage(0x401)`；`a1` 非 0 时调 `sub_454D91(0)`
  - `sub_402460`（`0x402460`）：绘制暂停开关（`byte_48A178`），
    值 1 时若正在绘制（`byte_48A179 & 1`）调 `sub_402250(0)` 收尾
- 全局变量：`dword_48A010`、`nIDEvent`

## 调用关系（模态嵌套示例）

```
WinMain
 └─ sub_4029FD 主菜单: 布局后 runModal(sub_40257A, 0)
     └─ 点击"新游戏" -> sub_401966(按钮索引) 退出主菜单模态，返回按钮索引
 └─ sub_406DE7 新游戏: ... runModal(sub_404E44, lParam)
     └─ ...
```

## 重写要点

- `[PORT]` Win32 局部消息泵 → SDL3 事件循环：
  原版通过 `PostMessage(0x401)` 让主窗口过程转发事件给栈顶函数；
  SDL3 无窗口过程，`runModal` 直接 `SDL_PollEvent` 并把事件交给栈顶处理器。
  替换依据：`dword_48A010` 栈语义等价于"模态事件处理器栈"，
  `0x401/0x402` 消息语义等价于"重绘请求/退出请求"，可直接函数调用。
- 退出条件 `WM_USER+2` → 返回结果码，保留 `lParam` 语义。
- 注意：原版 `PeekMessageA` 会处理**窗口消息**（含鼠标/键盘/重绘），
  重写后需要保证渲染帧循环在此处仍执行（SDL 侧模态循环内也要 present）。

## 验证方式

- 模态栈嵌套深度、返回值传递路径与原始一致（用日志对比按钮点击流程）
