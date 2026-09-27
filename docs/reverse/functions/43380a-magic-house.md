# 魔法屋（case 16 → `magicHouseVisit` 0x43380A）

> 2026-09-26 专项逆向。`landingEvent` 0x41982D case 16（跳转表 0x4197E9 第 16 项 → 0x41B3CB
> `sub_43380A`）。帮助 [HELP 38]：「进入后女巫的水晶球会依出现的条件施法，再由玩家决定那些人的
> 惩罚」。IDB 已重命名：`magicHouseVisit/magicHouseWndProc/applyMagicPenalty/magicSelectTargets/
> magicDrawBase` + 全局表。
> **拍卖系统（惩罚 11「拍賣當格土地」+ 新闻 idx 7 共用）已拆分至 `43bde5-auction.md`。**

## 1. 总览与调用链

```
landingEvent case 16 (0x41B3CB) → magicHouseVisit 0x43380A
├─ 人类（g_playerAlive[cur]==1）：
│    audioRegisterEffects(g_magicHoverSound 0x4757E7={39})
│    载 panel.mkf[18]=背景SMP(35帧) / [19]=pick RAW(640×480 1B/px) / [20]=FLC
│    allocUiElement(144,128)=g_magicHoverBuf；musicPlayScene(7)
│    runModal(magicHouseWndProc) → 返回 惩罚id 0..11（见 §3）
│    musicStackPopRestore + 释放
├─ AI/非人类（else 0x43390B）：
│    do cond=rand()%12 while(!magicSelectTargets(cond))          // 条件须有目标
│    for i<4: if targets[i]-1==cur → pen=6                        // 自己中招→强制"得一張卡片"
│    if pen==-1: pen=rand()%11; if(pen==6) pen=7                  // 排除 6（自肥）；11 拍卖 AI 不用
│    showMessage("条件名\n\n惩罚名", 1500)   // 0x464842；条件名 '#NNN' 前缀跳 5 字节
│    （pen 直接进 applyMagicPenalty）
└─ applyMagicPenalty(pen) 0x431CAA：对 g_magicTargets（BYTE[4]，玩家号+1，0 终止）逐一执行
```

- 资源：`panel[18]` 35 帧（头 432B=12×36）、`panel[19]` RAW 307200B、`panel[20]` FLC（女巫施法动画）。
  音效：hover=Effect[39]、确认 FLC 音=59。音乐：场景 7（魔法屋）。
  惩罚 11 另需 `panel[26]` 拍卖资源（见 `43bde5-auction.md`）。

## 2. `magicSelectTargets` 0x431842 —— 12 条件 → 目标名单

写入 `g_magicTargets`（0x48C380，BYTE[4]，值=玩家号+1，**最多 4 人**，0 终止），返回是否有目标：

| id | 名称（g_magicCondNames 0x4756B8） | 选择逻辑 |
|----|------|----------|
| 0 | 財產最多的人 | `playerTotalAssets` 最大**并列全取**（截 4） |
| 1 | 土地最多的人 | estate+corp 拥有数（+25 owner==p+1）最大并列 |
| 2 | 房屋最多的人 | 拥有**且有建筑**（+26 level/设施≠0）数最大并列 |
| 3 | 現金最多的人 | cash 最大并列（cash≠0） |
| 4 | 存款最多的人 | bank 最大并列（bank≠0） |
| 5 | 點券最多的人 | points 最大并列（≠0） |
| 6 | 走路的人 | `travel&3==0` 全部（Player+17，IDA 误名 g_playerVehicle） |
| 7 | 騎機車的人 | `travel&3==1` 全部 |
| 8 | 開汽車的人 | `travel&3==2` 全部 |
| 9 | 神明附身的人 | `cellTableIdx(+63)!=0` 全部 |
| 10 | 所有男生 | `byte20(+20)!=0` 全部（性别） |
| 11 | 所有女生 | `byte20==0` 全部 |

> 0..5 是"最大值并列"语义（v42 递增重置列表）；6..11 是"全部符合"。

## 3. `magicHouseWndProc` 0x4325C2 —— 施法 UI 状态机

100ms timer（`SetTimer 0x64`）。`byte_48C3A2` 状态、`byte_48C3A3`=条件 id、`byte_48C3A1`=倒计时/悬停项、
`byte_48C3A5`=跳过动画（0x401 初始化 `= !(settings[1])`，右键/双击置 1）。

