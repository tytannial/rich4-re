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
| 提示/消息/气泡/拾取/卡片获得 | 0x440CAC/0x417559/0x441F73 | `message/object_tip/turn_system` | 全局 | §15（演出屏蔽+打断见 §39） |
| HUD 常驻（工具条/页签/日历/小地图/前进面板） | 0x417E26/0x417191 | `game_panel/game_loop` | 游戏内 | §16 |
| 头像/精灵选择网格 | 0x40423C/0x404E44 | `new_game_dialog.cpp` | 选人 | §17 |
| 动画（转盘/进场/闪烁/棋子/FLC/事件FLC 阻塞播放） | 0x43F7C6/0x404E44/0x45144F | `roulette/new_game/game_loop/turn_system` | 事件、开局 | §18 |
| 资源绘制（SPR索引/区域/RAW/灰度/暗化） | 0x456512/0x4553DA | `number_input/blit.cpp` | 全局 | §19 |
| 帧锚点 offset 与落点对齐 | 0x455B3A/0x455C52/0x455E24 | `tools/frame_align.py` | 全部元素绘制 | `frame-anchor.md` |
| 全屏面板底图/帧布局 | — | `UiImage` | 各面板 | §20 |
| 软件光标 | 0x4021F8 | `cursor.cpp` | 数字框 | §21 |
| 模态框架 | 0x4018E7 | `event_stack.cpp` | 全部对话框 | §22 |
| 交互音效 | 0x4542CE | `audio.cpp` | 全部 | §23 |
| 交互门控 | 0x4186BE/0x46CAFD | `game_loop.cpp` | 游戏内 | §24 |
| 选择面板框架（头像格/浮动消息/语音/音乐栈） | 0x43CAAB/0x43DA27/0x44EC30..44EE18 | `float_message/jail_dialog` | 保释/出院（P2） | §25 |
| 多页签查询面板 + 右侧面板页签 | 0x424492/0x423CF3/0x423070/0x4225A3/0x415F69 | `query_dialog/game_panel` | 查詢（[HELP 16]）/資訊面板 | §27 |
| 商店面板（抽屉滑入/店员动画/买卖列表） | 0x42E931/0x42D37F | `shop_dialog.cpp` | 百貨公司（case 15） | §28 |
| 网格选号（号码格 + 已售变暗 + 金额数字串） | 0x42F7FC | `lottery_dialog.cpp` | 樂透（case 9） | §29 |
| 小游戏全屏模态（数字字形/大字结算/标题FLC/枪光标） | 0x4154DC/0x414789 | `minigame_dialog.cpp` | 七彩氣球等（case 6/7/8） | §30 |
| 双页选卡/选道具面板 | 0x44192A/0x4413EC | `card_bag_dialog.cpp` | 命运生日卡（case 3） | §31 |
| 选人对话框（横排头像+绿框悬停） | 0x440E1A/0x43FF56 | `player_select_dialog.cpp` | 嫁祸卡/命运 | §32 |
| 径向图标选择（RAW pick + 悬停大图/名称） | 0x4325C2/0x431842 | `magic_house_dialog.cpp` | 魔法屋惩罚选择 | §33 |
| 拍卖竞价面板（7 按钮/AI 出价/头像翻滚/落槌） | 0x43BDE5/0x43A2DD/0x439F0D | `auction_dialog.cpp` | 魔法屋惩罚 11 / 新聞 idx 7 | §34 |
| 图标条选择框（等级门槛灰度禁用） | 0x44101D/0x4402D7 | `lab_dialog.cpp` | 研究所研发道具 | §35 |
| 挂单网格 + 按下滑动子按钮 + 详情模态 | 0x4284BE/0x427C21/0x42704E | `trade_market.cpp` | 交易市場「公佈欄」 | §36 |
| 色彩蒙膜叠加（涨价红/查封蓝/闪烁） | 0x456C33/0x4554FC | `map_render.cpp` | estate/corp 状态色 | §37 |
| 全屏演出模态（滚动预览+sprite 行走+帧预合成） | 0x4060E9/0x456180/0x4562A5 | `victory.cpp` | 通关选图/失败读档 | §38 |
| 演出打断（msg/line/flc skip）+ 光标隐藏/脏块修复 | 0x4528B9/0x4544F6/0x45144F | `message/item_effects/turn_system/cursor` | 全局 | §39 |

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
- **帧锚点 offset**：SMP/SPR 帧头 `x/y` 是锚点偏移，原版与重写 blit 的落点都是
  **实参 − offset**（重写见 `clipBlit`；原版 0x455B3A/0x455C52/0x455E24）→
  **移植直接抄原版实参，勿自行加减 offset**（曾双重补偿踩坑）。校验工具与全部已扫描
  清单见 **`frame-anchor.md`**。
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

**重写实现**（公共 `blit.h:pressDown`，每帧全量重绘、抬起即复原，无需保存/恢复背景）：
```cpp
void pressDown(Surface& dst, int x, int y, int w, int h, int shift, const uint8_t tbl[32]) {
    uint16_t* p = dst.pixels();
    for (int row = h - 1 - shift; row >= 0; --row) {            // 自底向上，避免覆盖
        uint16_t* d = p + (y + row + shift) * Surface::kWidth + (x + shift);
        const uint16_t* s = p + (y + row) * Surface::kWidth + x;
        std::memmove(d, s, (w - shift) * sizeof(uint16_t));     // 内容右下平移 shift px
    }
    scaleSurfaceChannels(dst, x, y, w, shift, tbl);             // 顶行
    scaleSurfaceChannels(dst, x, y + shift, shift, h - shift, tbl);  // 左列（不含顶角）
}
```

**触发时机**：**仅在按住时**（`pressed` / `pressedBtn` / `pressedSlot >= 0`）绘制；悬停（hover）**不**下沉。
`BUTTONDOWN` 记 pressed + 重绘，`BUTTONUP` 清 pressed + 执行动作 + 重绘。

**强度调节**（按钮越大越可夸张）：

| 场景 | 平移 | 阴影宽 | 变暗表 | 调用点 |
|------|------|--------|--------|--------|
| 热键小按钮/列表项（55×16） | 1px | 1px | `kChannelHalf` | `hotkey_dialog.cpp` |
| 卡片/道具格（78×54） | 1px | 1px | `kChannelHalf` | `card_bag_dialog.cpp` / `item_bag_dialog.cpp`（0x44184A/0x445D59） |
| 股市顶部大按钮 | 2px | 2px | `kChannelThird` | `stock_market_dialog.cpp` |

**经验**：
- 原版 `word_46CAEC=640` 是 surface 句柄/宽，**不是颜色**；选框用 `drawRectBorder`，下沉用 `pressDown`，勿混。
- **卡片/道具格原版就是下沉，不是白框**：`cardModal/itemModal` 的 `LBUTTONDOWN` 直接 `highlightRect`（0x4416F0/0x445C14），无任何选框；重写早期误用 `drawRectBorder` 白框，2026-09-25 修正。
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

**嵌套模态须保存/恢复背景**（原版 0x453A32：进入 `sub_451E7E`=saveBackground 框矩形、
退出 `sub_451EDB` 写回）：重写为单 Surface 事件驱动绘制，外层模态不会自动重绘 →
确认框退出后不恢复会**残留**（表现为"点 NO 后卡住/无法交互"）。重写用公共
`saveRegion/restoreRegion`（`blit.h`，0x451A97/0x451EDB）在 `runModal` 前后包裹。
同一机制适用于任何"叠加在外层画面上的嵌套模态/悬停板"。

### 3.4 步进 ±按钮

`ai_dialog`（0x41DDA9）：现金/股票比例条左右 ±按钮，按下态画画布帧 4/5 到 `rect+1`；
`UP` 对值 `±10` 并 `clamp 0..100`（原版字节回绕，重写显式限幅）。确定/取消按钮按下用
`scaleSurfaceChannels(…,kChannelDim)` **变暗 40×56** 代替换帧（§7）。

### 3.5 图标行选择

`facility_dialog`（0x43FAE4）：N 个等宽图标一行，`x = x0 + step*i`；命中 `hover=(x-x0)/step`。
悬停画**多层 `drawRectBorder`**（原版 68/66/64 三层，颜色 = 调用点 `push 0FFFF00h` 黄；
⚠ `word_46CAEC` 只是 640×480 surface 描述符，不是颜色）+ 名称文本；
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
| 2 / 3 / 4 | 落点水平居中（3=竖排；2=落点居中但**文本左对齐**；4=文本**每行独立居中**） |
| 5 | 垂直居中 |
| 6 | **右对齐 + 垂直居中**（数字/金额列标准） |
| 7 | 水平居中、底对齐 |

