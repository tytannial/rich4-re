# P4 语音专项清单（sub_44EF41 家族，进行中）

> 本文档汇总角色台词语音调用点与所需台词表，供后续专项逐步实现。
> 已实现的语音基础设施：`playLine`/`playItemLine`/`playValueLine`（`item_effects.cpp` 0x44EF41
> /0x44F230，`playLine` 支持 expr 表情帧）、`playCardLine`（`card_lines.cpp` 0x48123A 卡牌台词表）、
> `FloatMessage`（气泡）、`Audio::playVoice`（Speaking.mkf 单通道）。
> **30 卡台词/13 道具台词/百货价值台词/路障·地雷·炸彈踩中台词（P4-A）已接入。**
> 行号截至 2026-09-27（代码变动后以符号搜索为准）。

## 1. `sub_44EF41(p, expr, line)` 参数语义（2026-09-27 更正）

- `p`：说话玩家；`p & 0x8000`（bit15）→ **不先 refreshGameUi**（a1&=0x7FFF 后仍用同一玩家）。
- `expr`（旧文档误称 type）：**头像表情帧索引**——原版画 `pieceSprites[p]` 帧 `expr+1`
  （0=普通帧1 / 1=哭帧2 / 2=被炸帧3 / 3=笑帧4）。**不是表选择器**，表完全由调用点决定。
- `line`：台词文本；`#NNNN` → Speaking.mkf 语音（drawTextSpeak）、`#NNNN@MM` → 同时播语音并
  画 data.mkf[519] 帧 MM-1。
- 观察到的 expr 习惯（供 D1..D3 填参）：收款类 `sub_44F354`=3、付款类 `sub_44F42D`=2、
  欠债者 `sub_44F4ED`=1、路障/地雷=1、炸彈=2、神明笑=3/哭=2、状态提示（监/医/冬眠）=2/1、
  状态天数 `sub_44F2C2`=2（住宿/入狱/住院）、连锁吐槽 `sub_44F627`=0、封顶列15=0、
  价值/道具/卡牌=0。
- 表步长 27 列/角色（`off_4812xx` 家族）；文本含 BIG5，提取用 `get_bytes` cp950（勿用 `get_string`）。

## 2. 剩余调用点清单

### D1 收租 / 付费 —— **P4-B ✅ 2026-09-27**

| 重写位置 | 原版 | 状态 |
|---|---|---|
| 住宅收租（付款/收款） | 0x419F59/0x419F67（`sub_44F4ED` **列18**/`sub_44F42D`）、0x419FA1/0x419FF0（`sub_44F354`） | ✅ `playDebtorLine`/`playPayerLine`/`playCollectorLine`；同盟分账**地主份额**收款档、同盟无收款台词（对齐原版） |
| corp 收费 | 0x41A710/0x41A71E/0x41A735 | ✅ 嫁祸反弹（payer==owner）不播（原版 0x41A709） |
| 升级封顶 | 0x4199F1 `off_480886`（列15、expr0） | ✅ + FLC523 |
| 同路段≥3 吐槽 | 0x41A13E 买地必播**列16**（`off_48088A`）/0x419A2B 升级 1/3 **列17**（`off_48088E`） | ✅ `playEstateChainSpeech`（列16/17） |
| 住宿/入狱/住院 | 0x41A7E0/0x43D5F9/0x43ECA5 `sub_44F2C2`（列3/4/5 天数档、expr2）；入狱/住院 FLC 后另有**列19/20** expr2（0x43D71C/0x43EDCB，无条件含累加路径） | ✅ `playStatusDaysLine` + `jailPlayer`/`hospitalizePlayer` 收尾；住宿仅付款人==本人时播 |
| 买地/升级询问语音 | 0x4198xx（`sub_44EF41` 索引 0） | ✅ 审计确认无独立调用（landingEvent 4 处调用 = 封顶 0x419A19/0x41AB5B、航空公司免出国 0x41AC1F、得點券 0x41B211） |
| 出国/绑架 `startTravelState` | 0x40D3F8 `sub_44F2C2` | ✅ 天数档 expr2 |
| 大財神超额收款 | 0x40ED85 `sub_44F354`（attachObject） | ✅ `v>=5000×M` 才播（`playCollectorLine`） |
| 神明租金免付 | 0x41D7C1 `sub_44F567`（列12/13/14 意外之财） | ✅ `playWindfallLine`；大財神 `v==0` 时在 showMessage 后播（原金额档） |
| 地主状态免收租 | 0x41D6DD `off_48087E`（列13、expr3，说话者=当前玩家/付款人） | ✅ `ownerCanCollectRent` 末尾（查封/同盟/死神/状态/冬眠/梦游均播） |
| 新闻/命运收款付款 | 0x449A80 等 / `sub_44CD99`/`sub_44CF1E` / fate 438/459 | ⬜ 归 P4-D |

