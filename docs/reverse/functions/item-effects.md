# 道具效果（P4 / item-effects）

> 专项研究档：13 种道具效果函数 + 公共依赖（目标选择/台词气泡/破坏/快照）。
> 效果表 `g_itemEffectFuncs`（0x475DD4，`dword@(0x475DD4+4*id)`，索引 = 道具 id 1..13），
> 由 `itemPanelFlow`（0x447D97）在 `v8 != 0`（面板返回 id）时调用 `func()`；
> **返回非 0 = 结束道具流程；返回 0 = 重弹面板再选（不消耗）**。
> 重写挂点：`src/app/item_bag_dialog.cpp` `itemEffect()` 分派。
>
> 代码内道具 id 顺序（`kItemBagNames`）：1機器娃娃 2路障 3地雷 4定時炸彈 5機車 6汽車
> 7飛彈 8遙控骰子 9機器工人 10時光機 11傳送機 12工程車 13核子飛彈。
>
> **实现状态（2026-09-25）：13 道具全部实现 ✅**（`src/app/item_effects.cpp` 分派 +
> `playItemLine` 气泡台词 + `selectTargetDialog` 目标选择模态 + `expireAssets`/`getObjectPosition`
> 公共依赖）。已知差异（待实机迭代）：① 目标选择模态地图滚动 ✅（0x80 模式：边缘方向光标
> 34/40/38/36 + 50ms 加速滚动）；② AI 分支（`sub_420EEE` = dword_48BE64[0] 预选）待 P5；
> ③ 时光机快照未含 news/fate 顺序表（重写该子系统未建模）；④ 传送机事件槽（4..7）目标待 P5。
>
> **实机修正（2026-09-25 首轮）**：
> - 機器娃娃：槽 8 行走资源 = **data.mkf[521]/[522] 专用**（0x40B93B 槽 8 分支，非玩家角色资源）；
>   弹飞基准 = 娃娃当前格/上一格（0x41B4FF 的 word_498E6C/6E 随移动逐步更新）
> - 機器工人：施工前 `refreshGameUi(x,y)` **视口对准目标地块**（0x41D476）；商业用地设施选择
>   面板在 angelUpgrade 内弹出、关闭后才播 FLC 553
> - 遥控骰子：面板 = **不透明 blit**（0x4563F5 blitElementFullscreen；透明 blit 会花屏）
> - 下車按鈕（道具栏）：载具中显示 panel[11] 帧 15/16 @资源内 (325,117)，
>   `g_itemVisibleMap[14]=14` 固定网格位 14 → 点击返回 id14 → `0x447C00` 载具回包恢复步行
> - 定时炸弹：挂身即重绘+台词（0x41BC2B）/ 每步寿命与爆炸链路加日志（0x41B697/0x41B6CD）
>
> **傳送機空地放开（2026-09-26 实机反馈）**：人物/神明/物品目的地新增**无主空地**
> （住宅 owner==0&&level==0 / 商业 owner==0&&sub==0）——`selectTargetDialog(allowEmptyLand=true)`
> 让无主地块可选中（金十字光标），`resolveTeleportDest` 把 2000+/4000+ 对象 id 解析为
> special==id 且占用干净的格；落地触发买地。另有诊断日志（`rich4.log`）与拾取详情打印。
>
> **傳送機贴合原版（2026-09-26）**：0x447428 全流程逐行核对（见 §4）——
> ① **传送自己前保存时光机快照**（0x4477C3 `sub_44808A`，位置更新前；重写补 `saveTurnSnapshot`）；
> ② 人物/神明/物品目的地**放开医院/监狱/事件格**（原版拾取语义：这些格返回格 id，可传，
> 落地触发住院/入狱/事件；重写 `isVacantRoad` special 检查收窄为 2000..7999 拒绝地产/设施点）；
> ③ 神明/物品旧格占用清 **bit16-23 全部 8 位**（0x44793B `cellEnt+38 = 0`，原先只清单槽）；
> 保留魔改（用户确认）：源=完全空无主地拒绝、房屋目的地限无主空地、人物/神明/物品限空道路。
>
> **实机修正（2026-09-25 第二轮）**：
> - 炸弹计数器：头顶数字判定 = `cellNo`（挂身道具槽），非 `cellTableIdx`（附身神明；0x4098BD）
> - 機器娃娃不占格：0x40C05C 中槽 8（v65==4）跳过 `&= / |=` 占位——否则 bit16 与物件槽 1
>   占用位冲突，每格误判有物件（弹飞+删除+轮替 slot1）
> - 遥控骰子显示个数 = `diceShowCount`（rollDice 写：遥控单骰 = 1），非 diceCount（0x419572 v2）
> - 機器工人：FLC 553 前快照升级前旧建筑（angelUpgrade 后恢复 surface），switchFrame=44 重绘
>   才显示新建筑；设施面板残留由该快照清除
> - **无主加盖地产渲染**：estate `level>0` / corp `sub>0` 时绘制建筑（调色板 = owner，0 = 黑轮廓；
>   原版 0x4091E5/0x4093F9 不检查 owner）——修复机器工人/天使对无人空地加盖无显示变化
> - 工具条收尾 `sub_40DEFE`（0x40DEFE）：道具使用后当前玩家有状态 → `flags|=0x80` 自动跳过回合
>   （修复核弹自炸住院卡在等待操作）
> - 核弹范围 = 视野 440×440（`sub_40A45C(-1)`）→ 世界坐标 ±220，非全地图
> - 拾取 id 规范化：玩家/物件命中 → 原版 0x8000 编码（修复飞弹选玩家炸错坐标）
> - 傳送機 [HELP 94]：房屋→無主空地；人物/神明/物品→**空道路**（排除占用 + 房屋/设施/特殊点/
>   事件格）；完全空的无主地不可为**源**（原版可选中但搬空白耗道具）
> - 目标选择模态**地图滚动**：鼠标出视口（x==0/x>=440/y<=40/y==479）→ 方向箭头光标 +
>   50ms 加速（+4/次，≤64）滚动，回视口内停滚可确认；滚动中点击仅提示音

