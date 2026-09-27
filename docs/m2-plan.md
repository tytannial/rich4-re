# M2 执行计划（Debug 工具 + 测试修复 + 收租/建造）

> 本计划为交接文档，可在新会话中直接按序执行。所有原版依据均已用 IDA 核实
> （地址、公式、常量、字段偏移）。执行前请阅读 AGENTS.md 与
> `docs/reverse/functions/gameplay-map-mechanics.md`（机制总览与验收检查表）。
> **最新进度（2026-09-25）**：任务 A..E ✅ + 计划外股市/specPt/转盘 ✅；
> **任务 F 实为「查詢面板」**（旧称"建造菜单"有误，见 §6）✅ 2026-09-25（三页签+右侧面板 4 页签）；
> P2 事件格已起步（得點券/卡片格 ✅、監獄·醫院状态系统 ✅、銀行 ✅，见 §9）。

## 执行进度（2026-09-21..23 会话）

- ✅ **商業用地 corp（2026-09-22 会话二）**：landingEvent 4000..5999 分支全流程
  - 数据：`Corp` 补 `researchItem/researchLeft/buildPrice/feeTable[6]/lastFee`；新增 `diceValue`（0x48BAFC）
  - 无主购地 0x41A86B（`+34×M`）/ 建设施 0x41A1E0（`selectFacilityDialog` 人类选、AI `rand()%4+1`）/
    设施升级 0x41A2B3（`+36×M`，上限 `{1,5,5,1,5}`）/ 他人收费 0x41A370（旅館轮盘天数、購物轮盘倍数、
    加油站 `500×载具倍率×diceValue×M`）/ 旅館住宿状态 0x41A761（BYTE0 天数，住宿中免收租）
  - 新文件 `facility_dialog.cpp`：设施选择 UI 完整还原（帧 4 图标 + 三层边框 + 名称）
  - `rouletteValue`：轮盘数值按 `byte_475D0C` 表取有效值（UI 动画待接入）
  - 顺带：estate 升级询问文本修正为原版 `"%s\n\n升級費用:%d元\n\n是否升級？"`（0x46396D）
- ✅ **P0 收租补全（2026-09-22 会话）**：住宅用地联动 + 连锁店
  - 联动诊断：`estateRouteRent` 打印匹配明细（`matched/sum/total`）+ `Ctrl+9`（联动组归当前玩家，
    住宅=同街同名 / 连锁店=全部连锁店），用于定位"整条街未联动"
  - 联动高亮：`highlightBlink`(0x451985) 还原——16 帧 `g_highlightLut`(0x476380) 亮度闪烁 +
    `markPickBuffer`(0x456C0A) 标记联动组（30ms/帧 + 400ms 停留）；重写用 `mapHitRegions` 区域做
    RGB555 亮度偏移（`drawEstateHighlight`，非阻塞）
  - 连锁店：`Ctrl+4`（改建卡 `cardRebuildEffect` 0x44309B：`level!=0` 时 `type^=1`，变连锁店
    `level>1`→1）+ 收费（同地主数量×2000，跨街道高亮）；完整卡片流程 P4
  - 顺带修：`kBuildingNames` 补第 17 项"五級"（原 16 项越界）；`kMonthName` 误名 → `kWeekdayNames[7]`
    （原表 0x47511C 实为星期名）
- ✅ **任务 A** `e0c5ac5`：`--debug` 工具集（Ctrl+1..9；Ctrl+4 连锁店已接入）
- ✅ **任务 B** `00bf29d`：大地图填色（`bigMapBuffer`；标记帧 26/28/28、11392 缩放）
- ✅ **任务 C** `c60ab5b`：买地面板背景（`g_tipFrame` 帧 5 @ (220,140)，色键 blit）
- ✅ **任务 D** `36c3392` + 本次修正：收租
  - **调研修正（2026-09-21 二次）**：case 3（`sub_448A7E` 触发）**只记账不转账**——原版
    `addPlayerDebt(owner-1, cur, 30*g_moneyMul)` 累加欠款矩阵 `dword_496BB4`（不扣现金）+
    面向目标 + FLC 526；**实际收租在停留结算 `landingEvent` 他人地产分支（0x419A67）**，
    已实现（同路段联合租金 / 连锁店数量×2000 / 地主状态免收 / 付款方神明调整 / showMessage /
    transferMoney flags=0 收款入银行 / estate+44=本次租金）。早期 case 3 的"立即转账"已移除
    （否则与 landingEvent 重复扣款）
  - `landAfterMove`(0x40F381) 槽 9/10/12 实为**神明效果**——槽 12 = 土地公
    「強佔土地」、槽 10 = 小恶魔「拆毀一層房屋」；依赖神明携带系统 → 延后 P4
- ✅ **任务 E** `a491b8a`：升级/加盖（`landingEvent` 自己地产分支 0x419911：守卫 level<5 &&
  type==0 && state37==0，费用 `priceBase * moneyMul`，AI 自动升级，音效槽 18）
  - **外观修复** `12ea114`：`dword_48AE48[level]` 与 `g_specTiles[level-1]` **同址**
    （0x48AE48 + 4*level = 0x48AE4C 起数组）→ 住宅建筑外观 = `specPointTiles[level-1]`
    （`map.mkf[5*base+38+level]`）；此前 level>0 全画 estateFlag（"升级后变连锁店外观"）。
    **实机已确认加盖外观正确**
