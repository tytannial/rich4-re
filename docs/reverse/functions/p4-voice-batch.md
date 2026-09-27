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
  价值/道具/卡牌=0。
- 表步长 27 列/角色（`off_4812xx` 家族）；文本含 BIG5，提取用 `get_bytes` cp950（勿用 `get_string`）。

## 2. 剩余调用点清单

### D1 收租 / 付费（优先）

| 重写位置 | 原版 | 缺什么 |
|---|---|---|
| `turn_system.cpp` 收租段（约 2121-2124 注释） | 0x419F59/0x419F67（付款方 `sub_44F4ED`/`sub_44F42D`）、0x419FA1（收款方 `sub_44F354`） | 住宅收租三方台词（地主/同盟分账时收款方各一次） |
| `turn_system.cpp` corp 收费段（约 2381） | 0x41A710/0x41A71E/0x41A735 | 商業用地收费付款/收款台词 |
| `turn_system.cpp:2067`（买地询问） | 0x4198xx 询问后语音（`sub_44EF41` 索引 0） | 买地/升级询问台词 |
| `turn_system.cpp:2105`（升级封顶） | 0x4199F1 `off_480886` | 住宅满级台词 |
| `turn_system.cpp:2099`（`estateChainSpeech` 0x44F627） | 0x419A2B | 同路段≥3 吐槽（1/3 概率） |
| `turn_system.cpp:2400` 住宿段（可选） | 0x41A7E0 `sub_44F2C2` | 住宿消费台词 |

### D2 移动落地 / 物件 / 神明

| 重写位置 | 原版 | 缺什么 |
|---|---|---|
| ~~`turn_system.cpp` case16/17/18~~ ✅ | 0x41BD56 列14 expr1 / 0x41BEFF 列15 expr1 / 0x41C063 列16 expr2 | **P4-A ✅ 2026-09-27**：`kItemTouchLines` + `playLine` expr（炸彈曾误用放置表列3 已修） |
| `turn_system.cpp:317`（onPhase 注释）恶犬 | case11 无 `sub_44EF41` | 原版咬伤**无台词**（静默）——文档旧描述有误 |
| `map_objects.cpp:515`（attachEnd） | 穷神/衰神/死神哭台词 | 神明飘走哭台词 |
| `map_objects.cpp:616` | v>700 大笑 `off_48086A` | 老虎机结果大笑 |
| `map_objects.cpp:627` | `sub_44F354` 三档 | 大財神超额收款台词 |
| `map_objects.cpp:641`/`:654` | `sub_44F230` | 老虎机卡片/双卡语音 |
| `turn_system.cpp:1563`/`:1566`（godBlessUpgrade） | 0x40FA13 `off_480886` / 0x40FA49 `off_48084A` | 福神封顶/祝福台词 |
| `turn_system.cpp:1628`（土地公）、`:1775`（恶魔） | `sub_44EF41` 大笑 `off_48084A` | 神明事件高兴台词 |

### D3 事件 / 卡片 / 道具

| 重写位置 | 原版 | 缺什么 |
|---|---|---|
| `turn_system.cpp:1927` | 0x41B211 `sub_44EF41(player,0,…)` | 得點券 case 10/11 语音（case 12 原版无） |
| `turn_system.cpp:1946` | 0x41B302 段 | 卡片格获得语音 `sub_44F230` |
| `card_bag_dialog.cpp:269` | 0x4420D5 | 选卡对话框台词 |
| `map_objects.cpp:1060` | 0x444E7x `off_481292` | 请神符台词（`playCardLine` 槽 22 已播，另表未接） |
| `fate_event.cpp:438`/`:459`/`:585`/`:662` | `sub_44F42D`/`sub_44F354`/收到卡/面板 | 命运事件台词 |
| `news_dialog.cpp:198`/`:300`/`:378`/`:428` | `off_480856` | 新闻业主倒霉台词 |
| `news_dialog.cpp:473` | `sub_44F354` | 新闻入账收款台词 |
| `441baa-inventory-panels.md:136` | 0x4420D5 | 道具栏台词 |

### 其他

- 小游戏 AI/动画分支台词：`minigame_dialog.cpp:1368`/`:1394`（`off_48084A` 笑，AI/恶人分支，当前有意保留）。
- 状态文本语音：`turn_system.cpp:2790`（`sub_44808A` 等，M2 接入备注）。

## 3. 实现批次（已定序 A→B→C→D）

1. **P4-A ✅ 2026-09-27**：路障/地雷/炸彈踩中台词（`kItemTouchLines` 列14/15/16）+ `playLine`
   expr 表情帧参数（原版 a2 → 帧 a2+1，`card_lines`/`item_lines` 全透传）。
2. **P4-B（D1）**：收租/付费三连台词（`sub_44F354` expr3 / `sub_44F42D` expr2 /
   `sub_44F4ED` expr1，9000/5000/2000×M 档选句）——5 表提取 + 收租/corp 收费/封顶/连锁/住宿调用点。
3. **P4-C（D2）**：状态提示 `checkPlayerActionStatus`（列19/20/21 台词 50%）、神明哭/笑
   （`attachObject`/`attachEnd`）、老虎机（`off_48086A`/`sub_44F354`）、`godBlessUpgrade`、
   土地公/恶魔；恶犬咬伤确认无台词。
4. **P4-D（D3）**：事件/卡片/道具（`off_480856` 新闻、`off_481292` 请神、得點券/卡片格/命运、
   `sub_44F230` 补点、选卡对话框）。

每批：Python 提取表（cp950 → UTF-8）→ 落在 `item_lines.cpp`/`card_lines.cpp` 同款表文件 →
调用点接入（补 expr 参数）→ 实机核对（气泡头像表情 + 语音编号）。
