# 地图目标判定专项核查（map-target-audit）

> 状态：**进行中**（2026-09-27 第一批：目标选择统一链路）。
> 范围：所有"以地图为目标"的判定——道具 13 种、卡片 30 种、事件（新闻/命运/魔法屋/事件格）
> 涉及地图的部分。用户验收口径：道路格与旁边的地块格归属必须与原版一致。
>
> 公共链路：`sub_446AE8`（= `runModal(sub_445E4D, mode)`）→ `sub_445E4D` 目标选择模态 →
> `rebuildPickBuffer` 0x409B18（g_drawList + g_pickMask 形状）→ `pickMapObject` 0x40A9D7。
> 重写对应：`selectTargetDialog`（`target_select_dialog.cpp`）+ `mapHitRegions`（`map_render.cpp`）。

## 1. mode 编码（0x445E4D WM_USER+1 lParam；2026-10-05 按字节复核订正）

```
byte0 = flags（筛选：1 普通格/物件、2 住宅、4 商業、0x10 玩家、0x20 挂身物件；
        0x40 宏 → flags = (原 flags & 0x80) | 0x37 且**清 BYTE1**——单独 0x40 = 0x37（不能滚屏）；
        0x80 = 地图滚动模式，**仅 3 个调用点带**：飛彈 0x300C0 / 核子飛彈 0x400C0 /
        房地產公司 0x2090086）
byte1 = BYTE1 高字节过滤（case 1..8）——**非 0 时取代低 flags 判定**（原版 v14 清 0）
byte2 = cursorSelect index（命中时光标类型，0x445E4D 内 cursorSelect(x,1,10)）
byte3 = frameCount-1（命中光标动画帧数；0x445EC1 dword_48C58C=(mode>>24)+1）
```

> **无半径/距离/同路段射程**：人类选目标的唯一空间判据 = 目标锚点像素落在当前
> 440×440 拾取缓冲内（`rebuildPickBuffer` 0x409B18）；`sub_40A45C(a1)` 半径版中心 =
> **视口中心**（0x40A48F `441*(220-a1)`），以任意世界坐标为中心的是 `sub_40A0B1`
> （仅 2 个 AI 调用者）。（2026-10-05 射程专项撤回后的结论留档。）

## 2. BYTE1 case 1..8 语义（0x445E4D 反编译，2026-09-27 实现）

| case | 语义 | 使用处 |
|------|------|--------|
| 1 | 自己的住宅 / 自己的有设施商业 | （暂无调用者，备用） |
| 2 | 与**当前所站格**同为住宅或同为商業、且非同一格（`here = cellEnts[playerCellEntId]+32`） | 換地卡 0x0202 / 換屋卡 0x0204 |
| 3 | — | 无 |
| 4 | 玩家目标且**不是自己**（原版位掩码不含 `1<<cur`） | 均貧/搶奪/查稅/同盟 0x0410 |
| 5 | **他人的有建筑**住宅/商業（`owner != cur+1 && level/sub != 0`；无主加盖也放行） | 怪獸卡 0x0506 |
| 6 | case 5 + **路面道具**（挂身物件类型 16 路障/17 地雷/18 炸彈） | 拆除卡 0x0626 |
| 7 | 玩家目标且**不是自己** | 冬眠卡/陷害卡 0x0710 |
| 8 | **无主无建筑**地块（`owner==0 && level/sub==0`） | 傳送機房屋目的地 0x0802/0x0804 |

> 重写拾取编码映射：地块 = 归一化对象 id（2000+/4000+）；挂身物件 = `0xA100|槽+1`；
> 玩家 = `0xF000|玩家号`（原版单玩家位掩码等价）。实现见 `byte1FilterPass`。

## 3. 全部调用点核查矩阵（IDA `sub_446AE8` xrefs = 32 处，逐个核对）

