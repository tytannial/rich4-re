# UI 绘制管线（阶段 1-3 逆向总结）

本档案汇总主菜单/游戏内共用的 UI 绘制管线，重写实现位于 `src/render/`。

## 表面架构

| 原版 | 说明 | 重写 |
|------|------|------|
| `dword_48A08C` | 640x480 RGB555 后台缓冲（614400B），所有 UI 绘制目标 | `Surface`（`src/render/surface.cpp`） |
| `dword_48A0E0` | DDraw 后台表面 | SDL 纹理（每帧上传） |
| `dword_48A0DC` | DDraw 主表面（BltFast 目标） | `SDL_Renderer` 呈现 |
| `dword_4762CC` | 512x200 文本渲染表面 | `TextRenderer` 的 DIB Section |

绘制流程：CPU 写 `dword_48A08C` → Lock/Unlock + BltFast 上屏。
SDL 侧：写 `Surface` → `SDL_UpdateTexture` → `SDL_RenderPresent`。

[M4-A1] 画布尺寸运行期化：原版后台缓冲恒 640×480（`614400 = 640*480*2`），重写 `Surface`
宽高改为运行期（`create(renderer, w, h)`，默认 native 640×480）——所有像素步长/边界一律走
`width()/height()`（含 blit 族、区域保存恢复、各对话框快照）；`kWidth/kHeight` 仅作设计尺寸
常量（create 默认值、全局 clip 初值、`blitScrolledMap` 的 640 宽预览源行距）。
`--canvas WxH` 为开发验证入口（docs/m4-plan.md §15）。

## UI 元素结构（12 字节）

```
+0  u16 width
+2  u16 height
+4  i16 offsetX     # 绘制位置 = (x - offsetX, y - offsetY)
+6  i16 offsetY
+8  u32 size/pixels # 资源中为字节数；sub_450069 指针化后为像素地址
```

- 分配：`sub_451A5A`（`allocUiElement`）
- 帧指针化：`sub_450069`（`relocateFrames`）
  - SPR：帧 0 = 基址 + data_offset + 512（跳过 256 色 RGB555 调色板）
  - SMP：帧 0 = 基址 + data_offset；后续帧 = 前帧指针 + 前帧 size
- 重写：`UiFrameView` / `UiImage`（`src/render/ui_image.cpp`）

## blit 函数族

| 原版 | 色键 | 源区域 | 实现 | 重写 |
|------|------|--------|------|------|
| `sub_455C52` | 是 | 无 | 逐像素 | `blitElement` |
| `sub_455FD9` | 是 | 有 | 逐像素 | `blitElementRegion` |
| `sub_455E24` | 否 | 有 | qmemcpy | `blitElementRegionOpaque` |
| `sub_455B3A` | 否 | 无 | qmemcpy | `blitElementOpaque` |

裁剪：目标位置越界时缩小绘制范围并推进源偏移；
裁剪模式 1 使用全局矩形 `dword_4861B8/C0/BC/C4`（`setClipRect`）。

包装（目标固定）：
- `sub_456328` → `blitToCanvas`（目标为元素画布）
- `sub_456495` → `blitToBackbuffer`（目标 dword_48A08C）
- `sub_4563F5` → `blitElementFullscreen`（640x480 不透明）
- `sub_456418` / `sub_45643D` → 菜单悬停高亮/恢复

## 文本渲染（GDI，Windows 先行）

| 原版 | 功能 | 重写 |
|------|------|------|
| `sub_44F9D8` | 设置字体（细明体、CHINESEBIG5、样式位、字距） | `TextRenderer::setFont` |
| `sub_44FABC` | 绘制（`#NNNN` 语音前缀、阴影/描边、8 种对齐、测量后 blit） | `TextRenderer::drawText` |
| `sub_44F70C` | 非零像素包围盒测量（行距 512） | `TextRenderer::measure` |
| `sub_44F7C7` | 竖排文本（align 3） | `TextRenderer::drawTextVertical`（待实现） |