> **align 4/7 的居中 = 每行独立**（2026-10-05 实机订正）：原版 `sub_44FABC` 对 a5=4/7
> 在 512 缓冲上把 rc 收成居中带（`rc.left=256-w/2`）并以 GDI `DrawTextA(DT_CENTER)`
> 绘制 → **每一行各自水平居中**（影子/描边偏移版同用）。重写曾按"最长行"统一 ox →
> 短行看起来左对齐（买地询问框"新竹市/費用:1000元/是否買下此地?"实机反馈根因）。
> a5=2/3 则是**落点居中但 uFormat=0（左对齐）绘制**——两者勿混。

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
| 角色台词气泡 `playLine` | 0x44EF41 | `data.mkf[517]` **帧 6 云朵气泡**（`+84`，271×199，**非帧 5 消息框**）@(220,130) + 头像 `pieceSprites[13p]` 帧 `expr+1` @(170,130) + 文本 @(200,130) 或 `@MM` 表情 `data.mkf[519]` 帧 MM-1 @(240,130)；`#NNNN` 前缀播语音；同指针去重（`dword_4762C8`）；监/院/挂起态不显示；1000ms |

**提示框族资源**：`data.mkf[517]`（`g_tipFrame`，`st.estateTiles`）——帧头偏移 = `12 + 12*n`：
帧 4 设施图标底（355×83，设施选择，`+60`）、**帧 5 通用提示框**（249×170 绿装饰框，
`showMessage`/`askDialog`/`roulettePrompt`/`showCardGet`/老虎机/设施标题/研究所标题共用，`+72`）、
**帧 6 角色台词气泡**（271×199 红边云朵带下指尾，`sub_44EF41` 专用，`+84`，勿与帧 5 混用）、
**帧 7 研究所五格面板**（400×89 木纹底，`labDevelopDialog` 不透明铺底，`+96`）、
帧 0..3 方向气泡、帧 8..17 数字、帧 18..21 小地图旋转箭头。

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

**托管解除入口（[NEW]，非原版）**：原版工具条在非控制期整条锁死（0x418158
`if (!g_playerControl) return 0`）——单人局托管（`alive|=4`）后再无窗口打开「託管AI」面板解除。
重写放行 AI/托管回合的工具条第 3 按钮（case 2，x∈[80,120), y<40）：`handleLeftButtonDown`
在 `tipPerfBlocked` 之前（AI 回合多在移动中，`playerActionState!=0` 会吞点击）置
`pendingAction=102`；事件层放行对应 UP；`handleTopBarButton(idleOpen=true)` 跳过 0x40DEFE
收尾（不置 0x80、不抢控制），面板关闭后 AI 回合状态机原样继续。场景 `310_host_release`。

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
- **实测（2026-10-05）**：`moneyWndProc`（0x414FCD）**无任何 `cursorSelect` 调用**
  （0x4021F8 的 xref 仅 `digWndProc`/`balloonWndProc`）→ 喜從天降进出不改光标、
  无专属游戏光标；据此撤销旧判断"case 8 缺光标设置"（`m4-plan` §9.3 已记）。

## 22. 模态对话框框架（`runModal`，0x4018E7）

- `runModal(app, handler, &state, tickMs)` 阻塞循环；`handler(nullptr, user)` = 进入/重绘请求（原 `WM_USER+1`）。
- 事件：`SDL_EVENT_MOUSE_MOTION / BUTTON_DOWN / BUTTON_UP / KEY_DOWN` + `kModalTimerEvent`（`tickMs>0` 时合成）。
- 退出：`events().requestExit(result)`（原 `postModalExit 0x401966`）；循环 `pop()` 后 `clearExit()`，
  **嵌套模态返回不会误触发外层退出**。
- **嵌套 timer 保存/恢复**：`runModal` 存 `savedTimer`、退出前还原——否则子模态返回后外层 16ms tick 被清零而停摆。
- **子模态叠加**：若每帧 `renderGameFrame` 会擦父面板——须**进入时捕获 surface 快照、每帧恢复**（数字框 §13）。
- **吞事件覆盖层**：动画/提示类 handler 对无关事件 `return true`（转盘/消息框），阻止事件下传场景。
- `dispatch`（0x4019DD）始终派发给栈顶 handler（`dword_48A010[深度]`）。

### 22.1 宽屏 640 基准框架（M4-D，2026-10-05 实机收口）

- **全屏剧场模态**（`fillBars=true`：银行停留/週轉/催收、百货、医院、监狱、魔法屋、
  股市、月结/分红/开奖、拍卖、彩票投注、主菜单/选人/通关演出等 640×480 铺底界面）：
  进入时 `dispatchModalAware` 先 `renderModalBackdrop`（栈内有游戏循环层时，
  以原版 640 布局把世界重绘到居中区——布局派生经 `LayoutNativeGuard` 临时锁定 640、
  世界原点 +base，`uiPanelOffsetX` 叠加世界原点），再两侧填黑；handler 返回后补填
  （handler 内 `renderGameFrame` 覆盖不露）；退出时 `m_forceRepaint` 强制下一帧全量恢复
  宽屏画面（防静止帧跳过重绘导致黑边残留）。
- **叠加式模态**（`fillBars=false`）保留游戏画面：ATM 柜员机（320×338 悬浮面板）、
  新闻/命运（440×480 报纸占地图区、右栏保留）、存读档、大地图、设置/询问框等；
  `centerBase=false`（选目标/选骰等游戏世界交互）坐标不经 640 基准平移。
- **竖排文本**：原版 `drawText` **align==3 = 竖排**（`drawTextVertical` 0x44F7C7，
  逐字符基线 y += 字高+字距）；调用点=日历星期、银行柜台日历、托管"確定/取消"、
  右栏 4 页签。重写 `text.cpp` 的竖排测量框 = 单字宽 × 总字高，首字基线自缓冲 pad 起。
- **命中坐标双轨**：handler 内 `event->button.x` 已 -base（帧坐标直接可用）；读实时鼠标的
  `mouseLogicalPos` 自动减绘制 origin（=base）→ 返回 640 基准，与命中区一致
  （宽屏"按钮错位"根因修复；debug 合成输入覆盖坐标同步 +base）。
- **模态外预绘制禁止**（或必须包 `SurfaceOriginGuard(base)` 且 save/restore 坐标 +base）：
  进入重绘以 `handler(nullptr)` 为准；无 origin 预绘制会在宽画布左侧留未平移残影
  （ATM 幽灵面板根因），且会被 backdrop 覆盖（拍卖静态帧改在 handler 内绘制）。

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
  0x43EB28-0x43ECF3：眼/嘴覆盖帧 @139/165,158/180(183)）；**NPC 格释放动画**见坑③。
- **坑**：① 浮动消息判定"超时未完语音→继续等"（静音=纯 2s）；② 两面板格坐标/板位置不同勿混用；
  ③ NPC 格（i≥4）保释/出院动画已实现：**panel[64] 帧 (i-4) 全身像**（小偷/強盜/流氓/間諜）
  @監獄(365,450)/醫院(420,450)（落点 = 实参 − 帧 offset）+ **答谢消息** `dword_475BE2[i]`
  （#0123~#0126，監獄板 @(210,150)/醫院 @(200,200)）→ 消息播完重绘退出；人类费用 **300 点券**
  （`>=700` 仅 AI 分支）；**事件槽 NPC 路面行走/抓回 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）；
  ④ **阶段机的转换必须覆盖"消息被点击跳过"路径**——原版依赖 `floatMsgAdvance(0)` 在下一次
  WM_TIMER 返回 1（0x43DB7C）；重写若把转换写在 `msg.active()` 分支内，点击跳过开场白后阶段
  会卡住（症状：点格/右键全无反应）。写法：`msgDone = msg.active() ? msg.advance() : true;`
  再按 msgDone 执行 switch（`jail_dialog.cpp` `hospHandler`）；
  ⑤ 两面板补 ESC = `requestExit`（原版 WndProc 无 WM_KEYDOWN，对齐重写其他模态的现代化操作）；
  ⑥ 医院护士动画是**眼部/嘴部两个覆盖区**（帧内容 PNG 实测）：眼 `@(139/165,158)`、
  嘴 `@(139,180)/(165,183)`（帧 7/12 张嘴、帧 4/9 局部静止嘴、帧 8/13 闭嘴复位）；
  **原版复位帧 8/13 实参 (179,198)/(205,198) 是 `Rect.right/bottom` 笔误**（对帧 4 的
  像素对齐误差 300，正确嘴部 (139,180) 误差 19），重写按嘴部左上角修正；
  帧 5/6/7/8 为不透明整矩形（无透明像素，用 `blitElementOpaque` 保真）；
  ⑦ **帧锚点 offset**：blit 内部已按 offset 定位（`clipBlit`），直接抄原版实参即可；
  错位排查流程见 `frame-anchor.md`。

## 26. 数字老虎机（`slotMachineDialog`，0x440706/0x43F23E）