| 调用者 | 原版地址 | mode | BYTE1 | 重写状态 |
|--------|----------|------|-------|----------|
| 路障 | 0x446BAA | 0x1 | 0 | ✅ |
| 地雷 | 0x446C88 | 0x10001 | 0 | ✅ |
| 定時炸彈 | 0x446D69 | 0x20001 | 0 | ✅ |
| 飛彈 | 0x446FBC | 0x300C0 | 0 | ✅（0x40→0xB7|0x80） |
| 核子飛彈 | 0x447ACE | 0x400C0 | 0 | ✅ |
| 機器工人 | 0x447295 | 0x2090006 | 0 | ✅（AI 分支 stub 待接） |
| 傳送機（源） | 0x44746E | 0x1200036 | 0 | ✅ |
| 傳送機 住宅目的地 | 0x4474FF | **0x2090802** | 8 | **本批修**（原误记 0x2090002） |
| 傳送機 商業目的地 | 0x4475A2 | **0x2090804** | 8 | **本批修**（原误记 0x2090004） |
| 傳送機 人物/神明/物品目的地 | 0x447658 / 0x4478E4 | 0x2090001 | 0 | ✅（+空地魔改，[USER]） |
| 房地產公司（他人費/自家） | 0x41AA6F / 0x41AD04 | 0x2090086 | 0 | ✅ |
| 均貧卡 | 0x4421CD | 0x0E0C0410 | 4 | **BYTE1 本批实现** |
| 換地卡 | 0x44268A / 0x4428D1 | 0x0E0C0202/0204 | 2 | **同上** |
| 換屋卡 | 0x442B70 / 0x442D86 | 0x0E0C0202/0204 | 2 | **同上** |
| 過路費卡(id6) | 0x442F66 | 0x0E0C0010 | 0 | ✅ |
| 天使卡 | 0x4434E1 | 0x0E0C0006 | 0 | ✅ |
| 惡魔卡 | 0x4436F9 | 0x0E0C0006 | 0 | ✅ |
| 怪獸卡 | 0x443930 | 0x0E0C0506 | 5 | **本批实现** |
| 拆除卡 | 0x443B2D | **0x0E0C0626** | 6 | **本批修 mode + 实现**（原误记 0x0526） |
| 搶奪卡 | 0x443E55 | 0x0E0C0410 | 4 | **本批实现** |
| 停留卡 | 0x443F99 | 0x0E0C0010 | 0 | ✅ |
| 冬眠卡 | 0x4441F5 | 0x0E0C0710 | 7 | **本批实现** |
| 陷害卡 | 0x4444D8 | 0x0E0C0710 | 7 | **本批实现** |
| 查稅卡 | 0x44520F | 0x0E0C0410 | 4 | **本批实现** |
| 漲價卡 | 0x445446 | 0x0E0C0006 | 0 | ✅ |
| 查封卡 | 0x4455AC | 0x0E0C0006 | 0 | ✅ |
| 同盟卡 | 0x44572C | 0x0E0C0410 | 4 | **本批实现** |
| 烏龜卡 | 0x4458F8 | 0x0E0C0010 | 0 | ✅ |

> 新闻/命运/魔法屋**不调用** `sub_446AE8`（目标由内部 collect/随机/条件决定，见各专项），
> 因此不在本矩阵；其地图破坏走向 `expireAssets`（§5 批次）。

## 4. 本批修正（2026-09-27，wip 提交）

1. **`cardPickTarget` 无限递归**（`card_effects.cpp`）：人类分支误写 `return cardPickTarget(app, mode);`
   → 改 `selectTargetDialog(app, mode)`。此前人类使用任何需选地图目标的卡片都会栈溢出
   （9bc150a 引入）。
2. **BYTE1 过滤实现**（`target_select_dialog.cpp byte1FilterPass`，case 1/2/4/5/6/7/8）：
   此前完全缺失 → 均貧可选自己、怪獸可砸自己/空地、拆除不可选路面道具、換地不限同类、
   冬眠/陷害可选自己、傳送機房屋可覆盖任何地块。
3. **拆除卡 mode 0x0E0C0526 → 0x0E0C0626**（case6：他人有建筑 + 路面道具）。
4. **拆除卡路面道具判定**（`card_effects.cpp`）：`(v & 0x8000) && v < 2000` 中 `v < 2000`
   与原版编码（0x8000|槽<<8，槽1=0x8100）矛盾 → 删去（原版仅判 `(v8 & 0x8000) != 0`）。
5. **傳送機房屋目的地 mode 0x2090002/04 → 0x2090802/04**：原版 BYTE1=8 case8
   = "限无主空地"是**原版语义**（原重写误标为 [USER] 魔改并自行 guard 重选，已删）。
6. **拾取形状对齐原版**（`map_render.cpp`）：estate → g_pickMask 帧 `(8-(rot+dir))&1`、
   corp/specPt → 帧 `((8-(rot+dir))&1)+2`（原为统一帧 4 体感调整）；cellEnt 帧 4（51×51）。
   **地块拒绝依赖这些大菱形的后写覆盖**——cellEnt 段写格 id（§8 撤销归一化后）。
7. **AI 预选编码统一**（`ai_card.cpp tgtDemolish`）：`hit.id`（0xA100|槽+1）→
   `normalizeHitId`（0x8000|(槽+1)<<8）；导出 `normalizeHitId`（`target_select_dialog.h`）。
   此前 AI 拆除卡会算错槽号（0xA1xx & 0x7F00 >> 8）。

## 5. 批次总览（2026-09-27，全部完成）

1. **第一批**（`a25ef10`）：BYTE1 过滤实现 / 拆除卡 mode+条件 / 传送机房屋 mode /
   拾取形状对齐 `sub_409B18` / AI 编码（§2/§3/§4）。