- ✅ **2026-09-23 会话三（计划外，全部完成）**：
  - **行業設施點 specPt 收费**（`073669e`）：6 公式（航空轮盘/電腦×天数/保險轮盘/汽車·石油载具×步数/
    房地產按地价/index12）+ 公库收款（`transferMoney` to=specPt 索引，2000+ 映射）+ 落地诊断
  - **股市系统**（`fb63d8a`..`49303fe`）：数据层（交易量/保留股份/休市/分红期 + `kStockNames[8][12]`）→
    逻辑层（`buyStock`/`sellStock`/`refreshBankStockShares`/`stockTick` 接入休市/`advanceDay` 每日维护）→
    **精确图形面板**（`stock_market_dialog.cpp`：0x42B58F/0x42AAFF 行情/持股切换·買/賣·走勢圖饼+折线·
    休市·浮雕按钮·涨跌五态配色；`panel.mkf[75]` 帧 0/1/2）；档案 `stock-system.md`/`42b58f-stock-panel.md`
  - **specPt 认购/控股**（`spec_pt_dialog.cpp` 0x41D1A9 + 数字输入框 0x453544 `number_input_dialog.cpp`）：
    认购上限 `min(cash/价, sharesLeft, 1000)`、AI 保留 30% 初始资金、控股排序（平手归先拥有者）
  - **转盘 UI**（`1480b40`/`7f5e9df`：`roulette_dialog.cpp` 还原 `roulettePrompt` 0x44090E）+
    旅館住宿状态（走进/倒计时/「住宿中 還剩N天」/走出免重复收费）
  - **Debug Ctrl+Shift 扩展**（`91c0e14`）：瞬移结算/载具/神明/状态/设施/银行/过天/股市面板
  - 开局日期链修复（`d3304ca`：还原 0x411E8F/0x4119E3/0x406DE7，去 1998-1-1 硬编码）
  - UI 控件指南 `docs/reverse/ui-controls.md`（浮雕按钮/变暗表/文本对齐/数字框/模态框架）
- ✅ **P2 事件格第一步：得點券（case 10/11/12）**（2026-09-23）：
  - 原版依据：0x41B184/0x41B21E/0x41B2A3（反汇编）+ `sub_45144F` flcPlay（阻塞；flags&1 透明 =
    调色板索引 0，见 0x4506C7）+ FLC 537（31x39/14 帧@71ms）+ Effect.mkf[98] + BIG5 文本
    0x463A81/8E/9B；**case 12 原版无语音**（0x41B2FD 直接 jmp default，修正旧文档）
  - 实现：`playEventFlc`（阻塞逐帧 render+pumpEvents+音频续喂；`turn_system.cpp`）+
    `landingEvent` case 10/11/12 + `drawEventFlcFrame`（`game_loop.cpp` 色键跳过）+
    `FliDecoder::colorKey()`；角色语音 sub_44EF41 → P4
  - 文档：`41982d-p2-events.md` §2/§11 更新、help-checklist idx 29/30/31 ✅、本文件 §9
- ✅ **P2 事件格第二步：卡片格（case 13）**（2026-09-23）：
  - 原版依据：0x41B302（反汇编）+ `sub_441E12` 抽卡（赠卡池 `g_propStock` 0x499198 按数量展开 +
    `rand()%n`）+ `sub_4412E4/441262/44128F/441343`（卡包 `cardState60` 0x499120 15 槽/满则舍弃
    最低价 `byte_47FDEF`/池恢复）+ `sub_441F73` 显示（data.mkf[570+id] 165×256 @ (138,200) +
    提示框 (220,129) + 音效槽 23 + `sub_4528B9(1500)`）+ 卡片表 `dword_47FDEA`（30 项：
    名称/+4 池初值/+5 价格）
  - 实现：`drawFreeCard`/`giveCardToBag`/`cardBag*` + `showCardGet`（`turn_system.cpp`）；
    新表 `kCardNames[31]`/`kCardPrices[31]`（map_tables）
  - 文档：`41982d-p2-events.md` §3 全面归档（含旧文档订正：sub_441F73 参数是卡 id 非玩家、
    case 12 无语音、propStock 是卡片赠卡池）、help-checklist idx 25 ✅
- ✅ **P2 事件格第三步：監獄/醫院状态系统**（2026-09-23）：
  - 原版依据：`jailPlayer` 0x43D593 / `hospitalizePlayer` 0x43EC3F（移格/标志/FLC 538·524）+
    `sub_41C84F` 回合递减 + `releaseJailNpc`/`releaseHospitalNpc`（0x43D7BF/0x43EE6E 玩家分支）+
    `sub_44BA63` 保险理赔（0x4658FA）+ 0x41CC4B/0x41CAE3 保险期递减/清零
  - **订正**：`Player +62 g_playerCardCnt` 实为**保险期**（重写原误名 `cardCount` 且用于"发卡计数"，
    已改 `insuranceDays`）；`givePlayerCard` 实为**给道具**（`g_playerCards` 0x49915C = 道具库存，
    重写 `cardPool60` 改名 `itemStock`）；开局 6 次发放按原版改为"每玩家道具 1/2/3/4/8/9 +
    道具池（`misc8A`）递减"
  - 实现：`jailPlayer`/`hospitalizePlayer`/`insurancePayout`（`turn_system.cpp`）；`updatePlayerStates`
    补保险期递减与 `jailFlags`/`hospitalFlags` 清理；`jailCellEntId`/`hospitalCellEntId`
    （loadMapData 扫描 special 8002/8001）；debug `Ctrl+Shift+J` 入狱 / `Ctrl+Shift+L` 住院
  - 待做：保释/出院均 ✅（见下「面板底座」块）；P5 后补 NPC 格答谢消息+释放（TODO 注释就位）