## 0. 效果函数总表（已反编译）

| id | 道具 | 地址 | 一句话 | 依赖 |
|----|------|------|--------|------|
| 1 | 機器娃娃 | 0x446AFB | takePlayerCard(1)+台词；**虚拟槽 8**、走 9 步踢除沿路障碍；结束恢复玩家 | 台词、槽 8 行走、p==8 弹飞删除 |
| 2 | 路障 | 0x446BAA | 选格 → `createMapObject(16)` + `flyObjectSprite(100)` | 目标选择(1)、台词、飞行动画 |
| 3 | 地雷 | 0x446C88 | 同路障，对象 17，选择 mode 0x10001 | 目标选择(0x10001) |
| 4 | 定時炸彈 | 0x446D69 | 同路障，对象 18，选择 mode 0x20001 | 目标选择(0x20001) |
| 5 | 機車 | 0x446E4A | 已是机车→0；汽车→归还 slot5；travel=1/dice=2 | 歌词、loadWalkResources |
| 6 | 汽車 | 0x446F05 | 已是汽车→0；机车→归还 slot4；travel=2/dice=3 | 同上 |
| 7 | 飛彈 | 0x446FBC | 选目标 mode 0x300C0 → FLC 528 → `expireAssets(100,38,0,cur)` 降级+伤害+住院3 | 目标选择、getObjectPosition、expireAssets、FLC |
| 8 | 遙控骰子 | 0x4470F8 | panel[72] 面板六骰选择（`sub_446774`）→ `byte_475DD8=点数` + `sub_40DD1F()`（直接移动） | 骰子模态、rollDice 强制点数 |
| 9 | 機器工人 | 0x447295 | 选目标 mode 0x2090006 → `angelUpgrade(objId)` + FLC 553 | 目标选择、angelUpgrade ✅ |
| 10 | 時光機 | 0x447387 | `sub_448544()` 全状态回滚快照（成功→扣卡+地图/音乐重置） | 回合快照 g_playerMapBlocks |
| 11 | 傳送機 | 0x447428 | 选目标 mode 0x1200036 → 按类型选目的地（住宅 0x2090002/商业 0x2090004/空格 0x2090001）→ 搬迁；**自己传送前存时光机快照**；医院/监狱/事件格可传 | 目标选择、快照、大流程（§4） |
| 12 | 工程車 | 0x4479D2 | travel=31（Kind+100 暂存原载具/dice），走 18 步自动拆房 | 拆除机制（在路上） |
| 13 | 核子飛彈 | 0x447ACE | 选目标 mode 0x400C0 → FLC 530 → `expireAssets(-1,38,1,cur)` 全图归公+伤害+住院3 | 同飞弹 |

