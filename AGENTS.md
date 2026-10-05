# AGENTS.md

## 项目简介

《大富翁4》(Rich4) 逆向重建工程。原始 `rich4.exe`（1999 年，i386 PE，DirectDraw/DirectSound）
已无法在现代系统运行且无法跨平台。本项目通过逆向分析，用 **C++17 + SDL3** 重写引擎，
目标是 Windows/Linux 跨平台运行，并支持宽屏、快捷键等现代化操作。

游戏内码为 **BIG5（繁体）**；引擎内部统一使用 UTF-8。

## 当前进度

**M1（基本交互）已完成**：主菜单 → 选人/选地图 → 开局（跳伞入场，期间锁定交互）→
回合循环（掷骰三阶段：角色扔骰/骰子 FLC/点数停留 → 移动 → 过天）→
工具条（帮助/设置/大地图/读档/存档/托管AI）→ 右侧面板（玩家信息/日历/小地图/前进面板）→
地图物件（神明事件点/地标/住宅用地/商業用地/行業設施點/事件格）与**左键按住查看物件信息**。
非玩家控制期（AI 回合/掷骰/移动/结算）锁定交互（`g_playerControl`）。

**M2（进行中）**：P0 买地 ✅、P1 升级 ✅ / **他人住宅用地收租 ✅**（`landingEvent` 0x419A67：
同路段联合租金/连锁×2000/查封翻倍（**仅地主侧**）/免收状态/神明调整/showMessage/收款入银行；
**同盟分账 ✅ 2026-09-27**——地主 +65 对象的联合租金并入总额、按 `v134/(v8+v134)` 拆分转账、
消息 0x46399A「屬%s與%s」、高亮含同盟地块、破产解除同盟 0x40CE74；死神代付/卡片抵用 ✅
`resolveFeePayer`）、收费公式全部逆向归档 ✅（`pricing-formulas.md`）；
**商業用地 corp 全流程 ✅**（购地/建设施 `selectFacilityDialog`/升级/他人收费/旅館住宿，
`facility_dialog.cpp`+`roulette_dialog.cpp`）、**行業設施點 specPt 收费 ✅**（6 公式 + 公库 `fund`）、
**连锁店 ✅**（改建卡 0x44309B + 卡片栏 case8/`--exec card.rebuild`）；
Debug 工具 v2（Ctrl+1..9 构造/诊断 + Ctrl+Shift 分组，30 键全收口命令注册表，见 `debug-keys.md`）✅、
大地图填色 ✅、买地面板背景 ✅；**查詢面板 ✅（2026-09-25，旧称"建造菜单"为误：工具条 case 6
`sub_424492`→`sub_423CF3` = 帮助 idx 16 資產/地產/股票清單，无建造动作；`query_dialog.cpp` +
右侧資訊面板 4 页签 `game_panel.cpp`，见 `ui-controls.md` §27）**；**P2 事件格推进中**
（得點券 case 10/11/12 ✅、卡片格 case 13 ✅、監獄/醫院 case 4/5 ✅ 含保释·出院面板+护士动画
+ **开局默认在押 NPC**（小偷·強盜/流氓·間諜，0x40734F）+ **NPC 释放动画**（panel[64] 全身像
+ 答谢消息，0x43CFDB/0x43DE4C）+ **事件槽 NPC／四大恶人全链 ✅ 2026-09-26**（行走/抓回/恶行/存档，
`498df0-event-slot-npc.md`）；  **小游戏 case 6/7/8 ✅ 2026-09-25**
  （`minigame_dialog.cpp` `miniGameVisit`：**case 7 七彩氣球交互版 ✅**（0x4154DC：15s 射气球，
  分值/×2/÷2/? 随机效果/诱饵球，panel[78/79/91]+枪光标+音效 19/20/21+音乐场景 11）；
  **case 6 企鵝挖寶 ✅**（0x415215：15s 9×9 埋宝 8 方向企鹅行走，5 类宝物计分+火煤球即终+
  结局动画，panel[80..90]+RAW 掩码[81]+冰屋遮挡）；**case 8 喜從天降 ✅**（0x4155FC：18s
  鼠标控娃娃接财神钱袋，云投炸弹=爆炸 FLC 即终+评级表情，panel[92..99/100+ch]+data[526]）；
  AI/动画关走原版共享 else 0x415457——随机 50..69 点券 + "得點券%d點" +
  台词 `kValueLines[char][rand&1]`；四大恶人忽略；细节 `41982d-p2-events.md` §6）；
  **新聞 case 2 ✅ 2026-09-26**（`news_dialog.cpp`：`newsEvent` 0x44B6DF——panel[66] SMP 440×480
  画布 + data[441+idx] 388×251 插画 + 6 类标题 + 2400ms 阶段机；`newsEventCheck` 0x448BE2 36 条判定
  + `g_newsFuncs` 36 效果全实现（监狱医院/灾害破坏/三税/排行/股价停牌/超贷坐牢/公司罚款）；
  `stockNewsApply` 0x429040 入 `stock_system.cpp`；**拍卖 idx 7 ✅ 2026-09-26**（`auction_dialog.cpp`）、语音/浮动数字 P4、
   corp 单块高亮差异；详见 `44b6df-news-events.md`；调试 `Ctrl+Shift+A`）；
   **命運 case 3 ✅ 2026-09-26**（`fate_event.cpp` + `event_common.cpp`：37 项判定/载具改写 +
   49 效果（通用 33 + 4 地图专属坐牢 16，文本 #0185..#0233）/幸运判定 `sub_44B896`（luckB/C →
   免付/逃过/作废/加倍）/卡片联动（生日选卡面板 0x44192A、免罪 0x444BB2、嫁祸 0x44476A、
   选人对话框 0x440E1A）/出国·绑架状态 0x40D375；公共辅助自新闻提取；调试 `Ctrl+Shift+Z`；
   详见 `44db81-fate-events.md`）；
   **魔法屋 case 16 ✅ 2026-09-26**（`magic_house_dialog.cpp` 0x43380A：施法 UI 100ms 状态机
   （panel[18] 五段浮字 #0037..#0041 + 随机条件 0x431842（12 条件：财产/土地/房屋/现金/存款/点券
   最大并列 + 步行/机/车/附身/男女全部）+ 12 惩罚径向选择（panel[19] RAW pick + 悬停大图/名称 +
   音效 39）+ panel[20] FLC 退出；AI 条件+惩罚双随机排除自肥/拍卖）+ `auction_dialog.cpp`
   （0x43BDE5/0x43A2DD/0x439F0D 拍卖面板：7 按钮 PASS/5 档加价/放弃 + AI 出价公式 + 头像翻滚气泡 +
   落槌台词 #0136..#0147，**新闻 idx 7 同步激活**；`43bde5-auction.md`）；`43380a-magic-house.md`、`ui-controls.md`
    §33/§34；调试=传送魔法屋格 `Ctrl+Shift+G`（v2 删直开键）；**P2 事件格 case 2..16 全部完成**）；
   事件格 FLC 阻塞播放模板 `playEventFlc` + `drawEventFlcFrame` +
`showCardGet` + `jailPlayer`/`hospitalizePlayer`/`insurancePayout`；面板底座
`FloatMessage`/语音 `playVoice`/音乐栈见 `ui-controls.md` §25）。
**M2 股市 ✅**：`stock_system.cpp`（stockTick/认购/控股/买卖交易量）+ `spec_pt_dialog.cpp`
（0x41D1A9 认购 + 数字输入框）+ `stock_market_dialog.cpp`（0x42B58F/0x42AAFF 行情/持股切换·買/賣·
走勢圖饼+折线·休市，`panel.mkf[75]` 帧0/1/2）+ `advanceDay` 每日维护（停牌/新闻倒计时·休市·
交易量·stockTick）；`kStockNames[8][12]`；详见 `42b58f-stock-panel.md`。
**M2 银行/月度经济 ✅（2026-09-23）**：`bank_dialog.cpp`（柜员机 0x436EF8）+ `bank_stay_dialog.cpp`
（停留 0x435062/週轉 0x434492/催收 0x436034）+ `month_settle_dialog.cpp`（月初结息
0x439BFA/0x437E61：存款 ×1.1 + 悲情/冠军排行演出 + FLC 全身像）+ `dividend_dialog.cpp`
（15 号分红 0x42BA97/0x42B3EB，**含公司亏损由股东分担=负 fund 负分红**，2026-09-27 IDA
复核待实机）；`advanceDay` 接入 15 号分红/新月结息/卡片格·銀行格换位；
AI 买地 0x41D7D4；`transferMoney` 累计 monthSettleA/B；详见 `bank-system.md`。

