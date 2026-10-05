# 自动化测试方案（headless / 代码级三支柱）

> [NEW] 重写工程测试基础设施，**无原版对应**。总原则：自动化只判定**代码级事实**
> （状态 / 演出 Trace / 字节-数据）；**画面观感（错位/时序/听感/色彩）一律人工验证**。
> 引擎侧全部能力在 `--debug` / `--headless` / `--test-clock` 门后，正常模式行为零变化。
> **实施坑与设计取舍、待实机对照的疑点清单 → `docs/testing-issues.md`（推进时持续更新）**。

## 0. 三支柱与人工边界

| 支柱 | 载体 | 判定对象 |
|------|------|---------|
| 状态断言 | `assert <getter>`（待命令层落地） | `GameState` 字段（cash/owner/level/…） |
| 演出 Trace | `trace::logf`（§2，已落地批1） | 呈现请求**按逆向规格发生**（FLC 索引/坐标、消息文本、音效 id、转盘停值、转账方向、高亮集合…） |
| 字节-数据 | `--save-selftest`（已存在）、L0 单测（待建） | 存档往返稳定、公式表、解析器 |

人工职责：`--shot` 截图 + `tools/frame_align.py`（错位）、实机跑（动画观感/音频听感）。
**反哺约定**：人工发现的 bug 修复后，必须尽量降维成一条 state/trace 断言补进用例。

## 1. 已落地：引擎侧基础设施

### 1.1 虚拟时钟（`src/core/clock.{h,cpp}`）
- `nowMs()/delayMs()` 收口全部 `SDL_GetTicks/SDL_Delay`（44 处/15 文件；grep 校验仅剩
  `clock.cpp` 本体）。
- `--headless` 或 `--test-clock` → `setVirtualClock(true)`：`delayMs` 不睡眠、仅累加虚拟毫秒；
  `runModal` tick 循环每轮直接步进 `interval` 并派发 timer → 阻塞动画/消息
  （`playEventFlc`、showMessage 1500/2000ms、旁白 2400ms、骰子停留 500ms、飞行…）
  在 headless 下按虚拟时间**快速收敛**，整局测试秒级完成。
- 正常模式透传原语义（行为零变化）；开启时以当前真实 ticks 为基准（与既有 `startMs`
  比较连续）。

### 1.1b headless dummy 运行时（`Application::init(dir, headless)`）
- `--headless` → `SDL_SetHint(VIDEO/AUDIO_DRIVER,"dummy")` + VSync 关闭；窗口/renderer
  照常创建（软件渲染），**Surface 绘制与 `saveBmp` 全在 CPU，与驱动无关**。
- **已验证事实**：同一时刻 dummy 截图与真窗口截图**字节一致**；
  `--load-game 0`（存档→渲染→截图）headless 全链通过；
  `--headless --seed 42 --quickstart 0,4,0 --shot-frame 4000` 两次运行
  trace 499 条逐条一致 + BMP 一致（**可复现性成立**；唯一非确定字段=modal `h=` 指针，
  ASLR 所致，比对时归一化）。
- **合成输入无需"坐标直通层"**：dummy renderer 的 `SDL_RenderCoordinates*/WarpMouse`
  数学路径与真窗口一致，既有 `--shot-cursor/--auto-click/--game-click/--game-key`
  在 headless 下实测可用（点击可进选人对话框）。
- 性能：4000 帧全 AI 局 ≈ 22s（~180fps，CPU 全图重绘主导）；如需更快可加 `--no-render`（待）。

### 1.1c `--quickstart <map[,players[,humans]]>`
- `Application::run` 跳过主菜单模态；`newGameInit` 跳过 `newGameDialog`，预填
  `NewGameConfig`（charId=0..n-1，前 humans 位人类、其余 AI）。
- L1 场景：`humans>=1`（脚本驱动当前玩家）；L2 长跑：`humans=0` 全 AI 自动。