- ✅ **面板底座 + 監獄保释 + 醫院办理出院（2026-09-23，`ui-controls.md` §25）**：
  - 底座：`Audio::playVoice/voicePlaying/stopVoice`（Speaking.mkf 单通道，drawText `#NNNN` 0x45441A）
    + `FloatMessage`（0x44EC30/44ECB6/44EE18/44EF3B，最短 2s + 语音未完继续等）
    + `pushSceneMusic/resumeSceneMusic`（0x4549CF/0x454BCC 音乐栈，OGG 不续位）
  - `jail_dialog.cpp` 監獄保释：人类 UI（panel[63] 帧表/8 格头像/悬停板 100×95/点券板）+ AI 保释
    （0x43D3DF 个性候选 + 点券检查 + 释放）；`landingEvent` case 4 接入；
    NPC 格占位 =「點數不足」消息 + `TODO(P5)`；释放返还 `addPlayerDebt` 日志占位
  - 醫院出院（0x43E9A4/0x43DA27/0x43D88F）：阶段机 1 开场#0127 → 2 选择 → 4 #0129 出院 →
    released 退出 / 5 #0002 不足 / 6 NPC 答谢（P5）/ 7 #0130 告别；护士行走（帧 {5,6,5}/{10,11,10}
    @139/165,158 步 3 擦除）+ 下窗小像（帧 7/8、12/13，1/4 概率驻留 1..7 拍）；
    panel[65] 两列格 147×102/悬停板 98×95/帧 9 退场复位；case 5 接入
- ✅ **P2 事件格：新聞（case 2，2026-09-26）**：
  - 原版依据：`newsEvent` 0x44B6DF（panel.mkf[66] SMP 440×480 画布 + data.mkf[441+idx] 388×251
    插画 + 6 类标题 + `g_newsFuncs` 36 效果 + 2400ms 阶段 0/1）+ `newsEventCheck` 0x448BE2
    36 条触发判定（反汇编核对）+ `g_newsOrder`/`g_newsPos` 抽取循环
  - 实现：`news_dialog.cpp` / `news_dialog.h`：`newsEvent`/`newsEventCheck`/36 事件函数
    （0-3 监狱医院 / 4-5·14-15·18-21 灾害破坏 / 6·14 地价 / 8-10 排行 / 11-13 三税 /
    22-23 银行 / 24-28 股价停牌 / 29 坐牢 / 30-35 公司）；`stockNewsApply` 0x429040
    （`stock_system.cpp`）；`cardShuffle`→`newsOrder` 改名；`landingEvent` case 2 接线
  - 待做：**拍卖 idx 7 桩**（`sub_43BDE5` 另开一轮）、语音/浮动数字 → P4、corp 单块高亮
    （重写联动高亮机制仅支持 estate）
  - 文档：`44b6df-news-events.md`（主循环/36 判定/36 事件逐条/资源）、help-checklist idx 33 ✅
- ✅ **任务 F：查詢面板（2026-09-25；旧称"建造菜单"有误）**——IDA 订正结论：
  - 工具条 case 6 `sub_424492` → `sub_423CF3` = 帮助 [HELP 16]「資產清單/地產清單/股票清單」，
    **无建造动作**；完整实现见 §6
  - `sub_423B3B(player, tab)` 5 模式筛选（实为**地產清單子页签**）：0=estate+corp / 1=estate /
    2=corp / 3=有建筑普通地产（level≠0 && type==0）/ 4=有建筑连锁店（level≠0 && type≠0）
  - `sub_4225A3(player, dir)` 列表绘制（地點/開發狀況/價格/收費/租期 5 列，10 行/页）
  - `sub_423070` 页内容（資產清單 12 字段/道具/卡片；股票清單 12 行）；`sub_422443` 标签与玩家列表
  - 大地块（estate.type≠0 = 连锁店外观）"建造/加盖"仍走踩格 `landingEvent` + 改建卡；
    `sub_4246C5`/`g_miscTable336`（旧称"建造队列"）= 股票交易委托队列（0x4258C1..0x4284BE 交易区）

## 0. 上下文与工作流

- **分支**：`feat/m2-game-logic`（M1 已合入 `main`；P0 买地闭环完成，最近提交 `e004eb5`）
- **编译**：
  ```powershell
  cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build build'
  ```