**M2 P3 地图物件生命周期 ✅（2026-09-24）**：`map_objects.cpp`（attachObject 0x40EAD7 附身/
老虎机 0x440706/送丢卡/死神没收 + attachEnd 0x40E32C 飘走动画 + relocatePlayer 0x40CC56 +
damagePlayer/demolish/givePlayerItem）；`economy.cpp` deleteMapObject **配对轮替**（小↔大、惡犬↔土地公…）；
`onPlayerActionPhase` 物件分派（神明/惡犬/禮物/寶箱/路障/地雷/炸彈挂身·转移·爆炸 + 乞丐施捨）；
`updatePlayerStates` 附身寿命 7·13 天递减；渲染 8 方向帧/跟随玩家/浮空寿命数字/弹飞 flt；
`Player.luckA/B/C` + `GameState.trapStock`；详见 `map-object-refresh.md`。
**M2 P3.5 神明持续效果 ✅（2026-09-25）**：`landAfterMove`（0x40F381，landingEvent 公共末尾——
天使 `angelUpgrade` 0x40B110 免费加盖/建设施·封顶 FLC523、惡魔拆一层+记债 30×M+FLC526、土地公強佔
+赔偿 M×price×(level+2)/5）；福神付费升级后 `godBlessUpgrade` 0x40F8BE 免费追加一级；
死神代付 `resolveFeePayer`（0x40FBB8，三收费管线，含原版寶箱 idx14 照抄）+ 欠款矩阵
`addPlayerDebt` 0x40DF69（不入存档同原版；破产清算欠款列归集 ✅ `eliminatePlayer` 2026-09-27）；
**消息文本 BIG5 截断踩坑修正**
（0x4634C0/0x463514 IDA 显示 "%s" 实为 "%s顯靈\n\n加蓋一層房屋！/投資失敗！"）+
`checkCarriedGod` 守卫 helper（五处付费流标 [HELP 41/45/51]）；详见 `map-object-refresh.md` §13。
道具放置入口（0x446BAA/C88/D69）与 13 道具效果 ✅（2026-09-25，`item_effects.md`）；角色台词
气泡 ✅（`playItemLine` 0x44EF41）；**機器娃娃槽8 并入事件槽模型核证 ✅ 2026-09-27**
（`item-effects.md` §1.6：0x446AFB 写槽8/9步/踢物件/收尾等 flyCount/循环音槽9）；
**事件槽 NPC 四大恶人 ✅ 2026-09-26**（`498df0-event-slot-npc.md`；
**传送机 NPC 传送 ✅ 2026-09-27**（0x447857 分支：npcSlots cell/prev/dir/pixel + 镜像）；
AI 目的地 `sub_420EEE`=表读取 ✅ 已接；卡片/道具选 NPC 保持拒绝（原版双记录空转，见
`item-effects.md` §4.3 已知差异））。
请神/送神符效果函数 ✅（0x444E1A/0x444C45，`useInviteGodCard`/`useBanishGodCard`，
调试 `--exec "use.god invite|banish"` 直调）；cellTable 拾取 id 改 0xA100|槽+1 修段重叠。
研究所研发道具 ✅（2026-09-26，`lab_dialog.cpp` 0x44101D + `updatePlayerStates` 0x41C84F 尾倒计时产出）：
owner 停留 type4 逐级解锁选 1（`corp.sub` 门槛**灰度**）→ `researchItem/researchLeft=5` → 归零
`givePlayerItem(researchItem+8)`；机器工人/時光機/傳送機/工程車/核子飛彈 5 非卖品唯一入口，**道具 13 获取闭环**（`44101d-lab-develop.md`、`ui-controls.md` §35）；
**2026-09-27 修正（实机验证通过）**：触发移到收尾 loc_41B077（`turn_system.cpp labDevelopAfterLanding`）——
先弹通用升级询问、首次建设施即弹研究；面板改帧7 底图(不透明)+帧5 标题、未解锁灰度（非变暗）、悬停黄框、按下下沉。

**功能总览**：`docs/help-checklist.md`（帮助文档 99 条 → 实现状态，当前 ✅ 99 / 🟡 0 / ❌ 0；
**存档/读档/自动存档/时光机专项 ✅ 2026-09-28**，见 `docs/reverse/functions/402fd1-save-load.md`）。

**AI 功能专项 ✅ 完成（2026-09-29）**：道具链（0x420E9A 13 目标 + 0x40B221/0x40B343 路径候选域
+ 0x40A0B1/nearIso 溅射）+ 卡片链（0x41E69E + 31 目标函数**全量回验 11 处订正**，含烏龜条件方向
汇编核验）+ **回合动作链**（0x418DE6 买股 / 0x418DF4 卖股 / 0x418DFE 週轉 / 0x418E13 交易 /
0x418E31 状态守卫 / 0x418E70 骰子动态调整 / beginPlayerTurn 视口刷新）全实装；专项测试
`run_tests.py --filter ai_` 8 场景 + L2 `84_ai_marathon` 全绿；**全量矩阵 84/84 PASS 冻结基线**。
档案：`420e9a-item-ai.md`、`441baa-card-effects.md`、`418c55-ai-turn-actions.md`。

**M3 执行计划**：见 `docs/m3-plan.md`（终局链 checkVictory/通关地图选择/失败界面/AVI + 宽屏/快捷键 + 跨平台冻结）

**M3-A 终局链 ✅ 完成（2026-09-29，实机问题驱动修正）**：`checkVictory`/`defeatFlow`/run scene
分派/`gameClearFlow`+`mapSelectDialog` 全链 + **观感修正**——playLine 视口切说话玩家
（0x44EFBD `refreshGameUi`，修"破产致胜卡倒闭画面弹不出气泡"）、破产致胜 `currentPlayer=winner`
+ 认输台词位序订正（0x40D060/0x40D237 清算后、仅总存活>1）、选图界面全屏演出重做（滚动预览
`blitScrolledMap`+胜者角色行走 `panel[charIdx+100]`+装饰 `panel[93]`/`byte_46CCC4`，即时模式
每帧全屏重画**消重影**）、**蓝星(帧10)/红勾(帧8)预合成**（`UiImage::blitIntoFrame`，
`blitElementToCanvas 0x4562A5`=(dst帧,src帧,x,y) 语义）、multiple 仅「恭喜過關」大字无面板
（**多人局回主菜单=原版行为**，连续通关仅单人）、scene4 真 `loadDialog` 选档；
`player.bankrupt <p>` 调试命令 + `283_victory_last_stand`（全矩阵 89/89 绿）；
详见 `victory-flow.md`、`ui-controls.md` §38；余 284 四图连通实机验收、A5 AVI 不实现。

**M3-B 体验优化 ✅（2026-09-29）**：**演出打断加速**——原版核实 `sub_4528B9`/`sub_4544F6`
延时消息泵检测 514/517/257 **可提前结束并吞掉消息**（重写曾误做死等：showMessage/playLine/
showCardGet/showGodNarration/新闻·命运阶段/掛牌提示全部接左键按下+Esc/Enter/Space 打断，
playLine 打断并 `stopVoice`、showMessage 不停=原版语义）；`flcPlay` 打断由
`flcOpen flags bit1`（g_flcInterruptible）门控——**55 调用点全量回验：仅开场 playIntro 与
月结悲情/冠军全身像（kFlcParams 表 3/515/1027/1539）可断，游戏事件动画全部不可断**
（`playEventFlc(interruptible)` 默认 false=1:1，`playSettleFlc` 可断）。**GO 层级**——原版
=面板先画、FLC/高亮/跳伞在 backbuffer 末层叠加（动画盖 GO@(180,120)）→ `renderGameFrame`
重排为 地图→提示→面板→跳伞/FLC/高亮。**演出段隐藏光标**（增强）——`Cursor::setHidden`
由 renderFrame 按 `eventFlcActive||parachuteActive` 统一驱动 + 乐透开奖 3/5 / 月结全身像段。
**乐透花屏根因修复**——`Cursor::update` 旧"移动先 uncompose 写回陈旧 32×32 背景"在即时
模式全量重绘界面闪脏块 → 改为只跟踪动画/位置，compose 每帧从最新干净背景重抓。
**物件提示演出屏蔽 ✅（2026-09-29 实机反馈）**——原版提示（0x417559）是一次性增量绘制、
动画泵吞掉抬起也会被后续重绘抹掉；重写状态式即时重绘导致"动画期间松开左键提示不消失
（幽灵悬挂）"。收口 `tipPerfBlocked`（eventFlc/跳伞/入场/滚动/任一玩家行动状态 1·2·3/
`events().depth()>1` 任意模态）：`renderGameFrame` 演出帧清除+不画、`handleLeftButtonDown`
演出期忽略（原版等价"看不到"；纯静止停顿仍可按住查看=保留 0x4186BE 语义）、
flyObjectSprite/attachEnd/playHighlightBlink/showGodNarration/playEventFlc 入口
`clearObjectTipForPerf`；调试命令 `press/presssel/release`（单 DOWN/UP 模拟按住）+
trace `tip show`/`tip clear (perf)` + 场景 `302_tip_perf`；全矩阵 **91/91 绿**。
trace `msg/line/flc skip` + 场景 `300_skip_msg`；详见 `ui-controls.md` §39。