- 字体名 `0x4660A0` = BIG5 "細明體"；样式位 `dword_4762D8`：
  bit0 阴影(1,1)、bit1 粗体、bit2 四向描边、bit3 特殊 blit
- 颜色：`dword_4762E0`（前景）/`color`（背景），RGB888 → COLORREF 转换
- 对齐参数 a5：0 左上 / 1 右上 / 2,4 中上 / 3 竖排 / 5 居中 / 6 左下 / 7 底中
- 迁移：DDraw 表面 GetDC → CreateDIBSection + GDI DC（`src/render/text.cpp`）

## 光标系统

| 原版 | 功能 | 重写 |
|------|------|------|
| `sub_4020FA` | 初始化：Data.mkf[0]（43 帧 SMP）、32x32 背景元素、隐藏系统光标 | `Cursor::init` |
| `sub_4021F8` | 选择光标组（默认 41 号箭头 23x23 offset 1,1） | `Cursor::select` |
| `sub_401E59` | 保存背景 + 绘制当前帧 | `Cursor::compose` |
| `sub_401F5E` | 恢复背景 | `Cursor::uncompose` |
| `sub_401F98` | 20ms 定时器：动画推进 + 位置跟踪重绘 | `Cursor::update` |
| `sub_40235D` / `sub_402250` | WM_PAINT 前移除 / 后重绘 | `Application::renderFrame` |
| `sub_4024C0` | 光标包围矩形 | `Cursor::getRect`（[M4-A1] 增 `const Surface&` 参数，裁剪到运行期画布尺寸） |

## 主菜单

- 资源：Data.mkf[1] SMP（11 帧）：帧 0 = 640x480 背景画布，帧 (1,2)..(9,10) = 5 按钮普通/高亮
- 按钮坐标 `word_46CB28`（[RE 0x46CB28]）：0:(190,380) 1:(328,380) 2:(468,378) 3:(328,450) 4:(62,380)
- 语义：0 新游戏 / 1 读档 / 2 设置 / 3 退出 / 4 新游戏（模式 1）
- 悬停：`sub_40257A` 用高亮帧 offset/尺寸算矩形；版本号 "V3.11" (638,470) 样式 3 对齐 6

## 进入游戏链路（阶段 4）

```
主菜单点击 → 0x406DE7 newGameInit
  → 0x404E44 newGameDialog（选图/选人模态，三段状态机；见 404e44-new-game-dialog.md）
  → 0x401543 showLoadingScreen（Data.mkf[601] 全屏图）
  → 0x407AD2 loadMapData（MAP.MKF GND + MAPDAT.MKF）
  → 0x4190CF loadPanelUi（panel.mkf UI）
  → 0x4291D6 stockTick（12 只股票）
  → 0x415872 playIntro（AVI 跳过）
  → 0x401981 enterGameLoop（压入 0x417E26 游戏内事件处理器）
```

两个入口：主菜单按钮 0（原版开始游戏，gameMode=0）与按钮 4（时空之旅，gameMode=1）；
`byte_46CAFC` 非 0 时（游戏内重新开始）保留上次选人配置并 10 tick 后自动確定。

重写：`src/app/new_game.cpp` / `src/app/new_game_dialog.cpp` / `src/app/game_loop.cpp`
（游戏内面板与地图渲染待后续）。

## 首页选项功能（读档 / 设置）

### 读档对话框

| 原版 | 功能 | 重写 |
|------|------|------|
| `sub_403D74` | 扫描 SAVE0-5.DAT（version==38）、绘制槽位（日期/地图图标/玩家头像） | `loadDialog`（`src/app/load_dialog.cpp`） |
| `sub_40363A` | 事件：列表区悬停（(y-24)/72）、黄色高亮框、左键选槽、右键取消 | `loadDialogEventHandler` |
| `sub_402AC5` | 读档进入游戏：读取全部状态 + loadMapData | `loadGameFromSlot`（`src/app/new_game.cpp`） |
| `sub_45620F` | 空心矩形边框（高亮） | `drawRectBorder`（`src/render/blit.cpp`） |

