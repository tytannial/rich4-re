# 调试工具（--debug）

> [NEW] 重写新增的测试辅助功能，**无原版对应**（原版无此热键）。
> 实现：`src/app/debug_keys.cpp`（**薄封装**：指向桥接 + 键→命令模板）/
> `src/app/debug/debug.cpp`（**命令注册表 = 状态写入单一事实源**）；
> 开关 `GameState.debugMode`（`src/main.cpp` 解析 `--debug`）；
> 接入点 `src/app/game_loop.cpp` 的 `SDL_EVENT_KEY_DOWN` 分支。
> **headless 自动化测试**（虚拟时钟/随机注入/演出 trace/脚本断言）见 `docs/testing.md`；
> 每个键等价 `--exec "<命令>"`，同一命令层三种入口（热键 / `--exec` / `--script`）共用。
>
> **v2（2026-09-29）**：45→30 键精简 + 全部热键收口到命令注册表；相似功能按键盘行相邻聚组。
> 旧键位迁移表见文末。

## 启用

```powershell
./build/rich4.exe --game resources/game --debug
```

未加 `--debug` 时全部热键不生效（`debugMode == false`）。

## 键位

### Ctrl+数字（主键盘：构造/诊断，1/2·3/4·5/6·7/8 成对）

| 键 | 功能 | 等价命令 | 细节 |
|----|------|----------|------|
| `Ctrl+1` | **获得土地** | `get.land` | 指向地块 owner=当前玩家（estate/corp/specPt 三类通吃）+ 小地图/大地图刷新 |
| `Ctrl+2` | **获得街区** | `get.street` | 指向住宅联动组归当前玩家：同街同名全部（住宅）/ 全部连锁店（对齐原版收租联动组 [RE 0x419A67]）；仅改归属保留建筑 |
| `Ctrl+3` | **清空土地** | `clear.cell` | 指向地块归属清零：estate owner/level/type/expire、corp owner/sub/type/研究、specPt 经营权；**格上物件不动**（用 `Ctrl+Shift+P` 踢飞） |
| `Ctrl+4` | **清空街区** | `clear.street` | 指向住宅联动组全部归无主空地（get.street 反操作） |
| `Ctrl+5` | **获取全部** | `get.all` | 全图 estates+corps+specPts 归当前玩家 |
| `Ctrl+6` | **清空全部** | `clear.all` | 全图归无主+清建筑/设施/研究 |
| `Ctrl+7` | **升级土地** | `level.up` | 指向地块等级 +1（estate level / corp sub，上限 5） |
| `Ctrl+8` | **降级土地** | `level.down` | 等级 -1（下限 0） |
| `Ctrl+9` | **dump 指向地块** | `dump.pointed` | 日志详情：estate 名字/type/owner/level/flag/dir/priceAdd/priceBase/fees/price/expire；corp/specPt/evtCell/cellEnt 类似（事件格定位用） |

### Ctrl+Shift+字母/数字（按键盘行聚组）