**M2 执行计划**：见 `docs/m2-plan.md`

**M4 执行计划**：见 `docs/m4-plan.md`（渲染/UI 现代化 + 追加体验：A 画布参数化 / B GO 面板 /
C 脏区+布局表+命中派生 / D 宽屏视口 / E 文本高分 / F `rich4.ini` / G 结构重组 / H 追加体验）。
**批次 B ✅ 2026-09-30（GO 面板演出期可见缺陷）**：原版 `byte_46CAFD` 生命周期 = 「进回合即 0
（0x418C55）→ 人类等输入才 1（0x4196F1）→ 按 GO/交棒回 0」，重写只看 `playerActionState==0`
就置 true 且 `beginPlayerTurn`/`nextPlayer` 从不置 0 → 控制位跨回合/跨演出保持 = GO 盖在
認輸演出/事件动画上。收口：新增窄谓词 **`blockingPerf`**（`eventFlcActive||parachuteActive||
pendingSpawnPlayer`，不含视口滚动/模态——原版这两类窗口面板仍在画面上）、
`drawAdvancePanel` 守卫 + trace `go panel draw cur=%d`、四入口显式 `disablePlayerControl`
（`beginPlayerTurn`/`nextPlayer`/`surrenderPlayer` 起手 + `topBarFinish` 的 `canResume` 加
`!blockingPerf`）；新命令 `trace.clear`、新场景 **`308_go_panel_surrender`**（`surrender;
trace.clear` 同帧清窗，**修前 FAIL 75 次 cur=0 → 修后 0 次**）。详见 `m4-plan.md` §14、
`ui-controls.md` §39 ⑤。验证：L0 **86/86**、L1 **93/93**（94 场景中 `84_ai_marathon` 按约定
不跑，见 `m4-plan.md` §9.2）、`re_map` 通过。
**批次 A1 ✅ 2026-09-30（画布运行期尺寸，scale 恒 1）**：`Surface` 宽高运行期化
（`create(renderer, w, h)`，默认 native 640×480）、95 处 `Surface::kWidth/kHeight` 全量
参数化（17 文件）、blit 家族行距/裁剪取自画布、全局裁剪窗口随画布初始化；新增 `--canvas WxH`
开发入口；L0 新增 4 用例。**修复 blitScrolledMap 高画布越界读**（行数 `min(画布高,480)`——
`--canvas 800x600` 选人界面崩溃根因）。详见 `m4-plan.md` §15。
**批次 A2 ✅ 2026-09-30（scale + 缩放 blit + 文本 1:1 光栅化 + preset/resize）**：
`Surface::scale/logicalToDevice/deviceToLogical` + `SurfaceScaleGuard`；blit 家族
**逻辑像素→设备块展开**（`blitScaled` 导出供 UI 散点复用；**像素完美**，scale=1 恒等）；
文本按 `设计字号×scale` 1:1 光栅化（阴影/描边/字距/边距乘 scale）；光标按 scale 放大；
**鼠标双坐标**（UI 逻辑 / 地图设备）；**free/wide 整幅画面放大**（worldScale=uiScale；
free 1280×960 = native 干净 2 倍）；**演出帧节拍改绝对截止时刻**（`frameWaitStep`，
放大后动画不变慢）；新增 `--scale <f>` / `--preset native|wide|free`（free 处理
`SDL_EVENT_WINDOW_RESIZED`）/ `--stats`（FPS/帧耗时诊断）；`run_tests.py` 新增 `--extra`。
实机修复（用户反馈）：free 界面错位→整幅放大、光标 SMP 不可见→SMP/SPR 双格式、
放大拖慢→块展开（free 17→9.8ms / wide 12→8.2ms）+ 动画绝对节拍。详见 `m4-plan.md` §16。
**下一步 = C 脏区重绘 + UI 布局表 + 命中派生**（D 宽屏视口依赖 C 的布局表）。

**批次 C1 ✅ 2026-09-30（静止帧跳过）**：`game_loop.idleFrame`——纯等待输入且无演出/
动画/滚动/模态时场景不变，timer 分支跳过每 tick 全量重绘（GO 闪烁翻转点仍重绘；
光标由 renderFrame 独立合成，不受影响）。性能（`--stats`，headless 静止期）：
native 5.5→1.5ms、free 9.8→3.5ms；free 空闲截图与全量重绘逐字节 diff=0（无残影）。
详见 `m4-plan.md` §17。

**批次 D1 ✅ 2026-09-30（宽地图区）**：等距投影表扩 **65×65**（表内原 29×29 逐项保真、
外圈边界步长外推；`tools/gen_map_tables.py` 生成）；`ui_layout` 布局派生（地图区宽 =
逻辑画布宽 − 右栏 200；宽屏右栏右移贴右缘）；宽屏走"与视口相交格点双循环"**去表化**
（地面菱形互不重叠 → 顺序无关），native 保留原 296 项遍历序逐字节保真；
`Surface` **绘制原点**（`deviceX/deviceY/spanX/spanY` + `SurfaceOriginGuard`）实现右栏
半径整体平移（右栏函数坐标保持 440 基准）；命中（小地图/箭头/页签/日历/地图区）全部
布局派生；FLC/跳伞素材在宽地图区水平居中。验证：native 逐字节 diff=0、free 降采样
2× diff 0.7%、wide 非黑 74%→97%（黑边填满）、冒烟全 PASS、性能 native 1.47 /
free 3.52 / wide 2.68 ms。详见 `m4-plan.md` §17。
**批次 C2 + D 收尾 ✅ 2026-10-01（提交 `b7410b7`→`d2bb9af`）**：
- **640 基准 UI 宽屏居中框架**：`Application::dispatchModalAware`——栈顶 `centerBase` 层且
  逻辑宽>640 时事件坐标 -base、绘制原点 +base（`SurfaceOriginGuard`）、全屏剧场模态
  （`fillBars`）进入时两侧填黑；游戏内循环/叠加式模态（设置/询问框/台词等 17 处）
  保留游戏画面。主菜单/选人/银行/股市等全部模态宽屏居中。
- **绘制原点全路径修正**：`blitElementRegion`/`blitSpriteFrameClipped`/`blitScrolledMap`/
  `scaleSurfaceChannels`/`saveRegion`/`restoreRegion`/`fillRect`/`blitSprScaled` 的
  `scale==1` 快速路径补 origin（`--canvas` 宽画布命中/绘制自洽）；`text.cpp` 落点
  deviceX 后 blit 侧 origin=0 防二次；`settingsDialog`/`dateDialog` 预绘制残影收口。
- **区域级脏区（原版 `dword_475110` 五位）**：`renderGamePanel(app, dirty)` +
  `renderGameFrameWith(app, dirty)` 分区重绘；timer 高频路径（滚动/掷骰/移动 → 地图层+
  小地图+顶栏；GO 翻转 → 地图层）保守局部，其余全量。**移动期"局部 vs 全量"同帧截图
  逐字节 IDENTICAL**。
- **世界锚定坐标派生**：旁白/飘走/飞行/回卷/裁剪全部逻辑画布派生（宽屏不截断）。
- **debug 合成输入**（click/clickr 等）走模态框架（640 基准 → +base 派发），headless
  截图与实机一致。
- 验证：native top/bank/set SHA 逐字节全等；`--canvas 1024x480` 居中+命中；22 场景全 PASS。
- 详见 `m4-plan.md` §18。
**M4 收尾 ✅ 2026-10-05（E~H + 实机多轮 + 并入 main）**：
- **E 文本高分**：用户决策跳过（低分辨率输出 + 线性过滤拉伸，见 §18.9/§18.10）。
- **F `rich4.ini` ✅**（`ef21ec9`）：`core/config.{h,cpp}`（INI 解析 / 缺失生成带注释模板 /
  `setConfigValue`；优先级 CLI > ini > RICH4.CFG > 内置默认；通用 `--set k=v`）；接入
  字体/字号/日志路径/媒体目录/vsync/fullscreen。RICH4.CFG 红线保持。
- **G 结构重组 ✅**：(1/2) 生成物集中 `src/gen/`、debug 归 `src/debug/`（`5bd7c8b`）；
  (2/2) `src/app/` 60 文件细分 `dialogs/`(33)+`ai/`(2)+`ui/`(5)+`game/`(20)（`b919f51`
  + 后续批，纯 git mv）；大文件拆分（turn_system/debug）留后续专项。
