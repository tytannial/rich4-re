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
| `sub_4024C0` | 光标包围矩形 | `Cursor::getRect` |

## 主菜单

- 资源：Data.mkf[1] SMP（11 帧）：帧 0 = 640x480 背景画布，帧 (1,2)..(9,10) = 5 按钮普通/高亮
- 按钮坐标 `word_46CB28`（[RE 0x46CB28]）：0:(190,380) 1:(328,380) 2:(468,378) 3:(328,450) 4:(62,380)
- 语义：0 新游戏 / 1 读档 / 2 设置 / 3 退出 / 4 新游戏（模式 1）
- 悬停：`sub_40257A` 用高亮帧 offset/尺寸算矩形；版本号 "V3.11" (638,470) 样式 3 对齐 6

## 进入游戏链路（阶段 4）

```
主菜单点击 → 0x406DE7 newGameInit（JUMP.MKF 地图预览 + 选人[跳过] + 玩家初始化）
→ 0x401543 showLoadingScreen（Data.mkf[601] 全屏图）
→ 0x407AD2 loadMapData（MAP.MKF GND + MAPDAT.MKF）
→ 0x4190CF loadPanelUi（panel.mkf UI）
→ 0x4291D6 stockTick（12 只股票）
→ 0x415872 playIntro（AVI 跳过）
→ 0x401981 enterGameLoop（压入 0x417E26 游戏内事件处理器）
```

重写：`src/app/new_game.cpp` / `src/app/game_loop.cpp`（骨架，面板与地图渲染待后续）。

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
| - | 页按钮（ctrl3-5） | 日期更改/熱鍵設定/遊戲說明（子界面待实现） |
| - | 取消（ctrl7）/確定（ctrl8） | 不保存 / 保存 CFG |

绘制细节（0x411B53）：
- 帧 0 背景；帧 5 滑块单位；帧 9 复选标记（0x40FD49）
- 页按钮文本（"日期更改/熱鍵設定/遊戲說明"，字号 20）画在帧 (page+10) 画布 (108,31)/(108,85)/(108,136)，
  帧 (page+10) 由 blitBackground 叠加到背景 (168,2)
- 取消/確定文本（字号 20）画在帧 0 的 (224,328)/(296,328)
- 悬停高亮：页按钮帧 6（101x36）、按钮帧 3/4（62x30 按下效果）

### 选项子界面（页按钮 ctrl3-5）

**日期更改**（`sub_4119E3` → `runModal(sub_410AC3)`，重写 `src/app/date_dialog.cpp`）
- 帧 2（199x220）居中；月份名 (48,32) + 年份 (133,30)
- 日历网格：x = 28 + 23×星期，y = 80，行距 18，x==166 换行；当日高亮 `fillRect(5345644)`
- 日历计算：`sub_451F8C`（自 1998-01-01 天数）+ `sub_4520A6`（星期 = (天数+4)%7，1998-01-01 周四）
- 8 控件：月±1/年±1（帧 12/13 箭头）、重置/取消/確定（帧 14 + 文本）、日历点击（±10 x, ±8 y）
- 日期打包 BYTE0=日, BYTE1=月, HIWORD=年；确认后写入 `dword_48BB50`/`dword_497160`

**熱鍵設定**（`sub_411A86` → `runModal(sub_411122)`，重写 `src/app/hotkey_dialog.cpp`）
- 帧 1（328x336）居中；28 项键位名（左列 x=132 项 0-14，右列 x=284 项 15-27，行距 16）
- 颜色：键位 0-7 青色 0xF0F0，8-27 黄色 0xF0F000
- 键位名：`byte_47EDFA` 表（78 项 VK→名称）；格式 "CTRL-" + 主键名
- 交互：点击列表项（仅键位 8-27 可编辑，1-based 索引与 `word_48BB0E[1..28]` 重叠）
  → 按键捕获（Ctrl → 0x1100；冲突检测：候选值与其余键位比较）
- 按钮：100 重置（`unk_47EDC2` 默认表）/ 101 取消 / 102 確定（`memcpy word_497168` + `saveConfig`）
- SDL→VK 映射见 `sdlToVk`（字母/数字/功能键/OEM 键）

**遊戲說明**（`sub_411A96` → `sub_44EB39(-1,-1)`，重写 `src/app/help_dialog.cpp`）
- help.mkf[0] 框架（400x400 居中，12 帧）；8 分类目录（位置 (59, 73+36i)）
- 分类表 `off_4761B4`（名称/起始页/页数）：
  操作說明(1,1) 遊戲畫面(2,6) 遊戲指令(8,12) 房地產(20,3)
  特殊地點(23,16) 特殊人物(39,18) 卡片(57,30) 道具(87,13)
- 页面标题表 `off_4761B8`（99 项，已录入 `kCategoryTitles`）
- 内容页（`sub_44DFB4`）：帧 1 标题栏 + 页面列表（帧 2/3）+ 正文（232, 90+18×行，最多 14 行）
- 正文来源 help.mkf[start+page]，BIG5 0x00 分隔行 → UTF-8（`big5ToUtf8`，Win32 API）

### 嵌套模态修复

`runModal`（`src/app/event_stack.cpp`）在返回前消费 `exitRequested`：
原版 WM_USER+2 被局部消息泵取走，不影响外层模态；重写时若不清理，
内层对话框退出会误退出外层（设置保存后误入读档流程）。

## MKF 索引修正（本轮发现）

索引表每一项都是资源头偏移（含最后一项），资源数 = 索引项数：
- Data.mkf 602（原 601）、合计 2673（原 2666）
- 依据：`index[601]` 指向 614400B 资源，原版 `0x401543` 读取 `sub_450441(handle, 601)`
- 已同步修正 `tools/mkf.py`、`src/resource/mkf.cpp`、`docs/formats/mkf.md`