- **提交前**：`python tools/re_map.py`（校验逆向标注 + 更新 `docs/reverse/address-map.md`）
- **逆向双落**：源码 `[RE 0xXXXXXX]` 注释（`依据:` 行只写在 `.cpp`）+ `docs/reverse/functions/` 档案 + IDB 重命名/注释并保存
- **提交粒度**：每个可测功能一个 `wip:` 提交；实机验证由用户执行
- **文档检查表**：`docs/reverse/functions/gameplay-map-mechanics.md`（地块机制，每完成一项打勾并记录差异）；
  `docs/help-checklist.md`（帮助文档 99 条功能实现总览，完成功能后同步更新状态）
- **P2 事件格研究**：`docs/reverse/functions/41982d-p2-events.md`（case 2..16 原版实现、
  数据结构、实现顺序与待深入清单；監獄/醫院/得點券/卡片/新聞/命運 已研究）

## 1. 任务 A：`--debug` 工具集（Ctrl+1..8）

**新文件** `src/app/debug_keys.cpp`（+ `include/game/app/debug_keys.h`）。
`Application` 解析命令行 `--debug` → `GameState.debugMode`；仅该模式生效。

实现要点：
- 在 `src/app/game_loop.cpp` 的 `SDL_EVENT_KEY_DOWN` 分支（约 427 行）插入处理；`SDL_GetMouseState` 取指针坐标
- 拾取复用 `state.mapHitRegions`（与 `showObjectTip` 同款命中逻辑），id：2000+ 住宅用地 / 4000+ 商業用地 / 6000+ 行業設施點
- 每次操作 `RICH4_LOGI`；标注 `[NEW]`（无原版依据）

| 键 | 功能 | 实现 |
|----|------|------|
| `Ctrl+1` | 指向地块 owner = 当前回合玩家 | `estates[i].owner = currentPlayer+1` + `buildMiniMapMarks` |
| `Ctrl+2` | level+1（≤5） | `level = min(level+1, 5)` |
| `Ctrl+3` | level-1（≥0） | `level = max(level-1, 0)` |
| `Ctrl+4` | 设为连锁店 | **待任务 F3 探明后接入（先留空）** |
| `Ctrl+5` | 全图住宅用地归当前玩家 | 遍历 `estates` |
| `Ctrl+6` | 全图住宅用地 level=1 | 遍历 |
| `Ctrl+7` | 当前玩家现金 +100000 | `players[p].cash += 100000` |
| `Ctrl+8` | 打印指向地块详情 | 日志：objId/type(+24)/owner(+25)/level(+26)/flag(+23)/priceAdd(+28)/priceBase(+30) |

**验收**：`--debug` 启动后各键生效；无 `--debug` 时不响应。

## 2. 任务 B：大地图填色修复

- **根因**：`src/app/map_dialog.cpp:23` 底图用 `state.miniMapRaw.frame(1)`（未填色原图）；小地图正常（用 `miniMapBuffer`）
- **原版**：`rebuildMiniMap`(0x40A4E1) 双分支——a1=0 小地图（5696 缩放）/ a1=1 大地图（11392 缩放）
- **修复**：
  1. `GameState` 增 `bigMapBuffer`（同 `miniMapRaw` 帧 1 尺寸，400x400）
  2. `buildMiniMapMarks`（`src/app/map_render.cpp`）同时重建两份
  3. `map_dialog.cpp` 改用 `bigMapBuffer`
- **验收**：大地图出现玩家色块（与小地图一致）

## 3. 任务 C：买地面板背景（提示框）

- **原版**：`askDialog`(0x440BA8) 先画 `g_tipFrame + 72`（帧 5，`sub_456418(dst, g_tipFrame+72, 220, 140)`）再画文本
- **待查**：`g_tipFrame`(0x48BAD8) 在 `loadMapData`(0x40808F) 的加载资源索引（mkf + index）
- **修复**：`GameState` 增 `tipFrame` + 加载；`confirmDialog(text)` 先 blit `frame(5)` 于 (220,140) 再画文本
- **验收**：询问框有宝石装饰背景（对齐原版截图）

## 4. 任务 D：收租（P1 核心）

### D1 状态机 case 3 重做（`src/app/turn_system.cpp` `updateGameState`）

原版 `playerActionStateMachine` case 3（0x40DA4B）：
- `byte_4749E0`（重写 `stepDwell`）等待递减；`byte_498EA3`（动画帧）非 0 时先播动画
- 读 `cellEnt+32` objId：2000..4000 住宅用地 / 4000..6000 商業用地
- `owner != 0 && owner != currentPlayer+1` → **`sub_40DF69(cur, owner-1, 30 * g_moneyMul)`**
- 之后：`g_playerRestoreDir = dir`、面向目标格（`sub_454FB4`）、后续动画

### D2 `landAfterMove`（0x40F381）实现

- 调用点：`nextPlayer`(0x418EBD @0x418F59) + 买地路径(0x41B086)
- 守卫：`stateFlags(player) & 0xFF` 非 0 或未存活 → 返回
- 按 `byte_496BA7[player]`（重写 `cellTableIdx`，携带物件槽）：
  - **槽 9**：公司相关提示（`showMessage(off_47ED9A, 1500)` + `sub_40B110`）
  - **槽 12**：完整流程——住宅用地/商業用地 owner 检查；他人 → `sub_40DF69(owner-1, player, g_moneyMul * word[+28] * (level + 2.0f) / 5.0f)`（公司用 `corp+34`）；无主 → `owner = player+1`（自动购地）；`g_cfgLandPerm` 补到期日；`rebuildMiniMap(0)`；随后 `showMessage(byte_4634F2, 1500)` + `sub_44EF41`（角色语音）
  - **槽 10**：住宅用地/商業用地 `level != 0` → `sub_40DF69(owner-1, player, 30 * g_moneyMul)` → `showMessage(byte_4634D7, 1500)` + `sub_40AB4A` + FLC 526 + 音效 `dword_4823DA`
  - 其他槽 → 无
