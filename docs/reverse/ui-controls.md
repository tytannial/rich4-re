# 游戏内 UI 控件指南

> 本文件汇总《大富翁4》重写中**各类可复用 UI 控件**的原版依据、实现方式与踩坑经验。
> **约定：每次新实现或发现一种控件，都要按末尾「新增控件模板」补充到本文件**，避免重复逆向与风格不一。
> 渲染原语见 `include/game/render/blit.h` / `surface.h` / `text.h` / `cursor.h`；模态框架见 `event_stack.h`。
> 全部控件在原版都是 `sub_4XXXXX` 窗口过程 + `runModal` 局部消息泵 + Data/Panel/Help/JUMP.mkf 资源的组合；
> 重写统一迁移到 SDL 事件 + `runModal(handler,user,tickMs)`，故各节"交互"列都隐含
> **左键命中→按下态、抬起→执行、右键/ESC→取消** 的原版约定（见 §24）。

## 0. 控件总索引（一览）

| 控件 | 原版代表 | 重写位置 | 首次/主要使用 | 关键经验锚点 |
|------|----------|----------|----------------|--------------|
| 浮雕按钮（按下下沉） | 0x451B9E | `hotkey/stock:pressDown` | 热键、股市 | §3.1 |
| 帧切换/悬停高亮按钮 | 0x4103A3/0x415D31 | `settings/game_panel/map` | 设置页、工具条 | §3.2 |
| YES/NO 半区确认框 | 0x453A32/0x45367E | `confirm_dialog.cpp` | 系统菜单、认购 | §3.3 |
| 步进 ±按钮 | 0x41DDA9 | `ai_dialog.cpp` | 託管AI 资金比 | §3.4 |
| 图标行选择 | 0x43FAE4 | `facility_dialog.cpp` | 設施類別 | §3.5 |
| 复选/单选/静音灯 | 0x40FD49 | `settings/ai_dialog.cpp` | 设置、AI | §4 |
| 滑块/拖动/可拖面板 | 0x4103A3/0x41DE44/0x452C02 | `settings/ai/number_input` | 音量、股数 | §5 |
| 选框/多层边框/底色高亮 | 0x45620F/0x4561BE | 各面板 | 列表/表格/悬停 | §6 |
| 变暗/调色 LUT | 0x4552E7 | `new_game_tables.cpp` | 全局 | §7 |
| 文本/对齐/大字阴影 | 0x44FABC | `text.cpp` | 全局 | §8 |
| 列表族（下拉/多级/槽/行） | 0x44E40B/0x404E44/0x40363A | `help/new_game/load/save/stock` | 帮助、选图、存档、股市 | §9 |
| 表格（列+状态色） | 0x4297F7 | `stock_market_dialog.cpp` | 股市行情 | §10 |
| 图表（折线/饼/标尺） | 0x429D65 | `stock_market_dialog.cpp` | 走勢圖 | §11 |
| 金额/数字格式化 | 0x452793/0x429691 | `game_panel/stock` | 全局 | §12 |
| 数字输入框 | 0x453544/0x452C02 | `number_input_dialog.cpp` | 买卖/认购 | §13 |
| 键位捕获编辑 | 0x411122 | `hotkey_dialog.cpp` | 热键设置 | §14 |
| 提示/消息/气泡/拾取/卡片获得 | 0x440CAC/0x417559/0x441F73 | `message/object_tip/turn_system` | 全局 | §15 |
| HUD 常驻（工具条/页签/日历/小地图/前进面板） | 0x417E26/0x417191 | `game_panel/game_loop` | 游戏内 | §16 |
| 头像/精灵选择网格 | 0x40423C/0x404E44 | `new_game_dialog.cpp` | 选人 | §17 |
| 动画（转盘/进场/闪烁/棋子/FLC/事件FLC 阻塞播放） | 0x43F7C6/0x404E44/0x45144F | `roulette/new_game/game_loop/turn_system` | 事件、开局 | §18 |
| 资源绘制（SPR索引/区域/RAW/灰度/暗化） | 0x456512/0x4553DA | `number_input/blit.cpp` | 全局 | §19 |
| 全屏面板底图/帧布局 | — | `UiImage` | 各面板 | §20 |
| 软件光标 | 0x4021F8 | `cursor.cpp` | 数字框 | §21 |
| 模态框架 | 0x4018E7 | `event_stack.cpp` | 全部对话框 | §22 |
| 交互音效 | 0x4542CE | `audio.cpp` | 全部 | §23 |
| 交互门控 | 0x4186BE/0x46CAFD | `game_loop.cpp` | 游戏内 | §24 |
| 选择面板框架（头像格/浮动消息/语音/音乐栈） | 0x43CAAB/0x43DA27/0x44EC30..44EE18 | `float_message/jail_dialog` | 保释/出院（P2） | §25 |

## 1. 渲染原语速查（`render/blit.h` / `surface.h`）

