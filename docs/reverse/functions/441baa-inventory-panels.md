# 道具栏 / 卡片栏（工具条 case 7/8）

## 基本信息

| 项 | 卡片栏 | 道具栏 |
|----|--------|--------|
| 工具条分发 | `sub_417D65` case 8 @ 0x417DE7 | `sub_417D65` case 7 @ 0x417DE0 |
| 入口流程 | `useCardFlow` @ **0x441BAA** | `itemPanelFlow` @ **0x447D97** |
| 栏绘制 | `drawCardBag` @ 0x441B0A | `drawItemBag` @ 0x447C6E |
| 选择模态 | `cardModal` @ 0x4416F0 | `itemModal` @ 0x445C14 |
| 效果表 | `g_cardEffectFuncs` @ 0x475D5C | `g_itemEffectFuncs` @ 0x475DD4 |
| 名/价表 | `g_cardInfo` @ 0x47FDEA | `g_itemNames` @ 0x47FEDA |
| 库存 | `g_cardState60` @ 0x499120 | `g_itemStock` @ 0x49915C |
| 重写符号 | `src/app/card_bag_dialog.cpp` | `src/app/item_bag_dialog.cpp` |
| 状态 | 已实现（外壳 + id7/22/23 真实效果） | 已实现（外壳 + 全 stub） |

## 共用流程骨架（useCardFlow / itemPanelFlow）

```
if (g_playerAlive[cur] == 1):                         # 人类在场
    sub_41D546()                                      # dword_48BE18(手动视角)=0 + sub_41906A(1):
                                                       #   gameWndProc WM_PAINT 全屏重绘 + 停地图动画 dword_475110=0
    v2 = mkfRead(panel, 11)                           # sub_450441(dword_48A05C, 11) 加载 panel.mkf[11]
    loop:
        drawXxxBag(0, v2, cur)                         # a1=0 → 就地绘制到 surface 帧 (卡=帧0/道=帧1)
        blitElementFullscreen(screen, frame, 14, 130)  # 帧 blit 到屏幕 (14,130)
        id = runModal(xxxModal)                         # 0 = 取消
        if id:
            sprintf("使用%s", 名表[id])                # 卡→ showCardGet(id,text)；道→ showMessage(text,1500)
            r = effectTable[id]()                       # 效果函数；返回非0=结束，0=重选
            if r: break
else if (alive & 6) and (g_playerAiCardItem[cur] & bit): # AI: 卡&1 / 道&2
    收集可用(卡 sub_441262 / 道 遍历 itemStock) → 随机试 sub_41E69E(卡)/sub_420E9A(道) 判定==1 → 使用
```

关键语义：**面板本身不扣库存**；消耗由各效果函数内部调 `sub_441343`(卡→`cardBagRemove`) / 道具减(→`itemStock--`)。取消(返回 0)→退出循环；效果返回 0→重弹面板。

## 资源 panel.mkf[11]（实测 17 帧，帧头 {w,h,offX,offY}）

| 帧 | 尺寸 | 用途 |
|----|------|------|
| 0 | 412×180 | 卡片栏背景（自带 15 卡位框），blit @(14,130) |
| 1 | 412×180 | 道具栏背景，blit @(14,130) |
| 2..14 | ~20–40 各异 | 13 种道具图标（**道具 id → 帧 = id + 1**） |
| 15 | 33×17 | 备用（载具/装饰） |
| 16 | 1×1 | 占位 |

## 网格几何（两栏一致，5 列 × 3 行 = 15 槽）

绘制坐标为「资源内坐标」，屏幕坐标 = 资源坐标 + (14,130)：

- 列 x = `45 + 80*i`（i=0..4，>365 换行 x=45）；行 y = `33 + 56*j`（j=0..2）
- **卡片栏**：`drawText(卡名, x=45.., y=33.., align=2)`，字体 `setTextFont(20,0xFFFFFF,0x101010,3,0)`
- **道具栏**：`blitElementToCanvas(图标帧 id+1, x-16, y)` + `drawText("×N", x+34, y-10, align=1)`；
  非空槽写入 `g_itemVisibleMap[可见序号] = id`（跳过空槽）

模态命中 / 高亮用**屏幕坐标**（cardModal / itemModal）：

- 命中区 `x∈[19,419)`, `y∈[135,303)`
- `slot = 5*((y-135)/56) + (x-19)/80`
- 按下反馈 `highlightRect`：`[80*(slot%5)+20 .. +98] × [56*(slot/5)+136 .. +190]`（即 w=78 h=54）
  内容右下移 1px + 顶/左 1px `kChannelHalf` 变暗（**下沉浮雕，不是白框**；重写 `pressDown`）