| 键 | 组 | 功能 | 等价命令 |
|----|----|------|----------|
| `Ctrl+Shift+G` | 移动 | **传送+落地结算**：指向格→瞬移→`landingEvent`（买地/升级/收租/转盘/事件一次触发） | `land` |
| `Ctrl+Shift+H` | 移动 | **转向**：当前玩家朝向 +45°（8 向循环），行走资源组立即重载 | `dir.cycle` |
| `Ctrl+Shift+J` | 状态 | **入狱 3 天** [RE 0x43D593]（移監獄格/FLC 538/保险理赔；后按 [RE 0x40DEFE] 补回合推进） | `jail <cur> 3` |
| `Ctrl+Shift+K` | 状态 | **住院 3 天** [RE 0x43EC3F]（FLC 524） | `hosp <cur> 3` |
| `Ctrl+Shift+A` | 事件 | **触发新闻** [RE 0x44B6DF]（抽下一条可触发，阻塞 2400ms；后补回合推进） | `news` |
| `Ctrl+Shift+Z` | 事件 | **触发命运** [RE 0x44DB81] | `fate` |
| `Ctrl+Shift+R` | 事件 | **乐透保送中奖**：清号→当前玩家 11 注（>10 → 已售号随机必中）+ 奖金池注入 + 开奖 | `lottery.fixwin <cur>` |
| `Ctrl+Shift+L` | 建设 | **全图铺建筑**：有主住宅用地 level=1（快速测收租） | `all.level1` |
| `Ctrl+Shift+C` | 财务 | **现金 +100000**（`addMoney` [RE 0x41D3F4] 原生入账含月度统计） | `player.give <cur> cash 100000` |
| `Ctrl+Shift+V` | 财务 | **存款 +100000**（同上入账银行） | `player.give <cur> bank 100000` |
| `Ctrl+Shift+B` | 财务 | **点券 +1000**（監獄/醫院 NPC 保释·出院 300 点券用） | `player.give <cur> points 1000` |
| `Ctrl+Shift+N` | 财务 | **月初结息** [RE 0x439BFA/0x437E61]（panel[25] 存款×1.1 + 悲情/冠军演出） | `settle` |
| `Ctrl+Shift+E` | 财务 | **15 号分红** [RE 0x42BA97/0x42B3EB]（panel[76] 按比例分配，亏损分担=负分红） | `dividend` |
| `Ctrl+Shift+D` | 日期 | **过一天**（`advanceDay`：到期/每日事件/行情；跨月结息、15 号分红） | `day` |
| `Ctrl+Shift+O` | 物件 | **面朝前 1 格生成物件**：沿当前朝向取下一道路格（无出口回退首出口），类型 1..18 循环 | `obj.createAhead` |
| `Ctrl+Shift+P` | 物件 | **踢飞指向格物件**：机器娃娃沿路踢除子步复用 [RE 0x41B42D]——`bounceObject` 弹飞 + 删槽（含配对轮替/附身解除） | `obj.kick` |
| `Ctrl+Shift+Y` | 诊断 | **dump cellTable**：活动槽 type/ent/life/owner/fly + 玩家附身/挂件/luckA/B/C + 礼物池/道具库存 | `dump.celltable` |
| `Ctrl+Shift+8` | 诊断 | **dump 事件槽**（四大恶人 4..7 + 槽8 機器娃娃：busy/status/bailer/cell/dir/timers） | `npc.dump` |
| `Ctrl+Shift+F` | 卡道具 | **发放全部 13 种道具 ×2**（`givePlayerItem` [RE 0x445A4D]；id≤8 受礼物池限制）→ 工具条「道具」case7 使用 | `items.all <cur>` |
| `Ctrl+Shift+I` | 卡道具 | **灌卡 1..15**（清卡包直写 15 槽，不动赠卡池）→ 工具条「卡片」case8 逐卡实测 | `refill.cards <cur> 1` |
| `Ctrl+Shift+U` | 卡道具 | **灌卡 16..30**（与 I 合覆盖 30 卡） | `refill.cards <cur> 16` |

预留空位（新键优先就近分组）：`Q/W/T/M/X/S`、Shift+数字 `1..7/9/0`。

拾取与右键物件提示同一套：鼠标位置 → `pickMapObject`（复用渲染期 `mapHitRegions`，
含 SPR 像素掩码精确命中）。headless/脚本用 `select <kind> <i>` 代替鼠标指向。
日志前缀 `debug:`。

## 行为说明

- **不受 `gamePlayerControl` 限制**：AI 回合/掷骰/移动中也可改地块（便于布置测试场景）。
- **跳伞入场期间被屏蔽**（与其他输入一致，原版跳伞阻塞语义）。
- 修改后地图与小地图立即刷新（`buildMiniMapMarks`）。
- **`Ctrl+Shift+G` 传送同步刷新角色朝向**（[NEW v2]）：`prevCellEnt=旧格`、
  `dir=facingBetween(旧格→新格)` [RE 0x407A8C 同款]、附身物件同步（0x40FC00）、
  行走资源组重载（0x40B93B）——旧手写块不清 prev/dir = 传送后朝向错乱根因。
  `player.teleport`/`sel.goto` 命令同用合一 helper `teleportPlayerToCell`。
- `jail`/`hosp`/`news`/`fate` 直调后若当前玩家失能（stateFlags≠0）自动补回合推进
  （[RE 0x40DEFE]，`debugAdvanceIfIncapacitated` 仅热键路径；脚本用 `wait idle`）。
