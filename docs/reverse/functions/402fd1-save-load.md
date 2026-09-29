# 存档 / 读档 / 自动存档 / 时光机（SAVE%d.DAT）

> 状态：**完整实现 ✅（2026-09-28）**。存档写 / 读档 / 自动存档 / 游戏内读档闭环 / 时光机
>   全链打通，**1:1 复刻原版字段布局**（自产档与原版 `SAVE*.DAT` 同尺寸，往返字节稳定，
>   可被原版读取，且能正确解析原版运行时值）。
> 原始函数：`sub_402FD1`(存档写) / `sub_402AC5`(读档) / `sub_403D74`+`sub_40363A`(读档界面) /
>   `sub_403396`+`sub_4039C2`(存档界面) / `sub_41904C`(自动存档触发) / `sub_44808A`(时光机快照存) /
>   `sub_448544`(时光机快照恢复) / `sub_4080F5`(场景复位) / `sub_407AD2`(loadMapData)。
> 重写落点：`src/app/save_data.cpp`（序列化内核）+ `src/app/new_game.cpp`（读档/场景）+
>   `src/app/turn_system.cpp`（时光机快照/自动存档）。

## 1. 核心机制（探查结论）

原版把"游戏内全部持久状态"以**连续 fwrite 字段序**写盘，地图地块单独以 **mapDat 内存 blob**
（含 5 表 + 相对偏移头，**内部全是相对偏移、无绝对指针**）整段 dump，并在尾部再 dump
**每玩家 `g_playerMapBlocks[2502*i]`（10008B）= 时光机回合快照** + `g_playerMapDatCopies`
（该回合 mapDat 副本，dwSize 字节）。

关键点：
- **存档内容 ≈ 时光机快照内容**：0x44808A(存) 与 0x448544(恢复) 操作的字段集，就是存档主体
  + mapDat。存档尾部 per-player `mapBlocks` 正是 `g_playerMapBlocks` 的原样字节 → **读档只要把它
  灌回内存，时光机读档后即可用**（"时光机基于存档"得到印证）。
- **地块状态在 mapDat blob**（不在 cellTable，cellTable 只是 46 槽物件表）。读档用 0x402AC5
  末尾 `g_cellEnts = g_mapDat + *(g_mapDat+1)` 等**相对偏移**重建五表指针；重写用
  `parseMapDatBlob` 反向解析回 5 个 vector（与 `mapdat.md` 同结构）。
- **指针字段自修复**：存档里 `g_players[+0]`（姓名）与 `g_stocks[+0]`（股票名）是 DGROUP 内存指针，
  原版读档**不信任**它们——读入后立即用 `charIndex→g_charData`（0x402BAE）、`mode/map→off_47F072`
  （0x402CA7）重建。故重写只要保证 `charIndex`/`map`/`mode` 正确，产出档即可被原版读取（name
  指针位写 0 安全）。

## 2. 存档字段序（version 起，旧档 `resources/MultiverseJourney/SAVE*.DAT` 实测偏移）