| 原语 | 原版 | 用途 |
|------|------|------|
| `blitElementOpaque(dst, frame, x, y)` | 0x455B3A | 不透明整元素 blit（面板底图、图标、框架） |
| `blitElement(dst, frame, x, y, clip)` | 0x455C52 | 色键(0 透明) blit（精灵、文字层、半透明叠加） |
| `blitElementRegion` / `blitElementRegionOpaque` | 0x455FD9 / 0x455E24 | 源区域子矩形 blit（拷贝背景、局部覆盖） |
| `blitSpriteFrame` / `blitSpriteFrameClipped` | 0x45663E / 0x456770 | 8bit SPR 帧 + 调色板（棋子、角色动画、地图精灵） |
| `drawRectBorder(dst, x, y, w, h, color555)` | 0x45620F | 空心矩形框（选中框/悬停框/当日框） |
| `scaleSurfaceChannels(dst, x, y, w, h, table)` | 0x4552E7 | 区域按 32 字节 LUT 调色（变暗/浮雕阴影） |
| `convertImageChannels(dst, src, n, table)` | 0x4552B7 | 整块缓冲按 LUT 调色（地图预览暗化滚动源） |
| `grayscaleImage(pixels, n)` | 0x4553DA | 灰度化（AI 遗留头像不可选） |
| `blitScrolledMap(dst, src, offset)` | 0x456180 | 每行 1280 环绕拷贝（地图预览水平无限滚动） |
| `fillRect(dst, x, y, w, h, color555)` | 0x4561BE | 实心矩形（列表高亮条、状态底色、闪烁反白） |
| `setClipRect(l,t,r,b)` | 0x4861B8 | 全局裁剪矩形 |
| `rgb888To555(c)` | 0x45523E | 绘制前颜色转换（表中色值都是 RGB888） |
| `runModal(app, handler, user, tickMs)` | 0x4018E7 | 阻塞模态事件循环（见 §22） |

**经验**：
- 原版画到"元素画布"再 blit（`sub_456418`/`sub_4563F5` 目标常为 `dword_48A08C`=640×480 后台缓冲）；
  重写直接画到 `app.surface()`，每帧全量重绘，故无需原版"保存/恢复背景"。
- 色值一律以 **RGB888 常量**书写再 `rgb888To555()`；原版栈上 `push 16776960` 之类即 RGB888 十进制。

## 2. 坐标基准与命中检测

原版所有面板命中都在**自身坐标**里比较；重写沿用，务必分清三套基准：

| 基准 | 代表 | 用法 |
|------|------|------|
| **屏幕绝对坐标** | 股市 `kBtns`/`hitRow`、工具条 `x/40`、小地图箭头 | 事件 x/y 直接比较（面板固定在 0,0 或已知绝对位） |
| **相对对话框坐标**（减去 `originX/originY`） | settings/date/help/hotkey/new_game 面板 | `rx=x-ox; ry=y-oy` 再查控件表；`origin` 由 `(320-帧宽/2, 240-帧高/2)` 居中 |
| **索引命中图**（像素值=控件 ID） | 数字框 `panel.mkf[22]`、前进面板 `panel.mkf[8]` | `ctrl = mask[ry*W+rx] (+偏移)`，无需矩形表 |

**通用 hitTest**（`settings/date/ai:hitTest`）：遍历 `kControls[]` 矩形数组返回下标。
`ai_dialog` 用**开区间** `x>left && x<right`（原版 `cmp` 语义），其余多用**闭左开右** `>=left && <right`——移植时对齐原版边界，差 1px 会导致边缘控件失灵。

**像素掩码拾取**（地图物件，`object_tip`/0x40A9D7）：不靠矩形，用物件 SPR 非零像素判定命中，
逆序遍历 `mapHitRegions`（最后绘制=最上层）取首个命中；避免矩形盖住道路格误选（0x409B18）。

## 3. 按钮类

### 3.1 浮雕按钮（按下下沉）★

**原版**：`highlightRect 0x451B9E`。按住时把矩形**内容向右下平移 N px**，再把**顶行 + 左列**
`scaleSurfaceChannels` 变暗，形成"凹陷"浮雕；抬起由 `sub_451D4E` 恢复。

**重写实现**（`pressDown`，每帧全量重绘、抬起即复原，无需保存/恢复背景）：
```cpp
void pressDown(Surface& dst, int l, int t, int r, int b, int SHIFT, const uint8_t* tbl) {
    const int w = r - l, h = b - t;
    uint16_t* p = dst.pixels();
    for (int row = h - 1 - SHIFT; row >= 0; --row) {            // 自底向上，避免覆盖
        uint16_t* d = p + (t + row + SHIFT) * Surface::kWidth + (l + SHIFT);
        const uint16_t* s = p + (t + row) * Surface::kWidth + l;
        std::memmove(d, s, (w - SHIFT) * sizeof(uint16_t));     // 内容右下平移 SHIFT px
    }
    scaleSurfaceChannels(dst, l, t, w, SHIFT, tbl);             // 顶行
    scaleSurfaceChannels(dst, l, t, SHIFT, h, tbl);             // 左列
}
```

**触发时机**：**仅在按住时**（`pressed` / `pressedBtn >= 0`）绘制；悬停（hover）**不**下沉。
`BUTTONDOWN` 记 pressed + 重绘，`BUTTONUP` 清 pressed + 执行动作 + 重绘。

**强度调节**（按钮越大越可夸张）：

| 场景 | 平移 | 阴影宽 | 变暗表 | 位置 |
|------|------|--------|--------|------|
| 热键小按钮/列表项 | 1px | 1px | `kChannelHalf` | `hotkey_dialog.cpp:pressDown` |
| 股市顶部大按钮 | 2px | 2px | `kChannelThird` | `stock_market_dialog.cpp:pressDown` |

**经验**：
- 原版 `word_46CAEC=640` 是 surface 句柄/宽，**不是颜色**；选框用 `drawRectBorder`，下沉用 `pressDown`，勿混。
- 平移方向决定"凹陷"观感：内容右下移 + 顶/左变暗 = 按下；反向会变"凸起"。
- 增量重绘需自行保存/恢复背景（对齐 `sub_451D4E`）；全量重绘面板天然抬起即复原。

### 3.2 帧切换 / 悬停高亮按钮

不用浮雕，而是**切换绘制帧**表达状态。三种子型：