- **`trace.clear`**（**非热键**，仅命令/脚本）：清空 trace 环形缓冲，供"某一段之后不再发生 X"
  的负向断言；与业务命令用 `;` 并列可在**同一帧**生效（`execLine` 分号语义）。
  M4-B 场景 `tests/scenarios/308_go_panel_surrender` 用 `surrender; trace.clear` 卡窗口，
  见 `docs/m4-plan.md` §14。

## 典型测试流程

**一键测试任意地块机制**（推荐，省去走路与等待）：
1. 布置场景：`Ctrl+1`（指向地块归当前玩家）/ `Ctrl+2`（整条街）/ `Ctrl+5`+`Ctrl+Shift+L`（全图铺建筑）
2. 鼠标指向目标地块 → **`Ctrl+Shift+G`** → 立即触发结算（买地/升级/收租/转盘/事件）
3. 日志核对：`debug: land player 0 -> cell N dir=D` + `debug: key -> land`

**商業用地全流程**（购地→建设施→升级→他人收费——v2 走真实 UI，不再调试直建）：
1. `Ctrl+1` 指向無主城市地块 → `Ctrl+Shift+G` → 弹「費用N元 是否買下此地?」YES
2. 再 `Ctrl+Shift+G` → 弹「請選擇設施類別」**真实选设施面板**（公園/旅館/購物中心/加油站/研究所）
3. `Ctrl+7` 升级设施等级 → G 再落地弹升级询问；批量脚本用 `--exec "corp.build <i> <type> [sub]"`
4. 旅館/購物中心收费走**转盘**（左键提前停）；加油站先 `--exec "player.vehicle 0 1"` 换載具或道具機車/汽車

**神明/载具/状态调整**（v2 改 `--exec` 注入或真实玩法获得）：
```powershell
--exec "player.god 0 2"          # 大財神（槽=cellTableIdx，1..15）
--exec "player.vehicle 0 1"      # 機車（diceCount=travel+1 [RE 0x407236]）
--exec "player.state 0 3"        # 監獄状态（0无/1住宿/2出國/3監獄/4醫院/5冬眠/6夢遊）
```
真实入口：請神符（卡 23）、機車/汽車道具、命运/事件格。**免收租验证**：
`--exec "player.state <地主> 4"` → 他人 G 落地 → 「住院中 免收過路費！」

**事件类格子全部走传送**（v2 移除直开面板键 W/U/M/Q/K/0）：
指向对应格 → `Ctrl+Shift+G`：監獄(保释面板)/醫院(出院)/銀行(柜员机+停留週轉)/
樂透(投注)/百貨(商店)/魔法屋(施法)/四大恶人释放用 `--exec "npc.release <npc> [fromJail]"`。
股市面板/查詢面板用**工具条真实按钮**（`--exec "open.stock"` / `open.query` 亦可）。

**收租·整条街联动**：
1. `Ctrl+5` 全图归自己 → `Ctrl+Shift+L` 铺 level=1
2. 轮到 AI 时指向目标街按 **`Ctrl+2`**（整条街归 AI）；连锁店按 `Ctrl+2`（指向任意连锁店=全部）
3. 自己走到 → 联动地块闪烁（`highlightBlink` 16 帧）→「請付N元過路費」，收款入银行
4. 日志 `routeRent: owner=2 cur=(台北市 type=0) matched=4 sum=4800 total=4800 (M=1)`

**连锁店**：改建走**工具条卡片栏 case8 真实用改建卡**（所站格 `type^=1`）；
脚本 `--exec "card.rebuild"`。收费 = 同地主全部连锁店 ×2000×M。

**物件链（附身/挂身/弹飞/轮替）**：
1. `Ctrl+Shift+H` 调整朝向 → `Ctrl+Shift+O` 向**面朝前 1 格**生成物件（类型 1..18 循环）
2. 指向物件格 `Ctrl+Shift+P` **踢飞**（弹跳动画 + 配对轮替 [RE 0x40E278]：小↔大、惡犬↔土地公）
3. `Ctrl+Shift+Y` dump cellTable 核对槽位/寿命/飞行；`Ctrl+Shift+8` 核对事件槽/機器娃娃槽 8