- 常量：`flt_46350C = 2.0f`、`flt_463510 = 5.0f`（0x46350C/0x463510）

### D3 关键说明

- **`sub_40DF69` 只记账**（`dword_496BB4[26*a1+a2]` 玩家间往来账，clamp 0），**不直接扣钱**；真正转账是 `sub_41D2C6`（重写 `transferMoney`）——实现时需确认原版何时结算记账（可能回合末/过天），**列为实现时的分析点**
- 提示文本 `byte_4634D7` / `byte_4634F2` 用 `get_bytes` 解码（BIG5；注意 `get_string` 会在 `\n` 截断）

**验收**：`Ctrl+5/6` 批量铺地后，走到他人有建筑地块扣款 + 提示；公司同理。

## 5. 任务 E：升级/加盖

- **原版**：`sub_41982D` case 0 自己地产分支（0x419911..0x419A52）：
  - 守卫：`level < 5`、`type == 0`、`state37 == 0`
  - 费用 `word[estate+30] * g_moneyMul`；现金检查 → `askDialog`（格式串约 0x41995C）→ `level++` → 音效/动画
  - 用户提示"可以选择分支建设"——实现时确认升级是否含建筑选择（结论：仅大地块 `type != 0` 走**改建卡**变更设施/连锁，无集中建造菜单）
- **验收**：`Ctrl+6` 铺 level=1 后停留自己的地可升级；`Ctrl+2/3` 调整后复测

## 6. 任务 F：查詢面板 + 右侧資訊面板 4 页签（✅ 2026-09-25）

> **命名订正（2026-09-25）**：工具条 case 6 `sub_424492` → `sub_423CF3/423070/4225A3` 经完整
> 反编译确认 = **帮助 [HELP 16]「查詢：資產清單 / 地產清單 / 股票清單」面板**，**无建造动作**
> （点列表项只切页/翻页）；旧称"建造菜单"为误。`sub_4246C5`/`g_miscTable336`（旧称"建造队列"）
> 实为**股票交易委托队列**（调用者 0x4258C1..0x4284BE 交易区），归交易系统（帮助 idx 11 交易 ❌）。
>
> 实现（2026-09-25，`query_dialog.cpp` + `game_panel.cpp`）：
> - 工具条 case 6 入口；3 页签（資產清單/地產清單/股票清單）+ 顶部玩家条 + EXIT 钮 + 翻页箭头
> - 資產清單：12 字段统计（现金/存款/贷款/总资产/持股市值/點券/保险期/控股企业/土地/连锁店/
>   房屋/设施）+ 道具 13 格 + 卡片 15 槽 + 携带神明图标与剩余天数
> - 地產清單：5 子页签（全部/住宅區/商業區/房屋/連鎖店）+ 10 行/页 5 列（地點/開發狀況/價格/
>   收費/租期，價格/收費/到期日按原版公式）
> - 股票清單：12 支 股票名/持有張數/總市價
> - 右侧資訊面板 4 页签（資金/地產/股票/其他）+ 物价指数 + 点击切换（`dword_48BE24` → `Player.panelTab`）
>
> 原版依据：`sub_423CF3`（0x423CF3，窗口过程/命中区）、`sub_423070`（0x423070，页内容）、
> `sub_4225A3`（0x4225A3，地產列表/翻页）、`sub_423B3B`（0x423B3B，5 模式筛选）、
> `sub_422443`（0x422443，标签/玩家列表）、`sub_415F69`（0x415F69，右侧面板全内容）、
> `0x4182FA`（页签点击）。控件沉淀见 `ui-controls.md` §27。

### F1 原调研存档（名称已订正）

| 原版 | 作用 |
|------|------|
| `sub_423B3B(player, tab)` | 5 页签筛选：0=我的地产+公司 / 1=我的地产 / 2=我的公司 / **3=普通地块（owner=me && level≠0 && type==0）** / **4=大地块（owner=me && level≠0 && type≠0）** |
| `sub_4225A3(player, dir)` | 列表绘制（住宅用地名/建筑名/费用 `g_moneyMul × (word[+28] + word[+30] × level)`/到期日），5 个 case 对应页签 |
| `sub_423CF3` | 外层对话框（页签/翻页/确认）；**入口待查**（疑似工具栏"锤子"按钮）|

实现：新文件 `src/app/build_menu.cpp`（参照 `ai_dialog.cpp` 对话框模式）。
**验收**：菜单 5 页签正确筛选 → 选中大地块建造特殊建筑（公園/旅館/購物中心/加油站/研究所）→ 地图显示对应图块。

### F2 大地块购买诊断