资源：Data.mkf[520]（界面，帧 0 背景/帧 10 槽位/帧 2+4*mode+map 地图图标）、
Data.mkf[2]（12 帧 72x72 头像，帧号 = 玩家角色 ID）。
存档头：version(4) + datetime(4) + map(2) + mode(2) + players(4) + 玩家 104B×4（偏移 19 = 角色）。

### 设置对话框

| 原版 | 功能 | 重写 |
|------|------|------|
| `sub_411B53` | 加载 Data.mkf[3]、绘制标签/页元素、居中显示 | `settingsDialog`（`src/app/settings_dialog.cpp`） |
| `sub_4103A3` | 事件：16 组控件矩形 `dword_474B92`、滑块/复选/单选/列表/取消/確定 | `settingsEventHandler` |
| `sub_40FD49` | 绘制背景/滑块（帧 5）/复选（帧 9）/单选 | `redrawSettings` |
| `sub_40FC57` | 音乐列表（8 首曲目 + 红色高亮）+ 文本 | `redrawSettings` |
| `sub_411F80` | 写 RICH4.CFG（16B 设置 + 56B 键位） | `saveCfg` |

控件映射（settings[16] 索引 → 原版全局）：
| 索引 | 控件 | 操作 |
|------|------|------|
| 0 | 游戏速度滑块 | `(x-81)/16`，0-3 |
| 1 | 動畫過程开关（ctrl9） | `^= 1` |
| 2 | 音乐音量滑块 | `(x-89)/16+1`，0-4；ctrl10 切换 0/4 |
| 3 | 音效音量滑块 | 同上；ctrl11 切换 0/4 |
| 4 | 自動存檔开关（ctrl12） | `^= 1` |
| 5 | 单选（ctrl13-15） | 0-2 |
| - | 音乐列表（ctrl6） | 8 首曲目选择 |
| - | 页按钮（ctrl3-5） | page0：日期更改/熱鍵設定/遊戲說明；page1：重新遊戲/認輸投降/結束遊戲 |
| - | 取消（ctrl7）/確定（ctrl8） | 不保存 / 保存 CFG |

绘制细节（0x411B53）：
- 帧 0 背景；帧 5 滑块单位；帧 9 复选标记（0x40FD49）
- 页按钮文本（字号 20）画在帧 (page+10) 画布 (108,31)/(108,85)/(108,136)，
  帧 (page+10) 由 blitBackground 叠加到背景 (168,2)；
  文本 = `off_474A54[3*page+12..14]`：page0 = 日期更改/熱鍵設定/遊戲說明，page1 = 重新遊戲/認輸投降/結束遊戲
- 取消/確定文本（字号 20）画在帧 0 的 (224,328)/(296,328)
- 悬停高亮：页按钮帧 6（101x36）、按钮帧 3/4（62x30 按下效果）

### 游戏内系统菜单（page=1）

`settingsDialog(1)`（工具条按钮 1 `0x417D65` case 1）与主菜单设置共用同一对话框，
仅页按钮文本与行为不同（注意：游戏内 **ESC 默认绑定"取消指令"**，抬起时由键盘钩子
`0x401010` 合成右键，等同右键取消，不打开此菜单）：

| 按钮 | 原版 | 行为 |
|------|------|------|
| 重新遊戲 | `sub_411AA3` | 游戏日期 = 系统日期；`g_sceneRequest=1`（回主菜单） |
| 認輸投降 | `sub_411AE0` | 当前玩家出局 |
| 結束遊戲 | `sub_411B46` | `saveConfig` + `g_quitGame=1`（退出） |