**放置类公共尾部**（路障/地雷/炸弹）：
```
台词(sub_44EF41, 0)
v = alive==1 ? sub_446AE8(mode) : sub_420EEE(0)   // 人类选择 / AI 预选 dword_48BE64[0]
if (v) {
    obj = createMapObject(type, v, 0, 0)
    flyObjectSprite(obj, playerX, playerY, cellEnts[v].x, cellEnts[v].y, 100)  // 100 帧飞行
    audioPlayEffect(对应音效)   // 路障 dword_48236A / 地雷 dword_482372 / 炸弹 g_effectSlot2(0x48235A)
    sub_41D546()               // 视口复位+面板刷新
    --itemStock[15cur + id-1]  // 直接扣（非 takePlayerCard）
}
return v;   // 0 = 取消 → 不消耗
```

**飞弹/核弹公共尾部**（0x446FBC / 0x447ACE）：
```
台词
v = alive==1 ? sub_446AE8(mode) : sub_420EEE(0)
if (v) {
    takePlayerCard(cur, id)
    getObjectPosition(v, &x, &y)
    flc = sub_450441(dword_48A0E4 /*data.mkf*/, 528/530)
    refreshGameUi(x, y, 0)        // 视口中心对准目标（expireAssets 以视野中心取范围）
    expireAssets(100/-1, 38, 0/1, cur)
    sub_45144F(flc, 0, 40, 589825 0x90001 / -2146893823 0x80090001, 81/83)
    sub_456E11(flc)
    for i in 0..playerCount:
        if (alive[i] & 0x40) { addPlayerDebt(i, cur, 90*g_moneyMul); hospitalizePlayer(i, 3) }
    sub_41D546()
}
return v;
```

## 1. 公共依赖

### 1.1 目标选择模态 `sub_445E4D`（`sub_446AE8(mode)=runModal(sub_445E4D, mode)`）

**mode 编码**（WM_USER+1 lParam）：
- bit0-15 = 筛选 flags（`dword_48C594`）
- bit16-23 = 光标类型（`dword_48C588`，`cursorSelect(type, 1, 10)` 命中时）
- bit24-31 = 光标 idx-1（`dword_48C58C = HIBYTE+1`）
- **bit6（0x40）特殊**：强制 flags = 0xB7（1|2|4|0x10|0x20|0x80），即"全可选+滚动模式"

**各道具 mode**：

| 道具 | mode | flags | 光标类型 | 滚动 |
|------|------|-------|---------|------|
| 路障 | 0x1 | 1（普通物件/格） | 0 | 否 |
| 地雷 | 0x10001 | 1 | 1 | 否 |
| 定時炸彈 | 0x20001 | 1 | 2 | 否 |
| 飛彈 | 0x300C0 | 0xB7 | 3 | **是** |
| 機器工人 | 0x2090006 | 2\|4（住宅+商业地产） | 9 | 否 |
| 傳送機 | 0x1200036 | 0x10\|0x20\|0x4\|0x2 | 0x20=32 | 否 |
| 核子飛彈 | 0x400C0 | 0xB7 | 4 | **是** |

**命中筛选**（`pickMapObject(mx, my-40)` → v13）：
- `flags&1 && 0<v13<2000`：普通格/物件（神明/设施/地点）
- `flags&2 && 2000<v13<4000`：住宅用地（a1=estate）
- `flags&4 && 4000<v13<6000`：商業用地（a2=corp）
- `flags&0x10 && v13&0x8000 && (u8)v13`：玩家（`sub_40D293` 位扫描解码；`>=4 || alive`）
- `flags&0x20 && v13&0x8000 && v13&0x7F00`：挂身物件
- 高字节条件（BYTE1(lParam)，道具均 0）：case 1..8 额外过滤（自宅/同类地产/无主地/路障炸弹格…），实现时可留档

**鼠标/滚动**（flags&0x80）：
- 鼠标出视口（x≥440 / y≥440 / y≤0）→ 方向 `dword_48C568`：1上 2左 3下（y==479）4右；
  `SetTimer(50ms)` 开始滚动；鼠标回视口内 → KillTimer + 立即判定