### D2 移动落地 / 物件 / 神明 —— **P4-C ✅ 2026-09-27**

| 重写位置 | 原版 | 状态 |
|---|---|---|
| case16/17/18 踩中三类 | 0x41BD56 列14 expr1 / 0x41BEFF 列15 expr1 / 0x41C063 列16 expr2 | ✅ P4-A（`kItemTouchLines`） |
| `attachObject` 全部 case | 小財神 v>700 大笑列8 expr3（0x40ECDE 共享块）/ 大財神 v≥5000×M `sub_44F354` / 福神抽卡·双卡 `sub_44F230` / 穷神·衰神·死神哭列22 expr2（0x40EF44/F00D/F0AC/F17E/F314，原版带 bit15=不刷新视口，重写无位差） | ✅ |
| `attachEnd` 飘走 | 0x40E659：穷/衰/死神 → 列23 expr2 | ✅ |
| `godBlessUpgrade` | 0x40FA13 封顶列15 expr0；未封顶 0x40FA49→0x40ECDE 列8 expr3 | ✅（原版未封顶非 off_48084A 随机——勘误） |
| `checkPlayerActionStatus` | 0x40CA51/0x40CACA/0x40CB4C：坐牢列19 expr2 / 住院列20 expr2 / 冬眠列21 expr1（各 50%，先台词后消息；冬眠会被 sub_44EF41 守卫拦=原版行为） | ✅ |
| 土地公强占 | 0x40F8AB：off_48084A 列0（= kValueLines 第一列）expr0 | ✅ |
| 惡犬咬伤 | case11 无 `sub_44EF41` | ✅ 原版即无台词（勘误） |
| 恶魔 | `landAfterMove` 内无 `sub_44EF41`（唯一 0x40F8AB 属土地公） | ✅ 原版即无台词 |
| 移动落地哭/笑（onPhase 等） | 无独立调用点（除上述已接） | ✅ 无缺口 |

### D3 事件 / 卡片 / 道具 —— **P4-D ✅ 2026-09-27**

| 重写位置 | 原版 | 状态 |
|---|---|---|
| 得點券 case10/11（0x41B184/0x41B21E） | case10 `off_48084A 列0/1 随机`、case11 列2（= kValueLines）expr0 | ✅；case12(+10) 原版无 |
| 卡片格 case13 | 0x41B38C `playValueLine(卡价)` | ✅ |
| 选卡/道具栏对话框 | **`sub_4420D5` = 空函数** | ✅ 勘误（原版无台词；旧 TODO 误标） |
| 请神符 | 0x444E7A = `off_481292 + 360*char` = **卡片表槽 22** | ✅ `playCardLine(22)`（"另表未接"系误判） |
| 命运 逃过罚款 | 0x44CE7E `sub_44F567`（原额档） | ✅ `playWindfallLine` |
| 命运 加倍付款 | 0x44CEF9 `sub_44F42D` | ✅ `playPayerLine` |
| 命运 收款/逃过收款 | 0x44D334 `sub_44F354`（金额档）、0x44D028 `sub_44F567` | ✅ `playCollectorLine`/`playWindfallLine` |
| 命运 id9 變賣股票 | 0x44C91F `off_48085E`（**列5**、expr2，非金额档） | ✅ `fateApplySellAllStock` |
| 命运 拆屋/载具/没收 | 0x44BF9F 列3/4 随机、0x44CA46 id10 列3/4 随机 / 0x44CB53 id11 列3 固定、0x44D777 列3（expr2） | ✅ `playUnluckyLine`（`random` 参数区分 id10/11） |
| 命运 逃过坐牢（33..48） | 0x44D873 `off_48084A` 列0 expr0（4 张地图共享 phase1 块） | ✅ `fateApplyJail` 幸运分支 |
| 命运 偷卡/面板 | 无 `sub_44EF41` xref | ✅ 勘误（原版无台词） |
| 新闻业主 ×4（怪獸/瓦斯/洪水/龍捲風） | 0x4494CD/44A5C3/44AB0F/44AE7F 列3/4 expr2 | ✅ 有主才播 |
| 新闻奖励入账 idx8/9/10 | 0x449A80 `sub_44F354` | ✅ |
| 淘汰认输 | 0x40D237 列25 expr2（sub_41906A(1) 后、FLC555 前） | ✅ `eliminatePlayer` |
| 终局胜利大笑（仅剩1人） | 0x40D060 列24 expr3 | ✅ `eliminatePlayer` |
| checkVictory 0x41D947 / defeatFlow 0x407956 | 列24 expr3 / 列26 expr3+bit15 | ⬜ 留 M3（场景未实现；表已备） |