| 子型 | 帧规律 | 例子（原版） |
|------|--------|--------------|
| 悬停切整张高亮帧 | 背景帧自带按钮，悬停换帧 | 设置页按钮帧 6、取消/確定帧 3/4（0x4103A3） |
| 正常/按下成对帧 | `frame = pressed ? 按下 : 正常` | 帮助滚动帧 4/5→6/7、继续阅读 8/9→10/11（0x44E40B）；小地图箭头 20/21→18/19（0x416E6D） |
| 索引帧对（普通/高亮） | `frame = (i==hover)? 高亮基+ i : 普通基 + i` | 顶部工具条普通帧 1+i、高亮帧 12+i（0x415D31/`panel.mkf[1]`） |

**经验**：`DOWN` 画按下帧、`UP` 才执行动作（帮助滚动条即此模式）；工具条/页签则 `DOWN` 记 `pendingAction`、`UP` 分发（§16）。

### 3.3 YES/NO 半区按钮（确认框）

`confirm_dialog`（0x453A32/0x45367E，`data.mkf[440]`）：背景帧 0、YES 高亮帧 1、NO 高亮帧 2。
命中只按**水平中线** `x < ox + w/2` 分左右半区，不用矩形表。ESC/右键=NO(0)，左键=按半区返回 1/0。
带文本变体 = `askDialog 0x440BA8`：先 `blitElement(data.mkf[517] 帧5)` 提示框 @(cx,140)，再 `drawText(...,align 4)` 居中（§15）。

### 3.4 步进 ±按钮

`ai_dialog`（0x41DDA9）：现金/股票比例条左右 ±按钮，按下态画画布帧 4/5 到 `rect+1`；
`UP` 对值 `±10` 并 `clamp 0..100`（原版字节回绕，重写显式限幅）。确定/取消按钮按下用
`scaleSurfaceChannels(…,kChannelDim)` **变暗 40×56** 代替换帧（§7）。

### 3.5 图标行选择

`facility_dialog`（0x43FAE4）：N 个等宽图标一行，`x = x0 + step*i`；命中 `hover=(x-x0)/step`。
悬停画**多层 `drawRectBorder`**（原版 68/66/64 三层，色 `word_46CAEC` 绿 0x0280）+ 名称文本；
左键返回 `hover`，右键/ESC 返回 -1（§6、§15）。

## 4. 开关与选择类

共用"把**勾选标记帧**画到目标位置"表达状态，位置/数量决定子型：

| 子型 | 实现 | 例子 |
|------|------|------|
| 复选框 | `if(on) blitElement(帧check, x, y)` | settings 動畫/自動存檔 `帧9 @ (98,50)/(98,146)`（0x40FD49）；AI 卡片/道具 `帧3 @ (288,y)`（0x41DB91） |
| 单选（radio） | 标记帧画在 `kRadioY[selected]`，互斥赋值 | settings `case13-15 → settings[5]=i-13`；AI 個性 `case4-6 → persona` |
| 静音指示灯 | 音量非 0 叠加标记帧，为 0 不画 | settings 音樂/音效 `帧9 @ (66,82)/(66,114)`（0x40FD49） |
| 位标志开关 | `flags ^= 1/2/4`（按位） | settings `^=1`；AI 托管 `alive ^= 4`、卡片/道具 `^=1/^=2`（0x41DDA9） |

**经验**：复选/单选都**共用同一标记帧**，区别只在"多项独立"还是"一组互斥单选某项"。
帧 9（16×16 复选标记）、帧 5（15×16 滑块单位）来自 `data.mkf[3]` 元素表（帧头 = 基址+12+12·帧号）。

## 5. 滑块与拖动类

四种拖动控件，共同点：`BUTTONDOWN` 置 `dragControl`、`MOUSEMOTION` 更新值、`BUTTONUP` 清除。

| 子型 | 换算 | 绘制 | 位置 |
|------|------|------|------|
| 离散档位条（点击+拖动） | `v = (rx - base)/step`（+偏置）clamp | 画 `v(+1)` 个滑块单位帧，间隔 `step` px | 速度(帧5 ×`settings[0]+1` @81,步进16)、音量(×`settings[2]` @89)（0x4103A3） |
| 连续比例条（拖动） | `v = 10*((x - barX)/8)` clamp 0..100（步进10） | 背景 `blitElementRegionOpaque` + `fillRect` 红格数=`v/10` | AI 资金比（0x41DE44，`sub_41DA61` 背景 (208,265)80×57→(310,327)，格 x=311+8i 7×22） |
| 数字框滑块（阈值表） | `step` 由 `kSliderSteps[34]` 反查，`v = max*(step/33)` | 轨道帧1 + 帧0 左段擦除比例宽 | numberInputDialog（0x452C02，§13） |
| 可拖动面板 | 记录抓取偏移 `dragOff = mouse-panel` | 每帧 `panel = mouse - dragOff` | 数字框（`pressedKey==1` 时） |
| 小地图拖拽/跳转 | `视口 = (761856·m)>>16` clamp 220..2084 | 设 `manualView` +（NEW）平滑插值 `startScroll` | game_loop（0x417E26，§16） |

**经验**：
- 原版音量/速度条**用帧 5 单位图形重复 N 次**，不是渐变；N 直接是设置值。
- 拖动中 `MOUSEMOTION` 要**立即重绘**（比例条实时跟随）；数字框滑块还每步播 `kSfxSlider`。
- 边界换算（`-81`、`-10`、`/16`、`/8`）必须逐字照抄原版，差一像素整条错位。

## 6. 选框与高亮类