- **用途**：大/小財神、大/小窮神附身时决定金额（`attachObject` case 1/2/5/6）。
- **资源**：`panel.mkf[67]` 24 帧（SMP）：帧 0 = **4 轮机身**（193×183）、帧 1 = **3 轮机身**
  （156×183）、帧 2 = 把手抬起（31×108）、帧 3 = 把手压下（31×72，锚 y=−35）、
  帧 4..23 = 数字条 **10 数字 × 2 帧**（38×36；定格 byte=2r+1 → 帧 byte+4）。
  **关键**：`slotMachineDialog` 内 `v1 = !(a1&1)` 传给 `slotMachineValue` 作为其形式参数
  —— **a1 偶（0 小財神 / 4 小窮神）→ v1=1 → 3 轮 0..999；a1 奇（1 大財神 / 5 大窮神）
  → v1=0 → 4 轮 0..9999**（千位仅 v1=0）——与 [HELP 42/44/46/49]「大=四位数、小=三位数」
  吻合；**勿按 a1 值直接分轮数**。
  轮 x `word_475CE8[4*v1+reel]`：4 轮 {145,182,219,256}、3 轮 {—,163,200,237}；
  把手 x `dword_475CE0[v1]` = {317,298}；blit 实参 y：机身/数字轮 320、把手 240（x 均按表）。
  **blit 语义**：机身与数字轮 = `blitElementFullscreen`(0x4563F5) → **blitElementOpaque 不透明**
  （0 像素照画）；把手与提示框(0x456418) = **色键透明**。数字帧矩形嵌在机身窗口内，
  用色键会在窗口边缘漏出地图背景（花屏）。
- **标题**：`data.mkf[517]` 帧 5 提示框 @(220,140) + 16 号字模板（0x4652A9..4652E7，
  「%s附身 + 向所有對手收.../送您.../付給每個人.../損錢...」，%s=神明名；
  原版实参 `*(&off_47ED7A + a1)` 为**字节错位指针**（a1=1/5 笔误），重写按 a1 对应神明名）。
- **交互**（0x43F23E 状态机，24ms/帧）：phase1 全轮滚（每 10 帧全体重随机跳一格；
  **人类左键点击拉杆**，`alive==1 && state37==0` 才可点；AI/托管/夢遊 40 帧自动）→
  phase2 画把手**压下**帧 3 + UI 点击音（原版 case2→3 约 5 帧持续压下后回抬起帧 2）→
  phase3 再滚 4 帧 → phase4..7 **自右向左逐轮定格**（每轮 8 帧；4 轮版停到轮 0、
  3 轮版停到轮 1，其后不再滚）→ phase8 提示框标题**替换**为金额 `%d` 停留 40 帧 → 结束。
- **结果**：`(vis==0 ? 千位 : 0) + 100×(byte1>>1) + 10×(byte2>>1) + (byte3>>1)`。
- **音效**：滚动循环 `Effect.mkf[51]`（`dword_475D3C`，同槽替换）；拉杆 UI 点击音（g_uiSoundClick）。
- **重写位置**：`src/app/map_objects.cpp` `slotMachineDialog`（`runModal` 模态 + timer 24ms；
  panel 资源缺失回退纯随机）。与 **转盘** `roulettePrompt`（0x44090E，panel[68..71]，
  12 格选天数/倍数）是**两套不同控件**，勿混用。
- **坑**：① 轮值 byte 循环 0..19，初值 2r+1、定格时按 8 帧节奏自然为奇数，数字 = `byte>>1`，
  偶数帧是滚动过渡态；② 千位仅 v1=0 参与，3 轮版 `byte_[0]` 不显示不计算；
  ③ 金额 phase8 起**停滚并替换标题**（叠加显示会花）——原版 case8/9 **不再调用滚轮绘制**
  （滚轮定格），金额框叠加其上；重写滚动循环必须以 `phase < 8` 为总开关，
  否则金额页滚轮复活、定格值与显示脱节（实机踩坑：数字一直滚+顶部弹金额）；④ 把手压下帧锚 y=−35 是 SMP 半尺寸锚，
  blit 实参与抬起帧一致仍 (x,240)——**勿自行补偿锚差**（`frame-anchor.md`）；
  ④b 机身/数字轮若误用色键 blit → 花屏（必须 blitElementOpaque，原版 blitElementFullscreen）；
  ⑤ 原版更新矩形只刷数字条/把手区域，重写逐帧全屏重绘（模态背景不动，视觉等价）。


## 27. 查詢面板（0x424492 → 0x423CF3/423070/4225A3/422443）与右侧資訊面板页签（0x415F69/0x4182FA）

- **原版依据**：工具条 case 6 `sub_417D65` → `sub_424492`（panel.mkf[9]+[74] 加载）→
  `runModal(sub_423CF3)`；窗口过程命中：EXIT 钮 (492..602, 9..38) 关闭、左 3 页签
  (12..109, y=282+64i..+40)、顶部玩家条 (16+88k..+88, 14..47)、地產 5 子页签
  (120+75k..+75, 64..97)、翻页箭头 (593..623, 369..399 上 / 417..447 下)；
  页面状态：`dword_4753FC` 页签（0 資產清單/1 地產清單/2 股票清單）、`dword_475400` 子页签、
  `dword_48C27C` 选中玩家、`dword_475404/408` 页首/页项数。
- **资源**：`panel.mkf[9]` 25 帧 —— 帧 0/1/2 = 640x480 页背景（标签由 0x422443 **画进帧内存**，
  重写改为每次重绘现画）、帧 3/4 = 88x33/88x32 玩家钮（选中/未选）、帧 5/6 = 58x19/53x18 EXIT
  钮（正常/按下，off 29,9 / 27,9）、帧 7/8 = 30x30 上/下箭头**按下态**（正常态仅在帧 1
  地產頁背景 (593,369)/(593,417) 自带）、帧 11 = 75x33 子页签底、帧 12 = 97x40
  页签**按下/选中**底、帧 13..24 = 12 种神明图标（`dword_475464` 0x475464 映射：槽+1 → 帧）；
  `panel.mkf[74]` 13 帧 = 13 种道具图标（帧 = 道具 id−1）。
- **内容**：資產清單 12 字段（`off_475418`：現金/存款/貸款/總資產/股 票/點 卷/保險期/企 業/
  土 地/連鎖店/房 屋/設 施；标签 x=142/430/142 y=88+48i，数值 font28 右对齐 x=330/602/250）
  + 道具 13 格（x=300 每格 72、>588 换行 +32，起 (300,281)，图标 @(x−16) + 数量 @(x+30)
  右对齐）+ 卡片 15 槽（起 (300,385)，名居中）；地產清單 5 列（x=168 地點/264 開發狀況/
  394 價格/490 收費/540 租 期，行 y=144+32r，10 行/页；價格 = M×(priceAdd+priceBase×level)
  或 M×(buildPrice+feeTable[0]×sub)；收費 = estateRouteRent / 连锁行=查看玩家连锁总租 /
  M×feeTable[sub]；租期 `%02d/%d/%d`（年%100/月/日）或「無限期」）；股票清單 12 行
  （列头 x=204/332/476 y=68；行 y=100+32i；名 @204、持有張數 @332+52、總市價 @476+60，
  均右对齐）。行文字色（0x4225A3）：**普通/住宅 0x101010（黑）、商業用地/設施 0x1010F0（蓝）**
  ——子页签 2（商業區）整页蓝；子页签 0（全 部）按列表序**首个 corp 行起后续行全部转蓝**
  （原版 v32 标志只切一次，estate 分支不再改回）。地產清單翻页仅本页可用，且按钮只在
  本页背景帧上（帧 0/2 无箭头）。
- **交互**（0x423CF3 窗口过程；**全函数无 audioPlayEffect 调用——本面板点击一律无音效**，
  与其它 UI 不同）：DOWN 记录 `byte_48C284` 并画按下态 → UP 按记录执行：
  1=EXIT（帧 6 按下态 @ (547,23)，关闭）、2..4=3 大页签（帧 12 @ (12,282+64i)，切页 +
  page==1 时列表重收集回首页）、5=上翻/6=下翻（帧 7/8 按下态；`sub_4225A3` dir=2/1 ±10）；
  **子页签 / 顶部玩家条为 DOWN 立即切换**（子页签 `sub_4225A3(...,0)` 回首页；玩家条
  byte_48C284=7 但 UP 分支无 case 7）。右键/ESC 关闭。列表收集 `collectOwnedAssetIds`
  0x423B3B：0=estate+corp / 1=estate / 2=corp / 3=有建筑普通地产 / 4=有建筑连锁店；切页/
  子页/玩家 → dir=0 重置 pageStart，翻页 dir=1/2 不重置（**实机踩坑：翻页误走 dir=0
  会永远回第一页**）。
- **右侧資訊面板页签（0x415F69/0x4182FA）**：布局 ≠2 时 x∈[616,640)、y∈[0,280) 命中，
  `tab = y/70` 写 `dword_48BE24[当前玩家]`（0x41831D idiv 0x46）；背景帧 = panel.mkf[0] 帧 tab
  （f0..f3 四页、f4=玩家条 200x80、f5=事件槽 200x280），页签文字 `off_475274` @ (627,
  15+73i+20) font18（当前页 0x101010 / 其他 0x404040）；内容数值 @ (600,102/166/230) 右上对齐
  （font18 沿用页签循环最后设置）：資金=現金/存款/總資產、地產=土地(estate+corp)/連鎖店/設施、
  股票=總市值/成本(playerAvgCost)/經營権家数、其他=點券/貸款/保險期；物价指数
  「物價指數  %d」@ (450,260) font12（全局 `g_moneyMul`，**任何 tab 均显示**，位于页签
  switch 之前；布局 2 玩家条不含此文字；`currentPlayer==8` 機器娃娃虚拟槽时面板/玩家条
  沿用槽 8 bailer（`npcSlots[4].bailer` = byte_498E70）；精确核对见 `415d31-game-panel.md`）。
