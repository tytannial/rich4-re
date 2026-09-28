# 测试推进疑难点记录（testing-issues）

> [NEW] 实施 headless 自动化过程中的**坑、设计取舍、以及代码级无法自证、需实机对照筛选的疑点**。
> 每条格式：**现象 → 根因 → 方案 → 状态**。§2 的 Q-x 是给你的实机验证清单（可对照勾销/补脚本）。
> 与 `docs/testing.md`（能力文档）互补：这里记"为什么这样设计 + 哪里还悬着"。

## 1. 实施期已解决（代码级结论）

### ISS-001 墙钟收口的时序风险
- **现象/风险**：44 处 `SDL_GetTicks/SDL_Delay` 统一为 `nowMs()/delayMs()`，headless 下
  `delayMs` 不睡仅累加——若某等待语义不是"时间到"而是"帧数到"，快进会改变行为。
- **方案**：逐处核对均为 `while(now<start+N){pump;render;delay}` 型超时 → 虚拟时间等价成立；
  `runModal` tick 循环加显式 `clockAdvanceMs(interval)` 步进（虚拟模式不空转）。
- **状态**：✅ 已实现；正常模式透传。**残留疑点见 Q-A**（正常模式实机回归）。

### ISS-002 注入边界：结果分支 vs 装饰随机
- **决策**：~60 处**影响结果**的 `std::rand()` 换成 `dbg::roll/raw`（未注入时逐位等价）；
  **装饰随机**（眨眼/口型/柜台动画/气球生成/洗牌展示顺序/台词二选一 `&1`）**保持原式不接钩**。
- **理由**：若装饰也走 raw()，`rng any` 兜底注入会吞掉装饰消耗、序列漂移不可预期；且装饰正确性
  归人工。`--seed` 保证同平台整局可复现。
- **状态**：✅。**注意**：跨平台复现只看不变量（ISS-007）。

### ISS-003 轮盘注入的生效点（踩坑修正）
- **现象**：`rng roulette 7` 注入后实测停格=2——注入值被**入口起始格**消费，人类点击后指针
  继续演化，最终格失控。
- **根因**：原版语义：入口 `cell=rand()%12` 只是**初始位置**；人类点击→减速随机前进→停在
  下一个有效格。注入点选错。
- **方案**：入口"有注入则不消费"（cell=0 起步），**出口 phase≥6 时若仍持有注入则覆写最终停靠格**
  （`roulette_dialog.cpp` 0x43F7C6 处 [NEW] 注释）。未注入零 rand 消耗、行为不变。
- **状态**：✅（40 场景 `roulette theme=1 cell=7` 断言通过）。

### ISS-004 阻塞命令与断言时序（两次迭代，最终规则）
- **现象**：`land` 后紧跟 `assert cash<…` 读到**旧值**；第二次 `dump` 打印**修改前**数据。
- **根因链**：
  1. `land` 内部状态机在**返回后的续帧**才完成转账（showMessage 自动超时→transferMoney）；
  2. 更普遍：任何命令内部都可能先弹**台词/框模态**（如 `cardRebuildEffect` 先
     `playCardLine` 阻塞 1 秒再改数据），阻塞期间 renderFrame 重入 tick，后续 step 被提前执行。
- **方案（最终）**：Cmd step 执行期间统一 `g_inBlock=true`；重入帧**只放行交互白名单**
  （`click/rclick/move/key/confirm`）与 wait 轮询，其余 step 冻结 pc → 严格保证
  "命令完全结束（含内部模态）→ 才执行下一条 step/断言"。
  转账类"续帧完成"用 `wait trace "xfer …"` / `wait log "rent: player"` 显式同步点。
- **状态**：✅ 16 场景全绿；`--exec` 默认 40000 帧上限防弹框无应答挂死。
- **建议**：新场景写断言前先 `dump`/`wait trace` 建同步点；同步点选择优先级
  `wait trace > wait log > wait frames`（后者最脆）。