### 1.2 确定性与定点注入（`src/core/debug_hooks.{h,cpp}`）
- `--seed <n>`：`gameInit` 播种改读 `dbg::seed()`（未提供则原版 tick 播种语义）。
  **跨平台一致性**：随机数已收口自带 PRNG（`src/core/rng.{h,cpp}` 复刻 MSVC CRT LCG，
  RAND_MAX=32767）→ `--seed` 两平台序列 **bit 级一致**，全量场景矩阵可跨平台复用
  （2026-09-30 验证：93 场景两平台结果完全相同，见 docs/cross-platform.md §6/§8）。
- `dbg::roll(slot, n)` ≡ `rng::next()%n`、`dbg::raw(slot)` ≡ `rng::next()`：未注入时**逐位等价**。
  已替换全部「结果分支」随机点（~60 处/22 文件）；**装饰类随机（眨眼/口型/表情/柜台动画/
  小游戏交互内生成/栏位洗牌）保留 `std::rand()`**——不接注入，避免消耗序列改变默认行为。
- 注入为 **one-shot**（命中一次即清除；`rng.clear` 清全部；`SlotAny` 兜底任意未专用槽的下一发）。

| slot | 覆盖点 | 取值约定 |
|------|--------|---------|
| `dice` | 骰子（turn_system 0x41…%6+1） | 0..5（调用处 +1） |
| `roulette` | 轮盘停格（roulette_dialog 0x43F7C6 %12） | 0..11 |
| `card` | 赠卡池抽卡（0x441E12）/ 死神没收抽卡 | 候选下标 |
| `item` | 礼物池抽道具（0x445ADA） | 候选下标 |
| `lottery` | 乐透摇号（0x43010C）/ AI 选号 | 0..35 或候选下标 |
| `news` | 新闻随机公司/股（0x44B6DF 效果内） | 候选下标 |
| `fate` | 命运判定表选择（0x44DB81） | 候选下标 |
| `magic` | 魔法屋条件 %12 / 惩罚 %11（0x43380A） | 条件 0..11 / 惩罚 0..10 |
| `minigame` | 挖寶埋宝位 / AI·动画关兜底分（0x415457） | 原始值 |
| `ai` | AI 用卡/道具（rand&1）、AI 选目标、保释决策、银行 AI | 原始值 |
| `aitrade` | 交易市場 AI 挂单 | 原始值 |
| `corp` | AI 建设施类型 %4+1（0x41A24D） | 0..3 |
| `god` | （预留：神明选格） | — |
| `npc` | NPC 行走目标/步数（0x41C7A6/0x498E80） | 候选下标 |
| `spawn` | 开局出生点/随机选角（0x40715F 段、relocate 换位、prevCellEnt） | 候选下标 |
| `jackpot` | 老虎机转数/金额（0x440706 老虎机、大財神 threshold） | 原始值 |
| `luck` | 运气判定 coin（0x44B896 luckB/C &1） | 0/1 |
| `shop` | 百货商品池数量/种类/自家赠礼二选一（0x42E931/0x42D37F） | 原始值 |
| `stock` | 股价漂移/噪声/交易量抖动（0x4291D6） | 原始值（0..32767 域） |
| `auction` | 拍卖 AI 出价系数（0x43BDE5/0x43A2DD） | 原始值 |
| `any` | 兜底：未专用注入槽的下一发 roll/raw | 原始值 |

### 1.3 演出 Trace（`src/core/trace.{h,cpp}`）
- `--trace` / `--trace-out <file.json>`（给路径即自动开启）。非 debug 不记录（一次 bool 判断）。
- 内存环 8192 条；API：`contains/count/lastMatch/exportJson`；脚本断言 `assert trace <substr>`
  与 JSON 报告都读此通道。
- **批1 格式表**（`kind k=v` 空格分隔）：