| 型 | 实现 | 例子 |
|----|------|------|
| 单层白/红框 | `drawRectBorder(...,0x7FFF 或 0xF800)` | 股市行框、日历当日框、小地图当前玩家白框 |
| 双层加粗框 | 两次 `drawRectBorder`（+1,+1 且 -2,-2） | 存档槽悬停黄框 `0xFFFF00`（`save/load_dialog`，0x45620F） |
| 三层绿色框 | 三次 68/66/64 | 設施類別图标（0x43FAE4，§3.5） |
| 行/格底色 | `fillRect(x,y,w,h,color)` 覆盖后再画文本 | 音乐当前曲目红条、下拉列表高亮条 `0xAA0000`、股市涨/跌停 `fillRect` 底色 |
| 闪烁反白格 | `if(editing && blink) fillRect(白)` | 热键待修改键位 53×13（0x411122，§14） |

**经验**：`fillRect` 色走 `rgb888To555`；文本底色高亮后要把前景切成白/黑保证可读
（股市涨停→白字、跌停→黑字，§10）。

## 7. 变暗/调色表（`scaleSurfaceChannels` / `convertImageChannels`）

32 字节 LUT，按 5bit 通道值查表重组 RGB555（`new_game_tables.cpp`）：

| 表 | 映射 | 用途 |
|----|------|------|
| `kChannelHalf` | i→i/2（31→15） | 标准变暗：按钮阴影、已选头像(-16)、地图预览暗化 |
| `kChannelThird` | i→i·0.35（31→11） | 更暗：大按钮夸张下沉、悬停名字条(-20) |
| `kChannelDim` | i→i·0.6（31→19） | 轻度变暗：AI 确定/取消按下、悬停条 |

**区域变换** `scaleSurfaceChannels(dst,x,y,w,h,tbl)` 就地改；**整块** `convertImageChannels(dst,src,n,tbl)`
用于生成滚动源（不污染只读原图）。灰度禁用态用 `grayscaleImage`（AI 遗留头像，§17）。

## 8. 文本绘制（`setFont` / `drawText`）

`setFont(size, fgRgb888, bgRgb888, style, spacing)`；`drawText(dst, utf8, x, y, align)`。

**align 语义**（`text.cpp`，与原版 `sub_44FABC` 第 6 参一致）：

| align | 效果 |
|-------|------|
| 0 | 左上（基准点=左上角） |
| 1 | 右对齐、顶对齐（`sx-=boxW`） |
| 2 / 3 / 4 | 水平居中（3=竖排，4=居中含自动行） |
| 5 | 垂直居中 |
| 6 | **右对齐 + 垂直居中**（数字/金额列标准） |
| 7 | 水平居中、底对齐 |

**style 位**：`kTextStyleShadow=1`、`Bold=2`、`Outline=4`。
- 原版 `setTextFont(...,3,1)` = 阴影+粗体（常用 16/20 号标题）。
- **有底色文字**用 style 2（无阴影），**大字**（"本日休市"、大数字日历）用 style 2 或 6。

**大字阴影 = 偏移画两遍**：先深色 @(x+4,y+4) 再浅色 @(x,y)（`stock:drawClosed` 72 号、前进面板）。

**经验**：
- 数字/金额列原版多用 `align 6`；误用 `align 1` 会顶对齐、文字下移半字。表头/名称用 `align 2` 居中。
- 多行文本原版是**逐行 `drawText`**（提示框行距 18、正文行距 14~18），重写按 `\n` 切分逐行画（§15）。

## 9. 列表类

### 9.1 下拉选项列表（展开—悬停—应用）

`new_game_dialog`（0x40482C）：选项行 `BUTTONDOWN` → `openList=row`，画列表背景帧（`kListBgFrame`）；
`MOUSEMOTION` → `listHover=(y-top)/rowH`；命中项 `fillRect(0xAA0000)` 高亮 + `align 6` 文本；
再次点击某项 → 写回 config、`openList=-1`；右键/点击外部 → 收起。

### 9.2 多级目录 + 页面列表 + 滚动 + 继续阅读（帮助）

`help_dialog`（0x44E40B/0x44DFB4/0x44DD9F，`help.mkf[0]` 12 帧）三栏联动：
- **目录列表**：8 分类固定列，选中行叠高亮帧 1 并重画名；
- **页面列表**：可见 8 项，选中帧 3/未选帧 2；页 > 8 时显示**滚动箭头**（帧 4/5 正常、6/7 按下），
  `page -= /+= 8` 且 `scroll` 夹在 `[0, pageCount-8]`；
- **正文**：`drawPageText` 从 `pageMark`（0 或 `@` 分页行）起最多 14 行，遇 `@` 停 → 置 `hasMore`；
  **继续阅读**按钮（帧 8/9 正常、10/11 按下）→ `sub_44DD9F` 前进/回退整屏写回 `pageMark`。

**经验**：按下动画（画 6/7/10/11）只重绘**不重算 `hasMore`**（`updateMore=false`），否则末屏会误清按钮。
`@` 行 = BIG5 0x40 分页符，`0x00` 分隔行。

### 9.3 存档槽列表

`save/load_dialog`（0x4039C2/0x40363A，`data.mkf[520]`）：N 个 72px 高槽纵排，
命中 `index=(y-first)/72`；每槽画槽位帧 10 + 地图图标帧 `4·mode+map+2` + 头像行 + 日期文本（§12）；
悬停画**双层黄框**（§6）。左键选槽返回 index，右键返回 -1。

### 9.4 悬停跟随 + 持续选中框（股市行）