### ISS-005 dummy 下坐标 API 可用 → "逻辑坐标直通层"取消
- **计划**：为 headless 写绕过 `SDL_RenderCoordinates*/WarpMouse` 的事件直投层。
- **实测**：SDL3 dummy renderer 的 logical presentation 数学路径与真窗口一致，既有合成输入
  在 headless 完全可用（点击可进选人框）；**直投层不需要**。
- **保留改造**：`Application::setMouseOverride`——合成 move/click 更新 `mouseLogicalPos()`
  （headless 无真实鼠标，拾取/物件提示依赖它）；真实鼠标一动即清除覆盖。

### ISS-006 人类轮盘不会自动停 → 合成点击需置 `uiClicked`
- **现象**：轮盘 `humanCtl` 模式下 6000 虚拟帧不停（等鼠标左键），脚本挂死。
- **方案**：合成左键时同时 `app.setUiClicked()`（与真实 WM_LBUTTONDOWN 语义一致），
  脚本 `click 320 240` 即可"点停"转盘。

### ISS-007 跨平台随机序列差异（CI 约束）
- **事实**：MSVC 与 glibc `rand()` 序列不同 → 同 `--seed` 在 Linux 得到不同局。
- **策略**：Linux CI 只跑 **L0 单测 + L2 不变量**（结构/守恒/无 ERROR，不依赖具体值）；
  L1 场景的确定性断言（金额/停格）全部走**定点注入**，不依赖序列；
  视觉基线（若将来做）分平台目录。

### ISS-008 `land` 直落不产生步数 → `player.dice`
- **现象**：加油站 land 后 0 元转账（公式=500×载具倍率×**步数**×M，`diceValue=0`）。
- **方案**：新增 `player.dice <p> <v>`（`[RE 0x48BAFC] g_diceValue` 是**全局**非 per-player，
  首版命令写错已修）；真实掷骰路径不受影响（`rng dice` 注入仍作用于骰子 roll）。

### ISS-009 确认框应答不走坐标点击 → `confirm yes|no`
- **理由**：YES/NO 半区坐标随面板尺寸/offset 变，脆；`confirm` 直接对栈顶模态
  `requestExit(1/0)`（对齐 0x401966 原版 postModalExit 语义），确定性应答。
- **限制**：一次只应答最内层模态；多层框需按序多个 confirm step。

### ISS-010 getter 模式匹配的通配吞噬
- **现象**：`assert state estates.sel.type` 恒 false。
- **根因**：`estates.*.type` 模式先把 `sel` 当索引捕获 → `parseInt("sel")=0` → return false。
- **方案**：语义分支（`estates.sel.*`）必须**排在通配模式之前**。
- **教训**：点分路径 DSL 需要"最长前缀优先"意识；后续加 path 一律先匹配更具体的。

### ISS-011 环境/卫生类（都会咬人一次）
- 控制台 GBK：Python 输出含 `✓/✗/中文` 需 `$env:PYTHONIOENCODING=utf-8`（脚本内避免装饰字符）。
- 行尾：项目 CRLF；新建文件/edit 工具可能引入 LF → 提交前统一转换（git 会 warn）。
- `python tools/re_map.py` 会**重写** `address-map.md`：`git stash` 前后跑它会造成 pop 冲突
  （踩了一次）。提交脚本类改动前先跑 re_map 再 stash/commit。
- PowerShell 批量替换：单元素嵌套数组被展平（`@(@('a','b'))` 变两字符串）→ 用 hashtable 载体。
- **正则替换尾括号陷阱**：模式尾 `)?` 把可选括号吃掉致 `)? ?` 语法错（ai_item case12）——
  批量替换后必须逐模式核对命中数 + 编译。

### ISS-012 断言 negate 逻辑写反（首轮全红）
- `if (ok != !negate) pass` 应为 `ok == !negate`——三支柱断言首跑即暴露，教训：**框架要先有
  一条故意失败的用例验证"FAIL 路径"**（已加：`--exec "assert state players==99"` → exit=1）。