- **H 追加体验 ✅**：小游戏 tick 间插值（`a715bde`，`kModalFrameEvent` 真实时钟派发 /
  虚拟时钟零变化）、悬停目标预览高亮（`0aad3bc`）、同格多目标消歧（`80f45a0`，无主空地
  并入 y 排序 items 通道）、射程限制**撤回**（用户确认理解有误，保持原版语义，无代码改动）；
  尾巴（`50f4cef`）：财神巡游插值 + 事件槽 NPC 悬停提示名 + `sub_40D293` 15 调用点全量复核。
- **实机多轮修复（§18.6~18.10）**：主菜单/选人残影、tick 追补 + 自适应 vsync、GO/骰子居中 +
  模态绘制边界、骰子提速 + 小地图视野框、**逻辑画布 + GPU 放大**（分辨率无关帧率）、
  入狱/住院 FLC 回卷拖影、线性放大过滤默认开启。
- **并入 main ✅（merge `27bc265`）**：M4 36 提交 + main 8 修复合并（构建 + 16 场景冒烟 +
  ctest 全绿）；详见 `m4-plan.md` §19/§19.1。
- **宽屏全屏模态修复 ✅ 2026-10-05（实机，`m4-plan.md` §20）**：ATM 重影（删模态外无 origin
  预绘制、save/restore +base）；银行 4 处 fillBars 订正回 true；**全屏剧场模态背景改为
  native 640 布局重绘居中**（`renderModalBackdrop`+`LayoutNativeGuard`，右栏不再被黑边切半）、
  handler 返回后补填两侧、退出 `m_forceRepaint` 恢复；`mouseLogicalPos` 减绘制 origin
  （真实鼠标路径按钮错位根因，debug 合成输入同步 +base）；拍卖静态帧移入 `handler(nullptr)`；
  分红/confirm/defeat/mapSelect 快照坐标 +base。详见 `ui-controls.md` §22.1。
- **实机二轮 ✅ 2026-10-05（`m4-plan.md` §20.4）**：**竖排文本修复**（原版 `drawText` align==3
  = 竖排 `drawTextVertical` 0x44F7C7；重写 text.cpp 测量框宽高写反+首字裁顶 → 日历"星期五"、
  托管"確定/取消"只显示单字；5 处 align=3 调用点=日历星期×2/托管/右栏页签）；
  **fillBars 分类定稿**——全屏剧场（两侧黑）=银行停留/百货/医院/监狱/魔法屋/拍卖/开奖/结算等
  640 铺底界面；叠加式（保留游戏画面）=ATM/新闻/命运/存读档/大地图/设置等面板。

**跨平台 ✅ 2026-09-30**（原 M3-C，Windows x64 + Linux x86-64）：GDI → **FreeType 统一文本渲染**
（删 366 行 GDI、字体 `resources/Fonts/` 不入库/缺失降级、`kFontSizeScale` 可调）、
**cp950 内嵌映射表**（`tools/gen_big5_tables.py` 生成，13752 项，替代 `MultiByteToWideChar(950)`）、
**自带 PRNG 复刻 MSVC LCG**（71 处 `std::rand` 收口 → `--seed` 两平台序列一致）、
**可写路径回退**（游戏目录→`SDL_GetPrefPath`）、资源名大小写无关解析、strict-aliasing/格式串/
事件类型截断等 Linux 首轮编译暴露问题清理（含 `--game-key` 失效 bug 修复）。
**两平台构建零警告、L0 67/67、L1 93 场景结果完全相同**（92 PASS + 同 1 TIMEOUT）。详见
**`docs/cross-platform.md`**；「窗口分辨率高清文本层」列后续专项（同档 §9）。

**M2 完成纪要**（✅ 全部完成 2026-09-29；各档案标注的「待实机验证/待实机验收」项——分红/週轉/
公告栏挂牌/买股卖股等——统一并入 **M3 收尾后一批实机测试**（用户 2026-09-29 决策）。
详见 `docs/reverse/functions/` 各档案「差异」段）：

- ~~P2 事件格实现~~ ✅ **case 2..16 全部完成 2026-09-26**（见 `41982d-p2-events.md`；
  银行/小游戏/樂透/百貨/新聞/命運/魔法屋全链 ✅；**魔法屋+拍卖 2026-09-26**：
  `magic_house_dialog.cpp` 0x43380A + `auction_dialog.cpp` 0x43BDE5（新闻 idx 7 激活），
  `43380a-magic-house.md`/`43bde5-auction.md`、`ui-controls.md` §33/§34，调试=传送魔法屋格 `Ctrl+Shift+G`；
  面板底座 `ui-controls.md` §25，事件槽 NPC 四大恶人 ✅ 2026-09-26；**投降召唤死神 ✅ 2026-09-27**
  （`surrenderPlayer` 0x411AE0 + `deathGodSummonDialog` 0x4339D9/0x433088））
- **P2 樂透（case 9）✅ 2026-09-25**：`lottery_dialog.cpp` + `lottery.h`——投注界面
  （0x42F7FC：9×4 网格选号/红圈帧 7/已售 -10 变暗/panel[14] FLC 循环/装饰闪烁/钱不足链；AI
  `cash>1000` 自动随机号）+ 15 号开奖（0x431712+0x43010C：panel[15/16/17]、摇号算法
  「全玩家 ≤10 注随机否则已售号随机」、中奖 FLC/角色名爆炸气泡/无人累积、人物眨眼·嘴部动画
  含 0x1000/0x2000 锁存位与覆盖帧持久保留）；数据 `lotteryNumbers`(0x4990B8) +
  `publicFund`(0x499080，原 stockPublicFund 改名) + 破产清号 + 存档/时光机快照；调试 `Ctrl+Shift+R`
  保送中奖；详见 `lottery-system.md`、`ui-controls.md` §29
- **P2 百貨公司（case 15）✅ 2026-09-25**：`shop_dialog.cpp`（0x42E931/0x42D37F——自家赠礼 +
  人类商店 UI（panel[10] 38 帧：抽屉缓动滑入/店员/招牌动画/切页/卡片·道具买卖/卖出 90%）+
  AI 自动买卖；`playValueLine` 0x44F230；`drawCardBagAt/drawItemBagAt` 带坐标复用；
  `landingEvent` case 15（实参 objId=special 6000+n）；调试=传送百货格 `Ctrl+Shift+G`（v2 删直开键）；
  详见 `42e931-department-store.md`、`ui-controls.md` §28
- ~~任务 F 建造菜单~~ ✅ 2026-09-25 订正为**查詢面板**（资产/地产/股票清單 + 右侧面板 4 页签；
  工具条 case 6 `sub_424492`→`sub_423CF3`；旧称"建造队列"的 `g_miscTable336`/`sub_428475`
  实为交易挂单表，已随下条实现）
- **交易市場「公佈欄」 ✅ 2026-09-27**（工具条 case 9，帮助 idx 11；`trade_market.cpp`/`trade_market.h`）：
  `GameState.tradeSlots[8][7]`（`g_miscTable336` 0x4967E0，类型 **1股票/2地产/3道具/4卡片**）；
  `clearInvalidTradeOrders` 0x42483E / `queueTradeOrder` 0x4246C5 / `removeTradeOrder` 0x4247D5 /
  `executeTrade` 0x4255DA / `ageTradeOrders` 0x428475；UI 主面板 0x427C21 + 详情 0x42704E +
  股票/地产/道具/卡片 4 子对话框（panel[73]/[74]）；**AI 自动交易** `tradeAiTurn`（0x4284BE
  AI 分支，接入 `beginPlayerTurn` case 2/5）；存档（偏移 9513）+ 时光机 + advanceDay 全接入；
  `4284be-trade-market.md`、`ui-controls.md` §36；**待实机验收**