| kind | 字段 | 来源函数 |
|------|------|---------|
| `flc idx= x= y= sound= freeze= switch=` → `flc done idx= frames=` | 事件 FLC 播放 | `playEventFlc` 0x45144F |
| `msg text= ms=` | 阻塞消息（文本原样→BIG5 截断类 bug 可断言） | `showMessage` 0x440CAC |
| `float text=` | 浮动消息 | `FloatMessage::show` 0x44EE18 |
| `sfx play id= [looping=1]` / `sfx stop id=` | 资源音效 | `Audio::playEffect* / stopEffect` |
| `sfx slot= play [looping=1]` / `sfx slot= stop` | 游戏音效槽（单 buffer 替换语义） | `Audio::playEffectSlot* / stopEffectSlot` |
| `voice id=` | 角色语音（Speaking.mkf） | `Audio::playVoice` 0x45441A |
| `line p= expr= text=` | 台词统一入口（playItemLine/playCardLine/playValueLine 均经此） | `playLine` 0x44EF41 |
| `hl owner= ally= type= estates=` | 收租联动高亮集合 | `startRouteHighlight` 0x451985 前置 |
| `fly obj= from= to= hold=` | 物件飞行（"飞过去再出现"时序） | `flyObjectSprite` 0x40E669 |
| `land cell= obj= type= p=` | 落地结算分派 | `landingEvent` 0x41982D |
| `roulette theme= cell= value=` | 转盘停止值与查表结果 | `roulettePrompt` 0x44090E |
| `cardget id= text=` | 得卡展示 | `showCardGet` 0x441F73 |
| `modal push h= tick= depth=` / `modal pop h= result=` | 模态栈开合（handler 指针；named handler 表随控件层落地） | `runModal` 0x4018E7 |

批2~4（数值判定/实体/事件/UI 状态机：`transferMoney/addPlayerDebt/stockTick/newsEvent idx/
fateEvent idx/attachObject/jailPlayer/hospitalizePlayer/createMapObject/NPC 恶行/
magicHouse 惩罚/结息分红/小游戏 phase/…）随测试用例铺开。

### 1.4 内存日志环（`log.cpp`）
- 最近 4096 条（含级别前缀）；`logContains/logCount/logLastMatch/logClear`
  → 脚本 `assert log <substr>` 数据源（与 trace 互补：log=既有诊断，trace=规格断言）。

## 2. 命令行（当前）

```powershell
# 场景脚本（断言失败 → 退出码 1；--exec 多命令用 ; 分隔）
./build/rich4.exe --game resources/MultiverseJourney --headless --seed 42 --quickstart 0,4,1 `
  --script tests/scenarios/20_rent_route.txt

# 无头全 AI 冒烟（虚拟时钟快进 + trace 导出；确定性回归的基准命令）
./build/rich4.exe --game resources/MultiverseJourney --headless --seed 42 --quickstart 0,4,0 `
  --trace-out build/trace.json --shot build/s.bmp --shot-frame 4000

# 确定性回归（存档往返字节稳定；--seed 固定随机）
./build/rich4.exe --game resources/MultiverseJourney --save-selftest 0
./build/rich4.exe --game resources/MultiverseJourney --debug --seed 12345 --load-game 0

# 参数：--seed <n>  --headless  --test-clock  --trace  --trace-out <json>
#       --quickstart <map[,players[,humans]]>  --script <file>  --exec "<a; b; c>"
```

### 2.1 脚本 DSL（`src/app/debug/debug.cpp`，[NEW]）

- 行式；`#` 注释（行首/空白后）；`id:/timeout:` 元数据；双引号 token 含空格。
- 执行器挂在 `Application::renderFrame` 尾部每帧步进；**阻塞命令**（land/day/news/fate/
  magic/jail/hosp/settle/dividend/shop/bank/lottery.draw/open.*）仅在顶层帧执行；
  其阻塞期间重入帧**只放行交互应答 step**（`click/rclick/move/key/confirm` + wait 轮询），
  其余 step（含 assert）冻结 pc——保证断言严格晚于阻塞命令完成
  （典型：`land` 内部 FLC537 动画阻塞时，`assert points>=50` 不会提前读到未入账值）。
  天然支持 `land`（内部弹框挂起）→ `confirm yes`/`click`（应答解除挂起）→ 继续断言。
- 失败计数 → `main` 退出码（CI 契约）；`quit [code]` 结束脚本。

**命令表**（`--exec "help"` 暂无，见代码 kCmds）：