## 2. 待实机对照的疑点（Q-x 清单，筛选后补脚本或勾销）

| ID | 疑点 | 代码级现状 | 建议验证方式 |
|----|------|-----------|-------------|
| Q-A | **正常模式行为回归**：收口+注入替换+trace 门后，游戏整体（动画节奏/音效/骰子/收租/演出）应与改前完全一致 | headless 全绿、dummy 截图与真窗口一致，但**未实机跑过一局** | 实机完整玩 2~3 回合（掷骰→移动→收租→事件），对照无异常即勾销；若动画"快了/跳帧"→ 回查 ISS-001 收口点 |
| Q-B | 15 号分红**负 fund=股东分担**（公司亏损）演出与扣款 | 56 场景只验证正路（fund=0 也弹会） | `specpt.fund <i> <负值>` + `day 14` + 实机看分担扣款/破产演出；或补脚本 assert `xfer to=股东` |
| Q-B2 | 结息/乐透/催收的**跨月触发**（settle/lottery dayevt trace 已有，未断金额） | 56 已断 `dayevt name=lottery/dividend` trace | 存款场景：`player.bank 0 100000` + `day 30` → assert `xfer from=银行?`——transferMoney 的银行侧 to=？需先确认 trace 字段语义再写 |
| Q-C | 旅館收费后的**住宿演出链**（走进建築消失→天数倒计时→走出恢复） | 40 场景断到收费转账为止 | 实机看演出；脚本可补 `assert state players.0.stateFlags >= 3`（BYTE0 住宿）——**待查 land 后是否同帧置位** |
| Q-D | 交互密集 UI（樂透投注网格/魔法屋惩罚径向选择/百货买卖/银行数字框/股市买卖）脚本未覆盖 | **基座已通**：`registerRegion`+`clickr`+`dialog open name=`（90_topbar_query 绿）；百货/乐透/股市等**控件登记未铺开** | 各面板入口加一行登记（矩形同源）后补场景；逐面板清单见 testing.md §2.3 |
| Q-E | corp 房地產费用公式标注过"简化"（pricing-formulas.md） | 未入场景 | 对照 IDA/实机核对该格收费；有出入先修代码再补 `assert trace "xfer amount=…"` |
| Q-F | 同盟卡联合收费分账两笔 `xfer` | 未入场景 | 脚本雏形：`give.card 0 3; use?`（同盟需 UI）→ 依赖 named region；暂人工 |
| **Q-G** | ~~L2 长跑未封装~~ **已升级**：`80_autoplay_60d.txt` 全 AI ~60 天两整月（跨分红/乐透/结息），assert errors==0/day 推进；4 种子全绿 | 回合主链 | 后续接 CI 自托管即可 | ✅ |
| Q-H | `card.rebuild` 后**卡包应有 7 号卡才扣卡**——命令层直调不带卡时 cardBagRemove(7) 可能移除空槽（原版经 useCardFlow 先扣卡） | rebuild 场景未断卡包 | 实机走卡片栏用改建卡核对；脚本可补 `give.card 0 7 + dump` |
| Q-I | trace `xfer to=` 语义：收款=玩家**索引0基**、=银行/公库时为其他编号（0x41D433 映射） | 72 场景断 `xfer from=0 to=1`（AI 玩家1=owner2-1）成立 | 银行收款场景先 `dump` 观察字段再写死断言；建议 testing.md §2.1 补"to 字段编号对照表" |
| **Q-J** | 请神符"第一回合地图不该有大财神却请到"（用户 2026-09-28 实机反馈） | **已结案=非初始放置缺陷**：`loadMapData 0x407D6F` 开局仅放偶槽 1,3,5,7,9,11（小神+天使+惡犬），大神/惡魔/土地公**绝不预置**（`kCellTypeInit` 的 +0 类型常驻 ≠ 在场——空槽判定 `+2 cellEnt==0`，潜伏槽不绘制/不拾取）。"提前见/请到大神"根因=**`Ctrl+Shift+O` 调试直放**（1..18 循环、当前格、绕过配对，用户确认触发过）。请神符第一回合候选实为小神+天使（惡犬 `canAttach=0` 请不到），符合预期 | 结案；轮替方向缺陷另立 BUG-003（见 §4） |
| **Q-K** | 请神符「最近」口径 | `pickNearestAttachable` 用 **cellEnt 格坐标欧氏距离**+视野(mapHitRegions)过滤 | 与原版 0x444D1A 的拾取坐标口径（屏幕像素/带 rotation?）待 IDA 细核；构造"近处小神+远处大神"用例可脚本化（place/teleport+select 后 use.god invite 断言 objId） |
| **Q-M** | ~~待深挖~~ **已结案（ISS-014）**：126 首击被 playItemLine 前置台词模态消费，目标对话框收不到 UP → 返回 0 | useItemRobotWorker 0x447295 | 修复=**二次 clicksel**（台词 ~60 帧后落目标框窗口）；126 绿 target=2001 flags=1 | → 已固化为 126_item_worker.txt |
| **Q-N** | 110 百货开场消息轮数随语音时长浮动 → 固定 wait 轮数 flaky | FloatMessage 2000ms+voice | 已"连点三下"吸收；通用教训：点击推进阶段的 UI 需 Ready 证据（wait trace/log）或冗余点击；长期=UI 阶段查询 trace 化 |