- 消息：LBUTTONDOWN(0x201) 命中非空槽→高亮+记 `g_cardSelId`/`g_itemSelId`+`g_uiSoundClick`；
  LBUTTONUP(0x202)→`postModalExit(selId)`；MBUTTON(0x205)→`postModalExit(0)` 取消；
  WM_USER+1(0x401)→清 sel + `InvalidateRect`（重绘=贴回背景）

## 数据结构

### 卡片（1..30）
- `g_cardState60[60]`（0x499120）：4 玩家 × 15 槽，存 **卡 id（1..30，0=空）**；已有辅助
  `cardBagCount`(0x441262) / `cardBagLowestValue`(0x44128F) / `cardBagRemove`(0x441343) /
  `giveCardToBag`(0x4412E4) / `drawFreeCard`(0x441E12)，见 `turn_system.cpp`。
- `g_cardInfo`(0x47FDEA)：每项 8B `{u32 namePtr @ +8*id, u8 price @ +5}`；重写 `kCardNames[31]` / `kCardPrices[31]`。
- `g_propStock[30]`(0x499198)：赠卡池剩余（drawFreeCard 抽走 -1、cardBagRemove 归还 +1）。
- `showCardGet`(0x441F73)：卡片图 `data.mkf[570+id]`（165×256 @(138,200)）+ 提示框，已实现。

### 卡片效果表 `g_cardEffectFuncs` @ 0x475D5C，指针 = `dword@(0x475D5C + 4*cardId)`

| id | 名 | 效果 | id | 名 | 效果 |
|----|----|------|----|----|------|
| 1 | 均富卡 | 0x4420D8 | 16 | 夢遊卡 | 0x4441DC |
| 2 | 均貧卡 | 0x4421B4 | 17 | 陷害卡 | 0x4444BF |
| 3 | 購地卡 | 0x442325 | 18 | 復仇卡 | 0x4420D5* |
| 4 | 換地卡 | 0x442622 | 19 | 嫁禍卡 | 0x4420D5* |
| 5 | 換屋卡 | 0x442B02 | 20 | 免費卡 | 0x4420D5* |
| 6 | 轉向卡 | 0x442F4D | 21 | 免罪卡 | 0x4420D5* |
| 7 | **改建卡** | **0x44309B** ✅ | 22 | **送神符** | **0x444C45** ✅ |
| 8 | 拍賣卡 | 0x443225 | 23 | **請神符** | **0x444E1A** ✅ |
| 9 | 天使卡 | 0x4434C0 | 24 | 紅卡 | 0x444F25 |
| 10 | 惡魔卡 | 0x4436E0 | 25 | 黑卡 | 0x44503F |
| 11 | 怪獸卡 | 0x443917 | 26 | 查稅卡 | 0x4451F0 |
| 12 | 拆除卡 | 0x443B0F | 27 | 漲價卡 | 0x44542D |
| 13 | 搶奪卡 | 0x443E3D | 28 | 查封卡 | 0x445593 |
| 14 | 停留卡 | 0x443F80 | 29 | 同盟卡 | 0x445710 |
| 15 | 冬眠卡 | 0x4440EA | 30 | 烏龜卡 | **0x4458DF**† |

\* 0x4420D5 = 未实现卡占位（`return 0`；18 復仇/19 嫁禍/20 免費/21 免罪为被动卡，走惩罚/收费管线触发，不挂主动表）。
† 旧档误记烏龜卡=0x446AFB（实为道具機器娃娃效果，两表 id30 与道具表[0] 共址）；`0x475D5C+4*30=0x475DD4` 处 dword 复核真实值 = **0x4458DF**。逐卡效果/依赖/实现分档见 `441baa-card-effects.md`。

### 道具（1..13）
- `g_itemStock[60]`(0x49915C)：4 玩家 × 15 槽（**索引 = 15*player + id - 1**），每槽上限 9；
  `givePlayerItem`(0x445A4D) / `takePlayerItem` 增减（原误名 g_playerCards）。
- `g_itemNames`(0x47FEDA)：每项 8B `{u32 namePtr @ +8*id, u8 type @ +4, u8 price @ +5}`。
  id1..13 = 機器娃娃 / 路障 / 地雷 / 定時炸彈 / 機車 / 汽車 / 飛彈 / 遙控骰子 / 機器工人 / 時光機 / 傳送機 / 工程車 / 核子飛彈。
  （`kItemNames[13]` 现有表为礼物池显示用、自「路障」起，与本栏 id 索引不同 → 新增 `kItemBagNames[14]` 对齐 g_itemNames id。）