| 类别 | 命令 |
|------|------|
| 选择 | `select estate\|corp\|specpt\|cell <i>` · `select clear` · `find.estate <owner>` · `assert state selected >= 2000` |
| 场景 | `estate.owner\|level\|type <i> <v>` · `sel.owner\|sel.level <v>` · `corp.build <i> <type> [sub]` · `corp.owner\|level <i> <v>` · `specpt.owner <i> <p>` · `all.owned <p>` · `all.level1` |
| 玩家 | `player.cash\|bank\|points <p> <v>` · `player.state <p> <0..6>` · `player.god <p> <slot>` · `player.vehicle <p> <0..2>` · `player.teleport <p> <cell>` · `give.card\|item <p> <id> [n]` |
| 事件 | `land [cell]`（默认 select 目标；teleport+landingEvent 同步） · `day [n]` · `news` · `fate` · `magic` · `lottery.draw` · `settle` · `dividend` · `shop` · `bank` · `jail <p> [d]` · `hosp <p> [d]` · `open.stock` · `open.query` |
| 注入 | `rng <slot> <v>`（one-shot，见 §1.2 槽表）· `rng clear` · `seed <n>` · `settings.anim <0|1>` |
| 断言 | `assert state <path> <==\|!=\|>=\|<=\|>\|<\> <v>` · `assert log\|trace <substr...>` · `assert no log\|trace <substr>` · `trace.clear`（清 trace 环形缓冲，"某段之后不再发生 X"负向断言的前置；与业务命令用 `;` 并列可**同帧**生效，见 308 场景） |
| 等待 | `wait frames <n>` · `wait log\|trace <pat> [max]` · `wait idle [max]`（等 gamePlayerControl） |
| 交互 | `click <x> <y>` · `rclick` · `move` · `key <spec>`（ctrl+/shift+）· `confirm yes\|no`（栈顶模态直接应答，免脆坐标） |
| 观察 | `dump state\|player <p>\|estate <i>\|corp <i>`（走 log，配 assert log） · `shot <path>` · `quit [code]` |

**state 路径**：`players.<p>.cash|bank|points|loan|stateFlags|alive|travel|god|cell`、
`estates.<i>.owner|level|type`、`corps.<i>.owner|sub|type`、`specpts.<i>.owner|fund`、
`cur`、`turn`、`day`、`month`、`year`、`players`、`moneyMul`、`publicFund`、`selected`。

**注意**：轮盘注入生效点=**最终停靠格**（人类点击路径指针会演化，入口不消费、出口覆写，
roulette_dialog 0x43F7C6 处 [NEW] 注释）；`roulette theme=<t> cell=<c>` trace 断言最终值。

### 2.2 现有场景（tests/scenarios/，全部 headless 绿）

| 脚本 | covers | 验证点 |
|------|--------|--------|
| `00_smoke.txt` | 冒烟 | quickstart 进局、wait idle、day、trace/log 管道 |
| `20_rent_route.txt` | [HELP 21] | 他人住宅收租：find.sel.land→状态机续帧转账（wait log "rent: player"）→cash 降 |
| `30_buy_land.txt` | [HELP 21] | 无主购地：land 弹确认框（阻塞）→重入 `confirm yes`→扣款 |
| `40_roulette_hotel.txt` | [HELP 22] | 旅館轮盘：`rng roulette 7` 注入→人类 click 停→`roulette theme=1 cell=7` trace→收费转账 |
| `41_roulette_no_inject.txt` | [HELP 22] | 旅館轮盘**无注入回归**：首次不得被 `g_inject` 零初始化钉 cell0→255（`assert no trace "value=255"`，ISS-019） |

**AI 行为专项（2026-09-28，`run_tests.py --filter ai_` 全绿）**——覆盖道具链（0x420E9A）、
卡片链（0x41E69E/0x420xxx）、回合动作链（0x418DE6..0x418E70，档案 `418c55-ai-turn-actions.md`）：