- 经济系统剩余：无（股市交易市场/挂单已随公佈欄完成）；~~P4 卡片栏/道具栏 UI~~ ✅（2026-09-24，工具条 case7/8 `useCardDialog` 0x441BAA / `itemBagDialog` 0x447D97，panel.mkf[11] 5×3 网格 + `cardRebuildEffect`/神符；改建/请神/送神卡可用，**卡片效果 30 卡全实现 ✅ 2026-09-26**（档A/B/C：`card_effects.cpp` + 被动 18復仇/19嫁祸/20免費/21免罪接入惩罚·收费链 `resolvePenaltyTarget`/`resolveFeePayer`；红黑卡选股 `stockPickDialog`；**AI/托管用卡链 ✅ `ai_card.cpp`**（0x41E69E 性格判定 + `dword_475324[31]` 目标表，`beginPlayerTurn` rand 触发）；**角色台词 ✅**（`card_lines.cpp` 生成表 0x48123A + `playCardLine`）；逐卡见 `441baa-card-effects.md`；**2026-09-30 语音时序勘误（IDA 逐卡复核，实机反馈）**：红/黑卡使用者在选股前（补缺失）、抢夺使用者在选卡前、拍賣使用者先在原主前、转向目标台词在转向后、乌龟/停留使用者仅目标≠自己、換地/換屋改条件单条、免费卡补收费方槽79、expr 按 IDA 补齐，卡片场景 23/23 PASS；**2026-09-30 二轮实机修复**：选目标 mode BYTE3+1=15 帧动画光标（红/黑卡选股 cursorSelect(12,15,10)）、无效使用补 g_uiSoundMusicTip、卡片特写绘制顺序 tip→文本→卡片、卡牌栏绘制前全屏重绘（0x441C90 restoreBackground 等价，消残影）、**冬眠/梦游 128 天不醒修复**（0x41C96B/0x41C9A7 bit7 醒来段补全）与**冬眠冰冻蓝白特效**（勘误：非灰化；BGR565 分支 0x45566E → `blitSpriteFrameFreezeClipped` `(B=31,G=R=gray&0x1F)`，旧纯灰+bit15 溢出=花屏）；**2026-09-30 三轮**：30 卡逐 IDA 核对 17/30 + **NPC 目标分支 6 卡补齐**（停留 timerC/乌龟 timerD/梦游 timerB/陷害坐牢/转向，吞卡后有效果）+ 梦游字段 byte58→byte66 + `sub_41D546` 统一（manualView=false+重绘）；**拍卖眼部动画修复**（逐帧擦 `frame(n-1)` bbox + 帧号=rollCnt/v21，此前缺擦除帧号差 1=重影）；全衰减字段逐行对照原版一致（住宿/出国/监狱/医院/冬眠/梦游/乌龟/停留/银行/同盟/保险/附身/载具/NPC 计时/研究）；**2026-09-30 四轮**：**剩余 13 卡核验完成 → 30/30 全卡核对完毕**（改建/天使/恶魔/怪兽/拆除/復仇/嫁祸/免費/免罪/送神/请神/红卡/黑卡；怪獸=id11 整拆 FLC557、拆除=id12 降级 FLC529 与卡名/卡价/帮助四方吻合）——修复**红/黑卡停牌天数 0x30/0x03→0x20/0x02**（帮助"三天"=当天+2）、**查税文案 "抽取%s\n\n%d元稅金！"**、**嫁祸 AI 阈值 mode 参数**（mode1 收费 `fee>现金或(rand%4000+4000)M<fee`、mode2 查税 `4000M<现金×0.2`）、**改建卡 AI 免弹人类面板**（aiCardTarget 预选）；研究所按用户裁定**恢复 IDA 原逻辑**（研究中可重选覆盖））；**AI/托管用道具链 ✅ 2026-09-26**（`ai_item.cpp` 0x420E9A + `funcs_420EE6`@0x4753A0 13 目标函数 + `dword_48BE64`；**2026-09-28 距离语义修正 ✅**——候选域补 前方/回溯路径预测（0x40B221/0x40B343 重写 `predictPathFwd/Back`）+ 中心采集（0x40A0B1 `collectAround`）：骰子点数=路径索引、地雷/炸彈拆分、飛彈/核彈溅射·比例保护、機器娃娃对 AI 解锁（0x446AFB 无 alive 守卫）、`kItemAiPersona[13]` 订正、AI 分支排除時光機+≤4件；`420e9a-item-ai.md` §1.1、`260_ai_items`）；13 道具效果全实现 ✅（2026-09-25，`item_effects.cpp`：气泡台词/目标选择模态/expireAssets/遥控骰子/娃娃槽8/时光机快照，逐道具见 `item-effects.md`）**
- AI 决策：AI 买地 ✅（`sub_41D7D4`，2026-09-23）；`alive & 6` 电脑/托管自动行动
  （`landingEvent` 0x41982D 回合主流程）
- ~~存档完整恢复~~ ✅ **2026-09-28**：`parseSaveBody`/`saveGameToSlot` 1:1 原版字段序，恢复玩家/地块
  mapDat/卡片·道具/股票(均价·停牌·新闻·保留·交易量交织)/挂单/回合·日期/事件槽/**每玩家时光机快照**
  （`snapshots[].block` 10008B）；自动存档 `nextPlayer`→SAVE0(AUTO) 0x41904C；游戏内工具条 case3 读档闭环；
  `loadMap(app, fresh)` 拆分（读档保留存档地块/cellTable）；`resetSceneForReload` 0x4080F5 修视角/跳伞串局；
  `initTurnState` 不再硬置 currentPlayer=0。时光机快照改 10008B blob 内核 + news/fate 纳入 + 删 `misc8B`。
  **`--save-selftest` 验证 SAVE0..5 往返 STABLE=YES、与原版同尺寸**；`402fd1-save-load.md`。
- ~~事件槽 NPC（玩家 4-7）~~ ✅ **2026-09-26 全链实现**（`498df0-event-slot-npc.md`：
  开局关押 busy=1/1/2/2 → 保释行走 → `LABEL_88` 恶行 → 同类建筑抓回；物件分支/回合联动/
  渲染面板/存档；调试 `Ctrl+Shift+8` dump、释放 `--exec "npc.release <npc>"`）
- **P4 语音专项 ✅ 全部完成（2026-09-27）**：清单 → `docs/reverse/functions/p4-voice-batch.md`
  （**P4-A** 踩中台词+`playLine` 表情帧 expr；**P4-B/D1** `kMoneyLines` 金额/状态台词 +
  收款/付款/欠债/状态天数/连锁/意外之财六函数（收租/同盟分账/corp/封顶/连锁/住宿入狱住院出国/
  大財神/神明免付）；**P4-C/D2** `attachObject` 全 case（哭 列22）+ `attachEnd` 飘走 +
  `godBlessUpgrade` + 状态提示（列19/20/21）+ 土地公大笑；**P4-D/D3** `playUnluckyLine`
  （灾害/拆屋/载具/没收 列3/4/5）+ 得點券（kValueLines）+ 卡片格 + 命运收付款 + 新闻业主 ×4 +
  淘汰/终局胜利；勘误：选卡·道具栏=空函数、请神符=卡片表槽22 已播；**修正 playStatusDaysLine
  列映射 bug**（原误用付款列9/10/11 → 倒霉列3/4/5）；checkVictory/defeatFlow 留 M3；
  **注意 expr 不是表选择器**——详情见专项 §1；
  **2026-09-30 列勘误（实机反馈）**：`kMoneyLines` 位置7/8/9 ↔ 原版列16/17/18 被用反——
  `playDebtorLine` 列16→**列18**（欠债不再播「我是個大地主」）、`playEstateChainSpeech`
  买地→**列16**/升级→**列17**（加盖不再播「兄弟，我記住你了」）；同批补
  `ownerCanCollectRent` 免收列13、`fateApplySellAllStock` 列5、载具 id10 列3/4 随机、
  逃过坐牢列0、`jailPlayer`/`hospitalizePlayer` 列19/20（含累加路径），见专项 §5）
- **角色名/公司名空格专项 ✅（2026-09-29）**：原版名字表带 `0x20` 排版空格（4 字宽），
  消息 sprintf 经 `copyNameNoSpaces`(0x452946) 去空格、drawText 直绘保留——重写逐点回验 44 调用点
  矩阵并订正 13 处消息漏去空格 / 6 处误去空格（魔法屋惩罚·復仇/免費/免罪卡·AI 銀行回退）、
  补齐 AI 紅黑卡「對%s使用%s！」消息与「買進/賣出%d**張**」量词；
  详见 `452946-name-no-space.md`

## 目录结构

| 路径 | 说明 |
|------|------|
| `resources/MultiverseJourney/` | Steam 原版游戏文件（exe + mkf + dat + cfg）+ IDB，**不提交** |
| `docs/formats/` | 资源格式规范（已完整解析，见 `README.md`） |
| `docs/reverse/` | 逆向规范与函数档案（注释格式、地址映射表、逐函数笔记） |
| `tools/` | Python 逆向与资源解析工具 |
| `include/game/` | C++ 头文件 |
| `src/` | C++ 源码 |
| `build/` | 构建输出（已忽略） |

## 构建

工具链：**Windows = MSVC (VS 18 Community)**；**Linux = GCC/Clang**；均用 CMake + Ninja
（跨平台方案与验证记录见 **`docs/cross-platform.md`**）。

### 准备依赖

- **SDL3**：从 https://github.com/libsdl-org/SDL/releases 下载 `SDL3-devel-<ver>-VC.zip`，
  解压到 `third_party/`（如 `third_party/SDL3-3.4.16/`）。CMake 会自动查找该目录（Windows）；
  Linux 走系统包（`sdl3`）或 FetchContent 源码构建（较慢）。
