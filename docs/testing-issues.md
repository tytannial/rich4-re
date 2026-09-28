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
| Q-G | L2 长跑不变量未封装 | **已固化**：`80_autoplay_20d.txt`（全 AI 20+天，assert errors==0 / day 推进；seed 42/7/99/2024 四种子全绿） | 可扩至年尺度+`--repeat`；发现深层 bug 时补 assert |
| Q-H | `card.rebuild` 后**卡包应有 7 号卡才扣卡**——命令层直调不带卡时 cardBagRemove(7) 可能移除空槽（原版经 useCardFlow 先扣卡） | rebuild 场景未断卡包 | 实机走卡片栏用改建卡核对；脚本可补 `give.card 0 7 + dump` |
| Q-I | trace `xfer to=` 语义：收款=玩家**索引0基**、=银行/公库时为其他编号（0x41D433 映射） | 72 场景断 `xfer from=0 to=1`（AI 玩家1=owner2-1）成立 | 银行收款场景先 `dump` 观察字段再写死断言；建议 testing.md §2.1 补"to 字段编号对照表" |

## 3. 使用约定

- 新场景失败先判类：`wait` 超时→时序（ISS-004）；金额/格值不符→注入/公式（ISS-003/008）；
  路径 unknown→getter 顺序（ISS-010）。
- 修复产品 bug 后**必须**把该 bug 变成一条场景断言（反哺），并在本文件对应 Q-x 标注
  `→ 已固化为 NN_xxx.txt`。
- Q-x 被实机验证/证伪后写结论（勾销 / 转 ISS / 转修复）。