- **重写位置**：`src/app/query_dialog.cpp` `queryDialog`（工具条 case 6 接入 `game_loop.cpp`）、
  `src/app/game_panel.cpp` `drawPlayerInfoPanel` + `game_loop.cpp` `handleLeftButtonDown`
  页签命中。
- **经验/坑**：① 原版三页背景帧的字段标签是 0x422443 用 `drawText(dword_48C270+12/24/36,…)`
  **直接画进资源帧**（+12/24/36 = 帧 0/1/2 元素头）——重写不要改资源，按当前页在 surface 上
  重画标签即可；② `dword_475464` 槽 11/13/14 映射帧 0（无图标）、槽 18 是原版越界数据，
  重写 `frame>0` 才画；③ 页签内容字体是页签循环**最后设置**的 font18（非 12），照抄原版；
  ④ 「建造菜单（任务 F）」为旧文档误名——该面板**无建造动作**，点列表项只切页/翻页；
  ⑤ `sub_4246C5`/`g_miscTable336`（旧称"建造队列"）实为**股票交易委托队列**（调用者
  0x4258C1..0x4284BE 交易区），勿与本地块功能混用；
  ⑥ **颜色常量按十进制核对**：0x1010F0（蓝）= 1052912，勿凭感觉写 0x101110 之类近似值
  （实机踩坑：商業用地行颜色错）；
  ⑦ 按下态/触发时机照抄原版：页签/EXIT/翻页 **DOWN 画反馈 + UP 执行**，子页签/玩家条
  **DOWN 立即执行**；且本面板无音效（勿照搬其它面板的 g_uiSoundClick）。

## 28. 商店面板（百貨公司，0x42E931/0x42D37F；抽屉滑入 + 店员动画 + 买卖列表）

- **用途**：特殊地点 case 15 卡片店/道具店；人类 UI（`runModal(shopModalHandler, 50ms)`），
  AI 走 `shopAiVisit` 无 UI。详见 `functions/42e931-department-store.md`。
- **资源**：`panel.mkf[10]` **38 帧**（卡片店 0..15 / 道具店 16..31 / 共享 32..37）：
  底图 0/16（640×480）、抽屉 1/17（222×462）、柜台 2/18、眼睛 4..8 / 20..24、
  嘴部 9..11 / 25..27、切页按钮 13/14 / 29..30（85×85 @542,13）、离开 35/36（80×40 @556,246）、
  点券框 37 @(230,246)、浮动消息板 15 @(120,10)；`panel.mkf[11]` = 卡包/道具包（412×180）。
- **抽屉滑入动画**：位置 `x -222→5`（`+=vx; vx-=3`）、`y 640→227`（`-=vy; vy-=7`），50ms/步
  约 8 步；到位后才画按钮/点券并显示提示消息。点击可跳过消息（跳过则位置直接到位）。
- **店员动画**：`dword_48C32F` 低 4 位状态机（case0 每 100ms 1/32 概率进入 A/B 动画；
  A 帧表 `byte_4755B8[8]={7,8,7,5,21,22,21,23}`、B 帧 4..6 / 23..24，**两套都落 (405,60)/(417,50)**
  ——原版 A 组实参误传 Rect.right/bottom，像素合成验证后按 Rect 左上角修正）；
  嘴部口型帧 9/10/11、25/26/27 @(405,91)/(417,89)（9/25 闭合、10/11、26/27 张开）。
  重写用 `animStage`/`clerkEyeFrame`/`mouthFrame` 状态 + 全量重绘。
  **眼睛**常驻随机眨眼（降频 1/32）；**嘴部**在**消息显示期间持续循环**（原版条件
  `floatMsgActive() || dword_48C314`：1/4 每 100ms 张嘴 + 倒计时 1..7 后闭嘴），浏览时静止。
  **眼睛常驻眨眼（降频）、嘴部口型仅语音开始时播一次**（用户实机反馈 2026-09-25 二次修正，
  原版两者均常驻随机）。
- **商品列表**：卡片行高 24 @内(90,24i+83)/(194,24i+75)，道具行高 48 @(90,48j+92)/(194,48j+84)；
  文字画在抽屉上随动画移动。**按下即购买**（未抬起结算），售罄清 0；卖出区 5×3 80×56
  按下即卖出 + `pressDown` 下沉，抬起复原。
- **浮动消息**：进入/提示/点券不足/栏位满/离开 5 类（`off_4755C0`，含 `#NNNN` 语音）。
  **坑**：重写全量重绘会覆盖 `FloatMessage` 板 → 新增 `FloatMessage::redraw()` 每帧补画
  （跳过 `#NNNN` 前缀避免重播语音）。
- **重写位置**：`src/app/shop_dialog.cpp`（`renderShop/shopTick/shopAdvanceClerk/shopAdvanceMouth/
  shopMouseDown/shopMouseUp`）；`landingEvent` case 15；`drawCardBagAt/drawItemBagAt`（带坐标公共绘制）。
- **经验/坑**：① 实参是 **objId（cellEnt+32 special 6000+n）**，不是 cellEnts 下标（landingEvent
  0x419854 var_18）；② 首次进入每页才播动画/消息（`byte_48C349 = settings[1]^1`）；
  ③ 切页按钮按下帧 14/30、离开按钮 36，抬起才执行动作；④ 卖出返点 90% 是 x87 就近舍入
  （`std::nearbyint`）；⑤ 商品列表命中行索引原版无上界（越界读道具表），重写防越界；
  ⑥ **购买后置灰保留**（原版灰字画入资源表面，非清空消失）——用 `sold[]` 标志分白/灰两趟绘制；
  ⑦ **店员眼睛帧落点**原版 A 组传 Rect.right/bottom 笔误，须用 Rect 左上角，且 A/B 共用同一
  覆盖帧状态（渲染顺序 B 在后会盖住 A）；⑧ **点券框/切页离开按钮在抽屉到位后才画**（进入消息/
  滑入期间不显示）；⑨ **眼睛与嘴部分属两套动画勿混**：眼睛 4..8/20..24 常驻眨眼（降频）；嘴部
  9..11/25..27 在消息显示期间持续循环（原版 `floatMsgActive()||dword_48C314` 门控；帧内容已像素确认）。

## 29. 网格选号（樂透投注，0x42F7FC；号码格 + 已售变暗 + 金额数字串）

- **用途**：特殊地点 case 9 樂透投注（`lotteryVisit`）；100ms tick，panel.mkf[12]/[13]/[14]。
  详见 `functions/lottery-system.md`。
- **命中**：9 列 × 4 行 = 36 格，(30,271) 起、格 64×48；原版判定为**闭区间** `x∈[30,606]`、
  `y∈[271,463]`（右/下边界会算出格号 37..45 越界读相邻内存 → 重写保护 `0..35`）。
- **已售变暗**：打开时对每格 (31+64c, 272+48r) 62×46 用 **-10 通道表**（0x485C28，重写
  `kChannelTen`）`scaleSurfaceChannels`；底图先铺再变暗，帧 1 面板后画覆盖。
- **选中标记**：点击未售格画**帧 7 = 手绘红圈**（58×47 off(28,25)，实参 (col*64+62, row*48+295)，落点 −offset）；
  原版对格号 2/3（号码 3/4）额外用帧 1 的 (0,268) 64×30 区域 blit 到 (210,263)（quirk，照抄）；
  重写需记录购买格号每 tick 补画（原版点击画一次靠表面保留）。
- **金额数字串**：panel.mkf[13] 帧 0-9 数字 / 10 逗号 / 11 `$`；**从右往左**绘制，步进
  数字 18、逗号 6（先 +6 再 −12）、`$` 18；锚点 (184,41)。
- **重写位置**：`src/app/lottery_dialog.cpp`（`drawBuy` / `buyHandler` / `lotteryVisit`）。
- **经验/坑**：① 一次只能买一注（点号即进入退出流程 #0014）；② 已售格点击无任何反馈；
  ③ AI 分支在**同一入口函数**（`cash > 1000` 严格大于，人类面 `>= 1000`——原版不一致照抄）；
  ④ 装饰动画帧 3/4/3 @(273,63)、闪烁帧 5/6 @(273,105) 仅消息期间随机启动；
  ⑤ 开奖界面状态机靠**表面保留**累积绘制（FLC 末帧/重铺擦除范围），重写全量重绘需按状态
  复原最终画面（详见 `lottery-system.md` §7）。


## 30. 小游戏全屏模态（0x4154DC 系；数字字形条 + 大字结算 + 标题 FLC + 游戏光标）

- **用途**：特殊地点 case 6/7/8 三种小游戏（企鵝挖寶/七彩氣球/喜從天降）共用范式；
  100/100/50ms tick 全屏 640×480 模态。三游戏已全部接入（`miniGameDigRun`/
  `miniGameBalloonsRun`/`miniGameMoneyRun`，2026-09-25）。
- **SPR 缩放 blit（[RE 0x4568C2]）**：16.16 定点 scale（65536=1.0x），目标
  (x-offX, y-offY, scale*w>>16, scale*h>>16)，源步长 65536²/scale 累加、8bit 索引查
  调色板、0 透明、全裁剪返回 false（接钱钱袋 0.5x→1.0x 透视）；重写 `blitSprScaled`。