### ISS-013 specPt costType 是 `g_specPtCostMap` 索引，不是帮助序号
- **现象**：84 场景初版选 `costType=4(電腦)` 断言 dayCount 收费——实际 **costType4=保險公司(轮盘)**
  （`spIdx==3` 才是銀行=feeBase×dayCount），且保險轮盘=人类模式必须 `click` 停格；
  land 后挂在轮盘+认购双框上，wait trace xfer 超时。
- **方案**：场景改 `specpt.type 1 3`（銀行·确定性公式），缴费后 `confirm no` 放行认购询问框。
- **教训**：给"数值断言"型场景选分支时，先 `dump specpt` + 对照 turn_system specPt 注释表
  （0x41A168 段）确认 costType→公式，**不要按帮助/帮助页序号直觉映射**。

### ISS-014 命令内嵌台词模态吞交互点击（通用规则：前置双点）
- **现象**：`use.item 9`（機器工人）后紧跟 `clicksel`，目标选择框返回 0（未选中）。
- **根因**：effect 函数序章 `playItemLine`/`playCardLine` 先弹 ~1s 台词模态；clicksel 的
  move+click 两帧全落在台词窗口（FloatMessage 点击=跳过消息），台词结束后目标框才出现，
  已无后续事件 → UP 永缺。
- **方案**：脚本规则=**前置台词类 effect 连发两次选择点击**（`clicksel; wait frames 240; clicksel`）。
  适用全部 use.item/use.card 带台词者（122 機車无地图点击所以一发过）。
- **状态**：✅ 126 二次 clicksel 绿（target=2001）。

### ISS-015 阻塞标志的嵌套清零事故（prevInBlock）
- **现象**：白名单应答 step（clickr/confirm）执行完把 `g_inBlock` 直接置 false，
  外层 land 仍在阻塞 → 后续 assert 被提前放行（读到半完成状态）且冻结保护失效。
- **方案**：进入时 `prevInBlock = g_inBlock`，退出时恢复 prev（重入保持 true）。
- **状态**：✅。

### ISS-016 冻结步骤挡住后续应答 → 受限前瞻（prefetch）
- **现象**：脚本 `assert` 排在 `clickr shop.leave` 前；模态阻塞期 assert 冻结 pc →
  leave 永远到不了 → 模态永不关 → 互等挂死（110 实证，90s TIMEOUT）。