- 确认框：`sub_453A32(320,200)` → `runModal(sub_45367E)`，data.mkf[440]（96x48，帧 0 背景 /
  帧 1 YES / 帧 2 NO）；左半 = YES 返回 1，右半 = NO 或右键返回 0（重写 `confirmDialog`，
  `src/app/confirm_dialog.cpp`）
- 确认后 `postModalExit(ctrl-2)`（1/2/3），由 `settingsDialog` 返回值分发到上述行为
- 嵌套模态返回后需恢复外层 timer 间隔（`runModal` 保存/恢复），否则游戏内 16ms tick 停止

### 选项子界面（页按钮 ctrl3-5）

**日期更改**（`sub_4119E3` → `runModal(sub_410AC3)`，重写 `src/app/date_dialog.cpp`）
- 帧 2（199x220）居中，**不透明 blit**（原版 `blitElementFullscreen` = `blitElementOpaque`，
  0 像素直接写入——星期图标为黑底白字，透明 blit 会透出底层造成花屏）
- 月份名 (48,32) + 年份 (133,30)（字号 15 粗体）
- 日历网格：x = 28 + 23×星期，y = 80，行距 18，x==166 换行；当日高亮 `fillRect(5345644)` + 白字
- 日历计算：`sub_451F8C`（自 1998-01-01 天数）+ `sub_4520A6`（星期 = (天数+4)%7，1998-01-01 周四）
- 控件表 `dword_474CE8`（8 组）与行为（**左列 = 月份选择器，右列 = 年份选择器**）：
  - case 0 月-1 (74,21)、case 1 月+1 (74,31)、case 2 年-1 (160,21)、case 3 年+1 (160,31)
  - case 4/5/6 重置/取消/確定 (9/72/134, 180)、case 7 日历点击（±10 x, ±8 y，Down 立即执行）
- 按下动画：LButtonDown 画按下态（上箭头帧 12 / 下箭头帧 13 / 按钮帧 14 + 文本 +1,+1），
  LButtonUp 执行动作并重绘（`dword_474D78` 记录按下控件）
- 日期打包 BYTE0=日, BYTE1=月, HIWORD=年；确认后写入 `dword_48BB50`/`dword_497160`

**熱鍵設定**（`sub_411A86` → `runModal(sub_411122)`，重写 `src/app/hotkey_dialog.cpp`）
- 帧 1（328x336）居中；每行 = 功能名 + 按键名两列：
  - **功能名**（`off_474ABC` 28 项）：左列 x=62 / 右列 x=208，行距 16，12 号白色 0xF0F0F0 带阴影；
    原版由 `settingsDialog`（0x411B53）初始化时预画到帧 1 画布，重写改为热键界面每次重绘
  - **按键名**：左列 x=132 项 0-14，右列 x=284 项 15-27
- 颜色：键位 0-7 青色 0x00F0F0，8-27 黄色 0xF0F000（style 1 阴影）
- 键位名：`byte_47EDFA` 表（78 项 VK→名称，名称区 0x4666B0-0x46677D）；格式 "CTRL-" + 主键名
- 交互：点击列表项（命中左列 x 105-160 / 右列 x 257-312，y 25-264，行高 16；
  仅键位 8-27 可编辑，1-based 索引与 `word_48BB0E[1..28]` 重叠；编辑中忽略列表点击）
  → 按键捕获（Ctrl → 0x1100；冲突时保持编辑态；捕获中该项显示清空）
  → 编辑中闪烁框 53x13（原版 250ms 定时器，重写为静态框）
- 按钮：100 重置（`unk_47EDC2` 默认表）/ 101 取消 / 102 確定（`memcpy word_497168` + `saveConfig`）
- SDL→VK 映射见 `sdlToVk`（字母/数字/功能键/OEM 键）

**遊戲說明**（`sub_411A96` → `sub_44EB39(-1,-1)`，重写 `src/app/help_dialog.cpp`）
- help.mkf[0] 框架（400x400 居中，12 帧；帧号 = 原版偏移/12 - 1）：
  帧 0 框架 / 帧 1 目录高亮条 66x33 / 帧 2 列表未选中 85x33 / 帧 3 列表选中 87x33 /
  帧 4/5 滚动箭头 / 帧 6/7 滚动按下 / 帧 8/9 继续阅读 / 帧 10/11 继续按下