| 状态 | 动作 |
|---|---|
| 0x401 | 全清零；`magicDrawBase` 0x432511：帧0 全屏背景 + 帧1(165×213 女巫全身,睁眼) @(241,140)；`floatMsgSetup(帧8=280×173, 320,384)`；Post 0x405 |
| 0x405 | 状态1 + 浮字 #0037「進來魔法屋，就得完全照我的指示！」 |
| 1→2 | #0038「我選出符合條件的人。」 |
| 2→3 | #0039「你來決定他們的命運～」显示**结束**（面板消失）→ 画女巫**闭眼贴片**帧3(60×35) @(286,188) = 开始冥想；倒计时 `byte_48C3A1 = 动画开?1:10`（1s） |
| 3→4 | 倒计时到 0 → `do cond=rand()%12 while(!magicSelectTargets(cond))`；**重绘女巫全身帧1 @(241,140)（重新睁眼）**+ 条件图标帧 `cond+11` @(326,296)（图标压在女巫下半身上，顺序不能反）；状态5；浮字=条件名（#NNN 跳5） |
| 5→6 | 若未跳动画：浮字 #0040「嘿～輪到你了！」 |
| 6→7 | `setPauseDraw(1)` + 自投 WM_MOUSEMOVE → 交互 |
| 7 | **悬停**（WM_MOUSEMOVE）：`v17 = g_magicPickRaw[x + 640*y]`（0 无 / 1..12 惩罚 / 13=眼部贴片区）。变化时：旧项擦除（帧0 patch 64×64 @(off_4756E4[id])）+ 新项画帧 `v17+22`(64×64 小图标)；若 1..12：`saveBackground`(144×128 @ 大图区 dword_47570C/10[id]) → 画大图帧 `g_magicPenaltyIcons[id].frame` + `drawText(名称)` + 音效 39。**点击**（WM_LBUTTONDOWN 或双击，wParam=悬停 id-1）：非 0/13 → 恢复大图区背景 + 帧2(284×210 确认罩) @(182,142) + 条件图标重画 → `setPauseDraw(0)` 状态8 + #0041「天靈靈地靈靈～」；13 → 回状态6 重显条件名 |
| 8 | KillTimer + `playEventFlc(g_magicFlc @(0,0), sound 59)` → `postModalExit(惩罚id)` |

- **说话口型**（timer 每 tick，`rand()>>11 < 4` 即 25%）：**FloatMessage 显示期间**（=#0037..#0041
  各段台词+条件名）随机画帧5(60×21 张嘴) @(286,217) 或帧1 嘴区 patch(45,77,60,21) 擦回，
  `byte_48C3A0`=1..7 拍驻留，末拍画帧4(60×18 闭嘴) @(286,220) 归位——**说完话嘴停**。
  帧3/4/5 实为女巫眼/嘴贴片（图像核实 2026-09-26），**不是** EXIT 按钮/水晶球装饰；
  pick 值 13 的命中区恰在眼部贴片一带，点击=重显条件浮字。
- **返回值 = 惩罚 id 0..11**（悬停项-1），与 AI 分支 pen 同域 → `applyMagicPenalty`。

### 惩罚图标表 `g_magicPenaltyIcons` 0x475708（stride16 × id 1..12：{大图帧, x, y, 名称文本}）

| id | 帧 | x | y | 名称（0x464794..0x46481d） | 惩罚 |
|---|---|-----|-----|------|---|
| 1 | 9 | 208 | 167 | 變賣所有卡片 | case 0 |
| 2 | 10 | 510 | 150 | 抽取命運三張 | case 1 |
| 3 | 7 | 545 | 88 | 立刻坐牢三天 | case 2 |
| 4 | 7 | 568 | 147 | 原地停留一回合 | case 3 |
| 5 | 7 | 550 | 234 | 存入所有現金 | case 4 |
| 6 | 7 | 510 | 320 | 就地加蓋房屋 | case 5 |
| 7 | 7 | 422 | 320 | 得一張卡片 | case 6 |
| 8 | 6 | 134 | 318 | 向後轉 | case 7 |
| 9 | 6 | 92 | 250 | 變賣所有道具 | case 8 |
| 10 | 6 | 72 | 130 | 就地拆除房屋 | case 9 |
| 11 | 6 | 77 | 85 | 住院檢查三天 | case 10 |
| 12 | 9 | 122 | 154 | 拍賣當格土地 | case 11 |