| 脚本 | 覆盖 | 验证点 |
|------|------|--------|
| `84_ai_marathon` | L2 全 AI 长跑 | `wait log aiItemSelect` 链路实际执行 + 120k 帧自然回合不崩（errors==0；实测触发 道具312/卡73/骰调29/买股16/卖股5）。**常规矩阵不纳入**（2026-09-30 用户约定：非主动要求不跑），按需单跑 |
| `260_ai_items` | 道具 AI 候选域 | aiItemSelect 命中/`rejected` 日志；target 在前方/回溯路径∩视口（0x40B221/0x40B343） |
| `262_ai_turn_guard` | 0x418E31 状态守卫 | AI 置态后全程 `assert no trace " p=1"`（不掷骰不移动） |
| `264_ai_dice_adjust` | 0x4221C0 骰子调整 | 汽车 AI → `aiDiceAdjust: ... ownFree=4 other=0` → diceCount 1..3 |
| `266_ai_bank_advance` | 0x418DFE 週轉接入 | 非经营者欠 bankAdvance → AI 回合「銀行經營權易主」清算 |
| `268_ai_stock_buy` | 0x42BF03 | rng(ai)=0 过门槛 → `aiStockBuy: p1 stock=n`（预算只用 bank、24/6 日均线择优） |
| `270_ai_stock_sell` | 0x42C79F | 持仓 + rng → `aiStockSell`（初始配股 AI 卖出）；贷款临期强制循环 |
| `272_ai_card_recheck` | 卡片 AI 链 | 给 AI 发查封/烏龜/怪獸/同盟/黑卡/涨价 → aiCardSelect 命中/拒用日志链 |

### 2.3 named region 控件层（基座已通，逐面板铺开中）

- debug::registerRegion(name,x,y,w,h)：产品侧 nterGameLoop 一次性登记，矩形与既有
  命中判断同源常量、不改产品逻辑（[RE 0x417E26 工具条 x/40]、[RE 0x4182FA 页签 y/70]）。
- 脚本 clickr <name>：查表取中心合成 move+click（两帧），并记 	race clicked name=；
  配合 ssert trace dialog_open（高频 9 面板已带 dialog open name= trace），
  交互场景不硬编码像素。
- 已登记：	op.0..top.8 工具条（0帮助 1设置 2AI 3读 4存 5地图 6查詢 7道具 8卡片）、
  	ab.0..tab.3 页签（资金/地產/股票/其他）。
- 待登记（各面板入口一行）：前进 GO/骰子、百货行列与按钮、乐透网格、股市買賣、
  魔法屋惩罚、银行、監獄保释格——Q-D 系列场景的前置。
- 示范场景：90_topbar_query.txt（clickr top.6 打开查詢面板并断言）。

## 3. 待落地（既定路线）

1. **L1 场景铺开**：按覆盖矩阵补至 ~22 条（corp 五类/事件 case2..16/卡 30/道具 13/银行/乐透/
   股市/NPC/结息分红…）；`python tools/run_tests.py --matrix` 即未覆盖工作清单。
2. **named region 控件层**：各对话框控件表登记 → `click <region>` + `assert clicked=`；
   交互密集场景（百货/股市/银行/魔法屋选惩罚）依赖此项；规范并入 `ui-controls.md`。
3. **L2 长跑不变量**：封装 `invariants` 脚本（现可手工 `--headless --quickstart 0,4,0` 跑通）。
4. **trace 批2~4 全量插桩**（演出/判定边界约 40 点）。
5. **待登记 UI（交互链下一批）**：银行柜员机 18 键面板（kVisitRects 0x475914 →
   atm.btn.*）、研究所/前进面板内部按钮；64 场景已覆盖 99 条主干+多数模态。
6. ~~旧热键迁移到命令注册表~~ ✅ **2026-09-29 v2**：45→30 键精简（删面板直开/单卡/设施直建等
   重叠与功能期键，迁移表见 `debug-keys.md`）；热键层重写为「指向桥接 + 键→命令模板 →
   `debug::execLine`」，状态写入单一事实源；传送合一 helper 含朝向刷新。

### 3.1 已完成：L0 单测 + 编排器

- CMake 拆分 `rich4_core`（不含 main）静态库 + `rich4_tests`；`enable_testing()/add_test`；
  `ctest`（build 目录）或 `rich4_tests.exe [gameDir]`。
