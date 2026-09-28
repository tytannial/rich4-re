# 自动化测试方案（headless / 代码级三支柱）

> [NEW] 重写工程测试基础设施，**无原版对应**。总原则：自动化只判定**代码级事实**
> （状态 / 演出 Trace / 字节-数据）；**画面观感（错位/时序/听感/色彩）一律人工验证**。
> 引擎侧全部能力在 `--debug` / `--headless` / `--test-clock` 门后，正常模式行为零变化。

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
  **跨平台警告**：MSVC 与 glibc 的 `rand()` 序列不同 → 断言不依赖具体随机值，
  随机分支覆盖一律走注入；种子仅用于单平台复现。
- `dbg::roll(slot, n)` ≡ `std::rand()%n`、`dbg::raw(slot)` ≡ `std::rand()`：未注入时**逐位等价**。
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
# 无头全 AI 冒烟（虚拟时钟快进 + trace 导出；确定性回归的基准命令）
./build/rich4.exe --game resources/game --headless --seed 42 --quickstart 0,4,0 `
  --trace-out build/trace.json --shot build/s.bmp --shot-frame 4000

# 确定性回归（存档往返字节稳定；--seed 固定随机）
./build/rich4.exe --game resources/game --save-selftest 0
./build/rich4.exe --game resources/game --debug --seed 12345 --load-game 0

# 参数：--seed <n>  --headless  --test-clock  --trace  --trace-out <json>
#       --quickstart <map[,players[,humans]]>
```

## 3. 待落地（既定路线，见会话计划）

1. **统一调试命令层**（`src/app/debug/`）：45 个热键→命令注册表 + `select`（替代鼠标指向）+
   `rng.*`/`fixture.save`/`assert`/`wait`/`click <region>`/`shot`/`quit`；`--script/--exec` DSL
   （`# id/covers/timeout` 头）；失败计数→退出码 + `--report <json>`。
2. **L0 CTest**：`rich4_core` 静态库拆分；公式/序列化/解析器单测。
3. **L1 场景 ~22 条 + L2 `--autoplay` 不变量长跑**；`tools/run_tests.py` 编排 + 覆盖矩阵
   （`# covers: [HELP idx]` ↔ `docs/help-checklist.md`）。
4. **named region 控件层**：各对话框控件表登记 → `click <region>` + `assert clicked=`；
   规范并入 `ui-controls.md`。
5. **trace 批2~4 全量插桩**（演出/判定边界约 40 点）。

## 4. 设计约束（实施时强制）

- 新增测试钩子一律 `[NEW]` 标注 + 默认路径零行为变化（`dbg::roll` 未注入=原式；
  `nowMs` 非虚拟=原 ticks；trace 非 enabled 即返回）。
- 注入槽只接「结果分支」；装饰随机保持 `std::rand()`（防序列漂移）。
- 断言优先级：`assert state` > `assert trace` > `assert log`。
- 覆盖矩阵未覆盖项即待办；help-checklist 状态列不因「有脚本」自动改 ✅（实机验收仍是关卡）。