- **初始 category=0**（打开即显示首分类内容，非目录模式）
- 8 分类目录：文字中心 (59, 73+36i)，命中区 (26..91, 58+36i..90+36i)；
  选中项重画高亮条（帧 1）后需**再次重画分类名**（原版 drawHelpPage 画两处：
  目录位置 + 内容区 (140,57)）
- 分类表 `off_4761B4`（名称/起始页/页数）：
  操作說明(1,1) 遊戲畫面(2,6) 遊戲指令(8,12) 房地產(20,3)
  特殊地點(23,16) 特殊人物(39,18) 卡片(57,30) 道具(87,13)
- 页面标题表 `off_4761B8`（99 项，已录入 `kCategoryTitles`）
- 内容页（`sub_44DFB4`），全部**不透明 blit**（原版 `blitElementFullscreen`）：
  - 页面列表：帧 3/2 @ (108, 78+34j)，标题 15 号粗体中心 (150, 94+34j)；
    命中区 (108..192, 78+34j..110+34j)，仅可见项
  - 滚动箭头：帧 4/5 @ (170,43)/(170,59)，仅 pageCount > 8；**按下态帧 6/7**；
    LButtonDown 画按下态，LButtonUp 执行（上/下滚动一屏且 page 同步位移，scroll 与 page 绑定）
  - 正文：12 号，位置 (232, 90+18×行)，最多 14 行；遇 '@' 单独行（分页符）停止并显示
    继续阅读按钮（帧 8/9 @ (322,48)/(343,48)，**按下态帧 10/11**）
  - 屏标记 `dword_4762B8`：**0 或 '@' 行索引**（非绘制起点！）；绘制时若为 '@' 则从其后一行起；
    绘制循环**不更新**该标记
  - 继续阅读（`sub_44DD9F`）：下一屏从标记处跳过 '@' 前进 14 行（或遇 '@' 停）/ 上一屏
    回退 14 行（或遇 '@' 停），结果写回标记；翻屏**不重算** dword_48C5EC（按钮状态保持）
  - 翻页边界：前进到末尾时**不更新标记**（原版 `v2 == size` 直接返回）；首屏上一页 /
    末屏下一页为无效操作——内容保持不变，但**仍需重绘**以复位按下动画（重写实现细节）
- 正文来源 help.mkf[start+page]，BIG5 0x00 分隔行 → UTF-8（`big5ToUtf8`，Win32 API）

### RICH4.CFG 默认值（`sub_411E8F`）

CFG 不存在时：设置区 = `01 01 04 04 01 01 00...`（byte_497158=1/497159=1/49715A=4/
49715B=4/49715C=1/49715D=1），键位 = `unk_47EDC2` 默认表（末项 0x1151 = Ctrl+Q）。
重写：设置区存于 `GameState.settings[16]`（`game_init` 读 CFG 覆盖），
`saveCfg` 直接写 设置+键位（不再依赖现有 CFG，避免首次保存清空键位）。

### 嵌套模态修复

`runModal`（`src/app/event_stack.cpp`）在返回前消费 `exitRequested`：
原版 WM_USER+2 被局部消息泵取走，不影响外层模态；重写时若不清理，
内层对话框退出会误退出外层（设置保存后误入读档流程）。

## MKF 索引修正（本轮发现）

索引表每一项都是资源头偏移（含最后一项），资源数 = 索引项数：
- Data.mkf 602（原 601）、合计 2673（原 2666）
- 依据：`index[601]` 指向 614400B 资源，原版 `0x401543` 读取 `sub_450441(handle, 601)`
- 已同步修正 `tools/mkf.py`、`src/resource/mkf.cpp`、`docs/formats/mkf.md`