- `tests/unit/`（microtest.h 自写框架，无外部依赖）首批 22 checks：
  `estateRouteRent` 同路段求和/连锁×2000/moneyMul、存档 `save∘parse∘save` 字节稳定、
  MkfArchive/LZHUF 解压确定性。
- `tools/run_tests.py`：ctest + 场景批量（超时看门狗/`--filter`/`--repeat`/`--extra`）+
  `build/test-report.json` + **覆盖矩阵**（`# covers: [HELP n]` ↔ help-checklist 99 条，
  输出未覆盖清单）；任一失败 → 退出码 1。
  `--extra "<rich4 参数>"` 透传给每个场景（如 `--extra "--canvas 1024x768"` 做大画布回归，
  M4-A1 用；见 m4-plan §15.4）。
- **性能诊断**：`rich4 --stats`（每 120 帧输出 `perf: FPS (ms/frame) canvas=WxH scale=S`，
  墙钟窗口平均；`--preset free|wide` 放大回归用；M4-A2 加，见 m4-plan §16）。

```powershell
python tools/run_tests.py --matrix
python tools/run_tests.py --filter rent --repeat 3
python tools/run_tests.py --no-ctest --extra "--canvas 1024x768"
```

## 4. 设计约束（实施时强制）

- 新增测试钩子一律 `[NEW]` 标注 + 默认路径零行为变化（`dbg::roll` 未注入=原式；
  `nowMs` 非虚拟=原 ticks；trace 非 enabled 即返回）。
- 注入槽只接「结果分支」；装饰随机保持 `std::rand()`（防序列漂移）。
- 断言优先级：`assert state` > `assert trace` > `assert log`。
- 覆盖矩阵未覆盖项即待办；help-checklist 状态列不因「有脚本」自动改 ✅（实机验收仍是关卡）。
| 50_god_free_rent.txt | [HELP 42] | 大財神免付所有租金（assert no trace xfer + cash 不变） |
| 300_skip_msg / 302_tip_perf / 304_music_resume / 306_music_rotate | M3-B | 演出可打断（msg/line/flc skip）/物件提示演出期屏蔽（press·release）/场景音乐栈续播/曲目游标换曲 |
| 308_go_panel_surrender | M4-B | 前进面板演出期可见（0x417191）：`surrender; trace.clear` 同帧清窗 → 認輸 + 死神选人窗口内 `go panel draw cur=0` 必须为 0（**修前 75 次 → 修后 0 次**） |
| 310_host_release | 托管 | 面板 toggle 托管（`config applied`）→ 当前回合起自动行动（`type=5` + `landmove p=0`）；AI 回合放行工具条第 3 按钮（`topbar idle host button down`）→ toggle 解除 → 下回合 `type=1` 恢复输入 |
| 312_npc_tip_name | M4-H | 事件槽 NPC 悬停提示名（原版 0x417B9F `dword_47ED5A` 分支）：release→teleport 同格→press 强制渲染→clickplayer 4（扫 0xF004 掩码点）→ `tip show id=61444`（小偷）。**注**：直改状态后 C1 idleFrame 跳过静止帧，脚本须 press 触发渲染 |
| 314_overlap_multi_player | M4-H | 同格多玩家拾取顺序（叠放上层=后绘制者）：player 0/1 teleport 同格 → clickplayer 1 → `tip show id=61441` |
| 316_minigame_money_doll | M4-H | 喜從天降交互版（settings.anim 1）：`minigame enter money` + `move` 合成鼠标驱动娃娃（mouseLogicalPos/鼠标覆盖）+ `minigame score` trace（trace 为本次新增） |
| 320_wide_layout_hit | M4-D | 布局派生命中区：`clickr tab.2`（panelX+176 派生）→ `tab switch 2`；`clickr top.6` → `dialog open name=query`；native 与 `--extra "--preset wide"` 双跑 |
| 52/54/62/64/66/68/70/72/74/82/84/90/94/96/100/102/103 | 见 run_tests --matrix | 得點券/監獄/卡片格/小衰神流标/醫院/新聞命運/NPC释放/连锁收费/改建卡/公園/行業設施點缴费/查询面板clickr/请神符+老虎机tap/漲價flag/樂透投注grid/監獄保释grid/醫院出院grid |