- **FreeType**（文本渲染）：`third_party/freetype`（本地源码）优先；Linux 可用系统
  `freetype2`（`find_package` 优先）；均无则 FetchContent（tag `VER-2-14-3`）。
- **字体**：`resources/Fonts/HarmonyOS_Sans_SC_{Regular,Bold}.ttf`（不入库，需自行准备；
  缺失时文本渲染降级为空操作，不影响逻辑与 headless 测试）。

### 编译

```powershell
# Windows
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```bash
# Linux（Arch: sudo pacman -S cmake ninja sdl3 freetype2）
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
```

> 注意：Strawberry Perl 自带的 cmake/ninja 在本机已损坏（缺 DLL），
> 请使用 VS 自带或独立安装的 CMake/Ninja。

## 运行

```powershell
# Windows
./build/rich4.exe --game resources/MultiverseJourney
# Linux
./build/linux/rich4 --game resources/MultiverseJourney
```

## 调试

```powershell
./build/rich4.exe --game resources/MultiverseJourney --debug
```

`--debug` 启用 **Ctrl+1..9 构造/诊断 + Ctrl+Shift 分组** 测试热键（30 键 v2，
每键等价 `--exec "<命令>"`，实现收口 `src/app/debug/debug.cpp` 命令注册表），
完整键位、旧键迁移表与测试流程见 `docs/debug-keys.md`。

## 测试

**自动化=代码级三支柱（状态/演出 Trace/字节-数据），画面观感一律人工**——总纲与已落地
能力见 **`docs/testing.md`**。已就绪：虚拟时钟（`--headless/--test-clock`，44 处墙钟收口）、
`--seed` + 定点随机注入（`dbg::roll/raw` 21 槽，装饰随机不接；**随机数已收口自带 PRNG
复刻 MSVC LCG → `--seed` 两平台序列一致**，见 `docs/cross-platform.md` §6）、演出 trace（`--trace-out`，
批1：FLC/消息/音效/语音/高亮/飞行/落地分派/转盘/模态）、内存日志环、存档往返
`--save-selftest`（`STABLE=YES`）、dummy 驱动无头渲染 + `--quickstart`、**统一调试命令层 +
`--script/--exec` 断言 DSL（assert state/log/trace + wait/confirm/click/rng，失败→退出码）**、
**L0 CTest（`rich4_tests`：收租公式/存档往返/MKF-LZHUF）**、**`tools/run_tests.py` 编排 +
覆盖矩阵**、首批 4 场景（smoke/收租/购地/轮盘注入）headless 全绿。待落地：L1 场景铺满矩阵、
named region、L2 长跑不变量、trace 批2~4、旧热键迁移命令表。
**推进疑难点/设计取舍/实机对照清单 → `docs/testing-issues.md`**（实施时持续更新）。

```powershell
./build/rich4.exe --game resources/MultiverseJourney --headless --seed 42 --quickstart 0,4,1 --script tests/scenarios/20_rent_route.txt
python tools/run_tests.py --matrix    # ctest + 全场景 + 覆盖矩阵（build/test-report.json）
./build/rich4.exe --game resources/MultiverseJourney --save-selftest 0   # 存档 save∘parse∘save 字节稳定
```

## Python 工具

```bash
# 一键解包全部资源 + 生成索引
python tools/extract_all.py resources/MultiverseJourney --out ./assets --manifest docs/formats/resource-manifest.csv

python tools/mkf.py list resources/MultiverseJourney/Data.mkf
python tools/spr.py extract <spr.bin> <outdir> [--rgb565]
python tools/smp.py extract <smp.bin> <outdir> [--rgb565]

# 逆向标注校验 + 生成地址映射表
python tools/re_map.py
```

## 代码约定

- **C++17**，根命名空间 `rich4`
- 文件/目录 `snake_case`；类型 `PascalCase`；函数 `camelCase`；私有成员 `m_`
- 头文件用 `#pragma once`
- 平台层不抛异常，错误以 `bool` / `std::optional` 返回
- 内部字符串 UTF-8；BIG5 仅在资源加载期转换
- **不要添加无关注释**；格式相关说明可引用 `docs/formats/*.md`
- **逆向标注强制**：每个重写函数/全局都要标注原始地址与依据
  （`// [RE 0xXXXXXX] 原符号名` + `依据:` 行；平台迁移用 `[PORT ...]` + `替换依据:`），
  格式见 `docs/reverse/README.md`，提交前跑 `python tools/re_map.py` 校验

## 重要参考

- `docs/cross-platform.md` — **跨平台方案与验证记录**（Windows/Linux 构建、FreeType 文本、
  cp950 内嵌表、自带 PRNG、可写路径回退；「窗口分辨率文本层」后续专项）
- `docs/formats/README.md` — 资源格式总览与工具索引
- `docs/formats/lzhuf.md` — 压缩算法（含易错点说明）
- `docs/reverse/README.md` — 逆向注释规范与工作流
- `docs/reverse/address-map.md` — 地址↔重写符号映射表（自动生成）
- `include/game/formats.h` — 全部格式的 C 结构定义
- `docs/help-checklist.md` — **游戏内帮助 99 条 → 功能实现检查表**（机制描述权威索引）
- `docs/reverse/functions/gameplay-map-mechanics.md` — 地块机制总览与验收检查表
- `docs/reverse/functions/pricing-formulas.md` — **收费/价格公式总汇**（物价指数、轮盘、住宅用地/商業用地/行業設施點、到期清算、MAPDAT 数值）
- `docs/reverse/functions/41982d-p2-events.md` — P2 事件格（case 2..16）专项研究
- `docs/reverse/functions/402fd1-save-load.md` — **存档/读档/自动存档/时光机**（1:1 原版 SAVE%d.DAT
  字段序 + mapDat blob 地块 + 每玩家 mapBlocks 10008B 快照 + 0x4080F5 复位；`--save-selftest` 往返验证）
- `docs/reverse/functions/map-target-audit.md` — **地图目标判定专项**（0x445E4D mode/BYTE1
  过滤/32 调用点矩阵/批次清单；道具·卡牌·事件"以地图为目标"的统一核查入口）
- `docs/reverse/functions/41982d-estate-rent.md` — 收租/收费专项（case 0 他人住宅用地收租全流程）
- `docs/reverse/functions/stock-system.md` — **股市系统**（行情 stockTick/控股/认购/行情界面/交易市场/数字输入框）
- `docs/reverse/functions/452946-name-no-space.md` — **角色名/公司名空格双轨制**（原版 4 字宽补空格、
  消息经 copyNameNoSpaces 去空格、drawText 保留；44 调用点矩阵 + 不去空格例外清单）
- `docs/reverse/functions/42b58f-stock-panel.md` — **股市面板**（0x42B58F 行情/持股/买卖/走勢圖/休市 精确实现）
- `docs/reverse/ui-controls.md` — **游戏内 UI 控件指南**（浮雕按钮/变暗表/文本对齐/数字框/模态框架/…；**发现新控件必须更新此文件**）
- `docs/reverse/frame-anchor.md` — **帧锚点 offset 与落点对齐**（原版落点=实参−offset、
  像素对齐校验流程 `tools/frame_align.py`、非零 offset 资源清单；元素错位类问题先查此文件）

## 工作经验（踩坑与约定）

### 逆向方法

- **不猜测，查 IDA**：音效索引、帧号、结构偏移、表内容一律先用 IDA 验证
  （`get_bytes` 读原始表、`decompile` 看调用链与常量），再写代码。
  凭猜测写 id/帧号是「音效对不上 / 贴图错帧」类问题的首要原因。
- **帧头公式**：精灵/元素帧头 = `资源基址 + 12 + 12*帧号`
  （如 `+84`=帧6、`+72`=帧5、`+24`=帧1）；结构数组内偏移如
  `&dword_48231A + 8*控件 + 8`。误读会画错帧或错位。
  **反查帧号时先列资源帧表**（Python 解析 SMP/SPR w/h + offsetX/Y）对号入座：
  `g_tipFrame`(data.mkf[517]) `+96` = **帧7**（400×89 研究所面板）不是帧8（数字'0'）、
  `+72` = 帧5（249×170 提示框）——2026-09-27 研究所面板"裸奔成黑块+大气泡"根因。
- **`word_46CAEC` 不是颜色**：它是 640×480 surface 描述符首地址（首 word=宽 640），
  `drawRectBorder(surface,x,y,w,h)` 的颜色来自调用点 `push` 的 RGB888 值（经 `convertColor`）——
  研究所/设施/选人悬停 = `0xFFFF00` 黄、股市行选中 = `0xFFFFFF` 白。曾把 640 误当
  RGB555 绿 `0x0280`（2026-09-27 修正）。