2. **第二批**（`77dbfc8`）：`expireAssets` 改原版屏幕空间口径（§6）。
3. **第三批**（`353242e`）：高亮改原版查表变换 / 怪獸+拆除卡 FLC 落点/帧/停留/台词顺序（§7）。
4. **第四批**（`b40ca6b`）：整路段卡闪烁基于修改前画面 / 天使卡 FLC 顺序（§7）。
5. **第五批**（`4ec6753`）：機器工人 AI 目标编码修正并接入。
6. **第六批**（本文 §8）：撤销地块格对象归一化（地块格边缘恢复可放＝原版）
   + 回退吸附魔改。

## 6. 范围清算专项（2026-09-27 第二批完成）

### 原版链路（0x40AC7B → 0x40A45C → 0x409DE7）

1. `sub_409DE7()`：清空 pickBuffer → 遍历 **g_drawList**（上次 `sub_40829D` 的绘制列表，
   玩家/事件槽/cellTable 物件/estate/corp/specPt/evtCell 各一条），把 **id 写到对象锚点
   像素**（每对象 1 像素，屏幕坐标 `g_drawListX/Y`；`|=`）；
   **过滤：有主挂身物件（`g_cellOwner[slot-1] != 0`）不写**（附身神明/炸弹不参与清算）。
2. `sub_40A45C(a1)`：`a1==-1` 扫全 440×440 缓冲；否则扫 `(220-a1)²` 起边长 `2a1` 方块
   （缓冲中心 = 屏幕 (220,260)）；收集非零 id → `rebuildPickBuffer(1)` 恢复。
3. `expireAssets` 按 id 段处理（维持原实现）：住宅/商業 归公或降级、玩家 `damagePlayer`、
   事件槽 `hospitalizePlayer`、挂身物件 `deleteMapObject`。

### 关键澄清：`refreshGameUi(x,y,0/2)` **会重绘**

`refreshGameUi` 的 `flags&1==0` 分支走 `drawMiniMap + sub_415E70(0)`，而 **`sub_415E70`
内部调 `sub_40829D(BE1C, BE20)` 完整重绘地图并重建 g_drawList**（用新视口）。
所以飞弹/外星人/山洪的 `refreshGameUi(x,y,0/2)` = **视口对准目标并立即重绘**，
`expireAssets` 的范围即以**目标处新视口**为中心——重写各调用点 `renderGameFrame`
（`launchMissile` / `focusObjId→focusView`）语义等价，**不需改动**。
（旧文档"原版只设视口不重绘、范围用旧视口"为误读，2026-09-27 修正。）

### 重写改动

- `expireAssets` 收集：世界坐标 `±radius` 遍历 cellEnt → **`mapHitRegions` 屏幕空间**
  （anchorX/anchorY = drawListX/Y；`[220±r) × [260±r)`；-1 = 全部），
  `normalizeHitId` 转原版编码，去重；补 **有主挂身物件过滤**（0x409DE7）；
- 核弹调用 `radius 220 → -1`（原版 `sub_40A45C(-1)` = 全视野，两者等价但 -1 为原值）；
- `cx/cy` 仅存参（不再用于筛选）。
- 影响调用点：飞弹/核弹（`launchMissile`）、外星人 news4、山洪 news19——均在
  `renderGameFrame`（视口对准目标）之后调用 ✓。

## 7. 剩余批次

1. ~~**地块高亮闪烁**~~ ✅ 2026-09-27 第三批：`drawEstateHighlight` 改**原版查表变换**
   （`unk_485D68 + 32*k`，k = `byte_476380[i]`；原实现误用 `k>>2` 线性偏移 → 幅度仅
   1/4 且无通道压缩）；`captureHighlightShapes`/收租组定义/帧序已核无需改。
2. **视口/FLC 时序**：本批已修 **怪獸卡 FLC 557**（落点 (0,40)、switchFrame=16）与
   **拆除卡 FLC 529**（落点 (0,40)、switchFrame=38）+ 两卡 500ms 停留与"原主台词在
   FLC 后"顺序（原重写误把目标世界坐标当 FLC 落点）；**天使卡** FLC 523 移到
   `refreshGameUi(0,0,1)` 之后（原版顺序）；**整路段类闪烁**（天使/恶魔）改
   `playHighlightBlink(preRedraw=false)`——闪烁基于**数据修改前画面快照**
   （原版 sub_4554FC 从上次绘制的地图表面复制，新画面由收尾重绘呈现）。
   剩：`refreshGameUi` "==玩家坐标→跟随"分支（仅状态差异，渲染等价）。
3. **拾取/占用**：`occMask` 全生命周期；三道具"无法指定道路位置"✅ 2026-09-27 实机
   复测通过（§8：归一化撤销 + 吸附回退 + 传送机 special 检查撤销）。