- **判定字段**：`estate.type`（+24）≠ 0（普通地块 = 0）
- 购买路径同普通地块（`loc_41A013` 无 type 检查）→ "无法购买"用 **`Ctrl+8`** 打印现场诊断
- 可能修复点：重写 `landingEvent` 的 objId 范围 / 价格计算 / `estates.size()` 边界

### F3 连锁店（改建卡）—— ✅ 已逆向并接入 Ctrl+4

- **原版结论**（`cardRebuildEffect` 0x44309B，卡片 7；效果表 `g_cardEffectFuncs` 0x475D5C）：
  - 守卫：**`estate.level(+26) != 0`（必须有建筑）**；用户原描述"小空地 level 0"有误，以 IDA 为准
  - `type(+24) ^= 1`（0=住宅 ↔ 1=连锁店）；变连锁店且 `level > 1` → `level = 1`
  - 消耗卡片 `sub_441343(player, 7)`；corp 分支 = `selectFacilityDialog(1)` 变更设施类型
  - 使用流程 `useCardFlow` 0x441BAA（人类 `sub_4416F0` 选卡 / AI `sub_41E69E` 可用判定）
- **收费**：`estateRouteRent(owner, 0)` = 同地主**全部连锁店数量 × 2000 × M**（不与同路段联合）；
  收租时高亮全部连锁店（跨街道）
- **重写**：`Ctrl+4` 实现切换逻辑；收费已随 `estateRouteRent` 生效；完整卡片流程留 P4

## 7. 关键原版依据速查

| 项 | 地址/值 |
|----|---------|
| 买地流程 | `sub_41982D` case 0 → `loc_41A013`；价格 `(level*priceBase + priceAdd) * moneyMul`；返回 0x88（重写用 `kLandingWait=0x90`）|
| 询问框 | `askDialog` 0x440BA8（文本 (220,140) 字体16/0xF0F0F0/0x101010/阴影+粗体 + `yesNoDialog(220,320)`）；`yesNoDialog` 0x453A32（data.mkf[440]）|
| 询问文本 | 0x4639E1（BIG5）`"%s\n\n費用:%d元\n\n是否買下此地?"` |
| 现金不足 | `showMessage(byte_46398B, 1500)`；实测文本 = **"您的現金不足！"**（0x46398B） |
| 购地音效 | `g_effectSlots[17]`（0x4823D2）|
| 格子音效表 | `g_cellTypeSound` 0x475299（17 项）|
| estate 绘制 | 0x4091DF：`level==0` 有主 → `pieceTiles`(map.mkf[25]) 帧=角色 ID、**不改调色板**；`level>0` → `dword_48AE48[level]`（type==0）/ `estateFlag`（type≠0），owner 色 |
| 调色板 | `g_drawListPal`(0x48A852)：0xFF=不改 / 0=黑 / 1..8=`g_playerColor[owner-1]`（u32 RGB888 → `rgb888To555`）|
| 收租（停留结算） | `landingEvent` 0x419A67：音效槽 20 → `ownerCanCollectRent`(0x41D559) → `estateRouteRent`(0x419744) 同路段 `fees[level]` 之和 / 连锁店数×2000，×`moneyMul` → flag(+23) 仅地主侧翻倍 → **同盟分账**（+65 对象 v134 并入总额、按 `v134/(v8+v134)` 拆分）→ `applyGodRentModifier`(0x41D709) → showMessage → transferMoney ✅ |
| 地主免收 | `ownerCanCollectRent` 0x41D559：查封/與付款人同盟(+65)/死神顯靈(槽15)/住宿/消失/坐牢/住院/冬眠/夢遊 |
| 神明调整 | `applyGodRentModifier` 0x41D709：小財神减半 / 大財神免付 / 小窮神 +50% / 大窮神加倍 |
| 收租消息 | 0x4639B3 `"%s\n\n此地屬%s\n\n請付%d元%s"`（%s=地名/地主/金额/"過路費"）；同盟分账 0x46399A `"%s\n\n屬%s與%s\n\n請付%d元%s"`（以 ally≠0 判断，即便同盟无同段地也显示） |
| 收租联动高亮 | `highlightBlink` 0x451985：16 帧 `g_highlightLut`(0x476380) 亮度偏移 + `markPickBuffer`(0x456C0A) 标记联动组（住宅=同 owner+同名 / 连锁店=同 owner+type≠0；**含同盟者同组地块** 0x419C44/0x419BE2）；标记数>1 触发，30ms/帧+400ms |
| 连锁店创建 | 改建卡 `cardRebuildEffect` 0x44309B：`level!=0` 时 `type^=1`（0住宅↔1连锁店），变连锁店 `level>1`→1；效果表 `g_cardEffectFuncs` 0x475D5C、流程 `useCardFlow` 0x441BAA |
| 移动结束 case 3 | `sub_448A7E` 触发（步行 travel&0x83==3 + 他人有建筑）；`addPlayerDebt(owner-1, cur, 30*moneyMul)` **仅记账** + 面向 + FLC 526（不转账）|
| 记账 | `sub_40DF69`（`addPlayerDebt`）→ `dword_496BB4[26*a1+a2]`（**a1 应收 a2**，不直接扣钱）|
| 转账 | `sub_41D2C6`（重写 `transferMoney`）：flags&4 付款银行优先 / flags&1 收款入现金（否则入银行）；收租用 **flags=0**（地主入银行）|
| 收租音效 | `dword_4823EA` = 音效槽 20 |
| 建造费用 | `g_moneyMul * (word[estate+28] + word[estate+30] * level)` |
| 大地图 | `rebuildMiniMap` a1=0 小(5696) / a1=1 大(11392) |
| 吐槽 | `estateChainSpeech` 0x44F627（同路段住宅用地≥3 → `sub_44EF41`）|
| 神明阻止 | `checkCarriedGod` 0x40FA61（槽 7/8/15）|