小图标位置 `off_4756E4`（13×int16 pair，id1..12 = (322,91)(414,82)(453,157)(509,242)(458,314)
(415,387)(322,394)(239,393)(188,316)(131,222)(184,161)(225,83)，环绕一圈；id0/13 未用）。
条件图标 = 帧 `cond+11`（11..22），惩罚小图标 = 帧 `pen+22`（23..34），大图 = 帧 6..10。

**坑**：`applyMagicPenalty`/`magicHouseVisit` 中 IDA 显示 `*(&off_475724 + 4*a1)` 是**误解析**——
实际 stride=16（读 `g_magicPenaltyIcons` 文本字段，0x475724 = 表基+28）。证据：4 步长得 0xa/0x1fe
等垃圾，16 步长得正确惩罚名。

## 4. `applyMagicPenalty` 0x431CAA —— 12 惩罚

公共：`for target in g_magicTargets[0..3]`：`cur = target-1` → `sub_41906A(1)`（WM_PAINT 强制场景
重绘）→ `sprintf("%s\n\n" + 惩罚名)` → `showMessage(1500)` → 执行 → 恢复 cur。
守卫类（5/7/9/11）：`stateFlags==0`（dword_496B9A +50）且当前格 objId∈(2000,6000)。

| a1 | 惩罚 | 实现 | 复用 |
|---|---|---|---|
| 0 | 變賣所有卡片 | `points += confiscateCards(p)` 0x441F21 + `sub_41D433` 刷新 + 延时200 | ✅ P3 |
| 1 | 抽取命運三張 | `fateEvent()` ×3（0x44DB81 递归调用，事件表推进） | ✅ |
| 2 | 立刻坐牢三天 | `addPlayerDebt(target, 入场者, 90×M)` 0x40DF69 → `resolvePenaltyTarget` 0x441210（免罪/嫁祸）→ `jailPlayer(victim, 3)` 0x43D593 | ✅ |
| 3 | 原地停留一回合 | `skipMove(+56) = (v+1)&0x7F` 0x496BA0 | ✅ 字段现成 |
| 4 | 存入所有現金 | `bank += cash; cash = 0` + 刷新 | ✅ |
| 5 | 就地加蓋房屋 | 守卫 → `getObjectPosition`+`refreshGameUi(坐标,0)` 视口对准 → 载 data[553] → `angelUpgrade(objId)` 0x40B110 → FLC 553 @(0,40) flags 0x2C0001 音效 91 → bit7 封顶 → `playGodBuildFlc`(523) → `playItemLine`(0x46482F) | ✅ P3.5 |
| 6 | 得一張卡片 | `sub_441E12`：g_propStock(30 类赠卡池) 展开随机 → `sub_4412E4` 加入卡包 → 消息「得%s！」(0x464839) | ✅ `drawFreeCard` |
| 7 | 向後轉 | `turnToAdjacentCell(p)` 0x40C78C：音效 dword_4823F2；`dir=(+16+4)&7` 反向；从 cellEnt+24*4 四邻接随机（排除 +36 方向掩码 & 来路 word_496B76）写 prevCell；`refreshGameUi(0,0,1)`+延时500 | 🆕 helper |
| 8 | 變賣所有道具 | `points += confiscateItems(p)` 0x445B3F + 刷新 | ✅ P3 |
| 9 | 就地拆除房屋 | 守卫 → 视口对准 → `demolishEstate(objId, 0)` 0x40AB4A → FLC 529 @(0,40) flags 0x260001 音效 97 → `playItemLine` | ✅ |
| 10 | 住院檢查三天 | 同 2 → `hospitalizePlayer(victim, 3)` 0x43EC3F | ✅ |
| 11 | 拍賣當格土地 | 守卫 → `runAuction(被罚玩家, objId, 1)` 0x43BDE5 | ✅ 见 `43bde5-auction.md` |

> 2/10 的坐牢/住院**不检查** stateFlags（直接进）；addPlayerDebt 先于 resolvePenaltyTarget
> （嫁祸卡会把债务+惩罚转给目标选的人）。case 5 的 angelUpgrade 返回 bit7=封顶。
> case 7 原版音效句柄 dword_4823F2 从未注册（静默 quirk）；NPC（事件槽 4..7）为惩罚目标时
> 已过滤（恶人不受魔法屋影响；重写 `p>=4` 不参与，见 `498df0-event-slot-npc.md` §5）。

## 5. 魔法屋 UI 文本（off_475694..A4）