- 每 tick：`speed dword_48C56C`（初 8，+4，上限 64）；
  `view.x += (vecX*speed)>>16; view.y += (vecY*speed)>>16`（16.16 定点），
  clamp [220,2084]，`refreshGameUi(view.x, view.y, 0)`（滚动期间鼠标也可命中，`LABEL_53` 仍在跑）
- 方向向量表 `dword_4751B0[2*v7]`/`dword_4751B4[2*v7]`（v7=`(camDir+2*mouseDir-2)&7`，
  8 项 = N/NW/W/SW/S/NE…，16.16：±65536、±46341=0.7071）
- 滚动方向光标 id：`off_475E0D[1..4]` = 0x22/0x28/0x26/0x24（34/40/38/36）
- 确认：LBUTTONUP 且 **计时器未激活**（`!dword_48C564`）且已命中 → `cursorSelect(41,1,0)` +
  `postModalExit(selId)`；未命中 → `g_uiSoundMusicTip` 提示音
- 取消：RBUTTON down/up、双击、MBUTTON → `postModalExit(0)`（`(flags&8)==0` 时）
- 默认/离开光标 `cursorSelect(5,1,0)`；进入模态 `setPauseDraw(1)` + `rebuildPickBuffer(1)` +
  PostMessage(WM_MOUSEMOVE 真实鼠标位置) 初始化
- 选择内存：`dword_48C584` = 选中 id；`dword_48C580` = 当前光标类型（变化才 cursorSelect）

### 1.2 角色气泡台词 `sub_44EF41(p, expr, text)`（**不是语音，是气泡**）

- 去重：同一文本指针连续调用只显示一次（`dword_4762C8`）
- p&0x8000 → 不 refreshGameUi；p 在监/院/挂起状态（`BYTE1(dword_496B9A[26p])`/`playerState37[104p]`/`byte_496B9E[104p]`）→ **不显示**
- 演出：tip 气泡框 `g_tipFrame`（0x48BAD8）@(220,130) + 角色头像
  `pieceSprites[13p]` 帧 `expr+1` @(170,130)
- 文本格式：
  - `#NNNN文本` → `drawText` 解析语音（重写 text.cpp 已处理 `#NNNN`→playVoice）
  - `#NNNN@MM` → 语音 + 显示角色表情帧 `MM-1`（`dword_48BAD4` 表情图；MM=19/20）
- 时长 1000ms（`sub_4544F6(1000)`），期间 saveBackground/恢复

**台词表**（`off_480D5A` = 0x480D5A，12 角色 × 26 列文本指针；道具 id1..13 = 列 0..12）：
每角色 13 条台词已 dump（见 §3 各道具行；语音 id = `#` 后 4 位十进制，音近 0236..0422）。

### 1.3 `expireAssets(a1, flags, dump, payer)`（0x40AC7B）+ 范围采集 `sub_40A45C`

`sub_40A45C(a1)`：把 **pickBuffer**（当前视野拾取表）里非零 id 收集到 `word_48B8C4[]`：
- `a1 == -1` → 全图 440×440
- 否则以 `(220-a1)` 起始、边长 `2*a1` 的方块（**以视野中心为中心**；飞弹=100 → 200×200）

`expireAssets` 对每个 cellEnt id：
- `flags&2 && 2000..4000`（住宅）/ `flags&4 && 4000..6000`（商业）：
  - `dump != 0` → **归公**：owner 欠债 `30*M*level`（住宅）/ `30*M*sub`（商业，回报给 payer `a4`），
    owner/level|sub/type=0、商业清 +52、`rebuildMiniMap(0)`
  - `dump == 0` → **降级**：owner 欠 `30*M`；level|sub `--`；连锁（type!=0）归零
- `flags&0x20 && id&0x8000`：低 4 位玩家位掩码 → `damagePlayer(j)`；
  bit4-7 → 事件槽玩家 `hospitalizePlayer(k,0)`；`BYTE1&0x7F` → `deleteMapObject(idx)`

调用于飞弹(100,38,0)/核弹(-1,38,1)/其它（0x44913D 小游戏？0x44AB2C）。