- 道具图标帧 = id + 1（帧 2..14）。

### 道具效果表 `g_itemEffectFuncs` @ 0x475DD4，指针 = `dword@(0x475DD4 + 4*itemId)`

| id | 名 | 效果 | id | 名 | 效果 |
|----|----|------|----|----|------|
| 1 | 機器娃娃 | 0x446AFB | 8 | 遙控骰子 | 0x4470F8 |
| 2 | 路障 | 0x446BAA | 9 | 機器工人 | 0x447295 |
| 3 | 地雷 | 0x446C88 | 10 | 時光機 | 0x447387 |
| 4 | 定時炸彈 | 0x446D69 | 11 | 傳送機 | 0x447428 |
| 5 | 機車 | 0x446E4A | 12 | 工程車 | 0x4479D2 |
| 6 | 汽車 | 0x446F05 | 13 | 核子飛彈 | 0x447ACE |
| 7 | 飛彈 | 0x446FBC | | | |

（路障/地雷/定時炸彈 = `createMapObject(16/17/18)` 路面道具放置，见 `40e033-map-objects.md`；
0x475DD4 本身 = 卡片表 id30 槽，两表共址紧邻，索引 id≥1 不冲突。）

## 关键串（BIG5）

| 地址 | 内容 | 用途 |
|------|------|------|
| 0x465305 | `使用%s` | useCardFlow → showCardGet 文本 |
| 0x4653E5 | `使用%s` | itemPanelFlow → showMessage 文本 |
| 0x4653E0 | `×%d` | 道具栏数量角标 |

## 改建卡 cardRebuildEffect（0x44309B）

针对**当前玩家所站格** `cellEnts[players[cur].cellEntId].special`（= 原版 `g_cellEnts+40*cellEntId+32` 的 objId）：
- objId ∈ [2000,4000) → estate（`52*(objId-2000)+g_estates`）：`level(+26)!=0` 才可；`type(+24)^=1`（住宅↔连锁店），变连锁且 `level>1`→`level=1`；成功 `cardBagRemove(cur,7)`。
- objId ∈ (4000,6000) → corp（`56*(objId-4000)+g_corps`）：`sub(+26)!=0` 才可；人类 `selectFacilityDialog(1)` 选新类型，`type=公園(0)/加油站(3)` 且 `sub>1`→`sub=1`；成功 `cardBagRemove(cur,7)`。
- 台词 `sub_44EF41(p,3,...)` `[TODO P4]`；不满足条件 → 不消耗、效果返回 0（重选）。

## 重写要点

- 原版「就地画进 mkf surface 帧 + saveBackground/highlightRect 原地改像素」→ 重写为**每帧全量重绘**（背景 blit + 文字/图标叠加 + 按住格 `pressDown` 下沉），等价。
  早期误用 `drawRectBorder` 白框当"选中高亮"，2026-09-25 修正为原版的按下下沉（`pressedSlot`，抬起复原）。
- 模态用现有 `runModal(app, handler, user)` + `events().requestExit(id)`；null event = 重绘请求（对应 WM_USER+1）。
- 效果分派：`g_cardEffectFuncs`/`g_itemEffectFuncs` 表以 `int effect(Application&)` 函数指针数组形式落地（索引 = id）。
  本次真实接入 **卡 id7 改建 / id22 送神 / id23 请神**（后两者复用 `map_objects.cpp` 现成 `useBanishGodCard`/`useInviteGodCard`，包 `bool→int`）；
  其余卡与全部道具 = `cardEffectStub`/`itemEffectStub` 返回 0（等价「不可用 → 重选」，**不消耗**）。

## 验证方式

Debug：给玩家塞入改建/送神/请神卡（`giveCardToBag`）与若干道具 →
工具条第 8（卡）/第 7（道）按钮：
- 卡片栏显示卡名网格；选请神符→当前玩家视野最近神明飞身附身（卡消耗）；无神明→重弹（不消耗）
- 选送神符→清挂身/送走负面神明（卡消耗）；正面神明/无对象→重弹（不消耗）
- 选改建卡→当前格住宅↔连锁店 / 商业设施变更（卡消耗）；不满足→重弹
- 道具栏显示图标+×N；选任一道具→showMessage「使用X」后重弹（stub 不消耗）
- 取消（右键/中键/Esc）关闭面板
对照 `Ctrl+Shift+I/H`（神符）、`Ctrl+4`（改建）走同一 `cardRebuildEffect`/`useXxxGodCard` 一致性。