- **帧锚点 offset**：SMP/SPR 帧头 `x/y` 是**锚点偏移**，原版与重写 blit 的落点都是
  **实参 − offset**（重写见 `blit.cpp` `clipBlit`）→ **移植直接抄原版实参，勿自行加减
  offset**（曾双重补偿踩坑）。元素错位用 `python tools/frame_align.py align` 做像素差扫描
  验证原版实参，**勿肉眼判断**；原版实参偶有笔误（医院复位帧 8/13 传了 Rect.right/bottom）。
  方法/清单见 `docs/reverse/frame-anchor.md`。
- **状态机帧序就是语义**：移动状态 case1 原版每帧顺序 = **先**处理上一步到达格的
  `onPlayerActionPhase`（0x40D932 到达标志）→ **再**判剩余步数收尾；重写把 `steps==0 → 转状态3`
  前置会让**最后一步（stopped=true）的停留类触发全部丢失**（附身/惡犬/禮物/寶箱/地雷/炸彈
  实机"踩中无反应"根因，2026-09-25 修）。逐帧状态机迁移时，"标志位处理在收尾判断之前/之后"
  必须逐行对照原反编译顺序。
- **时序精确到调用点**：同一功能在不同阶段表现不同（例：掷骰——扔骰动画无音效 →
  骰子 FLC 第30帧与结束各播一次落地槽 → 转移动才播载具槽）；必须看反编译中
  调用发生的**具体位置**，不能想当然。阻塞消息（showMessage）之前若有数据删除，
  必须对照原版的 `refreshGameUi(0,0,1)` 插入显式场景重绘（原版靠 DDraw 表面保留，
  重写画在旧帧上会出现"消息先弹、物件后消失"）。
- **区分资源格式**：`UiImage.load` 仅支持 SPR/SMP；RAW 无头位图用
  `decodeRawBitmap`（按字节数推断尺寸）；FLC 用 `FliDecoder`。
- **单 buffer 语义**：原版每个音效槽是单 DirectSound buffer
  （`sub_4542E9` 停 + `sub_4542CE` 播）→ 重写用同槽替换，避免多声部叠加。
- **循环音效 ≠ 每帧重播**：`sub_4542CE(handle, 1)` 第二参 = DSBPLAY_LOOPING（老虎机滚动音
  dword_475D3C 进入时播一次、case8 `audioStopEffect` 停）；每 tick `playEffect` 会叠加多
  声部（重写已加 `playEffectLooping`/`stopEffect`）。
- **阻塞 FLC 动画期间游戏必须冻结**：原版 `sub_45144F` 的 PeekMessage 只取消息**不派发**
  窗口过程；重写 `playEventFlc` 循环 `pumpEvents` 会重入 gameEventHandler（状态机在动画
  下偷跑）且 `renderGameFrame` 把视口按**传送后**的玩家重居中（住院 524/入狱 538 一开演
  画面就跳到医院/监狱的根因）。修复三件套：timer 分支 `eventFlcActive` 守卫直接返回；`playEventFlc(switchFrame)` = 原版 flcOpen flags BYTE2 **场景切换帧**（保帧播放至第 N 帧做一次**视口锁定**的全量重绘：524=30 / 538=18 / 525/532/526=3 / 552=1——`sub_40829D(-1,0)` 用上次视口 dword_48B2AC，**不随传送后玩家位置重居中**：视口原地不动、仅重建绘制列表擦除角色，动画播完返回主循环后视口才切到医院/监狱；重写以临时 manualView+viewSmooth 锁视口实现）；**棋子守卫对齐 0x4086A1**（`stateFlags!=0 && !(alive&0x20)` 隐藏，原重写只查 BYTE0 住宿 → 住院/入狱角色不消失）。
- **物件飞行（flyObjectSprite 0x40E669）是"背景快照叠加"不是"每帧重绘"**：原版
  `saveBackground(地图区)` + 每帧只恢复上一帧精灵区域 + 只画精灵 + flip，**飞行期间场景不重绘**；
  `createMapObject` 虽已把对象写入目标格（坐标绑定），但要等飞行结束调用方
  `sub_41D546`/`refreshGameUi(0,0,1)` 全屏重绘后物件才出现 = **"飞过去 → 出现"**。
  重写若每帧 `renderGameFrame`，已创建的对象立即显示 = "先出现 → 再飞"（2026-09-27 实机反馈）。
  循环内也不要 `pumpEvents`（原版不派发窗口消息，防状态机偷跑）；结束 `sub_45285E(a6)`
  是**停留毫秒**（放置类 100、请神 0），末帧精灵保留屏上。
- **`refreshGameUi(x,y,flags)` 不是"重绘"**：`0x41D476` 只有 flags&1 才 `sub_40829D` 重绘
  （`(0,0,1)` = 全量重绘）；`(x,y,0/2)` 仅设 `dword_48BE18/BE1C/BE20` 视口参数 + 小地图/面板
  ——**画面仍停在旧表面**（FLC/破坏动画在旧视口背景上叠加，播完 `sub_41D546` 才切视口）。
  且 `(x,y)` == 当前玩家坐标时 `dword_48BE18=0`（改回跟随玩家）。重写 `focusView` 直改
  manualView+重绘会提前切视口（外星人/飞弹类差异来源，见地图类专项）。
- **帮助文档是第一手机制依据**：`help.mkf` 99 条（8 分类，分类/标题表 `[RE 0x4761B4]`/`0x4761B8`）
  已提取为 `docs/help-checklist.md`；研究/实现时用 `[HELP idx]` 标注引用。
  但**帮助文本可能错**（idx 58 冬眠卡误用红卡文本）或与实现不符
  （医院罚款帮助写 1000/天，代码 `g_moneyMul*2000*days`）→ 以 IDA/实测为准。
- **术语以原文为准，简繁同字可自由转换**：专有名词（地块/设施/费用名、卡片/道具/神明名）
  对照 `help.mkf` 原文、MAPDAT 格名与 exe 名表（`g_costNames`/`g_facilityNames`/`kObjectNames` 等）；
  文档与注释**允许简体**（如 商業用地/商业用地、載具/载具 均可），
  **已处理过的繁体文本不再回改**；但同字转换勿改错字（"商場"实为住宅等级名 `kBuildingNames[3]`、
  设施名应为"購物中心"——帮助列表段与详述段用词不一时以 exe 表为准）。
- **MAPDAT 实测定类型**：cellEnt `+4` 名称 / `+36` 低字节类型 / `+34` 图块，
  用 Python 解析 `map.mkf[2*(4*mode+map)+1]` 逐项打印；比"猜测类型对应"可靠
  （特殊地点 16 类与帮助页逐项吻合，一次确定）。
- **switch case 读跳转表**：`get_bytes` 读 jpt（如 `landingEvent` 的 `0x4197E9` 17 项）
  比反编译 switch 更直接，能精确定位每个 case 的代码地址与合并的 default。
- **Hex-Rays 分支条件可能整体翻转**：`jle/jnz` 组合的 if/else 曾被解析反
  （挖寶方向公式 0x412658：反编译 `dcx<=0→3-dr / dcx>0→(dr+7)&7`，反汇编实为
  `dcx>0→3-dr / dcx<0→(dr+7)&7`，照抄导致**企鹅倒退走路**，2026-09-25 实机发现）。
  凡"方向/对称/阈值比较"类分支，实现前用 `disasm` 核对原始跳转。
- **get_bytes 读表必须覆盖到终止符**：Effect 音效表 = {index, handle} 对 stride 8、-1 终止
  （`audioPlayEffect` 第一参是**表项指针**非句柄值）。曾因读取范围恰好截断在表首项之前，
  把接钱音效表 {22,23,24,15} 误判成"空表静音 quirk"（2026-09-25 实机反馈才发现）。
- **像素值恰为 0 的黑色内容勿用色键 blit**：panel[79] HUD 小数字黑色填充 RGB555=0，
  色键 blit 会吃掉填充只剩线框（原版 `blitElementFullscreen` = 不透明拷贝）；
  同资源的大数字（结算用）才走色键 `sub_456418`。
- **指针表解码**：`off_XXXX`/`funcs_XXXX` 是指针数组，用 Python 读 exe 解码
  （VA→文件偏移 = `VA - 0x401A00`，BIG5 用 cp950；`get_string` 遇 BIG5 常失败）。
- **IDA 串视图会截断 BIG5 格式串**：反编译显示 `"%s"` 的地址实际字节可能是
  `"%s顯靈\n\n加蓋一層房屋！"`（0x4634C0/0x463514 踩坑：BIG5 双字节含 `\x5C` 等干扰解析）。
  移植任何 sprintf 消息文本前，用 Python 读原始字节 cp950 复核完整串。
- **图块交叉验证索引**：类型/名称索引用 `data.mkf[type+395]` 图块内容验证
  （407=土地公、408=禮物、410=死神、411=路障 → 确认 `kObjectNames[类型值]` 为 0-based，
  而非"类型-1"）。