### 其他

- 小游戏 AI/动画分支台词：`minigame_dialog.cpp:1368`/`:1394`（`off_48084A` 笑，AI/恶人分支，当前有意保留）。
- 状态文本语音：✅ P4-C（`checkPlayerAction` 消息 + 50% 台词列19/20/21）。
- **P4 专项至此全部完成**；`sub_44EF41` 112 处 xref 中除 M3 场景（checkVictory/defeatFlow）
  与节日音乐等外均已核对归属。

## 3. 实现批次（已定序 A→B→C→D）

1. **P4-A ✅ 2026-09-27**：路障/地雷/炸彈踩中台词（`kItemTouchLines` 列14/15/16）+ `playLine`
   expr 表情帧参数（原版 a2 → 帧 a2+1，`card_lines`/`item_lines` 全透传）。
2. **P4-B（D1）✅ 2026-09-27**：金额/状态台词表 `kMoneyLines[12][13]`（off_48084A 列
   6/7/8 收款、9/10/11 付款、15 封顶、16/17 连锁、18 欠债、12/13/14 意外之财）+ 六个函数
   `playCollectorLine`/`playPayerLine`/`playDebtorLine`/`playStatusDaysLine`/
   `playWindfallLine`/`playEstateChainSpeech`；接入住宅收租（含同盟分账）、corp 收费、
   升级封顶、连锁吐槽、住宿/入狱/住院/出国、大財神、神明租金免付。
   剩余调用点见 D1 表 ⬜（新闻/命运 → P4-D）。
3. **P4-C（D2）✅ 2026-09-27**：`kMoneyLines` 扩至 18 列（+19/20/21 状态、22 神明哭、
   23 飘走）；`attachObject` 全 case 台词（大笑/收款/抽卡/哭）、`attachEnd` 飘走、
   `godBlessUpgrade`（封顶列15/未封顶列8）、`checkPlayerActionStatus` 状态台词
   （坐牢/住院/冬眠 50%）、土地公强占大笑；恶犬/恶魔原版本就无台词。
4. **P4-D（D3）✅ 2026-09-27**：`kMoneyLines` 扩至 24 列（+18/19/20 倒霉列3/4/5、
   21/22/23 胜利/淘汰/失败列24/25/26）+ `playUnluckyLine`；得點券（kValueLines）、卡片格、
   命运（逃过/加倍/收款/拆屋/载具/没收/卖股）、新闻业主 ×4、奖励入账 ×3、淘汰/终局胜利；
   勘误：选卡·道具栏（sub_4420D5 空函数）、请神符（= 卡片表槽22 已播）、命运偷卡无台词；
   **修正 P4-C bug**：`playStatusDaysLine` 原误用付款列9/10/11 → 倒霉列3/4/5。
   checkVictory/defeatFlow 台词留 M3。**P4 专项至此全部完成。**
5. **2026-09-30 语音列勘误（实机反馈：「坏神倒霉语用了幸运语 / 加盖高兴语用了被收租语」）**：
   `kMoneyLines` 位置 7/8/9 ↔ 原版列 16/17/18 对应关系修正——`playDebtorLine` 列16→**列18**
   （欠债者不再播「我是個大地主」）、`playEstateChainSpeech` 买地 列17→**列16**、升级/加盖
   列18→**列17**（加盖不再播「兄弟，我記住你了」）；同批补正：`ownerCanCollectRent` 免收列13、
   `fateApplySellAllStock` 列5、载具 id10 列3/4 随机、逃过坐牢列0、
   `jailPlayer`/`hospitalizePlayer` 列19/20（含累加天数路径）。

每批：Python 提取表（cp950 → UTF-8）→ 落在 `item_lines.cpp`/`card_lines.cpp` 同款表文件 →
调用点接入（补 expr 参数）→ 实机核对（气泡头像表情 + 语音编号）。