| 地址 | 内容 |
|---|---|
| 0x4645E4 | #0037「進來魔法屋，就得完全照我的指示！」 |
| 0x46460B | #0038「我選出符合條件的人。」 |
| 0x464625 | #0039「你來決定他們的命運～」 |
| 0x46463F | #0040「嘿～輪到你了！」 |
| 0x464653 | #0041「天靈靈地靈靈～」 |
| 0x4756A8/AC | #0042/#0043「你想召喚死神，為你復仇嗎？/你要死神附身在誰身上？」→ **投降流程 `sub_433088`** ✅ 2026-09-27：`surrenderPlayer` 0x411AE0 + `deathGodSummonDialog`（magic_house_dialog.cpp，复用本场景 panel[18]/[20] + data.mkf[2] 头像）；按钮表 0x4757D8（2 人 {238,330}/3 人 {211,284,357}）、候选表 byte_4757E4、退出返回玩家+1 |

## 6. 重写映射（✅ 2026-09-26 实现，待实机验收）

| 原版 | 重写 |
|---|---|
| `magicHouseVisit` 0x43380A | `src/app/magic_house_dialog.cpp` `magicHouseVisit` |
| `magicHouseWndProc` 0x4325C2 | `magicHandler`（runModal 模态；FLC tick 化播放） |
| `magicSelectTargets` 0x431842 | `magicSelectTargets` |
| `applyMagicPenalty` 0x431CAA | `applyMagicPenalty` |
| `turnToAdjacentCell` 0x40C78C | `turnToAdjacentCell`（本文件，仅玩家分支） |
| `sub_441E12` 抽赠卡池 | `drawFreeCard`（turn_system.cpp，已有） |
| `sub_41906A` 强制重绘 | `renderGameFrame+renderFrame` |
| 拍卖（惩罚 11） | `src/app/auction_dialog.cpp`，见 `43bde5-auction.md` |

## 7. 踩坑记录

1. **off_475724 误解析**：IDA 反编译 `*(&off_475724 + 4*a1)` 实际 stride=16（惩罚图标表文本字段），
   4 步长得垃圾指针。凡指针表值异常先按 stride 16 重验。
2. **mkf list 列 ≠ 帧数**：panel[18] "432" = 头长度（12×36 → 35 帧）；smp.py 导出编号与帧头编号
   一致（已用尺寸核对）。
3. **确认点击在 WM_LBUTTONDOWN**（非 UP，与多数面板不同）；右键/双击仅 state<3 时跳动画。
4. **大图区与 FloatMessage 框（320,384）重叠**：hover 保存的背景会带入消息框残影——原版同构，勿"修复"。
5. **FloatMessage 门控必须每 tick 调 dvance()**：ctive() 只查标志不自愈，用 !active() 当
   状态机门控会导致消息永不超时 → 卡死在选择前（实机 2026-09-26 根因）；原版 loatMsgAdvance
   兼做超时恢复 + 返回可推进。
6. **帧 3/4/5 = 女巫眼/嘴贴片，非装饰**（图像核实）：帧3 闭眼(冥想)、帧4 闭嘴、帧5 张嘴；
   原版倒计时结束**重绘帧1 全身让女巫重新睁眼**——漏这一笔会导致"说完话眼睛一直闭着"
   （实机 2026-09-26 反馈根因）。"水晶球闪烁/EXIT 按钮"均为初判误读。
7. **#0037 原版文本自带换行**（"進來魔法屋，就得\n完全照我的指示！"，0x4645E4 字节核实）——
   移植消息文本必须 dump 原始字节含 \n，勿按显示排版自行拼接。

## 8. 验收清单

- [ ] 人类：5 段浮字 → 随机条件图标+条件名 → 12 惩罚图标 hover（大图+名+音效39）→ 点击 →
      确认罩+#0041+panel[20] FLC（音59）→ 惩罚逐一执行（目标名单=条件符合者，≤4）
- [ ] 右键/双击跳动画；EXIT 重选（回浮字状态）
- [ ] AI：条件+惩罚随机（排除6自肥/11拍卖；自己中招→6）+「条件名\n\n惩罚名」消息
- [ ] 惩罚 0..10 全部效果与复用链（坐牢/住院走 resolvePenaltyTarget 免罪/嫁祸；
      加盖/拆除/拍卖需 stateFlags==0 且地块 2000..6000）
- [ ] 惩罚 11 拍卖（见 `43bde5-auction.md` §7）
- [ ] `Ctrl+Shift+0` 直调；`re_map.py` 通过