- **方案**：冻结时在 pc 之后 ≤8 格窗口内找**首个交互 step** 先执行并标 `done`，
  正式经过时跳过。语义=「写在断言之后的点击视为对当前模态的预定应答」。
- **状态**：✅（110 绿）。配套：`--wall-cap` 墙钟上限定位挂死；场景 timeout 收敛 50000 帧。
- **衍生教训（Q-R/Q-P 两案）**：应答 step 仍需**等模态真正出现**（`wait log "slotMachine:"`、
  `wait log "confirm dialog"` 等同步点先行），前瞻只解决顺序死锁不解决抢跑。

### ISS-017 trace::logf 在 --trace 关闭时整体丢弃（wait log 半失效）
- **现象**：84 场景 `wait log "xfer from=0"` 恒超时，但 transferMoney 明确执行（specPt fee 日志在）。
- **根因**：`trace::logf`（xfer/debt/eliminate/stock/news/fate/dayevt…结构化事件文本）第一行
  `if (!g_enabled) return;`——无 `--trace` 时文本既不进 trace 环**也不进 log 环**，`wait log` 永远匹配不到。
- **修复**：格式化后无条件 `rich4::logMirror(buf)` 写入内存日志环（log.cpp，不写文件/stderr、
  不计 ERROR 数）；`g_enabled` 门只管 trace 环（--trace-out 导出）。
- **教训**：**双环（trace/log）系统里"同步点文本"必须保证两种运行模式都可见**；
  结构化事件如需被 wait/assert 消费，落环应独立于导出开关。
- **状态**：✅（84 随修复转绿；既有 62 等用 wait trace 的场景不受影响）

### ISS-018 prefetch 越过未执行的同步步骤（222 暴露）
- **现象**：`use.card auction`（同步跑完整场拍卖）之后的 `clickr auc.btn.0`，在**前一行 wait log
  冻结期**被前瞻派发——此刻 region 尚未注册（runAuction 未进）→ unknown region 且 done 标记
  吞掉正式执行机会 → 场景假死。
- **根因**：前瞻窗口 `pc+1..pc+8` 内**跳过非交互步骤继续找交互步**派发，但被跳过的同步步骤
  （use.card/land 等）正是交互步的前提。
- **修复**：窗口扫描遇到**未 done 的非交互 Cmd 立即 break**（wait/assert 之类照常放行）；
  前瞻只服务"紧随冻结步的应答"（如模态内的 confirm/clickr）。
- **附带结论**：**同步阻塞命令跑完整场交互（拍卖 AI 自动决策）时，其后的应答步骤是死代码**——
  场景设计先判断该 modal 是"命令内同步完成"还是"跨 tick 挂起等待"。
- **状态**：✅（222 转绿，62 场景回归无差异）

### ISS-019 `g_inject` 静态零初始化 = 全槽"首次注入 0"（旅館轮盘 value=255）
- **现象（用户实机）**：AI 回合第一次踩他人旅館，转盘**画面**停在有效值（如 4）却结算
  "休息255天 費用153000元！"；日志 `roulette result: cell=0 value=255`。
- **根因**：`debug_hooks.cpp` 的 `std::array<int, SlotCount> g_inject;` 处于命名空间作用域
  → 静态存储期**零初始化为 0**（非哨兵 `kNoInject=-1`）；而 `clearInject()` 只在个别 debug
  命令路径调用，**启动未执行** → 每槽 `hasInject()` 误报 true、`raw()` 首次返回注入值 0。
  轮盘最恶性：入口 `hasInject?0` + 出口覆写 `cell=0`，theme1(旅館) `byte_475D0C[cell0]=0xFF`
  → `value=255`；其余各槽（骰子/发牌/事件/AI…）仅"首次随机被钉索引 0"的静默偏差，第二次起
  `raw` 已置回 -1 恢复正常——故只有轮盘因 0xFF 格放大成可见错误。