## 9. occMask 全生命周期审计 + flag 蒙膜（2026-09-27 彻查，本批）

**occMask 写入点逐一比对（原版 ↔ 重写）——全部一致，无泄漏**：
spawn `|=256<<p`（0x40829D）/ moveOneStep 清旧设新（0x40C05C，`alive&0x30` 分支**不占位**、
NPC 仅 `v65<4` 占位、槽 8 跳过）/ createMapObject `|=(slot+1)<<16` / deleteMapObject
`&=~0x00FF0000` / attachObject 清旧格 / hospitalize·jail（真人清 `~(256<<p)`、NPC 清
`~(4096<<i)`）/ releaseEventNpc 置位 / relocatePlayer 清旧设新 / 旅館走进 0x40D5A5 清、
走出 0x40D6BE `|=256<<p` / fate 出國 0x40D399 清 / teleport 玩家+物件 / eliminatePlayer
**置**死亡占位（0x40CD87，出生点排除用）/ 读档 rebuildEventNpcFromSlots 重建——均对齐。

**本批发现并修复**：
1. **flag 蒙膜渲染缺失**：原版 `sub_40829D` estate(+23)/corp(+28) `flag!=0` →
   `sub_456C33` 按 g_pickMask 形状 `像素 |= 颜色`（涨价 0x7C00 红 / 查封 0x003F 蓝，
   `word_488EF0` 表）。重写数据层齐但无渲染 → 新增 `orColorMask`（`map_render.cpp`，
   详见 `map-object-refresh.md` §14）。
2. **`demolishAtObjId`（0x40AB4A）缺两处**：corp 任一模式使 sub→0 时
   `forceHotelCheckout`（0x40DFFA=强制退房，旧注释"费用统计"系误读）；mode1 没收后
   `rebuildMiniMap(0)`。已补。
3. **已知差异（不修，低频+视觉）**：跨状态转移（坐牢中住院/住院中坐牢）原版
   `sub_40D761→sub_40BF93` 会把 walkRes slot1 替换为出监/出院专用行走资源
   （data.mkf[128+21*char+19/20]）并重载；重写未替换（走出动画用普通资源）。
4. `demolishEstate`/`advanceDay` 地块到期均**不触 occMask**（原版核实）✓。

## 8. 道路格"无法瞄准"根因与修复（2026-09-27，按 IDB 撤销归一化）

**根因（IDB 证据链 + 原版实机截图几何对齐 + Python 复现）**：
- `rebuildPickBuffer`(0x409B18) cellEnt 段写的是**格 id**（`writePickBuffer(buf,
  g_pickMask+60=帧4 51×51, …, v1)`；`writePickBuffer` 0x456A1C **无缩放**、1:1 掩码）；
- 地块的拒绝由**随后**的 drawList 段（estate 帧 0/1 71×47/51、corp/specPt 帧 2/3
  143×103 大菱形）**后写覆盖**实现；
- **原版语义 = 大菱形内 → 对象 id（拒绝）；大菱形外的格菱形边缘 → 格 id（可放）**；
- 原版实机截图几何对齐：可放置光标位置 = cell14 格投影点（±4px），离开 31px 即 X
  ——与"节点级拾取 + 格 id 语义"完全吻合；
- 重写 c7ef631（2026-09-26）把 cellEnt 命中区 id 归一化为 `ce.special` → **地块格整个
  51×51 菱形被拒** = "无法指定道路位置"根因（医院/监狱 8001/8002 未归一化、仍可放 =
  用户所见"能放到大圆饼"）；
- Python 按原版写入顺序复现用户日志 14 点：**7 点原版可放**（cell35/36/38/75）、
  重写全拒绝。

**修复**：
- `map_render.cpp`：**撤销归一化**——cellEnt 命中区一律写格 id（地块拒绝交还 drawList
  大菱形，形状已按原版帧 0/1/2/3）；
- `target_select_dialog.cpp`：**回退** 2026-09-27 一度尝试的 `snapNearestCell` 就近
  吸附魔改（用户否决：原版无此机制）；
- `item_effects.cpp`：**撤销 `isVacantRoad` 的 special 检查**（归一化撤销的连带修正）——
  原版 `sub_446AE8(0x2090001)` 对格 id 无地块判别（occMask 干净即可，落地触发
  买地/住院/入狱/事件）；保留无主空地放开（`allowEmptyLand`）与
  `teleportSourceValid`（源=完全空无主地拒绝）两项 [USER] 魔改；
- 连带核对：整路段卡 BYTE1 过滤对格 id 一律 false（拒绝）＝原版（只有大菱形内对象 id
  可被 case 5/6/8 判定）；`expireAssets` 不处理格 id＝原版 `sub_409DE7`；AI 经
  `cellIdOfHit`/`objIdAt` 取格与对象不受影响。
