# 地址映射表

> 由 `python tools/re_map.py` 自动生成，勿手改。

## RE 映射（67 条）

| 地址 | 原符号 | 重写位置 | 依据 |
|------|--------|----------|------|
| `0x401010` | 全局键盘钩子 | `src/application.cpp:150` | fn(0x401010) 读 word_497168 键位表： word_497168/6A/6C/6E（游標上/右/下/左移）→ SetCursorPos(±10) word_497170（確定執行）→ PostMessageA(WM_LBUTTONDOWN/UP)（word_46CB09 去重） word_497172（取消指令）→ PostMessageA(WM_RBUTTONUP)（合成右键按下，模态按右键机制处理）（迁移: SetCursorPos/PostMessageA → SDL_WarpMouseInWindow/SDL_PushEvent（逻辑像素 ±10）） |
| `0x401543` | showLoading | `src/app/new_game.cpp:61` | 0x401543 反编译; sub_450441(dword_48A0E4, 601) 取 614400 字节全屏图, memcpy 到 dword_48A08C 后 BltFast 显示 |
| `0x4015D6` | gameInit | `src/app/game_init.cpp:12` | 0x4015D6 反编译; 失败路径弹 "DirectDraw Initial Error!" / "DirectDraw SetMode Error!"; 由 WinMain(0x401B9C) 调用, 返回非 0 才进入主循环（迁移: DirectDrawCreate + SetCooperativeLevel(FULLSCREEN\|EXCLUSIVE) + SetDisplayMode(640,480,16) → SDL 窗口 + 逻辑分辨率（Application::init）） |
| `0x401815` | gameShutdown | `src/app/game_shutdown.cpp:9` | 0x401815 反编译; if (!byte_46CB05) 保证幂等; 依次关闭音频/MKF/输入/调色板, 卸载键盘钩子, Release 主表面/后台表面/DirectDraw 接口 |
| `0x4018E7` | runModal | `src/app/event_stack.cpp:53` | 0x4018E7 反编译; dword_48A010[++nIDEvent]=handler; PostMessage(0x401) 通知进入; 局部消息泵直到 WM_USER+2(0x402); 返回 Msg.lParam; --nIDEvent（迁移: Win32 局部消息泵 → SDL 事件循环（Application::pumpEvents/renderFrame）; 0x401/0x402 消息 → handler(nullptr) / EventStack::requestExit） |
| `0x401966` | postModalExit | `src/app/event_stack.cpp:41` | PostMessageA(hWnd, 0x402, 0, lParam) —— 请求退出当前模态并回传结果 |
| `0x401981` | enterGameLoop | `include/game/app/game_loop.h:7` | 0x401981 压入 sub_417E26（游戏内事件处理器）并 PostMessage(0x401)； a1 != 0 时调 sub_454D91(0)（迁移: 模态栈 + 消息泵 → runModal（EventStack）） |
| `0x4019DD` | wndProc | `src/application.cpp:90` | 0x4019DD 窗口过程; WM_DESTROY(2) 走 sub_401815+PostQuitMessage, 默认分支转发给 dword_48A010[nIDEvent]（模态栈顶），无处理器时 DefWindowProcA |
| `0x401B9C` | WinMain | `src/application.cpp:12` | 0x401B9C 反编译; RegisterClassA("Rich4") + CreateWindowExA(0, "Rich4", "Rich4", 0x80000000=WS_POPUP, 屏幕宽高) + sub_4015D6() + ShowWindow(5)（迁移: Win32 窗口类/全屏窗口 → SDL_CreateWindow（窗口化，现代化目标）; 显示模式由 0x4015D6 的 SetDisplayMode(640,480,16) 决定 → 逻辑分辨率 640x480） |
| `0x401E59` | 保存背景并绘制当前光标帧 | `include/game/render/cursor.h:32` | 0x401E59 计算 (x-offsetX, y-offsetY)，裁剪到 640x480，先 sub_451A97 保存背景再 sub_4562A5 绘制光标; dword_48A168 记录绘制位置 |
| `0x401F98` | fptc | `include/game/render/cursor.h:37` | 0x401F98 中 word_48A176/word_48A174 控制帧间隔, word_48A172 递增回绕; GetCursorPos 与 dword_48A168/164 比较，变化或换帧时重绘（迁移: GetCursorPos（屏幕坐标）→ 由调用方传入 640x480 逻辑坐标） |
| `0x4020FA` | cursorInit | `include/game/render/cursor.h:13` | 0x4020FA 加载 data.mkf[0] 光标资源、创建 32x32 背景元素（sub_451A5A）、 ShowCursor(0) 隐藏系统光标、timeSetEvent(20ms, fptc) 驱动动画; 0x4021F8 选择光标组（dword_48A0F4 = 资源 + 12*index + 12） |
| `0x4021F8` | select | `include/game/render/cursor.h:25` | 0x4021F8 设置光标组索引、动画帧数 word_48A170、帧间隔 word_48A174 |
| `0x40257A` | mainMenuWndProc | `src/app/main_menu.cpp:70` | 0x40257A 反编译; 处理 WM_PAINT(15)/WM_MOUSEMOVE(0x200)/WM_LBUTTONDOWN(0x201)/ WM_USER+1(1025); 点击 case 0/3/4 经 sub_401966 回传索引退出模态, case 1 打开读档对话框(sub_403D74), case 2 打开设置(sub_411B53) 且不退出 |
| `0x4029FD` | mainMenuScene | `src/app/main_menu.cpp:143` | 0x4029FD 反编译; sub_450441(dword_48A0E4, 1, 0, 0) 取菜单资源; 5 按钮布局 word_46CB28; sub_4018E7(sub_40257A, 0) 进入模态循环返回按钮索引 |
| `0x402AC5` | loadGameFromSlot | `include/game/app/new_game.h:13` | 0x402AC5 反编译; showLoadingScreen + fseek(4) 读取全部存档状态 + loadMapData + 每玩家棋盘数据（10008B + 地图数据）; 由读档对话框返回后调用 差异: 完整状态恢复（股票/卡片/骰子/地图物件）待后续，当前恢复地图/模式/玩家数 |
| `0x40363A` | loadDialogWndProc | `src/app/load_dialog.cpp:106` | 0x40363A 反编译; WM_MOUSEMOVE 列表区域 (0x82..0x240, 0x19..0x1C7) 计算槽索引 v4 = (y-24)/72 并画高亮; WM_LBUTTONDOWN 有效槽 postModalExit(slot); WM_RBUTTONDOWN(0x204) postModalExit(-1); WM_USER+1 重置高亮 |
| `0x403D74` | loadDialog | `include/game/app/load_dialog.h:7` | 0x403D74 反编译; sub_450441(data.mkf, 520) 界面资源 + (data.mkf, 2) 头像资源; 扫描 SAVE0.DAT..SAVE5.DAT 绘制槽位; runModal(sub_40363A) 事件处理 返回选中槽索引（0-5）；取消（右键）或无效返回 -1 |
| `0x406DE7` | newGameInit | `src/app/new_game.cpp:37` | 0x406DE7 反编译; JUMP.MKF 地图预览; runModal(sub_404E44) 选图/选人; 确认后 dword_499114 = dword_46CB3C + 2 等玩家初始化 差异: 选图/选人界面（0x404E44）暂未实现，自动使用默认地图/玩家配置 |
| `0x407AD2` | loadMap | `src/app/new_game.cpp:73` | 0x407AD2 反编译; sub_4502FE("MAP.MKF") + sub_450441(handle, 2*(4*mode+map)) 取 GND 地图数据（dword_47493C）; MAPDAT.MKF 提供玩家初始位置（可选） 差异: 玩家/物件初始化（sub_40E033/sub_40B93B/sub_40A4E1）待后续实现 |
| `0x40FD49` | 滑块单位帧与复选标记帧。 | `src/app/settings_dialog.cpp:90` | 元素偏移 = 12 + 12*i；dword_48BB60+72 → 帧 5（15x16 滑块单位）; dword_48BB60+120 → 帧 9（16x16 复选标记） |
| `0x4103A3` | settingsWndProc | `src/app/settings_dialog.cpp:306` | 0x4103A3 反编译; WM_MOUSEMOVE(515) 查找控件矩形 dword_474B92 并处理; WM_LBUTTONDOWN(513) 分发控件操作; WM_USER+1 重置悬停并复制设置 |
| `0x4119E3` | dateDialog（日期更改界面） | `include/game/app/date_dialog.h:9` | 0x4119E3 中 _dos_getdate + runModal(sub_410AC3)；确认后 dword_48BB50/dword_497160 = 日期 日期打包: BYTE0 = 日, BYTE1 = 月, HIWORD = 年（对应 dword_48BB84） 返回新日期；取消返回 0（原版 -1 语义） |
| `0x411A86` | hotkeyDialog（熱鍵設定界面） | `include/game/app/hotkey_dialog.h:7` | 0x411A86 → runModal(sub_411122); 帧 1（328x336）居中; word_48BB10 工作键位（初值 word_497168）; 确定时 saveConfig（0x411F80） |
| `0x411A96` | helpDialog（遊戲說明界面） | `include/game/app/help_dialog.h:7` | 0x411A96 → sub_44EB39(-1,-1)；help.mkf[0] 界面框架（400x400 居中）; 8 个分类目录 + 页面文本（help.mkf[起始+页号]，BIG5 0x00 分隔行） |
| `0x411B53` | settingsDialog | `src/app/settings_dialog.cpp:370` | 0x411B53 反编译; dword_48BB58 = page; sub_450441(data.mkf, 3); 对话框居中 (320 - 帧0宽/2, 240 - 帧0高/2); runModal(sub_4103A3) |
| `0x411E8F` | 读取 | `src/app/game_init.cpp:39` | 0x411E8F CFG 存在时读 16B 设置 + 56B 键位；不存在时设置区保留默认值 （byte_497158=1, 497159=1, 49715A=4, 49715B=4, 49715C=1, 49715D=1） |
| `0x415872` | playIntro | `src/app/new_game.cpp:114` | 0x415872 反编译; JUMP.MKF 45/46 资源 + sub_451677 播放 AVI （AIRPLANE.AVI 或 off_4750E8[地图索引]） 差异: AVI 播放依赖 MCI，SDL 侧暂无替代（跳过） |
| `0x417E26` | gameEventHandler（骨架） | `src/app/game_loop.cpp:14` | 0x417E26 反编译; WM_PAINT 绘制面板（sub_415D31/415E70/415F69/416E6D/4166F8/4169BC）, WM_MOUSEMOVE 地图坐标换算（×761856>>16 ≈ 11.625，限幅 220..2084）与小地图拖拽, WM_LBUTTONDOWN 地图点击/玩家移动, WM_USER+1 绘制玩家信息面板 差异: 面板绘制与地图交互在后续阶段接入；当前仅保留事件骨架 |
| `0x4190CF` | loadPanelUi | `src/app/new_game.cpp:97` | 0x4190CF 反编译; panel.mkf 索引 0-8（dword_48BE0C/0x475118/0x48BE10/...）; sub_451A5A(200,200) 与地图尺寸离屏缓冲 差异: 面板资源与离屏缓冲在游戏内 UI 阶段接入 |
| `0x4291D6` | stockInit | `src/app/new_game.cpp:106` | 0x4291D6 反编译; 12 只股票随机初始涨跌（rand），历史记录 dword_497328 差异: 股票系统在游戏内经济阶段接入 |
| `0x44DD9F` | 继续阅读翻屏 | `src/app/help_dialog.cpp:250` | dword_4762B8 为屏起点标记（0 或 '@' 行）；前进/回退最多 14 行（遇 '@' 停）， 结果写回 dword_4762B8；绘制时若为 '@' 行则从其后一行开始。 无效翻页（首屏上一页 / 末屏下一页）不改变画面内容，但需重绘以复位按下动画 |
| `0x44F70C` | measure | `include/game/render/text.h:47` | 0x44F70C 扫描 rect 范围内非零像素，输出 [minX, minY, maxX, maxY] |
| `0x44F9D8` | setFont | `include/game/render/text.h:34` | CreateFontA(-size, 0,0,0, weight, 0,0,0, 0x88(CHINESEBIG5_CHARSET), 0,0,0,0, "細明體"); style bit1 时 weight=700 否则 400; spacing 存入 dword_4762DC; fg/bg 经 ((c&0xFF0000)>>16)\|((u8)c<<16)\|(c&0xFF00) 转 COLORREF |
| `0x44FABC` | drawText | `include/game/render/text.h:40` | 0x44FABC 反编译; '#NNNN' 前缀播放语音(sub_45441A)后从 +5 绘制; DT_CALCRECT 计算尺寸 + 边距 10; 阴影(bit0)/描边(bit2)多次绘制; 测量包围盒后按 align 调整位置 blit 到目标; 绘制后清零文本区域 align: 0 左上 / 1 右上 / 2,3,4 居中 / 5 垂直居中 / 6 左下 / 7 底中 |
| `0x450069` | relocateFrames | `include/game/render/ui_image.h:31` | SPR 帧 0 像素 = 基址 + data_offset + 512（跳过 256 色 RGB555 调色板）; SMP 帧 0 像素 = 基址 + data_offset; 后续帧 = 前一帧指针 + 前一帧 size |
| `0x4502FE` | mkfOpen | `src/resource/mkf.cpp:20` | 0x4015D6 中 sub_4502FE("data.mkf"/"speaking.mkf"/"panel.mkf"/"effect.mkf")， 返回句柄存入 dword_48A0E4/0x48A054/0x48A05C/0x48A058；0x450441 以句柄取资源 差异: 原版打开文件后按需 seek+解压（带缓存），当前实现全量读入 + 索引预解析 |
| `0x450441` | mkfRead | `src/resource/mkf.cpp:82` | 0x4029FD 中 sub_450441(dword_48A0E4, 1, 0, 0) 取 data.mkf 索引 1（菜单资源）； 0x406DE7 等多处按 (句柄, 索引, 输出, ...) 调用 差异: 原版返回内部缓存指针，当前返回独立 vector；关闭对应 sub_450404 |
| `0x4520A6` | 日历计算 | `src/app/date_dialog.cpp:80` | 0x4520A6 中 v6 = (天数+4)%7（1998-01-01 为周四）; 2 月闰年 29 天（年%4） |
| `0x45523E` | sub_45523E（经 | `include/game/render/surface.h:44` | 0x45523E 返回 (c>>3)&0x1F \| (c>>6)&0x3E0 \| (c>>9)&0x7C00（RGB888→RGB555）（迁移: SDL 侧内部固定 RGB555，原版 dword_47637C 显示格式分支（sub_45175D 检测）不再需要） |
| `0x455B3A` | sub_455B3A | `include/game/render/blit.h:31` | 不透明 blit（整元素, qmemcpy 快速拷贝）, 裁剪到目标尺寸 |
| `0x455C52` | sub_455C52 | `include/game/render/blit.h:16` | 逐像素 `if (v19) *v16 = v19`（色键 0 透明）; 目标位置 = (x - offsetX, y - offsetY); 裁剪模式 0 用目标尺寸, 1 用 dword_4861B8..C4; 返回是否实际绘制 |
| `0x455E24` | sub_455E24 | `include/game/render/blit.h:26` | 不透明 blit + 源区域（qmemcpy 快速拷贝，无色键判断） |
| `0x455FD9` | sub_455FD9 | `include/game/render/blit.h:21` | 色键 blit + 源区域 (srcX, srcY, w, h); 裁剪时同步调整源偏移 (a6/a7) |
| `0x4561BE` | sub_4561BE | `include/game/render/surface.h:32` | 0x4561BE 以 memset32 按行填充 (x,y,w,h)，颜色由 sub_4551F0 转换 |
| `0x45620F` | sub_45620F | `include/game/render/blit.h:35` | 0x45620F 用 memset32 画上下边、循环画左右边的空心矩形（颜色经 convertColor） 用于存档槽悬停高亮（0xFFFF00）与地图选择框 |
| `0x45709C` | start | `src/main.cpp:8` | 0x45709C 为 CRT 入口 thunk（jmp sub_458CED）; sub_458CED 初始化 CRT 后调用 WinMain(GetModuleHandleA(0), 0, lpCmdLine, 10)（迁移: CRT 启动 + WinMain → 标准 main(argc, argv); lpCmdLine → --game <dir> 参数） |
| `0x46CAD8` | nIDEvent | `include/game/app/event_stack.h:46` | 栈顶索引; 0x4018E7 中 ++nIDEvent 压栈、--nIDEvent 出栈 |
| `0x46CB05` | byte_46CB05 | `include/game/game_state.h:33` | 0x401815 清理函数的幂等标志（非 0 时跳过清理） |
| `0x46CB28` | word_46CB28 | `src/app/main_menu.cpp:17` | DGROUP 原始数据 0x46CB28: BE00 7C01 4801 7C01 D401 7A01 4801 C201 3E00 7C01 为 5 组 u16 (x, y)；0x4029FD 循环 i<5 用 word_46CB28[2i]/word_46CB2A[2i] 布局 索引语义（由 0x40257A 点击分支与 WinMain switch 推断）: 0 新游戏 / 1 读档 / 2 设置(不退出) / 3 退出 / 4 新游戏(模式 1) |
| `0x474A54` | 设置项标签（BIG5 | `src/app/settings_dialog.cpp:62` | 索引 5 = "樂 曲"(0x46355B)、6 = "視 窗"(0x463562)、7 = "日、月曆"(0x463569) |
| `0x47EDFA` | 可绑定键表（78 | `src/platform/input.cpp:14` | 0x411122 WM_KEYDOWN 以 wParam(VK) 查表；0x401010 钩子以 wParam 匹配键位表 |
| `0x4861B8` | dword_4861B8/C0/BC/C4 | `include/game/render/blit.h:11` | 0x455C52/0x455FD9/0x455E24 裁剪模式 1 使用该矩形 (left, right, top, bottom) |
| `0x48A010` | dword_48A010 | `include/game/app/event_stack.h:13` | 0x48A010 为 20 项函数指针数组; 0x4019DD 窗口过程把消息转发给 dword_48A010[nIDEvent]; 0x4018E7 压栈/出栈并返回 handler 的结果码 |
| `0x48A054` | dword_48A054 | `include/game/game_state.h:16` | 0x4015D6 中 sub_4502FE("speaking.mkf") 的返回值 |
| `0x48A058` | dword_48A058 | `include/game/game_state.h:24` | 0x4015D6 中 sub_4502FE("effect.mkf") 的返回值 |
| `0x48A05C` | dword_48A05C | `include/game/game_state.h:20` | 0x4015D6 中 sub_4502FE("panel.mkf") 的返回值 |
| `0x48A08C` | dword_48A08C | `include/game/render/surface.h:11` | 0x401543 memcpy(dword_48A08C, data.mkf[601], 614400)（614400 = 640*480*2）; 0x451B36 sub_456F60(dword_48A08C, 0, 614400); 0x456418/0x4563F5 等以其为绘制目标; 0x40257A WM_PAINT 把绘制结果经 DDraw 后台表面 BltFast 到主表面（迁移: DDraw 后台表面 Lock/Unlock + BltFast → SDL 纹理每帧上传（RGB555 与原版一致）） |
| `0x48A0E4` | dword_48A0E4 | `include/game/game_state.h:12` | 0x4015D6 中 sub_4502FE("data.mkf") 的返回值; 0x450441 以其为资源库句柄 |
| `0x48A184` | dword_48A184 | `src/app/main_menu.cpp:36` | 0x40257A 中鼠标移动时记录的悬停按钮索引，-1 表示无 |
| `0x48A354` | dword_48A354 | `include/game/game_state.h:63` | 0x406DE7 分配 0x96000 字节并由 JUMP.MKF 地图图填充 |
| `0x497158` | byte_497158 | `include/game/game_state.h:28` | 0x411E8F CFG 不存在时初始化 byte_497158=1/497159=1/49715A=4/49715B=4/ 49715C=1/49715D=1；存在时由 CFG 前 16 字节覆盖 |
| `0x497160` | dword_497160 | `include/game/game_state.h:59` | 0x4119E3 中日期更改确认后写入 dword_48BB50/dword_497160 |
| `0x49910C` | dword_49910C | `include/game/game_state.h:49` | 0x417E26 中 *dword_49910C 为当前玩家索引 |
| `0x499114` | dword_499114 | `include/game/game_state.h:45` | 0x406DE7 中 dword_499114 = dword_46CB3C + 2（玩家数） |
| `0x4991B6` | word_4991B6 | `include/game/game_state.h:37` | WinMain case 4 设置（新模式）; 0x406DE7 中参与资源索引 4*mode+map |
| `0x4991B8` | word_4991B8 | `include/game/game_state.h:41` | 0x406DE7 中地图索引; 0x407AD2 中参与 MAP.MKF 索引 |