### 1.4 `getObjectPosition(objId, &x, &y)`（0x40AF12）

- <2000 → `cellEnts[objId]`；(2000,4000) estate；(4000,6000) corp；(6000,8000) specPt；(8000,10000) evtCell
- `&0x8000`：低 4 位=玩家位掩码（取 `playerSpriteX/Y`）；bit4-7=事件槽（`miscTable80+8i`）；
  `BYTE1&0x7F`=挂身物件（`word_496D0A[12*idx-12]` → cellNo → cellEnt 坐标）

### 1.5 遥控骰子 —— 骰子选择模态 `sub_446774` + 强制点数

- 面板 `panel.mkf?`（`dword_48A05C`）索引 72 帧 1 blit @(92,300)，6 骰按钮
  `x=104+40*i, y=314..343`（30×30）；悬停帧 `12+12*(i+2)`；DOWN 无；
  **LBUTTONUP** → `postModalExit(i+1)`；RBUTTONUP → 0 取消；进出 cursor 无特殊
- `byte_475DD8`（0x475DD8）= 一次性强制点数：`sub_447285()` 读后清零；
  `rollDice(a1@edx, a2)` 里 `if (a2) { v2=1; dice=[a2]; }`（**单骰**）
- 流程：模态返回 v0 → `sub_40DD1F()`（开始移动，0x40DD1F）→ takePlayerCard(8) → `byte_475DD8=v0`

### 1.6 機器娃娃 —— 虚拟槽 8

- `startDemolishWorker`（0x446AFB，旧称"拆除卡"实为娃娃）：
  `takePlayerCard(1)`；台词列 0；保存玩家坐标/cellEntId 到 `word_498E68/6A/6C/6E`、
  `byte_498E70=玩家号`、`byte_498E71=byte_496B78`、`byte_498E72=0`；`g_currentPlayer=8`；`sub_40DD1F()`
- `startPlayerMove`（0x40DD1F）对 `currentPlayer>=8`：`stepsRemaining=9`、状态 1、
  `dword_4749D4=9`、音效 `dword_482382[4]`（+16 字节）
- `endTurn`（0x418EBD）对 8：`g_currentPlayer=byte_498E70`；`byte_498E72` 0→3；
  `sub_415E70(1)`（恢复玩家精灵状态）
- **p==8 到达格处理**（onPlayerActionPhase 0x41B42D）：踩到物件 → `bounceObject`（0x40FAFD
  设 f32 位置/速度+flyCount 弹飞）+ `deleteMapObject`（重写见 map-object-refresh.md §5）
- case0 收尾：`sub_40FAD6()`（`g_cellFlyCount[24*i]` 全 0 才继续）

### 1.7 时光机 —— 回合快照 `g_playerMapBlocks`

- 每玩家 2502 dword 快照（`g_playerMapBlocks`，含日期 `dword_48CB84[2502p]` 起 2 dword 头）
- `sub_448544()`：快照头非 0 → 全状态 memcpy 回全局（日期/players/miscTable80/cellTable/
  cardState60/itemStock/propStock/giftPool/turnCounter/stockHistory/playerShares/stocks/
  miscTable336/moneyMul/dayCount/dword_499084/DC/7C/78/EC/80/miscTable36A/jail/hospital flags/
  newsPos/fatePos/newsOrder/fateOrder/mapDat）+ `sub_40C03B()` + 全员 loadWalkResources
- **快照写入点（已定位）**：① `sub_40DD1F` 开始移动前（0x40DD53，人类且非住院/入狱时）；
  ② `sub_40C912` 状态展示分支（0x40C97C，有挂身状态且非住院/入狱）；③ **傳送機自己传送前**
  （0x4477C3，位置更新之前）。三者都先查 `alive & 1`；重写 `saveTurnSnapshot` 调用点
  见 turn_system.cpp `startPlayerMove` 与 item_effects.cpp `useItemTeleport`
- 外层（0x447387）：成功 → 音乐 g_musicTimer 处理（停/重播 track0）+ takePlayerCard(10) +
  `rebuildMiniMap(0)` + `sub_41906A(1)`

## 2. 重写计划（每道具一提交）

顺序：基础设施 → 简单 → 依赖递增。