### 常见坑

- **`g_playerAlive` 时序 = 跳伞落地才置位**：原版 newGameInit 后 alive=0（g_charData +21=0），
  `beginPlayerTurn`（0x418C55）播完跳伞 FLC 才 `g_playerAlive = g_playerKind`（0x418D07）；
  **未入场玩家对所有目标/统计/排行不可见**（全文 231 处引用：投降候选 sub_4339D9、AI 用卡
  目标、物价指数 updateMoneyIndex、拍卖/保释名单…）。重写曾开局即 `alive=1`，导致未跳伞 AI
  被列为死神候选/AI 目标/统计样本（2026-09-27 实机"开局投降可选未跳伞 AI"暴露）；
  修复 = newGameInit 置 0、落地置位，并把出生点分配抽为 `spawnPlayerAt` 于
  `updateGameState` 回合开始前触发（重写 tick 顺序 update→render，否则 `beginPlayerTurn`
  早于渲染分配，未入场 alive=0 会被 `checkPlayerAction` 判无行动而跳过回合）。
- **FLC 解码越界**：部分 FLC 数据尾部含超出声明帧数的 chunk，必须按
  `m_frameCount` 封顶，否则会多播一帧（回绕成第0帧，表现为「闪回首帧」）。
- **`rewind()` 只重置偏移不清像素**；播完应冻结末帧（不回绕）。
- **原版阻塞动画靠"表面保留"叠加**（0x45144F 播完不清屏、0x40E2A2 旁白/老虎机金额都画在
  末帧上）→ 重写的逐帧 `renderGameFrame` 即时模式必须**显式冻结末帧**（`playEventFlc(freeze)`
  + 叠加期间不重绘、结束解冻），否则出现"动画消失后才出文字"类时序差。
- **音效单 buffer 语义**：原版每个音效槽是单 DirectSound buffer
  （`sub_4542E9` 停 + `sub_4542CE` 播）→ 重写用同槽替换，避免多声部叠加。
- **循环音效 ≠ 每帧重播**：`sub_4542CE(handle, 1)` 第二参 = DSBPLAY_LOOPING（老虎机滚动音
  dword_475D3C 进入时播一次、case8 `audioStopEffect` 停）；每 tick `playEffect` 会叠加多
  声部（重写已加 `playEffectLooping`/`stopEffect`）。
- **阻塞 FLC 动画期间游戏必须冻结**：原版 `sub_45144F` 的 PeekMessage 只取消息**不派发**
  窗口过程；重写 `playEventFlc` 循环 `pumpEvents` 会重入 gameEventHandler（状态机在动画
  下偷跑）且 `renderGameFrame` 把视口按**传送后**的玩家重居中（住院 524/入狱 538 一开演
  画面就跳到医院/监狱的根因）。修复=timer 分支 `eventFlcActive` 守卫直接返回 +
  `playEventFlc(preserveScene)` 不重绘场景、每帧回卷区域背景叠帧（对应原版
  saveBackground/表面保留）。传送后过渡动画（住院/入狱）必须用 preserveScene。
- **视口**：`manualView=true` 且 `viewSmooth=0` → 场景跳左上角，并触发重选出生点/跳伞；
  小地图旋转只改 `mapRotation`，**不要**切视角模式。
- **事件槽（玩家 4-7）**：**原版模型 = `busy(0x498E32)==0` 才进槽/绘制/可动**；
  `byte_498DF2[16*p]` 与 `498E32` 是**同一字节**（旧文档误标独立"占用位"）——细节
  `498df0-event-slot-npc.md` §1.1/§2.1。
- **槽 8（機器娃娃）与事件槽同构**：`0x498E68..E77` = `npcSlots[4]`（bailer=发起玩家=
  `byte_498E70`、busy=`byte_498E72`、cell/prev/pixel/dir）；`0x446AFB` 写槽8 → 走 9 步踢物件 →
  `nextPlayer` busy 0→3。**循环移动音 = 槽 9**（`&dword_482382[4]` == `&g_effectSlots[2*9]`，
  勿用 `playEffectLooping(effectId)`，用 `playEffectSlotLooping(slot)`）；收尾 `sub_40FAD6`
  等 46 槽 flyCount 清空才交还回合。**勿再建 `workerPlayer` 之类独立字段**（2026-09-27 并入）。
- **角色名/公司名空格双轨制**：原版 `g_charData+0`（0x4665C4）与 `g_stocks+0` 名字表按
  **4 字宽排版补 `0x20` 空格**（「約 翰 喬」「台 積 電」）。`drawText` 直绘（选人/右侧面板/
  物件提示/行情/分红…）**保留空格**；`sprintf` 进消息前原版经 `copyNameNoSpaces`(0x452946)
  **去空格**（44 调用点矩阵见 `452946-name-no-space.md` §2.1），但**有例外**：魔法屋惩罚、
  復仇/免費/免罪卡、AI 銀行還借款、週轉強制償還、aiStock 消息的玩家名部分等**直引不去空格**。
  勿"顺手"统一去空格或加空格；两轨与例外清单为准（重写 helper：`nameNoSpaces`/`playerNameNoSpace`）。
- **音乐**：取消对话框**不改动**音乐；確定仅在音量 0↔非0 时停/重播（非0 之间只调音量）。
- **重新开局**：`newGameInit` 需重置 `mapRotation`/骰子/回合/音乐计时器等残留状态。
- **音效 id 空间**：UI 音效（`g_uiSoundHover/Click/Confirm/Cancel` = Effect.mkf 0/1/2/4）
  与游戏音效槽（`g_effectSlots[slot]`）是两套，勿混用。
- **多套顺序勿混用**：同一批数据可能同时存在 type 顺序 / 图块顺序 / 帮助页顺序三套
  （特殊地点 type 7/8 = 七彩氣球/喜從天降，但图块 sprite 15/13 互换、帮助页顺序又不同）；
  重写时按用途查对应表，不要假设一致（见 `gameplay-map-mechanics.md` §3）。
- **帮助页标题与内容需交叉验证**：页面标题表在 exe（`[RE 0x4761B8]`），
  与 `help.mkf` 页内容可能错位/重复（idx 58）；发现异常时在文档标注，不照抄。
- **重写命名要对照逆向**：如 `GameState.cardShuffle` 实为**新闻事件顺序表**
  `g_newsOrder`（0x499090，`newsEvent` 用）——错误命名会误导后续开发；
  发现后先记文档，实现时随功能改名。
- **PowerShell 环境**：Python 输出中文前设 `$env:PYTHONIOENCODING="utf-8"`
  （否则 GBK 控制台报 `UnicodeEncodeError`）；本机无 `rg`，搜索用 grep/glob 工具。

### 工作流

- 每项修正单独 `wip:` 提交；用户实机验证后再推进；**不要**把 `wip:` 当里程碑。
- 实机问题优先加**诊断日志**（限帧/状态变化）定位，再改代码，避免盲改。
- 编译前确保游戏已退出（否则 `LNK1104` 文件占用）；`build.cmd` 不提交。
- 逆向成果需**双落**：源码注释 + `docs/reverse/functions/` 档案 + IDB 重命名/注释
  （IDB 修改后保存）；改动后跑 `python tools/re_map.py` 校验。
- **大功能先出专项研究文档再实现**：如 P2 事件格 → `41982d-p2-events.md`
  （处理函数/数据结构/实现顺序/待深入清单），避免重复逆向与遗漏依赖。
- **状态同步**：完成功能后更新 `help-checklist.md`（对应帮助条目状态）与
  `gameplay-map-mechanics.md`（阶段勾选）；IDB 重命名后同步文档中的符号名。
- **控件沉淀**：实现中用到/发现任何可复用 UI 控件（浮雕按钮、变暗表、数字框、列表框、
  滑块、光标、模态叠加…），**必须**总结进 `docs/reverse/ui-controls.md`（按文末模板），
  记录原版地址/资源/交互/重写位置/踩坑经验，供后续功能复用、避免重复逆向。
- IDB 批量操作：`rename`（函数/全局）、`set_comments`、`get_bytes`（表/jpt）、
  `analyze_batch`（多函数概览）→ 完成后 `idb_save`。

## 注意事项

- `resources/MultiverseJourney/` 下的游戏资源文件**不要修改或提交**（版权 + 体积；
  IDB 文件与 `rich4.exe` 同目录，由全局 `*.i64`/`*.id0` 等规则同样不入库）
- `resources/MultiverseJourney/rich4.exe.i64` 是权威逆向数据库（重命名/注释成果），**修改后需保存**
- `src/resource/lzhuf_tables.cpp` 由 `tools/gen_lzhuf_tables.py` 从 exe 生成，**勿手改**
- LZHUF 解压器每次解压需重置工作区（自适应树有状态）