- **方案**：(a) `g_inject` **声明处**用 lambda `fill(kNoInject)`；(b) `roulette` 注入落 0xFF 格时
  **回退到第一个有效格**，而非写死 `cell=0`（旧注释"表定义必有值"对 theme1 不成立）。
- **教训**：静态/命名空间作用域的**哨兵数组必须显式初始化到哨兵值**——C++ 默认零初始化会把 0
  当成合法注入；新增调试槽位勿依赖"运行期才 clear"。凡"回退默认值"须遍历确认该值在**所有分支表**
  里都合法（此处 cell0 仅对 theme1 非法）。
- **状态**：✅ 回归=`41_roulette_no_inject`（`assert no trace "value=255"`，正=40 注入路径）。

| **Q-W** | 公佈欄**下單深鏈**未測（股票/地產/道具/卡片 4 子對話框的按鈕幾何+主面板 8 槽交互）；`queueTradeOrder/executeTrade/ageTradeOrders` 純函數可由 L0 單測補 | 0x4284BE/0x42704E/0x4255DA | 250 已驗開閉；下一步=子對話框 named region 或 CTest 純函數測試 | ? 掛賬 |

## 3. 阻塞模态人工干预点清单

| 干预点 | 原版 | 停滚/确认方式 | 脚本手段 | 状态 |
|--------|------|--------------|----------|------|
| 老虎机（附身 大/小財神·大/小窮神 收款/送钱） | 0x440706/0x43F23E | 滚动阶段任意**左键**停滚→自动减速→结果→退出 | `tap` | ✅（94 绿） |
| 转盘（旅館天数/購物倍數/航空/保險） | 0x44090E/0x43F7C6 | 任意左键**提前停**（人类；AI 40 帧自动） | `tap` 或 `click 320 240` | ✅（40 绿） |
| 确认框（买地/出院/各种 yes-no） | 0x45367E | YES=左半/NO=右半；右键=NO | `confirm yes\|no`（免坐标） | ✅（30/64 绿） |
| 樂透投注网格 | 0x42F7FC | 点格买注；右键退出 | `clickr lotto.<0..35>` + `rclick` | ✅（100 绿，已登记） |
| 魔法屋 12 惩罚径向选择 | 0x431CAA（panel[19] RAW 拾取） | 悬停选一项→确认 | **待登记** named region（RAW 掩码不规则） | ⏸ 未登记→场景会 TIMEOUT |
| 監獄保释/醫院出院 头像格 | 0x43D304/0x43E9A4 | 首击跳过场消息→再击头像格→yesNo→答谢演出 | clickr bail.cell.<i> / hosp.cell.<i> + confirm yes | OK（102 小偷格4 / 103 流氓格6；格索引=flags 数组位，开局在押 jail{4,6} hosp{6,7}，flags 日志 8 连数字逐位读） |
| 数字输入框（认购/贷款/汇款额） | 0x453544 | 小键盘+M 确认；面板可拖拽（登记用初始位 256,144） | clickr numkey.0..9 / .ok / .cancel / .clear / .back / .slider | 已登记（场景待：贷款/认购链） |
| 百货商品行/买卖、股市 買/賣/页签 | 0x42E931 / 0x42B58F | 点行/按钮 | **待登记** | ⏸ |
| 前进面板 GO/骰子数 | 0x417E26 panel[8] 掩码 | 点 GO(ctrl=13)/骰子区(11) | **掩码不规则**：`key`（前进键 bind[10]/骰子 bind[11]）可用 `key <bind名>` 替代；或扫 mask 定位 | ⏸ |
| 設施選擇/研究選択/AI 托管对话框 | 0x4431C4/0x44101D/0x41E345 | 点选项 | **待登记** | ⏸ |