1. 基础：`playItemVoiceLine()`（气泡台词 0x44EF41 + 台词表）+ itemEffect 分派骨架
2. id5 機車 / id6 汽車（无新依赖）
3. id12 工程車（travel=31 + 拆除机制已有 demolishAtObjId；18 步计数接入）
4. id8 遙控骰子（骰子模态 + rollDice 强制点数）
5. 目标选择模态（0x445E4D 重写：flags/光标/滚动/确认）
6. id2/id3/id4 放置三件（路障/地雷/炸弹）
7. id9 機器工人（目标选择 + angelUpgrade + FLC553）
8. id7 飛彈（expireAssets/getObjectPosition/FLC528）
9. id13 核子飛彈
10. id1 機器娃娃（槽 8）
11. id11 傳送機
12. id10 時光機（快照）

## 3. 待深入清单

- [x] 传送机 0x447428 完整流程（0x5AA：选目标 → 目的地 → 分类搬迁 + 快照 + 拾取语义，
  见 §4）；剩 AI 目的地 `sub_420EEE`（P5）与事件槽 4..7（P5）
- [ ] 时光机快照写入点（回合开始/advanceDay？）
- [ ] 工程车 18 步计数与拆除逻辑（行走中的对应代码）
- [ ] 机器工人 FLC 553 flags（0x2C0001=帧?）与 sub_45144F 参数映射
- [ ] 高字节条件 BYTE1 case 1..8（当前道具不用；卡片重建可能用）
- [ ] AI 分支 `sub_420EEE` = `dword_48BE64[0]`（AI 预选目标；P5）
- [ ] `sub_40DFFA`（商业设施拆分）/ `sub_40C03B`（地图重建）
- [ ] 光标资源 id 0..4/9/32 帧验证（data.mkf[0]）
- [ ] `sub_41D546`（视口/面板刷新）细节

## 4. 傳送機专项（0x447428，2026-09-26 彻查）

> IDB 重命名：`0x447428 = itemTeleport`、`0x44808A = saveMapBlocksSnapshot`；
> 重写 `src/app/item_effects.cpp useItemTeleport`。

### 4.1 流程

```
台词 sub_44EF41(cur, 0, off_480D82[charIndex])
目标 v40 = 人类 ? sub_446AE8(0x1200036) : 0x8000|(1<<cur)     // 单次调用，取消→return 0
  flags=0x36：住宅(2)|商业(4)|玩家(0x10)|挂身(0x20)；光标类型 32；BYTE1=0 无附加过滤
  ├ 玩家 pick 过滤：sub_40D293 位扫描；>=4（事件槽）恒可选；<4 查 alive
  └ 挂身物件 pick：0x8000|(槽+1)<<8（重写 0xA100|槽+1 → normalizeHitId）
物件槽 → owner：cellTable[(槽-1)*24+5] 非 0 → v40 = 0x8000|(1<<(owner-1))   // 0x447494 段
分派：
  2000<v40<4000 住宅：v4 = sub_446AE8(0x2090002)（flags=2，无过滤）
    dst.owner/level/type = src 对应；dst.expireDate(+48) = src；src 全清 + src.price(+44)=0
    rebuildMiniMap(0)
  4000<v40<6000 商业：v7 = sub_446AE8(0x2090004)（flags=4，无过滤）
    dst.owner/sub/type = src；dst.expireDate(+52) = src；src 全清 + src.lastFee(+48)=0
    rebuildMiniMap(0)
  0x8000|低字节 玩家：v35 = sub_40D293
    dest = 人类 ? sub_446AE8(0x2090001)（flags=1） : sub_420EEE(0)
    收集 dest 可用出口（occMask bit30-33 为 0）→ 与当前朝向差最小者作 dir（sub_407A8C(dest,exit)）
    另一出口作 prevCellEnt（唯一出口=同值；无可用出口=原版 UB，重写 prev=0/dir=0）
    if (v35 == cur) { sub_44808A() /*快照*/; steps=0; state=1; byte_46CAFB=1 }
    清源格玩家位 → 写 cellEntId/prevCellEnt/dir/spriteX/Y → updateCarriedObjects
    → 目标格置玩家位（alive!=0）→ loadWalkResources
  0x8000|槽 且 owner==0 神明/物品：dest = sub_446AE8(0x2090001)
    旧格 cellEnt+38 = 0（清 occMask bit16-23 全部）→ cellTable[槽] cellNo = dest
    → 目标格 |= 槽<<16 → 朝向 = sub_407A8C(第一个出口, dest)
  事件槽 v35>=4：word_498DEC/word_498DEE/byte_498DF1/word_498DE8/EA（重写 P5）
收尾：成功 → refreshGameUi(0,0,1) + takePlayerCard(cur, 11)；v0=1
```