- **RAW 资源两种**：panel[81] 点击掩码 = 640×480 **8bit**（值=row*9+col，直接字节索引）；
  panel[92] 背景 = 640×480 **RGB555**（memcpy 铺满）。
- **鼠标跟随（[RE 0x4135F4]）**：`SDL_GetMouseState` 直接取位（原版 GetCursorPos +
  边缘回卷 quirk 语义=钳位，SDL 窗口内天然满足）；10px/tick、死区 ±8、dir 不清零。
- **资源**：**panel.mkf[79]** = 数字字形 SMP 20 帧——帧 0..9 小数字（HUD 用，步进 20px）、
  帧 10..19 大数字（结算大字，字宽 66）；**panel.mkf[78]** = 标题 FLC（640×480，25 帧，
  三小游戏共用，透明 @(0,0) 叠加）；游戏场景图各自 panel[80..99]/[100+char]。
- **HUD 布局**（y=421，不透明 blit，原版 `blitElementFullscreen`=重写 `blitElementOpaque`）：
  倒计时 `%03d` @(49,69,94) + 单位帧 0 @(114)；计数/得分挖/接 `%03d` @(549,569,589)、
  气球 `%04d` @(529,549,569,589)（气球无计数位，四框留白）。
- **大字结算**（[RE 0x414789]）：`sprintf("%d", 分)`，每位 = panel[79] 帧 `10+d`，
  x=`353-66*len/2` 起、步进 66、y=150；停留 2000ms（sub_45285E）后 postModalExit(得分)。
- **光标**：进入等待期=默认箭头；标题 FLC 播完 → `cursor().select(9, 3, 5)`（枪，3 帧动画
  delay 5，data.mkf[0] 光标组 9）；结算恢复 `select(41, 1, 0)`。挖宝=42、接钱=41（无换枪）。
- **时序**：0x401 初始化（倒计时 150/150/360 + 开场计数 99）→ **首次 WM_PAINT 把 99
  降为 5/—/10**（氣球 0x414FB3、接钱 0x4151F1、挖寶 intro=10 无缩短）→ ~0.5s 后
  0x405 标题 FLC（重写按 tick 逐帧推进，原版阻塞）→ 游戏体 →
  倒计时到 0 → 等待场上气球全清 → 大字 2s → 退出。
  **勿照抄 99**（实机表现为"进去干等 10 秒才 ready go"）。
- **数字 blit 两种方式勿混**：HUD 小数字（帧 0..9）原版 `blitElementFullscreen`(0x4563F5)
  = **不透明**拷贝（黑色填充像素值=0，色键会吃掉填充只剩线框）；结算大字（帧 10..19）
  原版 `sub_456418` = `blitElement` **色键**封装（opaque 会出黑底方块）。
- **标题 FLC 叠加勿漏**：三游戏 uiState==1 期间每 tick 在场景上叠画 panel[78]
  READY GO 帧（挖寶曾漏 → 实机看不到 ready go）。
- **接钱音效表 = {22,23,24,15}**（0x4750BF，stride 8 {index,handle}）：投掷 22、
  **引线嘶嘶 24 循环**（playEffectLooping，接中/漏接停）、爆炸 15。
  `audioPlayEffect` 第一参是**表项指针**；读表必须覆盖到 -1 终止符（曾因 get_bytes
  范围截断误判空表）。
- **气球绘制裁剪 y<387**：sky 区画布 clip → 气球从底部面板**后面**升起（初生 y=420
  不可见）；重写 `blitElementRegion` 限高等价。
- **挖寶=记忆游戏**：宝物堆仅开场（intro+标题 FLC）显示，FLC 后 `digInitDraw(0)`
  重铺背景堆消失；**方向公式以 0x412658 反汇编为准**（Hex-Rays 把 jle/jnz 分支条件
  解析反了，照抄反编译 → 企鹅倒退走路）：`col差>0→3-row差；col差==0→row差>0?1:5；
  col差<0→(row差+7)&7`，帧表 0=下 1=右下 2=右 3=右上 4=上 5=左上 6=左 7=左下。
- **接钱音效补全**（原版空表静音 quirk）：云投弹 = Effect[19]、接炸弹 = Effect[82]。
- **交互**：WM_LBUTTONDOWN **与** UP 各触发一次射击（原版 quirk，一次点击两枪）；
  一次射击遍历所有气球可命中多个；命中盒以气球锚点 (x,y) 为中心（type≥6 小 ±18/±26、
  大 ±22/±30）。音效单 buffer 语义：一次射击多球声只落最后处理的一声。
- **重写位置**：`src/app/minigame_dialog.cpp`（`balloonHandler`/`balloonTick`/`drawBalloon`/
  `balloonClick`/`miniGameBalloonsRun`；`decodeFlcOnce`/`blitFlcFrame` 同 lottery 辅助）。
- **经验/坑**：① 门控 `alive==1 && settings[1]`（動畫過程）——AI/托管/动画关一律走
  0x415457 随机得分兜底，**交互版内不再 showMessage/台词**（原版 else 专属）；
  ② 特殊气球表 byte_475039 末项 **0x80 不是哨兵 -1**：实际生成"诱饵气球"（静止、不可点、
  8 tick 消失、画正常气球帧）——原版 quirk 照抄；③ 破球标记 type=60(0x3C) 借高 nibble
  倒计时（-0x10/tick），与速度表索引 `type&0xF` 复用同一 word，勿按"状态+类型"两字段建模；
  ④ 生成列候选条件 `y>300`（气球还压在下方未升起）防同列叠球；⑤ 得分 cap 999 只在
  加分路径检查，×2 可越 999（%04d 显示 4 位）。

## 31. 双页选卡/道具面板（生日收卡，0x44192A / 0x4413EC）

- **原版依据**：0x44192A 人类分支加载 panel.mkf[11] 元素 → `drawItemBag(0,面板,目标)`
  + `drawCardBag(0,面板,目标)` → `blitElementFullscreen(backbuffer, 帧0, 14, 70)`；
  `drawItemBag` 返回 1（目标有道具）时再 `blitElementFullscreen(帧1, 14, 270)`；
  `runModal(sub_4413EC, 目标 | (道具页可见<<16))`。
- **资源**：`panel.mkf[11]` 帧 0 = 卡包（412×180）、帧 1 = 道具包；卡名/图标网格 5×3
  （列 80/行 56，图标帧 id+1）。
- **交互**：命中 x∈[19,419)、卡 y∈[75,243) / 道具 y∈[275,443)；按下 `highlightRect`
  下沉（卡区 t=56j+76 / 道具区 t=56j+276，78×54）+ `g_uiSoundClick`；**抬起确认**
  （`postModalExit(选中)`）；右键 `g_uiSoundCancel` → 0。返回值：卡 = 卡 id；
  道具 = `0x8000|道具 id`（原版 `dword_48C538`）。
- **重写位置**：`src/app/card_bag_dialog.cpp:selectCardOrItemFromPlayerDialog`
  （复用 `drawCardBagAt`/`drawItemBagAt` 与 `pressDown`）。
- **经验/坑**：① 与工具条卡包（y=130、命中 135..303）落点不同，勿混常量；② 道具槽经
  `drawItemBagAt` 的 `visibleMap[15]` 映射回道具 id；③ 抬起确认而非双击；④ 原版人类
  逐人顶部行演出（「向%s收一張卡片」）在重写中省略（面板直接弹出）。

## 32. 选人对话框（0x440E1A / 0x43FF56）

- **原版依据**：0x440E1A 按候选数取 `data.mkf[518]` 帧 `count-2`（2..8 → 帧 0..6）作横排框
  @(220,320)；框内逐个 `blitBackground(..., data.mkf[2] 头像帧 charIndex, 12+80i, 12)`；
  提示 = `sub_456418(g_tipFrame 帧 5, 220, 140)` + `drawText(220,140,align 4)`；
  `runModal(sub_43FF56)`。
- **交互**：命中 x∈[220-offX+12, +80n)、y∈[320-offY+12, +72)；悬停 `g_uiSoundHover` +
  三层黄框 80×78（颜色 = 调用点 `push 0FFFF00h`；⚠ `word_46CAEC` 只是 640×480 surface 描述符）；
  左键 `g_uiSoundClick` → 返回玩家索引；右键 `g_uiSoundCancel` → -1。
- **重写位置**：`src/app/player_select_dialog.cpp:selectPlayerDialog`
  （嫁祸卡调用；`resolvePenaltyTarget` 0x441210 → `passOnCardDialog` 0x44476A）。
- **经验/坑**：① 帧头 offset 参与落点（`blitElementOpaque` 实参 − offset），命中起点按
  `220-offX+12` 计算；② 头像落点实参 = `框内坐标 + 头像 offset`，直接用
  `blitElementOpaque(..., bx+12+80i, by+12)` 即自动扣除；③ 帧数不足（<count-1）时降级返回 -1。

## 33. 径向图标选择（魔法屋惩罚，0x4325C2）