## 8. 已知差异清单（2026-09-23 重整）

- ~~任务 F 建造菜单未实现~~ → ✅ 2026-09-25 实为**查詢面板**已实现（见 §6 命名订正；大地块"建造"= 踩格升级/建设施，早已实现）
- ~~事件格 case 2..16（P2）~~ ✅ **全部完成 2026-09-26**：新聞/命運/魔法屋（含拍賣面板，
  拍卖 idx 7 同步激活；`43380a-magic-house.md`/`43bde5-auction.md`）；~~物價指數面板~~ ✅ 右侧面板 0x415F69
- ~~AI 买地决策 `sub_41D7D4`~~ ✅ 2026-09-23（`aiBuyDecision` 接入 landingEvent 住宅/商業用地 AI 分支）；`alive&6` 自动行动（P5）
- ~~`updateMoneyIndex` 物價指數月度更新~~ ✅ 2026-09-23 接入（每日 advanceDay；**右侧面板显示 ✅**
  d0b4154，物價指數显示核对 2026-09-25 见 `415d31-game-panel.md`）；~~到期清算 `expireAssets`~~ ✅
  `economy.cpp`（飛彈/核彈接入；地块期限到期回收在 `advanceDay` 内联——语义订正见 `gameplay-map-mechanics.md`）
- ~~银行贷款/存款利息（case 14 銀行格）~~ ✅ 2026-09-23 全链；~~每月 15 号分红~~ ✅ 2026-09-23（`dividend_dialog.cpp`）；~~月初结息~~ ✅ 2026-09-23（`month_settle_dialog.cpp`）；~~资产清单（idx 16）~~ ✅ 2026-09-25（查詢面板 `query_dialog.cpp`）；~~股市交易市场/挂单~~ ✅ 2026-09-27（交易市場「公佈欄」`trade_market.cpp`，见下一行）
- **交易市場「公佈欄」✅ 2026-09-27**（工具条 case 9，帮助 idx 11）：`src/app/trade_market.cpp` +
  `trade_market.h`——`TradeSlot`/`GameState.tradeSlots[8][7]`（g_miscTable336 0x4967E0）；
  数据层 `clearInvalidTradeOrders`(0x42483E)/`queueTradeOrder`(0x4246C5)/`removeTradeOrder`(0x4247D5)/
  `executeTrade`(0x4255DA)/`ageTradeOrders`(0x428475)；UI 主面板(0x427C21/0x4249C2，panel[73]/[74]) +
  详情(0x42704E) + 股票(0x4258C1)/地产(0x42608F+0x424AEA)/道具(0x4267A4)/卡片(0x426C2E) 4 子对话框；
  **AI 自动交易 `tradeAiTurn`**（0x4284BE AI 分支：1/15 挂卖重复卡或库存≥3 道具、1/3 改价、
  1/4 低价买入）接入 `beginPlayerTurn` case 2/5；`advanceDay` age++；`newGameInit` 清零；
  存档（0x403146/0x402CB8，偏移 9513 336B）+ 时光机（0x44808A/0x448544）含挂单。
  订正：类型表 **3=道具 / 4=卡片**（旧文档写反）。**待实机验收**；`4284be-trade-market.md`、
  `ui-controls.md` §36；help-checklist idx 11 ✅（累计 95/99）
- ~~研究所研发 `labDevelopDialog`（P4 道具系统）~~ ✅ 2026-09-26（`lab_dialog.cpp` 0x44101D + `updatePlayerStates` 0x41C84F 产出；道具 13 获取入口闭环，非卖品 5 项；见 `44101d-lab-develop.md`）
  - 修正 2026-09-27：触发移到收尾 loc_41B077（先升級询问后研究、首次建设施即弹）；面板帧7 底图(不透明)+帧5 标题+未解锁灰度+黄框+按下下沉
- ~~收租的同盟分账 / 死神代付 / 免租卡片~~ ✅（2026-09-27 同盟分账收尾；死神/免租已由 `resolveFeePayer` 接入）
- 收租角色台词（付款方 `sub_44F4ED`/`sub_44F42D`、收款方 `sub_44F354`——实为按金额档选句的台词表，
  **非"浮动数字"**）与全局角色语音 `sub_44EF41` / 卡片获得语音 `sub_44F230`（P4）
- ~~P4 卡片栏/道具栏 UI + 使用流程外壳~~ ✅ 2026-09-24（工具条 case7/8：`useCardDialog` 0x441BAA /
  `itemBagDialog` 0x447D97，panel.mkf[11] 5×3 网格；**改建卡(id7)/请神符(id23)/送神符(id22) 可用**，
  其余 27 卡 + 13 道具效果 stub；见 `441baa-inventory-panels.md`）