## PORT 映射（8 条）

| 原调用点/API | 替换位置 | 替换依据 |
|--------------|----------|----------|
| Win32:UnhookWindowsHookEx 键盘钩子卸载 | `src/app/game_shutdown.cpp:27` | 0x401815 UnhookWindowsHookEx(hhk)；SDL 侧无全局钩子，无需对应 |
| DDraw:Release 主表面(0x48A0DC)/后台表面(0x48A0E0)/lpDD 释放 | `src/app/game_shutdown.cpp:30` | SDL3 纹理由 Renderer 析构统一释放（Application::shutdown） |
| Win32:GetCursorPos | `src/application.cpp:118` | 原版全屏 640x480，鼠标屏幕坐标即游戏坐标； SDL 窗口可缩放 + letterbox 逻辑呈现，需把窗口坐标换算为 640x480 逻辑坐标 |
| DDraw:BltFast 后台表面 → 主表面 | `src/render/surface.cpp:14` | 0x401543 BltFast(dword_48A0DC, ..., dword_48A0E0) 把绘制结果送上屏幕 → SDL_UpdateTexture + SDL_RenderTexture；像素格式 RGB555 与原版资源一致 （SDL3 命名为 SDL_PIXELFORMAT_XRGB1555，bit10-14=R / bit5-9=G / bit0-4=B） |
| WINMM:DirectSoundCreate/mciSendStringA 音频输出 | `include/game/platform/audio.h:8` | 0x4015D6 中 sub_453B55 初始化 DirectSound；0x4019DD 用 mciSendStringA 控制 vfw/mid/cdtrack 音轨（"pause vfw"/"resume mid"/"play cdtrack notify"） → SDL3 SDL_AudioStream 播放 WAV；MCI/MIDI/CD 音轨迁移待办 简单音频层：打开 22050Hz / 8bit / mono 输出流，播放 WAV(MKF 中 RIFF 资源)。 |
| Win32:SetWindowsHookExA(WH_KEYBOARD) 键盘输入 | `include/game/platform/input.h:11` | 0x4015D6 安装全局键盘钩子 fn(0x401010)，读取 word_497168 键位表实现 快捷键（见 docs/formats/cfg.md）→ SDL_EVENT_KEY_DOWN 在事件循环中分发 对应 RICH4.CFG 的 28 个功能键位，索引语义见 docs/formats/cfg.md。 |
| DDraw:SetDisplayMode/CreateSurface 显示与表面 | `include/game/platform/renderer.h:7` | 0x4015D6 SetDisplayMode(640,480,16) + CreateSurface（主表面 512x512 / 后台表面 640x480）→ SDL_Renderer + 逻辑分辨率 640x480； 原版 16bit 调色板表面（sub_44F935 设色）在纹理上传时展开为 RGBA 逻辑分辨率固定 640x480，实际窗口可宽屏缩放（letterbox / 拉伸可配置）。 |
| Win32:CreateWindowExA 主窗口 | `include/game/platform/window.h:7` | 0x401B9C CreateWindowExA(0, "Rich4", "Rich4", WS_POPUP(0x80000000), 屏幕宽高) → SDL_CreateWindow；原版全屏无边框，SDL 侧窗口化可缩放 |