- **原版依据**：panel.mkf[18]（35 帧）+ panel[19]（**RAW 640×480 pick buffer**，像素值 = 选项
  1..12 / 眼部区 13 / 0 无）+ panel[20] FLC。100ms 状态机 1..8：五段 FloatMessage（#0037..#0041）→
  随机条件（0x431842 目标名单非空）→ 条件图标帧 `cond+11` @(326,296) → 交互 → 确认 → FLC。
- **资源**：帧 0 全屏背景、帧 1 水晶球 165×213 @(241,140)、帧 2 确认罩 284×210 @(182,142)、
  帧 3 女巫**闭眼贴片** 60×35 @(286,188)（#0039 说完 → 冥想）、帧 4 闭嘴 60×18 @(286,220)、
  帧 5 张嘴 60×21 @(286,217)（消息期间 25%/tick 口型，末拍归闭嘴）、帧 8 消息框 280×173 @(320,384)、
  帧 11..22 条件图标、帧 23..34 惩罚小图标；大图帧 6..10（144×128 级，stride16 表
  0x475708{id}={帧,x,y,名}）；小图标位 off_4756E4[1..12]（环形 12 点）。
- **交互**：WM_MOUSEMOVE 读 pick → 变化时旧小图标帧 0 patch 64×64 擦除 + 旧大图区（中心-72/-64
  144×128）saveBackground 恢复 → 新小图标 + 新大图区存背景 + 画大图 + 名称 14 号 + 音效 39；
  **LBUTTONDOWN**（非 UP）确认 → 恢复大图 + 确认罩 + 条件图标重画 → #0041 → 播 FLC →
  `postModalExit(id-1)`；13（眼部区）→ 回浮字；右键/双击（state<3）→ 跳动画直达选择；
  倒计时结束**重绘帧 1 全身 = 重新睁眼**（漏画会"说完话眼睛一直闭着"）。
- **重写位置**：`src/app/magic_house_dialog.cpp:magicHandler`（tick 化；FLC 每 tick 按 speedMs 播 N 帧）。
- **经验/坑**：① 惩罚名文本表 = stride16 表第 4 字段——IDA 反编译显示 `off_475724+4*a1` 为**误解析**
  （实际 stride 16），4 步长得垃圾指针；② 大图区与 FloatMessage 框（320,384）可能重叠，
  hover 保存的背景会带入消息框残影——原版同构，勿"修复"；③ 确认点击在 **BUTTONDOWN**（与多数
  面板 UP 不同）；④ 动画开关 = `settings[1]`（動畫過程）关闭时倒计时 10→1 tick。

## 34. 拍卖竞价面板（0x43BDE5 / 0x43A2DD / 0x439F0D）

- **专项文档**：`43bde5-auction.md`。
- **原版依据**：panel.mkf[26]（184 帧）。静态：帧 0 背景 + 帧 25 左列罩 @(123,24) + 帧 17 右列罩
  @(67,63) + 缩略图帧（estate 90/91+char/29+5*map+level/115+5*map+level/50/131+map；corp
  103/104+char/51/151/51+5*(type-1)+sub/0x475BD2[map]+5*(t-1)+sub/0x475BE2[map]）@(232,180) +
  「標價：%d」+ 4 行（y=80+120i：头像 SPR panel[27/28/29+3*char] @(590,y)、现金 @(620,y+14)）。
- **交互**：100ms 状态机（#0131→#0132→轮转）；人类 7 按钮帧 `2i+4` 正常/`2i+3` 按下 @(406,
  g_auctionBtnY[i]=133+48i)：**wParam = 悬停索引 0..6**（PASS/+100/+500/+1000/+5000/+10000/Give up，
  `g_auctionBidSteps[1..5]` 0x475BA2 错位表——[0]/[6]/[7] 是相邻数据垃圾）；AI 按与预算
  （`auctionAiMaxBid` 0x439F0D）差额选档 + 封顶领先者现金+500 + 单人强制最低加价；
  出价 → 头像 SPR 翻滚（帧计数 ≤ SPR 帧数）+ 气泡（帧 1 @(480,rowY-15)：价/ＰＡＳＳ/放棄）；
  轮转判定：全出局→#0148 流标；v32−v33==1 且有领先→落槌（帧 21 罩 + 音 29 + #0135 + 表情翻滚
  0..60 + 台词 #0136..#0147 + 帧 25/17 重覆盖）→ `postModalExit(赢家玩家号)`。
- **成交链**：focusView → 1s → 产权转移（空地+地权写 expireDate）→ `buildMiniMapMarks` → 1s →
  `transferMoney(赢家, seller, 价, 0)`（seller=-1 → 公库）。
- **重写位置**：`src/app/auction_dialog.cpp:runAuction/auctionHandler/auctionAiMaxBid`。
- **经验/坑**：① `dbl_465018/465020` 是 **double 0.3/0.5**（IDA float 显示 4.17e-08/0.0 误读，
  disasm `fmul qword` 核对）→ AI 随机折扣 [0.5,0.8]；② 出局状态 word_48C436：1..8 初始原因
  （1住宿中…7賣方/8现金不足），**PASS 也置 1**（仅"不再竞争"计数，人仍在轮转名单）；放弃=出列；
  有效出价后**全部清零**（重新开放）；③ 表情帧 78+charIndex 只画在首个正常行（原版一次性 quirk）；
  ④ 赢家表情帧原版 blit 实参传 Rect.right/bottom=(223,95) 为笔误（同医院 bug），重写按 (183,65)；
   ⑤ 法槌闪烁双车道（帧 26/27 @(163,45) 28×15 + @(127,90) 16×14）纯装饰，相位 3=patch 恢复。

## 35. 图标条选择框（等级门槛灰度禁用，0x44101D / 0x4402D7）

- **用途**：研究所（corp type4）研发道具选择（`labDevelopDialog`），`44101d-lab-develop.md`。
  与 `selectFacilityDialog`（0x440AAC）同族（图标横条 + 悬停描边 + 居中名 + hover/click/cancel 音效），
  **增量 = "可选数 = 等级 `corp.sub`" 的门槛禁用**（未解锁图标灰度且点击无效）。
  触发在 landingEvent 收尾（loc_41B077，0x41B0B3..0x41B106）：升级询问/建设施等 owner 路径
  汇聚后判定 type4 → 弹本框（首次建研究所即弹；先"是否升級？"后研究）。
- **资源**：图标 = panel.mkf[11] 帧 10..14（**帧 = 道具 id + 1**，id=researchItem+8=9..13，
  機器工人/時光機/傳送機/工程車/核子飛彈，与道具栏 `kItemBagNames`/帧序一致）；
  底图 = estateTiles(data.mkf[517]) **帧 7（`+96`，400×89 五格木纹面板）@(20,280)，不透明
  `blitElementOpaque`（原版 blitBackground）**；标题框 **帧 5（`+72`，249×170）@(220,140)** 色键。
  帧头公式 = `12 + 12*n`。⚠ 曾把 +96 误当帧8（数字 '0'）→ 面板裸奔成黑块、+72 误当帧6 → 大气泡，
  2026-09-27 修正。
- **命中/布局**：5 格步进 76；图标 `blitElement` 实参 `(68+76i, 324)`（panel[11] 帧头 offset ≈(33,30)
  → 实际像素落 ≈ `(35+76i, 294)`，即描边/命中起点）；悬停命中 `x∈[35,406] y∈[297,351]`（原版含端点），
  `idx=(x-35)/76`；双描边 `drawRectBorder(32+76i, 294, 0x47, 59)` + `(33, 295, 0x45, 57)`
  （**颜色 = 调用点 `push 0FFFF00h` 黄**）；名 `drawText(kItemBagNames[9+idx], 220, 154)`。
- **灰度禁用（关键）**：`redraw` 顺序 = 底图帧7（不透明）→ 逐图标色键 blit → **未解锁区**（`i>=sub`）
  对 (35+76i, 297) 66×54 逐像素**灰度** `gray=(R+G+B+16)>>2`
  （原版 `sub_4553FE → funcs_4553F1[dword_47637C=0]=0x455442`，**不是**通道减半变暗）
  → 标题框帧5 → 标题 → 悬停描边/名 → 按住格 `pressDown(1px, kChannelHalf)`（`0x451B9E`）。
  灰度必须在**底图之上**做；交互：`LBUTTONDOWN`（`hover<sub`）播 click+下沉、`LBUTTONUP` 退出
  （**无音效**）、右键/ESC 取消；AI（`alive!=1`）不弹框、`idx = sub-1`。
- **重写位置**：`src/app/lab_dialog.cpp:labDevelopDialog/labEventHandler/redrawLab`。
- **经验/坑**：① 图标帧 `10+i` ≠ 选择框 index，是"道具 id+1"（id=idx+9），跨"研究所/道具栏"两套序勿混；
  ② `researchLeft` 写入恒 5，产出倒计时在 `updatePlayerStates`（非本控件）；
  ③ **`word_46CAEC` 不是颜色**——它是 640×480 surface 描述符首地址（首 word=640），
  描边色须看调用点 `push` 的 RGB888 值（本控件/设施/选人 = 0xFFFF00 黄，股市行选中 = 0xFFFFFF 白）。

## 36. 挂单网格 + 按下滑动子按钮 + 详情模态（交易市場「公佈欄」，0x4284BE / 0x427C21 / 0x42704E）