**樂透保送**：`Ctrl+Shift+R` → 当前玩家 11 注 + 立即开奖（必中演出 + 奖金入现金）；
无人中奖路径：G 传送乐透格投注 → `Ctrl+Shift+D` 推到 15 号。

**監獄/醫院/保释**：`Ctrl+Shift+J/L` 入狱住院 → `Ctrl+Shift+D` 过天倒计时；
NPC 保释：G 传送監獄格弹面板（`Ctrl+Shift+B` 先补点券 ≥300）。

**道具/卡片实测**：`Ctrl+Shift+F` 全道具 + `Ctrl+Shift+C` 备现金 → 工具条「道具」case7；
`Ctrl+Shift+I/U` 灌上半/下半卡 → 工具条「卡片」case8；`Ctrl+Shift+Y` 核对库存。

## 已删键位迁移表（v1 → v2）

| 旧键 | 去向 |
|------|------|
| `Ctrl+2/3` 等级± | 主区 `Ctrl+7/8` |
| `Ctrl+4` 改建卡直调 | 工具条卡片栏 case8 真实使用 / `--exec "card.rebuild"`（命令保留供脚本） |
| `Ctrl+6` 全图铺 level=1 | `Ctrl+Shift+L`（`all.level1`） |
| `Ctrl+7` 现金 | `Ctrl+Shift+C`（改走 `addMoney` 原生入账） |
| `Ctrl+8` dump 地块 | 主区 `Ctrl+9`（`dump.pointed`） |
| `Ctrl+9` 联动组 | 主区 `Ctrl+2`（`get.street`）；新增清空对：`Ctrl+3`/`Ctrl+4`/`Ctrl+6` |
| `Ctrl+Shift+1..5` 设施一键布置 | G 落地真实 `selectFacilityDialog` / `--exec "corp.build <i> <t> [sub]"` |
| `Ctrl+Shift+V` 载具循环 | `--exec "player.vehicle <p> <0..2>"` / 真實機車·汽車道具 |
| `Ctrl+Shift+T` 神明循环 | `--exec "player.god <p> <slot>"` / 真實請神符 |
| `Ctrl+Shift+S` 状态循环 | `--exec "player.state <p> <0..6>"` / 真實事件 |
| `Ctrl+Shift+C` 清状态+神 | `--exec "player.state <p> 0; player.god <p> 0"` |
| `Ctrl+Shift+X` 寿命置 1 | 踢飞链改用 `Ctrl+Shift+P`（obj.kick）；飘走轮替自然触发 |
| `Ctrl+Shift+O` 当前格生成 | 改语义「面朝前 1 格」（`obj.createAhead`）——观察飞行/附身来向 |
| `Ctrl+Shift+6/7` 发免罪/嫁祸卡 | `Ctrl+Shift+I/U` 灌包后卡片栏选用 / `--exec "give.card <p> 21"` |
| `Ctrl+Shift+9` 批量释放 NPC | `--exec "npc.release 4"`…逐个（命令保留） |
| `Ctrl+Shift+B` 存款 | 保留（改 `player.give … bank`） |
| `Ctrl+Shift+P` 点券 | 保留（`player.give … points`）；P 位让给 obj.kick |
| `Ctrl+Shift+M/Q` 股市/查询面板 | 工具条真实按钮 / `open.stock`·`open.query` |
| `Ctrl+Shift+K` 买股 100 | 股市面板真实买入 / `stock.buy <p> <s> <n>` |
| `Ctrl+Shift+W/U` 百货/银行 | G 传送对应格原生触发（百货 case15/銀行 case14） |
| `Ctrl+Shift+0` 魔法屋（**死键**：派发层从未映射 `SDLK_0`） | G 传送魔法屋格（case16）原生触发 |

## 已知限制

- ~~联动高亮不含同盟地块~~ ✅ 2026-09-27；`get.street` 联动组定义不含同盟对象（构造用，无碍）。
- 主区数字键不含 `Ctrl+0`（派发层映射 `SDLK_1..9`）；需要 0 位键时先扩展 `game_loop.cpp`。
- `obj.createAhead` 的前 1 格按 `cellEnt.exits` 与朝向匹配，转角处可能回退首出口（日志有 `dir=`）。
- 不修改欠款矩阵（重写未建模，见任务 D 说明）。