| 偏移 | 大小 | 原版符号 | 字段 | 重写来源 |
|---|---|---|---|---|
| 0 | 4 | — | version=38 | `kSaveVersion` |
| 4 | 4 | dword_497160 | gameDate | `gameDate` |
| 8 | 2 | word_4991B8 | mapIndex | `mapIndex` |
| 10 | 2 | word_4991B6 | gameMode | `gameMode` |
| 12 | 4 | g_playerCount | playerCount | `playerCount` |
| 16 | 416 | g_players | Player[4]×104 | `serializePlayer/deserializePlayer` |
| 432 | 4 | g_humanCount | humanCount | `humanCount` |
| 436 | 80 | g_miscTable80 | 事件槽 5×16 | `npcSlots` |
| 516 | 1104 | g_cellTable | 物件槽 46×24 | `cellTable` |
| 1620 | 60 | g_cardState60 | 卡包 4×15 | `cardState60` |
| 1680 | 60 | g_itemStock | 道具库存 4×15 | `itemStock` |
| 1740 | 30 | g_propStock | 地产库存 | `propStock` |
| 1770 | 8 | g_giftPool | 礼物卡池 | `misc8A` |
| 1778 | 4 | g_turnCounter | 回合计数 | `turnCounter` |
| 1782 | 6912 | g_stockHistory | 12×144×4 | `stockHistory` |
| 8694 | 384 | g_playerShares | 48×{i32 shares, f32 avgCost} | `playerShares`+`playerAvgCost`（交织，见 §4） |
| 9078 | 432 | g_stocks | 12×36B 记录 | `stocks`+`stockHalted/News/Reserved/Volume`（交织，见 §4） |
| 9510 | 336 | g_miscTable336 | 挂单 28×12（4 玩家×7 槽） | `tradeSlots` |
| 9846 | 4 | g_currentPlayer | 当前玩家 | `currentPlayer` |
| 9850 | 28 | cfgTravel/LandPerm/DaysLimit/WinMoney/StartMoney/MoneyMul/DayCount | 7×u32 | 同名 |
| 9878 | 20 | 499084/DC/7C/78/EC | stockDivPeriod/Closed/TotalValue/MarketIndex/GlobalDrift | 同名 |
| 9898 | 4 | g_clearedMaps | 通关进度（dword_4990F0） | `clearedMaps` |
| 9902 | 12 | g_charState | 上轮通关遗留 AI | `newGameConfig.aiUsed`（非 0=2） |
| 9914 | 4 | g_publicFund | 公库/乐透池 | `publicFund` |
| 9918 | 36 | g_lotteryNumbers | 乐透归属 | `lotteryNumbers` |
| 9954 | 8 | g_jailFlags | 在押 | `jailFlags` |
| 9962 | 8 | g_hospitalFlags | 住院 | `hospitalFlags` |
| 9970 | 4 | g_newsPos | 新闻索引 | `newsPos` |
| 9974 | 4 | g_fatePos | 命运索引 | `fatePos` |
| 9978 | 36 | g_newsOrder | 新闻顺序表 | `newsOrder` |
| 10014 | 37 | g_fateOrder | 命运顺序表 | `fateOrder` |
| 10051 | 4 | dword_499088 | 地图旋转 | `mapRotation` |
| 10055 | 4 | dwSize | mapDat 字节数 | `buildMapDat().size()` |
| 10059 | dwSize | g_mapDat | **地块五表 blob**（见 mapdat.md） | `buildMapDat/parseMapDatBlob` |
| … | ×playerCount | 每玩家 | `g_playerMapBlocks`10008 + `g_playerMapDatCopies`dwSize | `snapshots[i].block/mapDatCopy`（§3） |

**尺寸校验公式**（旧档实测 100% 命中）：
`size = 10059 + dwSize + playerCount × (10008 + dwSize)`
（map0/mode0：dwSize=7956；4 人=89871、2 人=53943；map3：dwSize=9056）

## 3. 时光机快照 `g_playerMapBlocks[2502*i]`（10008B，0x44808A/0x448544）

dword 索引 ×4 = 字节偏移（`save_data.cpp kBlk*`）：

| dword | 字节 | 内容 |
|---|---|---|
| 0 | 0 | valid=1 |
| 1 | 4 | gameDate（dword_48CB84） |
| 2..105 | 8 | players 416B（serializePlayer×4） |
| 106 | 424 | miscTable80 80 |
| 126 | 504 | cellTable 1104 |
| 402 | 1608 | cardState60 60 |
| 417 | 1668 | itemStock 60 |
| 432 | 1728 | propStock 30 |
| 439(+2) | 1758 | giftPool(misc8A) 8 |
| 442 | 1768 | turnCounter（dword_48D268） |
| 443 | 1772 | stockHistory 6912 |
| 2171 | 8684 | playerShares 384（含均价） |
| 2267 | 9068 | stocks 432（含 halt/news/reserved/vol） |
| 2375 | 9500 | miscTable336 336 |
| 2459..2466 | 9836 | moneyMul/dayCount/499084/DC/7C/78/EC/publicFund |
| 2467 | 9868 | lotteryNumbers 36 |
| 2476/2478 | 9904/9912 | jailFlags/hospitalFlags 8/8 |
| 2480/2481 | 9920/9924 | newsPos/fatePos |
| 2482 | 9928 | newsOrder 36 |
| 2491 | 9964 | fateOrder 37 |
| — | +mapDatCopy | 该回合 mapDat blob（dwSize） |

- **保存点**（0x44808A）：`startPlayerMove` 人类掷骰前（0x40DD1F）/ 传送自己前（0x4477C3）/
  状态展示（0x40C912）。
- **恢复**（0x448544）：`useItemTimeMachine`（0x447387 时光机 id10）触发；回灌后全员 `loadWalkResources`
  （0x448A53）+ `sub_40C03B`(buildMiniMapMarks) + `rebuildEventNpcFromSlots`。
- 原版 news/fate 顺序表**在快照内**（dword 2482/2491）→ 回滚会还原抽事件进度；重写此前未建模，
  本次经 blob 内核补齐。

## 4. stocks / playerShares 记录内嵌字段（差异归位）

重写曾把以下"寄生在原版大记录内的字段"拆成独立数组，导致存档丢失——本次**归位到原版字节布局**：