- **用途**：工具条 case 9 玩家间公开挂单交易（`trade_market.cpp`，`4284be-trade-market.md`）。
  三层模态：主面板（格子网格）→ 详情（`runModal`）→ 4 个挂单子对话框（嵌套 `runModal` + 数字框）。
- **资源**：`panel.mkf[73]`（20 帧）+ `[74]`（13 道具图标）。帧表见 `4284be-trade-market.md` §0。
- **主面板交互（原版"按下-滑动-松开"）**：
  - `LBUTTONDOWN` 命中三区记 `byte_48C2CA`：格子（有挂单）= `grid+100`、EXIT=2、投标带=1
    （槽 6 非空 → 提示「公佈欄已滿」+阻塞，不记位）；
  - `MOUSEMOVE` **仅 `byte_48C2CA==1`（按住投标区）时**更新子按钮 `byte_48C2CB = (y-125)/39 + 2*((x-473)/63) + 1`
    （2×2：左上股票/左下地产/右上道具/右下卡片 = 1039..1042）；
  - `LBUTTONUP` 按记位执行：1 → 子对话框、2 → 退出、≥100 → 详情。
  - 高亮 = `scaleElementChannels(surface, x,y,w,h, -12)`（`kChannelDim` 表）——**不是**描边。
- **格子网格**：72×72 @(104+72c, 114+72r)；图标帧 = `4*玩家 +20 字节 + type + 8`（type 1..4 × 2 色）；
  玩家棋子头像 blit @(66, 102+72r)（原版预绘进帧0 的 (44,36+72i)）。
- **详情模态**：背景帧 6/7/8（按 type）居中；标签是**原版预绘进帧内存**的文本（重写绘制时叠加）；
  数据列 @帧内(120/178)；左按钮（撤件/購買）自己撤件、他人 `confirmDialog`+购买，
  **取消/失败停留、其余任意点击退出**（原版 `PostMessage(0x205)`）；按钮悬停 = `pressDown` 下沉。
- **子对话框**：列表行悬停用 `drawRectBorder` **白色**（调用点 `push 0FFFFFFh`，0x425C23/0x4262CC；
  重写 `rgb888To555(0xFFFFFF)`）；股票列表行区 64+32i；地产 5 页签复用 `kQuerySubPageNames` +
  `collectOwnedAssetIds` 11 行/页，右侧 15px 宽 3 按钮（退出/上页/下页）；道具/卡片 panel[74] 图标 +
  卡名网格。
- **按下态 vs hover（2026-09-27 实机修正，务必区分）**：
  | 控件 | 原版消息 | 效果 |
  |---|---|---|
  | 股票/地产**列表行** | MOVE | 黄框 hover（唯一有 hover 的控件） |
  | 主面板格子/投标/EXIT | DOWN | `scaleElementChannels(-12)` 变暗 |
  | 股票 EXIT | DBLCLK | 帧18（重写：DOWN 画帧18、UP 退出） |
  | 地产 3 按钮 | DOWN | `highlightRect` 下沉（重写：DOWN 判定 `pressedBtn` + pressDown） |
  | 详情 撤件/購買/EXIT | DOWN | `highlightRect` 下沉（重写：DOWN 判定 `hlBtn`） |
  | 道具/卡片 格/EXIT | DBLCLK | `highlightRect` 下沉，矩形 (141+72c, 193+32r, 70, 30)（重写：DOWN） |
  | 主面板投标子按钮 | 按住滑动 | `scaleElementChannels(-12)` |
  重写首版把按钮按下效果误做成 MOVE hover（悬停即亮）——已全部改为 DOWN 显示、UP 执行。
- **嵌套模态退出残留**：帧1/2（336/416×416 @152/112,32）大于主面板（596×348 @22,66），
  上探工具栏区（32..66）、下探地图区（414..448）；提示框 (227,42) 也超面板顶。
  只重画主面板会残留 → 退出后必须 `renderGameFrame(app)` + `drawTradeMain(s)`
  （`redrawTradeMainFull`；原版靠 `saveBackground/sub_451EDB` 恢复）。
  **子对话框内数字框取消**（提示框 y=42 仍在）→ `redrawSubLayer` = `renderGameFrame` +
  `drawTradeMainBase` + 子对话框自身重绘（2026-09-27 实机"挂道具/卡牌取消后面板残留"根因）。
- **经验/坑**：① 原版股票/道具/卡片对话框 EXIT 与格子命中挂 `WM_LBUTTONDBLCLK`（**需双击**），
  重写统一单击（差异）；② 道具对话框 5 列网格（`v45>464` 换行），卡片 15 槽 5×3——原版绘制/命中列数
  曾不一致（bug），重写统一；③ 原版把列头/标签/EXIT 文本预绘进 sprite 帧（`sub_450441` 可写副本），
  重写绘制时叠加等价；④ **`sub_424502` 提示框**（panel[73] 帧5 184×88 @ (227,42/196) + 文本
  0x101010 居中 + 阻塞 1500ms）在数字框前显示、关闭后重绘对话框清除——曾用 `showMessage`
  （错误资源 g_tipFrame/位置）导致"缺顶部市值面板"；⑤ **blit 方式逐帧核对**：帧 3/4（道具/卡片，
  顶部整行透明）必须**色键**（原版 `sub_456418`），帧 0/1/2/5/6/7/8/9..19 不透明——曾全不透明
  造成黑底/黑边；⑥ 子对话框点击必须**按 UP 坐标重算命中**（原版依赖 MOUSEMOVE hover 记录 + UP 分发；
  对话框刚打开时无 hover 记录会点击无响应——"点物品/卡片不出数字框"根因）；⑦ 主面板子按钮底板帧17
  仅**按住**投标区时出现；棋子头像实参 (66,150+72p)；⑧ 详情价格值带「元」（格式串 `aS_43` 0x463EE0
  IDA 显示 "%s" 实为 "%s元"——BIG5 格式串视图截断坑）；⑨ 挂单数据操作一律先 `clearInvalidTradeOrders`。

## 37. 色彩蒙膜叠加（0x456C33 / 0x4554FC）
- 原版语义：按 SPR 掩膜形状非透明像素对目标像素做 **`|= 颜色`**（OR 半透明色彩叠加），
  两个使用者：① estate/corp `flag` 蒙膜（涨价红 0x7C00 / 查封蓝 0x003F，颜色表
  `word_488EF0[4*(flag&1)+色深]`，0x456C33）；② 高亮闪烁（pickBuffer==0xFFFF 像素按
  `byte_476380[i]` 索引查 `unk_485D68+32*k` 表变换，0x4554FC——**非线性含通道压缩，
  不是线性偏移**，重写曾误用 `k>>2` 致幅度 1/4）
- 资源：形状 = `g_pickMask`（MAP.MKF[26]）帧 0/1（71×47/51 estate）、2/3（143×103
  corp/specPt）、4（51×51 cellEnt）；SPR 8bit 索引，判定用 `frameBytes` 非零
- 交互：无（纯渲染叠加）；裁剪 = 全局裁剪矩形（地图区）；落点 = 实参 − 帧 offset
- 重写位置：`src/app/map_render.cpp:orColorMask`（flag 蒙膜，renderMap 内 estate/corp
  循环，位于 items 绘制前）；`drawEstateHighlight`（闪烁查表，`kHlTable` dump 自 0x485D68）
- 经验/坑：① OR 与"整色绘制"（0x456384 blitColorMask）不同，勿混用；② 蒙膜在物件
  绘制列表**之下**（原版排序前绘制），放 items 之后会被建筑盖住；③ flag 颜色选择是
  `flag&1`（高位 nibble 只是倒计时）

## 38. 全屏演出模态（滚动预览+sprite 行走，0x4060E9 / 0x456180 / 0x4562A5）
- 原版依据：`mapSelectWndProc 0x4060E9` WM_TIMER 0x406283..0x40657E：每 50ms tick
  **双缓冲全屏重铺**——`blitScrolledMap(dword_48A08C, g_mapPreview, scrollX)`（0x456180，
  640×480 RGB555 每行 1280 字节环绕拷贝，scrollX +=4/tick、1280 回绕）→
  `blitSpriteFrame` 角色（`panel[charIdx+100]` SPR 25 帧：5 站立 + 2 方向×10 行走，
  帧=((dir^1)·(count−4)/2+walk+5)；walkX −100..740 ±10/tick，出端 → walkY=rand%360+100
  随机重入+换向；x=walkX±90）→ 装饰（`panel[93]` 19 帧，帧表 `byte_46CCC4`
  {0,1,2,3,5,6|12,13,14,15,17,18}，x 与角色对侧 ∓90）→ 前景面板/文字 → flip
- **图集帧预合成** `blitElementToCanvas 0x4562A5`：**参数 = (dst帧描述符, src帧描述符,
  x, y)**（= `blitElement(dstW,dstH,dstPixels,srcDesc,x,y,0)`），落点=实参−src.offset、
  色键 0 透明——通关蓝星（jumpUi 帧10 27×27 off(14,14)）合成进面板底 @(220,40·i+28)
  与悬停项帧 @(32,22)；红勾（帧8 27×25）合成进点击项帧 @(214,8)