`drawSelection`：悬停行与选中行各画 `drawRectBorder`（白 0x7FFF），二者可同色且并存（§6、§10）。

## 10. 表格类（列 + 状态色）

`stock_market_dialog`（0x4297F7/0x4296C1，`panel.mkf[75]` 帧 0/1 双视图）：
固定 12 行 × 多列，`kDataRow0 + kRowH·i` 排布；每列独立 `drawText(align 6)`。

| 元素 | 做法 |
|------|------|
| 视图切换 | `view ^= 1` → 换全屏背景帧（持有股數表 / 股價表），列头随之变 |
| 状态色 | `stockStatus` 0涨/1涨停/2跌/3跌停/4平 → 字色 `priceColor`/`deltaColor` |
| 涨/跌停底色 | `fillRect` 红/绿底 + 白/黑字（style 2 无阴影） |
| 第一大股东 | `fillRect` 白底 + 黄字（`kYellow`） |
| 二次点击行 | `selStock==row` → 打开走勢圖子模态（§11） |

**经验**：表格用**全量重绘 + 每帧重画选中/悬停框**，不做增量；列 x 坐标是原版魔法数，逐列对齐。

## 11. 图表类（走勢圖，`stock:renderChart` 0x429D65）

`panel.mkf[75]` 帧 2 = 587×375 框图画到 (26,52)，其余手绘：
- **折线**：`drawLine`（Bresenham，`kWhite555`）逐段连 144 期历史；
  纵轴自适应 `scale = span/mid<=0.3 ? 109/(mid·0.6) : 109/span`（窄幅放大），左端画 `hi/lo` 标尺文本；
- **饼图**：底"阴影"椭圆（`cBlue`，y+7）+ 顶扇形——按 `atan2` 角度分区（持股扇=蓝、其余=红），
  `frac=min(1, mine/10000)`；无持股→全蓝；
- **公司图标**：`kStockIconTable[mode][map][costType]`（0x47552C）取 `panel.mkf[75]` 帧，仅 80×112 帧才画。

**经验**：走势图是**独立子模态**（`stockChartDialog`），任意点击/按键退出；历史环形缓冲 `stockHistory[s][idx%144]`
按 `turnCounter` 起算，全 0 期跳过。

## 12. 金额 / 数字格式化

- `formatMoney`/`fmtMoney`（原版 `sub_452793`）：千分位逗号；`'$'` 前缀由调用方加；负数版（stock）先记符号再反序插逗号。
- 价格格式档 `sub_429691`：`<15→%.2f`、`<150→%.1f`、`else %.0f`；涨跌用 `%+.nf`（同档）；走势图均值/高低用无符号档。
- 十进制转换原版 `sub_457D61`；重写各面板直接 `snprintf`，各带局部 `fmt*` 副本。

## 13. 数字输入框（`numberInputDialog`，0x453544/0x452C02）

综合控件范本，集索引命中 + SPR 绘制 + 滑块 + 可拖动 + 快照叠加：
- 资源：`panel.mkf[21]`（SPR：背景/按键/数字字形）、`panel.mkf[22]`（128×192 索引图，像素值=按键 ID）。
- 绘制：`blitSprRegion`（SPR 索引 + RGB555 调色板，对齐 0x456512，不透明含索引 0；§19）；
  数字字形帧 16..25='0'..'9' 从右到左每字符 12px（≤9 位）；滑块轨道帧 1 + 帧 0 擦左段；按下态画帧 `keyId`。
- 控件（`kKeyRect`/`kKeyChar`/`kSliderSteps` 原版表）：0-9、退格、清除、最大、确定、滑块、面板可拖。
- 交互：确定/回车→`atoi`；取消（ESC/右键）→ -1；`maxValue` clamp。
- 进入 `cursor().select(27)`、退出 `select(41)`（§21）。

**叠加显示关键**（§22）：作为父面板上的子模态，`drawAll` **首次捕获进入时 surface 快照、每帧恢复**再画自身，
否则每帧 `renderGameFrame` 会擦除父面板（股市）。

## 14. 键位捕获编辑（`hotkey_dialog`，0x411122）

`runModal(..., 250ms)` 驱动：
- 点列表项（仅 index ≥ 8 可编辑）→ `editing=i`、`savedValue=旧值`、清空该项；
- `kModalTimerEvent` → `blink=!blink`，编辑项 `fillRect(白 53×13)` 反白闪烁（§6）；
- `KEYDOWN`（捕获中）→ CTRL 记 `mod=0x1100`；主键 `candidate=work|VK`，**冲突检测**（与其它项相同则忽略、保持捕获），否则写回退出捕获；
- 右键：捕获中→恢复 `savedValue`；空闲→退出；按钮"原始設定/取消/確定"按下用 `pressDown`（§3.1）。
- 键名表 `kKeyNames`（0x47EDFA，78 项 VK→名）、功能名表 `kHotkeyLabels`（0x46362E，28 项）；颜色 `i<8` 青、`i≥8` 黄。

## 15. 提示与消息类