### 4.2 拾取/目的地语义（rebuildPickBuffer 0x409B18）

- 格 id 仅当 `occMask & 0xFFFF00 == 0` 时登记（bit8-11 玩家/12-15 事件槽/16-23 物件槽）
- drawList 覆盖（后写优先）：estate `2000+i`/corp `4000+i`/specPt `6000+i`/evtCell `8000+i`/
  玩家 0x8000|位掩码/挂身物件；地产拾取形状 = `g_pickMask` 帧 2/3（143×103 大菱形，
  重写按体感改帧 4 = 51×51）
- **结论**：目的地（flags=1 / 0x2090001）原版可选 = occMask 干净的格 id，含
  **医院(8001)/监狱(8002)格**（无 drawList 覆盖，落在格 id）与**事件格图块外**
  （evtCell 图块形状外返回格 id）→ 落地触发对应事件；地产/设施点被大菱形拾取覆盖而拒绝
- **[2026-09-26 修正] 格→对象拾取归一化**：`cellEnt+32` 指向地块/设施（住宅 2000+i/
  商业 4000+i/设施 6000+i，**商业/设施每项占 2 格**，MAPDAT 实证）的格，拾取 id 一律
  归一化为该对象 id（`map_render.cpp` cellEnt 循环）。原版由大菱形覆盖整块地实现同一语义；
  重写帧 4（51×51）+ 表项锚点（与格锚点最大差 ~47px）盖不满 → 格 id 暴露，导致
  ① flags=1 的路障/地雷/炸弹**能放在地产格上**（原版不能）；② 传送机目的地选中后被
  `isVacantRoad` 误拒并弹"只能移到空的道路！"（原版静默选不中）。归一化后三道具行为一致
  （医院/监狱 8001/8002 保持格 id，与原版同）

### 4.3 差异（重写 vs 原版）

| 项 | 原版 | 重写 | 处置 |
|----|------|------|------|
| 源 = 完全空无主地 | 可选（= 清空目的地/原地清空的拆迁用法） | 拒绝 + "這塊地沒有東西可移動！"重选 | **[USER] 保留魔改** |
| 房屋目的地 | 无校验，直接覆盖任意地块（含他人产业） | 限 `owner==0 && level/sub==0` 无主空地 | **[USER] 保留魔改** |
| 人物/神明/物品目的地 | 格 id（医院/监狱/事件格可传）；无主空地=大菱形覆盖不可选 | 格 id 走 `isVacantRoad`（special 2000..7999 拒绝 + occMask 干净）；**无主空地放开**：`selectTargetDialog(allowEmptyLand)` + `resolveTeleportDest` 解析为该地块格（[USER]，[HELP 94]"空道路/空地"） | **贴合原版 + 空地魔改** |
| 传送自己快照 | `sub_44808A`（0x4477C3） | 原先缺失 → 本次补 `saveTurnSnapshot` | **贴合原版** |
| 神明/物品旧格清位 | `cellEnt+38 = 0`（bit16-23 全部） | 原先单槽清除 → 本次改 `&= ~0x00FF0000u` | **贴合原版** |
| 目标/目的地模态 | 单次调用（取消即退出） | guard 重选循环（魔改配套） | 保留（随魔改，目的地分支归一化后不再触发） |
| 地产/设施格拾取 | drawList 大菱形 143×103 覆盖整块地 → 返回 2000+/4000+/6000+ | 帧 4（51×51）盖不满 → 格 id 暴露（路障能放、传送误报） | **2026-09-26 修**：格拾取按 special 归一化为对象 id（map_render.cpp） |
| AI 目的地 / 事件槽 | `sub_420EEE` / word_498DEC 等 | 未实现（P5） | 待办 |
| 地产拾取形状 | `g_pickMask` 帧 2/3 大菱形 | 帧 4（51×51，体感调整） | 保留（已知差异） |