- **P4 卡片效果专项分析 ✅ 2026-09-26**：`441baa-card-effects.md`——30 卡逐卡效果函数、三类交互范式（A 脚下格 / B 选玩家 / C 选玩家地产）、依赖原语盘点（多数已实现可复用）、被动卡链（18/19/20/21 走 `sub_444691`/`applyExemptCard`/`passOnCardDialog`/`applyFreeCard`）、实现分档 A/B/C、待细化清单；IDB 重命名 26 个效果/helper 函数；**烏龜卡指针订正 `0x446AFB`→`0x4458DF`**。下一步按档 A→B→C 逐卡实现。**档A 8 卡已实现 ✅ 2026-09-26**（`src/app/card_effects.cpp` + `card_bag_dialog.cpp` `cardEffect` switch 接线；`turnToAdjacentCell` 迁 `turn_system.cpp` 导出）；**档B/C 全完成 ✅ 2026-09-26**（30 卡全实现：`card_effects.cpp` 档B 天使/惡魔/停留/冬眠/夢遊/陷害/查稅/漲價/查封/同盟/烏龜/紅/黑 + 档C 換地/換屋；被动 18復仇/19嫁祸/20免費/21免罪；`resolveFeePayer` 免费·嫁祸接入 + `stockPickDialog` 选股）；**AI/托管用卡链 ✅**（`ai_card.cpp` 0x41E69E + `dword_475324` 目标表 + `beginPlayerTurn` rand 触发）；**角色台词 ✅**（`card_lines.cpp` 0x48123A 生成表 + `playCardLine`，含对方/受害者槽）；**AI/托管用道具链 ✅**（`ai_item.cpp` 0x420E9A + `funcs_420EE6` 13 目标函数 + `dword_48BE64`，`420e9a-item-ai.md`）。
- 存档完整恢复（P5）；**事件槽 NPC／四大恶人 ✅ 2026-09-26**（`498df0-event-slot-npc.md`）
- ~~现金不足/神明阻止提示~~、~~询问框背景~~、~~连锁店创建~~、~~corp/specPt 收费~~ —— 均已完成

## 9. 执行顺序（2026-09-23 已确认路线）

**A..E ✅ → 计划外股市/specPt ✅ → P2 事件格由简到繁（进行中）**：

1. ~~得點券（case 10/11/12）~~ ✅ 2026-09-23（建立事件格 FLC 阻塞播放模板 `playEventFlc`）
2. ~~卡片格（case 13）~~ ✅ 2026-09-23（赠卡池抽卡 + 卡包 15 槽 + `showCardGet` 卡片图显示）
3. ~~監獄/醫院（case 4/5）~~ ✅ 2026-09-23 全链（入狱/住院 + 回合 + 保险 + **保释/出院面板**）；
   NPC 格占位待 P5
4. ~~銀行（case 14）+ 月度经济~~ ✅ 2026-09-23 全链（柜员机/停留/週轉/催收 + 15 号分红 +
   月初结息 + AI 买地；详见 `bank-system.md`）
5. ~~任务 F 建造菜单~~ → ✅ 2026-09-25 查詢面板（资产/地产/股票清單）+ 右侧資訊面板 4 页签
6. ~~物價指數面板（idx 5）~~ ✅ 2026-09-25（右侧面板 0x415F69「物價指數  %d」@(450,260) 核对完成）
   + ~~新聞（case 2）~~ ✅ 2026-09-26（36 事件全实现；拍卖 idx 7 ✅ 2026-09-26 `44b6df-news-events.md`）；
   ~~命運（case 3）~~ ✅ 2026-09-26（37 判定 + 49 效果 + 卡片联动；`44db81-fate-events.md`）
7. ~~小游戏~~ ✅ 2026-09-25 三游戏交互版全实现（`minigame_dialog.cpp`：挖寶/氣球/接錢
   0x415215/4DC/5FC + 共享 else 0x415457 兜底，四大恶人忽略；`41982d-p2-events.md` §6）；
   ~~樂透~~ ✅ 2026-09-25 投注+15 号开奖（`lottery-system.md`）；~~魔法屋~~ ✅ 2026-09-26
8. ~~百貨公司（case 15）~~ ✅ 2026-09-25（人类商店 UI + 自家赠礼 + AI 自动买卖，
   见 `42e931-department-store.md`、`ui-controls.md` §28）
9. ~~魔法屋（case 16）~~ ✅ 2026-09-26（`magic_house_dialog.cpp` 0x43380A：施法 UI 12 条件×12 惩罚
   + `auction_dialog.cpp` 0x43BDE5 拍卖面板（新闻 idx 7 激活）；`43380a-magic-house.md`/`43bde5-auction.md`、
   `ui-controls.md` §33/§34；**P2 事件格 case 2..16 全部完成**）

**待确认（已解决/顺延）**：
1. ~~优先级顺序~~ → 已确认见上
2. ~~建造菜单入口若是工具栏按钮，与菜单同批实现~~ → 已确认 = 工具条 case 6（且非"建造"而是查詢，见 §6）
3. ~~Ctrl+4 留空~~ → 已接入（改建卡效果，调试支持）