| 控件 | 原版 | 特征 |
|------|------|------|
| 纯消息框 `showMessage` | 0x440CAC | `data.mkf[517]` 帧 5 提示框 @(220,140) + 居中 `align 4` 文本；`runModal(...,16ms)` **吞所有输入**，`kModalTimerEvent` 计时到自动退出（原版 `sub_4528B9` 纯延时） |
| 确认框/askDialog | 0x453A32/0x440BA8 | 半区 YES/NO（§3.3），带文本变体先画提示框再居中 |
| 物件信息气泡 `object_tip` | 0x417559 | 像素掩码拾取（§2）取最上层 → 生成多行文本 → **方向感知**画框 `data.mkf[517]` 帧 0..3（`v5 = (y-40<框高)?+1 | (x>440-框宽)?+2`，帧自带方向偏移），逐行 `align 2`、行距 18 |
| 悬停信息条 | 0x41DB91 等 | 悬停行画变暗条 + 居中名（§4/§7） |
| 卡片获得显示 `showCardGet` | 0x441F73 | 卡片图 `data.mkf[570+id]`（165×256 **无头 RGB555** @(138,200) 不透明拷贝）+ 帧 5 提示框 @(220,129) + 文本；音效槽 23；`runModal(...,16ms)` 停留 1500ms 吞输入（重写逐帧 `renderGameFrame` 免背景保存/恢复） |

**提示框族资源**：`data.mkf[517]`（`g_tipFrame`，`st.estateTiles`）——帧 4 设施图标底、**帧 5 通用提示框**、
帧 0..3 方向气泡、帧 18..21 小地图旋转箭头。

## 16. HUD 常驻控件（`renderGameFrame`/`renderGamePanel`，0x417E26/0x416E6D…）

右侧面板按 `settings[5]&3`（byte_49715D）三布局组合下列模块：

| 模块 | 原版 | 实现要点 |
|------|------|----------|
| 顶部工具条 | 0x415D31（`panel.mkf[1]`） | 背景帧 0 + 11 按钮：普通帧 1+i、悬停帧 12+i @`(40i+20,20)`；命中 `x/40`（§3.2） |
| 玩家信息条/面板 | 0x4166F8/0x415F69（`panel.mkf[0]` 帧 4/0） | 颜色条 `fillRect` + 棋子帧 0 + 同盟头像帧 2 + 页签文本（4 页签，阶段 2 才联动） |
| 日历 | 0x4169BC（`panel.mkf[2]`） | `calendarMode`（byte_497164）**双模式**：月历网格（帧 `kMonthFrame[月-1]+4`，1..N、节日红字、当日 `drawRectBorder`）/ 大数字（背景帧 + 星期名 + 大号日；节日换 `data.mkf[kCalendarRes]` RAW 插画背景）；两模式共有年/月 |
| 小地图 | 0x416E6D | 工作副本 `miniMapBuffer` 逐行 `memcpy` 到 surface（不 blit 帧）；旋转箭头帧 20/21→按下 18/19；玩家标记 `pieceSprites[i]` **帧 6**（5696/65536 缩放）+ 当前玩家白框 + 手动视野红框；拖拽/点击跳转/右键复位（§5） |
| 前进面板 | 0x417191（`panel.mkf[7]`） | `gamePlayerControl` 才显示、状态效果中隐藏；背景帧随 `v1+advancePanelBlink`（GO 闪烁）；GO/骰子图标帧 6..11（选中/未选成对）；命中用 `panel.mkf[8]` 字节 mask，`ctrl=像素+10`（13=GO、11=骰子数、12=拖，§2） |

**缩放魔法数**：小地图 `5696/65536`、大地图 `11392/65536`、拖拽 `761856/65536`（≈11.625，限幅 220..2084）。
**大地图叠加**（`map_dialog` 0x40A801）：不清屏、直接画工作副本 + 玩家标记**帧 5**（与小地图帧 6 不同）。
**节日**：`findHoliday 0x4521F0`（type0 固定 / type2 第 N 个星期X；type1 浮动未实现）、`isSpecialDate 0x4523D5`（周日或命中即红字，股市休市复用）。

## 17. 头像 / 精灵选择网格（`new_game_dialog`，0x40423C/0x404E44）

12 格 6×2 头像网格（`Data.mkf[2]`）多选：
- **未选**正常画；**已选** `scaleSurfaceChannels(帧, kChannelHalf)` 变暗（§7）；
- **悬停未选**：头像斜移 `(8,1)` + 名字条（`kChannelThird` 变暗 + 居中名）；
- **禁用（AI 遗留）**：`grayscaleImage` 灰度副本 + 帧 9 红 X 叠加，不可选（0x40423C）；
- `charState[i]` 0/1/2 三态驱动；`toggleAvatar` 选/取消（AI 不可取消）。
- 底部已选角色动画用 `blitSpriteFrame`（§19），随 timer 推进 `animFrame`（§18）。

## 18. 动画类

| 动画 | 原版 | 驱动/要点 |
|------|------|-----------|
| 转盘 | 0x44090E/0x43F7C6 | `runModal(...,40ms)` 状态机 phase 1滚动/2过渡/3减速/5停留/6结束；`panel.mkf[68+theme]` 帧 2+格 @(220,320)；**覆盖层吞事件**、逐帧 `renderGameFrame`；人类可点击提前停、AI 40 帧自动；起始格 `rand()%12` |
| 进场过渡 | 0x404E44 状态 2 | `setTimer(50ms)`；`transSpeed+=2`、`transAccel` 递增；地图左移/面板右移/头像右飞，`transMapX<=-640` 结束 `requestExit(1)` |
| 三阶段状态机 | 0x404E44 | `phase` 选择(0)/补AI(1)/进场(2)；`handler(nullptr)` 进入初始化（下调玩家数、`setTimer(100ms)`）；事件按 phase 分发 |
| 闪烁 | 0x417E26 / 0x411122 | `kModalTimerEvent` 翻转 `advancePanelBlink`（GO）/ `blink`（热键反白） |
| 棋子/角色动画 | 0x45663E | `blitSpriteFrame(frame++ % count)` |
| 跳伞入场 FLC | 0x45144F | `FliDecoder` 帧画到地图区（色键 0），入场期间**锁定交互**（§24） |
| 事件 FLC 阻塞播放 | 0x45144F/0x4506C7 | `playEventFlc`（`turn_system.cpp`）：landingEvent 内**同步阻塞**播放（得點券 537 @(204,180)，14 帧 @71ms），逐帧 `renderGameFrame`+`pumpEvents`+音频续喂；**透明模式（flags&1）= 调色板索引 0 色键**（`FliDecoder::colorKey()`；勿用 `c==0`，537 的 palette[0]=(0,139,83)≠0）；首帧后播 `Effect.mkf[soundId]`（`sub_454304`/`sub_45434F`） |