- **规则**：脚本在 `land`/事件触发后若进入上表模态，必须跟一个干预 step（`tap`/`confirm`/
  `clickr`），否则 `--script` 到 timeout 报 FAIL（快速暴露，不会静默挂死）。
- **通用兜底探测**：`--exec "... ; tap"` 已可解老虎机/转盘/任意"点一下继续"类。
- 待登记项按场景需要逐个补（每处=入口函数一次性 registerRegion，矩形与既有命中常量同源）。

## 4. 自动化挖出的产品 bug / 死锁（按发现记录）

### BUG-001 请神符 headless "挂死" —— **结案：非 bug，是老虎机人工干预点**
- **现象（历史）**：`use.god invite` 后 60000 帧无进展（headless 与实机观察一致：实机也需
  玩家点一下老虎机停滚）。
- **根因**：attach 财神/穷神（type 1/2/5/6）→ `attachObject` 内弹**老虎机 0x440706**
  （`humanCtl` 等任意左键停滚，phase1→2）；脚本没有干预动作 → 挂起等待。转盘（旅館/購物/
  航空/保險 0x44090E）同理。**这是"非 AI 玩家需手动点一次"的通用形态**，不是缺陷。
- **方案**：新增 `tap [x,y]` 通配干预（默认 320,240；老虎机/转盘 handler 均不检查坐标，
  任意 BUTTON_DOWN 即停）+ 交互白名单；94 场景 `wait trace "attach p=0" → tap` 通过。
- **状态**：✅ 已解（场景 94 绿）。干预点全表见 §5。

### BUG-002（观察）96_priceup_flag 一次通过但**翻倍金额未断言**
- 现断 cash<200000；更严格应 `assert trace "xfer ... amount=<2×fee>"`——
  fee 依赖同路段块数，需先 `dump` 定值；作为金额断言铺开示例待补。

### BUG-003 神明配对轮替方向写反（奇槽大形态永不翻回小形态）— **已修 2026-09-28**
- **现象**：玩到多回合后场上大财神/恶魔/土地公越来越多、小神/天使/恶犬渐绝，"该轮换的不轮换"。
- **根因**：`economy.cpp` `releaseCellTableSlot` 末尾 `pairType=(idx&1)?idx+1:idx+2`——
  奇数槽(大形态)被消耗时算出 `idx+1`=**同奇槽大形态**，`createMapObject` 按 `type-1` 写回原槽 →
  原地复现、永不翻回偶槽小形态。原版 `0x40E278` 反汇编 `lea ebx,[edx-1]/[edx+1]` 后 `inc ebx`
  实为奇槽→`idx`(小形态)、偶槽→`idx+2`(大形态) 严格双向交替。
- **修复**：抽 `godPairType(idx)=(idx&1)?idx:idx+2`（`economy.h/.cpp`），releaseCellTableSlot 调用之；
  L0 单测 `tests/unit/test_god.cpp`（偶→大、奇→小 各 6 例）锁定方向。
- **附带核实**：神明**无独立天数刷新/换位**——`advanceDay` 仅新月 1 号换位槽 13/14；静止神 `life=0&owner=0`
  永不被寿命递减段触碰；交替唯一成本=被消耗(踩中/请走/送神)或附身寿命 7 归零→立即对向重建。见
  `map-object-refresh.md §2.1/§2.1.1`。
- **状态**：✅ 已修，待实机双向回归（消耗小神→出大神→再消耗→翻回小神；恶犬↔土地公、天使↔惡魔）。
## 5. 使用约定

- **新场景失败先判类**：`wait` 超时→时序（ISS-004）；金额/格值不符→注入/公式（ISS-003/008）；
  路径 unknown→getter 顺序（ISS-010）。
- 修复产品 bug 后**必须**把该 bug 变成一条场景断言（反哺），并在本文件对应 Q-x 标注
  `→ 已固化为 NN_xxx.txt`。
- Q-x 被实机验证/证伪后写结论（勾销 / 转 ISS / 转修复）。