| 重写数组 | 原版宿主 | 位 |
|---|---|---|
| `stockHalted[12]` | g_stocks 每支 dword[1] | bit16..23（`byte_496986`，+6 字节） |
| `stockNews[12]` | g_stocks 每支 dword[1] | bit24..31（`byte_496987`，+7 字节） |
| `stockReserved/volume` | g_stocks 每支 dword[2] | 低/高 u16（word_496988/49698A） |
| `playerAvgCost[4][12]` | g_playerShares 每支 8B | +4 f32（`flt_4971A4`） |

`buildStocksRecords/parseStocksRecords`、`buildSharesRecords/parseSharesRecords` 负责交织读写；
存档主体与 mapBlocks 内 stocks/shares 段共用。

**删除**：`misc8B[8]`——经查无原版宿主（注释地址 0x496B60 实为 g_hospitalFlags）、无业务读写点、
不在存档字段序、0x44808A 快照也不含 → 移除。

**原版不持久化**：`trapStock`（路障/地雷/炸彈回收，byte_497321..23 与 giftPool 重叠）；
`playerDebt` 欠款矩阵；`aiCardTarget`/`aiItemTarget`（均不入档，原版同）。

## 5. 场景复位与读档链

- `resetSceneForReload`（0x4080F5）：读档/退局/重开局前无条件复位
  `pendingSpawnPlayer`(0x475114)/`manualView`(0x48BE18)/视口(0x48BE1C-BE20) + 重写补充的
  跳伞/事件 FLC/掷骰/高亮/状态机标志，杜绝视角错位、重复跳伞、动画串帧。每玩家快照的 memset
  在 `loadMap(fresh)` 内（对齐 0x407CCF），读档路径保留存档恢复的快照。
- `loadMap(app, freshMapDat)` 对齐 0x407AD2 的 `if(!g_mapDat)` 守卫：
  fresh=true（新游戏）读 MAPDAT 资源 + 重置 cellTable/初始神明/specPt 初值；
  fresh=false（读档）跳过该块，五表 vector/cellTable 已由 `parseSaveBody` 灌入，仅载
  GND/图块/walk/监狱医院扫描/refreshStockSpecPtMap/buildMiniMapMarks。
- 读档链（0x402AC5）：`reloadGameFromSlotData` = showLoading → resetSceneForReload →
  parseSaveBody → charIndex→name 重建 → loadMapPreview → loadMap(false) →
  rebuildEventNpcFromSlots → initTurnState。`loadGameFromSlot` = 上 + `enterGameLoop`（主菜单路径）；
  游戏内工具条 case 3（0x417D65）只调 `reloadGameFromSlotData`，由现有 `gameEventHandler` 模态继续。
- `initTurnState` 不再硬置 `currentPlayer=0`，改 `playerActionFlags[currentPlayer] |= 0x80`
  （对齐 0x402F92 `byte_498EA0[52*cur]|=0x80`），读档保留存档当前玩家。

## 6. 自动存档（0x41904C）

`nextPlayer`（0x418EBD）在过天分支（`v1=dayPassed`）末尾：
`if (dayPassed && settings[4]/*byte_49715C*/) saveGameToSlot(0)` → 写 **SAVE0.DAT（AUTO 槽）**。
开关在设置对话框 idx9「自动存档」（settings[4]，0x40FD49）；读档界面槽 0 显示 `AUTO`。

## 7. 验证

`--save-selftest <slot>`（headless，仅依赖自由函数）对 `resources/MultiverseJourney/SAVE0..5.DAT`：
- 全部 `parseSaveBody` 成功，运行时值合理（cash/bank/points/charIndex/alive、stocks
  cur/prev/spec/reserved/vol/halt/news、每玩家快照 valid）；
- 往返 `save(a)→parse→save(b)` 字节 **STABLE=YES**，且 `save(a)` 尺寸 == 原版 `orig` 尺寸
  （89871/95371/53943 逐一相等）→ 字段序双向对称、与原版同布局（可读原版、可被原版读）。

## 8. 已知差异 / 待实机

- `playerAvgCost` 经 `playerShares` 8B 记录 +4 f32 持久化，往返稳定；语义（买入加权均价）由交易
  链维护，原版读档亦恢复同值。
- 股票名/玩家名指针位存档写 0，读档上层按 charIndex / kStockNames 重建（与原版指针表重建等价）。
- 通关进度 `clearedMaps` 已随存档读写保偏移，但通关标记/界面（0x4075C1 `sub_4075C1`）属 M3。
- 实机验收项：存→读完整往返（地/卡/股/NPC/挂单/回合/日期）、读原版旧档、过天自动存档 + AUTO 读、
  读档后时光机回滚、游戏内 case3 读档、切视角后退局/读档不再错位、不重复跳伞。