**经验**：动画统一用 `runModal` 的 `tickMs` + `kModalTimerEvent`，不另起线程；
`event==nullptr` 分支 = 原版 `PostMessage(WM_USER+1)`（进入/初始化）。

## 19. 资源绘制原语细节

- **SPR 索引 + 调色板区域 blit**：`number_input:blitSprRegion`（对齐 0x456512，非 `blit.h` 导出）——
  按索引查 `frameBytes`/`palette()` 逐像素写 RGB555（含索引 0），用于 8bit SPR 面板；
  整帧透明色键则用 `blitSpriteFrame`（索引 0 透明）。
- **区域拷贝背景** `blitElementRegionOpaque(dst, frame, dstX,dstY, srcX,srcY,w,h)`：从画布某矩形拷到另一处
  （设置音乐列表背景 0x40FC57、AI 比例条背景 0x41DA61）。
- **RAW 无头位图** `decodeRawBitmap`（`game_panel` 节日插画，按字节数推断尺寸）——`UiImage.load` 只吃 SPR/SMP。
- **暗化滚动源** `convertImageChannels(...,-16)` 生成 `mapDark`（0x4552E7 分支），滚动用 `blitScrolledMap`。
- 帧号↔元素偏移：`偏移 = 12 + 12·帧号`（+72=帧5、+84=帧6、+24=帧1…）；结构数组内如 `&dword_48231A + 8·控件 + 8`。

## 20. 全屏面板底图与帧布局

- 底图多为 `panel/data/help/JUMP.mkf[N]` 的 SMP 多帧；`UiImage::load(blob)` 后 `frame(i)` 取 `UiFrameView{width,height,offsetX,offsetY,pixels}`，`blitElement*` 画到全屏或子区域。
- 帧数用 `frameCount()`；取帧前一律 `if(frame < ui.frameCount())` 守卫（资源缺失降级不崩）。
- 例：`panel.mkf[75]` 帧0=股價表、帧1=持股表、帧2=走勢圖框(587×375→26,52)、帧3+=公司图标(80×112)；
  `help.mkf[0]` 帧0框架/1目录高亮/2未选/3选中/4-7滚动/8-11继续；`data.mkf[520]` 帧0背景/2+=地图/10槽。

## 21. 软件光标（`render/cursor.cpp`）

原版 `ShowCursor(0)` + 自绘（`data.mkf[0]`，0x4020FA）：
- `cursor().select(id, frameCount, frameDelay)`：`dword_48A0F4 = 基址+12·id+12`（0x4021F8）；
  `frameCount>1` 时 `update` 按 `frameDelay` 推进动画帧（0x401F98）。
- `compose`（0x401E59）：先保存光标 32×32 背景区（`m_saved`，裁剪到屏）、再色键 blit 光标帧（帧自身尺寸如 23×23）；
  `uncompose`（0x401F5E）写回背景。**位置 = (mouse - offsetX, - offsetY)**，热区在帧头 offset。
- `getRect`（0x4024C0）给脏矩形。
- 常用 id：面板交互光标 **27**、默认箭头 **41**；模态进入设、退出恢复（§13）。

## 22. 模态对话框框架（`runModal`，0x4018E7）

- `runModal(app, handler, &state, tickMs)` 阻塞循环；`handler(nullptr, user)` = 进入/重绘请求（原 `WM_USER+1`）。
- 事件：`SDL_EVENT_MOUSE_MOTION / BUTTON_DOWN / BUTTON_UP / KEY_DOWN` + `kModalTimerEvent`（`tickMs>0` 时合成）。
- 退出：`events().requestExit(result)`（原 `postModalExit 0x401966`）；循环 `pop()` 后 `clearExit()`，
  **嵌套模态返回不会误触发外层退出**。
- **嵌套 timer 保存/恢复**：`runModal` 存 `savedTimer`、退出前还原——否则子模态返回后外层 16ms tick 被清零而停摆。
- **子模态叠加**：若每帧 `renderGameFrame` 会擦父面板——须**进入时捕获 surface 快照、每帧恢复**（数字框 §13）。
- **吞事件覆盖层**：动画/提示类 handler 对无关事件 `return true`（转盘/消息框），阻止事件下传场景。
- `dispatch`（0x4019DD）始终派发给栈顶 handler（`dword_48A010[深度]`）。

## 23. 交互音效（`audio.cpp`，0x4542CE）

UI 音效与游戏音效槽**是两套**，勿混：

| 常量 | Effect.mkf | 语义 | 触发 |
|------|-----------|------|------|
| `g_uiSoundHover` | 0 | 悬停 | `hover` 变化时（非按住） |
| `g_uiSoundClick` | 1 | 通用点击 | 命中任意控件 `BUTTONDOWN` |
| `g_uiSoundConfirm` | 2 | 確定/选槽 | 确定、选存档 |
| `g_uiSoundCancel` | 4 | 取消 | 右键/ESC/取消 |