- 重写位置：`src/app/victory.cpp`（MapSelState/redrawMapSelect/mapSelHandler +
  gameClearFlow 素材加载/蓝星合成）；`UiImage::blitIntoFrame`（`src/render/ui_image.cpp`）
- 经验/坑：① **即时模式必须每帧全屏重画**——往残留表面叠滑入面板 = 重影（2026-09-29
  实机根因）；② 原版面板底帧 15/20 **已含 4 项槽位**，项帧 11-14/16-19 是**悬停高亮**
  （只画 hover 项），非全部常显；③ 单人右键不可退出（0x406970 仅 multiple 响应），
  点击确认后 10 tick（500ms）延迟退出（byte_48A43A/48A43B 状态机）展示红勾；
  ④ 进入 `musicPlayScene(0x8006)` bit15=不压栈、退出 `musicStop`

## 39. 演出打断与光标管理（0x4528B9 / 0x4544F6 / 0x45144F g_flcInterruptible / 0x450D97）
- 原版依据：`sub_4528B9(ms)` 通用延时消息泵 PeekMessage(PM_REMOVE) 检测
  WM_LBUTTONUP(514)/WM_MBUTTONDOWN(517)/WM_KEYUP(257) → 返回 1 **提前结束并吞掉该消息**
  （不派发窗口过程）；`sub_4544F6(ms)`（playLine 0x44EF41 台词等待）先等语音（同三消息打断
  → voiceStop 0x454493）再转 sub_4528B9 补足差额。`flcPlay 0x45144F` 的打断由
  `flcOpen flags bit1`（g_flcInterruptible 0x48C880，置位于 0x450D97）门控，命中提前退出
  = 停在当前帧、不重绘。**2026-09-29 全量回验 55 处 flcPlay 调用点 flags：仅开场
  playIntro（0x415A04/0x415B60/0x415C7E，flags=3/0x14000003）与月结悲情/冠军全身像
  （0x43894E/0x439050，flags 表 dword_475A0B/0x4759F7 值 3/515/1027/1539 全含 bit1）可断；
  游戏事件动画（神明插图 540..551/施工 523/拆除 526/入狱 538/住院 524/新闻 527..539/
  命运/魔法屋 553·529/得點券 537/卡片 536/骰子/跳伞 559/破产 555·556…）全部**不可断**。
  `sub_45285E`（物件飞行停留）纯延时不可断
- 交互（重写定稿 2026-09-29）：左键**按下** + Esc/Enter/Space（现代化跟手；原版为抬起，
  消息被吞不穿透下层按钮）
- 重写位置：`Application::consumeSkipInput()`（pumpEvents 置位、一次性消费，
  `include/game/application.h`）；事件驱动打断：showMessage（`message_dialog.cpp`）、
  playLine（`item_effects.cpp` itemLineHandler，打断+`stopVoice`）、showCardGet
  （`turn_system.cpp` cardGetHandler）、showGodNarration（`map_objects.cpp`）、
  新闻/命运阶段（`startMs` 提前到下一 tick 推进）、掛牌提示（`trade_market.cpp`
  tradeTipEvent）；标志轮询打断：`playEventFlc(interruptible)`（调用点默认 false=1:1 原版）、
  `playSettleFlc`（`month_settle_dialog.cpp`，原版表值全含 bit1=可断）。trace：
  `msg skip` / `line skip` / `flc skip`；场景 `tests/scenarios/300_skip_msg.txt`
- 光标：`Cursor::setHidden`（[NEW] 演出段隐藏软件光标，原版全程自绘不隐藏=纯增强）——
  `Application::renderFrame` 按 `eventFlcActive || parachuteActive` 统一驱动（事件 FLC
  含冻结旁白期 + 跳伞入场）；乐透开奖 state3/5、月结全身像段按界面级 state 设置。
  **乐透"轻微闪烁花屏"根因与修复（2026-09-29）**：`Cursor::update` 旧实现在鼠标移动/
  换帧时**先 `uncompose` 把上一轮 compose 保存的 32×32 旧背景写回表面**——即时模式界面
  （乐透投注/开奖每 tick 全量重绘、变暗格/装饰/FLC 持续变化）新背景与旧块不符 →
  下一帧 present 出现光标旧位置脏块一帧（鼠标静止不闪）。重写管线
  renderFrame 已保证"compose（抓当前干净背景）→ present → uncompose（精确恢复）"，
  表面恒为无光标内容 → update 改为**只推进动画与位置跟踪**，重绘交 renderFrame
  每帧 `if(!visible) compose` 从最新背景重抓（`src/render/cursor.cpp`）。
  `Cursor::invalidate()` 备用：调用方整帧重绘后作废保存块（增量绘制界面勿调，否则拖影）
- 经验/坑：① 演出等待入口先 `consumeSkipInput()` 清残留（上一次关对话框的按钮点击
  不应秒跳过下一段旁白/动画）；② showMessage 打断**不停语音**（原版 sub_4528B9 无
  voiceStop），playLine 打断**要停**（sub_4544F6 打断分支 voiceStop）——两套语义勿混；
  ③ **前进 GO 面板（0x417191）与演出叠加层的层级**：原版 = WM_PAINT 画完整场景
  （地图+工具条+右侧/前进面板，GO 在地图区 ~(180,120)）后，flcPlay/高亮/跳伞在
  backbuffer 上 saveBackground 后末层叠加 = **动画盖住 GO**；重写
  `renderGameFrame` 已同步为 地图→物件提示→面板(含GO)→跳伞/事件FLC/高亮
  （2026-09-29 层级修正，此前 GO 恒在动画之上）；
  ④ **按住查看的物件提示（0x417559）在演出期必须屏蔽+清除**（2026-09-29 实机反馈）：
  原版提示=一次性增量绘制，动画消息泵吞掉 WM_LBUTTONUP 也无妨——任何后续全量重绘
  自动抹掉；重写状态式即时重绘若不管，演出期间抬起被
  `gameEventHandler` 守卫吞掉 → 动画播完提示"幽灵悬挂"。重写收口
  `tipPerfBlocked(app)`（`game_loop.cpp`：eventFlcActive/parachuteActive/
  pendingSpawnPlayer/viewScrolling/任一 `playerActionState!=0`（1 移动 2 掷骰 3 特殊）/
  `events().depth()>1`（showMessage·playLine·对话框等任意模态））+
  `clearObjectTipForPerf()`（trace `tip clear (perf)`）：`renderGameFrame` 演出帧清除并
  不绘制、`handleLeftButtonDown` 演出期整体忽略（原版=重绘即时抹掉，等价"看不到"）、
  `flyObjectSprite`/`attachEnd`/`playHighlightBlink`/`showGodNarration`/`playEventFlc`
  入口显式清（这些不走带谓词的 renderGameFrame 或先快照背景）。纯静止停顿（AI 等待、
  倒计时、非控制无演出）**仍可按住查看** = 保留原版 0x4186BE 语义。trace `tip show`；
  场景 `302_tip_perf`（presssel/release 按住调试命令见 debug.cpp）
  ⑤ **前进 GO 面板在演出期的可见性（M4-B，2026-09-30）**：③ 只解决"层级"（动画末层盖 GO），
  盖不住"大面积透明 / 窄带素材"——524 住院是 **440×74 横带 @(0,210)**，GO@(180,120) 在其上方必然可见。
  根因在**控制位**：原版 `enablePlayerControl`(0x4196F1) 只在"纯等输入"窗口被调用（跳伞/事件 FLC
  之后，0x418C55 末尾动作），重写只看 `playerActionState==0` 就把控制位置回 true（**工具条-設定→
  認輸投降** = 最小复现）。收口三件：
  ① 新增**窄谓词** `blockingPerf(app)`（`src/app/game_loop.cpp`：`eventFlcActive ||
  parachuteActive || pendingSpawnPlayer`；**不含** `viewScrolling`/模态——原版这两类窗口面板
  本来就在画面上，直接复用 `tipPerfBlocked` 会误伤"滚动期 GO 消失""对话框后面板缺块"）；
  ② `drawAdvancePanel` 守卫加 `blockingPerf(app)` + trace `go panel draw cur=%d`；
  ③ 四个控制位入口显式 `disablePlayerControl`：`beginPlayerTurn` 起手 / `nextPlayer` 起手 /
  `surrenderPlayer` 起手 / `topBarFinish` 的 `canResume` 追加 `!blockingPerf(app)`。
  场景 `308_go_panel_surrender`（`surrender; trace.clear` 同帧清窗；**修前 75 次 cur=0 / 修后 0 次**）；
  新增命令 `trace.clear`（负向断言窗口前置）。`m4-plan.md` §14 有完整实测记录。
  ⑥ **事件 FLC 素材一律 440 宽**（2026-10-05 宽地图区实测）：事件动画帧/回卷区以
  native 440×440 地图区为基准；宽屏（地图区 >440）播放与回卷须水平平移 `(mapW-440)/2`
  （`drawEventFlcFrame` 与 `playEventFlc` 回卷区同源；native 偏移 0）。曾因回卷区未含
  该偏移，入狱/住院过渡动画在宽屏留拖影（2026-10-01 实机五轮修，见 `m4-plan` §18.9）。

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