- 特例：音乐列表点"已静音"→ id 3（提示不切歌）；数字框按键 `kSfxKeyPress=7`、滑块 `kSfxSlider=9`（走游戏槽 `g_effectSlots`）；转盘 id 52。
- **时机对齐原版调用点**：有的框在 `DOWN` 播（settings/ai/help），有的 `DOWN` 记态、`UP` 才执行但仍 `DOWN` 播（hotkey/confirm 确定）。别把 hover 音当 click 音。

## 24. 交互门控与取消约定（`game_loop`，0x417E26/0x4186BE）

- **非玩家控制期**（`!gamePlayerControl`：AI 回合/掷骰/移动/结算）：`MOUSEMOTION`/`BUTTONUP`/右键直接消费、
  键盘各分支自检；**左键 `BUTTONDOWN` 仍放行地图区物件提示**（0x4186BE 在 `byte_46CAFD` 检查之前）。
- **跳伞入场期**（`parachuteActive`/`pendingSpawnPlayer`）：除 `QUIT` 外全吞（原 `sub_45144F` 阻塞播动画）。
- **统一取消**：右键、ESC 常作取消（`requestExit`），且**取消不改状态**（如设置取消不切歌、热键右键还原捕获值）。
- **全局键**：ESC = "取消指令"（`bind[5]`，抬起合成右键取消手动视角）；前进 `bind[10]`、选骰 `bind[11]`（0x401010）。

---

## 25. 选择面板框架（保释/出院类头像格面板，0x43CAAB/0x43DA27 族）

后续百货/交易/乐透/魔法屋/银行/查询等 30+ 面板同范式（浮动消息/语音被 `sub_44EC30/44ECB6/44EE18/44EF3B`
全局共用，xref 已验证）：

- **框架**：`runModal(handler, tickMs=100)` + 进入（`event==nullptr`，对应 WM_USER+1）初始化+全绘；
  `setPauseDraw(1)/0`；退出 `requestExit`（对应 postModalExit）。面板全屏底图盖住游戏画面，退出后重绘。
- **资源帧表惯例**（`panel.mkf[63]` 監獄 22 帧 / `[65]` 醫院 31 帧 / `[64]` 4 帧=NPC 出狱标记）：
  帧 0 底图(640×480)；浮动消息板帧（監獄=1、醫院=1/2）；悬停信息板 100×95（監獄=2、醫院=3）；
  在押框/空格框（監獄=3/4）；头像帧 = `charIndex+k`（監獄 k=5、醫院 k=14）、NPC 头像 `i+k2`（13/22）；
  点券板+数字（監獄帧21@(542,432)+@622,452、醫院帧30@(8,432)+@88,452，20 号白字阴影 align 6）。
- **头像格选择**：坐标表（監獄 `dword_475C04/08`=(33/185/336/487)×(24/183) 格 121×137；
  醫院 `475C64/68`=(297/481)×(1/121/241/361) 格 147×102）；悬停 = 保存旧板区→画板帧（監獄 格+20,+120；
  醫院 格-80,0）+ 16 号名/`保釋點數`/`%d點`（费用表 監獄 `dword_475C44`、醫院 `dword_475CA4` =
  {30×4,300×4}）；悬停变化先恢复上一板矩形再存新（`saveBackground` 语义）。
- **确认/执行**：`yesNoDialog(320,240)`；确认后格子**擦回底图帧 0 同区域**（blitOpaque）+ 画空格框；
  玩家释放 `stateFlags BYTE=0x80`+清 `jailFlags/hospitalFlags`+扣点券+`addPlayerDebt`（返还
  `-100×M×天数`，欠款矩阵未建模则日志）。
- **浮动消息** `FloatMessage`（`float_message.cpp`，0x44EC30/44ECB6/44EE18/44EF3B）：
  `show` 画板+20 号大字（文字 y 偏移原版 -6 为视觉微调非动画）；期间 `active()` 屏蔽悬停/点击；
  `advance`（100ms tick）**最短 2000ms 后**轮询 `voicePlaying()`，语音完才恢复背景结束→面板全重绘；
  点击 `finish` 跳过（停语音+恢复）。
- **音频**：进入 `pushSceneMusic(15 監獄/16 醫院)`、退出 `resumeSceneMusic()`（0x4549CF/454BCC，
  重写 OGG 从头恢复）；语音由 drawText `#NNNN` 前缀自动播（0x45441A→playVoice 单通道）。
- **重写位置**：底座 `src/app/float_message.cpp`（`FloatMessage`）；`src/app/jail_dialog.cpp`
  含 **監獄保释**（0x43D304 人类 UI + AI）与 **醫院办理出院**（0x43E9A4 阶段机 + 护士动画
  0x43EB28-0x43ECF3：上窗行走 {5,6,5}/{10,11,10}@139/165,158 + 下窗小像 7/8、12/13 驻留复位）；
  NPC 格按③占位。
- **坑**：① 浮动消息判定"超时未完语音→继续等"（静音=纯 2s）；② 两面板格坐标/板位置不同勿混用；
  ③ NPC 格（i≥4）头像帧号在底图帧表内，显示可点但释放依赖事件槽 NPC（P5）。

## 新增控件模板（发现新控件时复制填写）

```
## N. 控件名（原版 0xXXXXXX）
- 原版依据：反编译关键分支/资源/坐标
- 资源：mkf[?] 帧布局 / 尺寸
- 交互：命中区、按下/悬停/取消、返回值
- 重写位置：src/app/xxx.cpp:func
- 经验/坑：与原版差异、易错点、复用注意
```

> 更新本文件后，若控件被某功能首次使用，同步在该功能的 `docs/reverse/functions/*.md`「重写要点」引用本节；
> 并在 §0 总索引补一行。
