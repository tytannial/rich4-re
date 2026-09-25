# 土地公開拍賣（`runAuction` 0x43BDE5 + `auctionWndProc` 0x43A2DD + `auctionAiMaxBid` 0x439F0D）

> 2026-09-26 专项逆向（自 `43380a-magic-house.md` 拆分）。IDB 已重命名：
> `runAuction/auctionWndProc/auctionDrawRow/auctionAiMaxBid` + `g_auction*` 全局表。
> 重写：`src/app/auction_dialog.cpp`（✅ 2026-09-26 实现，待实机验收）。

## 0. 调用者与资源

| 调用者 | 参数 | 价款去向 |
|---|---|---|
| 魔法屋惩罚 11「拍賣當格土地」（`applyMagicPenalty` 0x431CAA case 11，0x4324D5） | `runAuction(被罚玩家, objId, 1)` | seller=被罚玩家 → 入其银行 |
| 新聞 idx 7「公開拍賣公有土地一處」（`evt07` 阶段 1，0x4498A1） | `runAuction(-1, objId, 1)` | to==-1 → `g_publicFund` 公库 |
| （卡片拍賣卡 0x443225 / 破产清算 `sub_40CD87` 族） | 待查 | — |

资源：`panel.mkf[26]`（184 帧，头 2220B）+ 每角色 3 组头像 SPR `panel[27/28/29 + 3*charIndex]`
（正常/翻滚/悲伤表情）；表情帧 `panel[26] 帧 78+charIndex`；音效：出价 `g_auctionBidSound
0x475BC2={63}`、成交 `g_auctionDoneSounds 0x475BBA={29,63}`；音乐场景 5（a3≠0 时压栈）。

## 1. `runAuction(seller, objId, playMusic)` 流程

1. **标的与起拍价**：estate(2000..4000)：base=+28、corp(4000..6000)：base=+34；
   `price = g_moneyMul × (int)(base × (1 + level × 0.5))`（+26=level，flt_4650B0=0.5）。
2. **缩略图帧** `dword_48C494`（panel[26] 帧号，画 @(232,180)）：
   - estate：无主=90；有主无建筑=`91+charIndex`；有建筑=`29 + 5×word_4991B8 + level`
     （word_4991B8=地图索引 0..3，newGameInit 设置）；有建筑且有主
     （word_4991B6≠0 分支）=`115 + 5×map + level`；type≠0 特殊=`131+map`。
   - corp：无主=103；`g_auctionCorpThumbA[map] 0x475BD2={134,167,151,51} + 5×(type-1) + level`；
     type3=`g_auctionCorpThumbB[map] 0x475BE2={150,183,167,62}`；type4=151；
     word_4991B6==0 时 type0→151、其余→51。
   - `word_4991B6` = 地图模式开关（newGameDialog 设置，**待核语义**）。
3. **静态画面**（panel[26]）：帧0 全屏背景；帧25(154×352)@(123,24) 左列罩；帧17(109×388)@(67,63)
   右列罩；缩略图；「標價：」(0x4650A6) @(182,242)；`%d元` 现价 @(272,262) 右对齐 18 号白字；
   4 行（y=80+120i）：存活玩家头像 SPR（`3*char+27` 正常/`+28` 翻滚/`+29` 表情）@(590,y)、
   现金 `%d` @(620,y+14) 右对齐；出局者（状态 1..7）画状态文本 `g_auctionStatusText 0x475B34`
   （1住宿中/2消失中/3坐牢中/4住院中/5冬眠中/6夢遊中/7賣方）+ 表情 SPR、出列；现金≤现价 → 状态8
   （画现金）。`dword_48C438[i] = auctionAiMaxBid(p+1, objId)` 预算（仅 AI/托管）。
   首个正常行画表情帧 `78+charIndex`（原版一次性 quirk）。
4. `floatMsgSetup(帧2=244×100, 410, 60)`；`audioRegisterEffects(g_auctionDoneSounds)`；
   a3 → `musicPlayScene(5)`；`runModal(auctionWndProc, 可出价人数)`。
5. 返回赢家玩家号（-1=流标）：`refreshGameUi(标的坐标, 4)`（视口对准，mode4 附加未建模）→ 1s →
   产权转移（owner=赢家+1；**空地+地权设置** `g_cfgLandPerm` → `到期日 = 打包日期+g_landPermDays[cfg]`，
   estate+48/corp+52）→ `rebuildMiniMap(0)` → 全量重绘 → 1s →
   `transferMoney(赢家, seller, 现价, 0)`（买家现金优先→存款；seller=-1 入公库）。

## 2. `auctionAiMaxBid(p+1, objId)` 0x439F0D —— AI 意愿价

```
v20 = rand()/32767.0 × 0.3 + 0.5                    // ∈[0.5,0.8]（dbl_465018/20 是 double！IDA float 显示误读）
estate: 同地段自有数 v3 = count(owner==p && 名称strcmp 相同)   // +4 名称
        空地比 = v2/g_estateCount；cap = (rand×1.5258789e-05 + 3.0) × base×M   // 3.0~3.5×基价
        bid = M × price × (v3 + level>>1 + 1) × (6.0 − 空地比×4.0) × v20
corp:   同式，v3=0，base=+34
return min(bid, cash)
```

## 3. `auctionWndProc` 状态机（`byte_48C4AC`，100ms）

`dword_48C4A4` 位域：b0=当前 bidder slot；(>>4)&3=AI 气泡子相（0=价格文本/1=「ＰＡＳＳ」/2=「放棄」）；
b1=翻滚帧计数；b3=赢家翻滚计数。`word_48C434[5*slot]`=玩家号+1（0=出列）、`word_48C436`=不再竞争
标记（初始 1..8 出局原因）、`dword_48C4A8`=领先者 slot（-1 无人）。

| 状态 | 动作 |
|---|---|
| 0x401 | 清零；`byte_48C4B1 = lParam`（可出价人数）；timer 100ms；Post 0x405 |
| 0x405 | #0131「公開拍賣土地一處。」→ 状态1 |
| 1→2 | #0132「底價%d元\n請意者出價。」；首个有效行设为当前 bidder |
| 2 | 轮到 slot（跳过出列者）：人类 → 状态3；AI → 状态4 |
| 3（人类） | 画 7 按钮帧 `2i+4`（正常 94×46）@(406, g_auctionBtnY[i]=133+48i)；`setPauseDraw(1)`。悬停（WM_MOUSEMOVE，x∈[363,449)）→ 帧 `2i+3`（按下 87×39）+ `byte_48C4AF=i+1`；**LBUTTONUP** → 恢复 `2i+4` + `PostMessage(0x407, i)`（wParam=0..6）。现金<现价 → 自动 0x407 wParam=6 |
| 4（AI） | 现金<现价 → wParam=6；否则按与预算 dword_48C438 差额选档：price+10000≤cap→5、+5000→4、+1000→3、+500→2、+100→1、否则 0；若领先者存在：档位不得超 `领先者现金+500 − price` 所在档；**单人（byte_48C4B1==1）时 wParam>0 强制=1** → 0x407 |
| 0x407 | 音效 {63}。wParam=0 → `word_48C436[slot]=1` + PASS 气泡(0x110)；=1..5 → `price += g_auctionBidSteps[1..5]={100,500,1000,5000,10000}`（超现金则不执行）→ 更新左下「標價」+该行橙色价 + `dword_48C4A8=slot` + 全行清"不再竞争" + 翻滚启动(b1=1)；=6 → 头像换悲伤 SPR + 出列 + 放棄气泡(0x120) |
| 5（轮转） | 停翻滚（恢复行）。全部出列 → #0148「無人出價，宣佈流標。」→ 11；**v32==1 或 v32−v33==1 且有领先者** → 9 落槌；否则下一 slot（重画表情帧+头像）→ 2 |
| 9 | 帧21(199×349)@(124,27) 左列覆盖 + 音效{29} + `byte_48C4B0=1` + 赢家翻滚启动(b3=1) → #0135「%d元成交」→ 10 |
| 10 | 帧24@(124,27) 再覆盖 + 帧25/17 重画左右列 + 赢家表情 SPR 翻滚（b3 0..60 mod SPR帧数）+ 表情小动画（帧22/23 @(183,65) 随机）+ 法槌闪烁（帧{26,27,26} 双车道 @(163,45)/(127,90)）→ `floatMsgShow(g_auctionWinLines[charIndex])`「#0136..#0147 恭喜X購得此地！」→ 11 |
| 11 | `setPauseDraw(0)` + KillTimer + `postModalExit(领先者玩家号)` |

- 按钮表 `g_auctionBidSteps` 0x475BA2：**[0] 未用、[1..5]=100/500/1000/5000/10000**；wParam=6=放弃
  （帧 16「Give up」）、wParam=0=PASS（帧 4 第 1 按钮）。7 按钮 = PASS/+100/+500/+1000/+5000/+10000/Give up。
- 头像 SPR 三组：`3*char+27` 正常 / `+28` 翻滚（出价动画，帧数=SPR 头 +4 字段）/ `+29` 悲伤（放弃）。

## 4. 文本/音效汇总

| 地址 | 内容 |
|---|---|
| 0x465038 | #0131「公開拍賣土地一處。」 |
| 0x46507D | #0132「底價%d元\n請意者出價。」 |
| 0x465098 | #0135「%d元成交」 |
| 0x465063 | #0148「無人出價，宣佈流標。」 |
| 0x4650A6 | 「標價：」 |
| 0x465055/5E | 「ＰＡＳＳ」/「放棄」（AI 气泡） |
| 0x475B54 | `g_auctionWinLines[12]`：#0136..#0147 恭喜約翰喬/沙隆巴斯/忍太郎/錢夫人/阿土伯/莎拉公主/宮本寶藏/糖糖/烏咪/孫小美/小丹尼/金貝貝購得此地！（表长 48B=12 指针，按 charIndex 索引；**勿只读 8 项**——12 角色） |
| 0x475B34 | `g_auctionStatusText[1..8]`（住宿中/消失中/坐牢中/住院中/冬眠中/夢遊中/賣方/现金不足画现金） |
| 0x475BC2 | {63} 出价音 |
| 0x475BBA | {29,63} 成交音 |
| 0x475B84 | 按钮 y 表 {133,181,229,277,325,373,421}（step 48） |

## 5. 踩坑记录

1. **dbl_465018/465020**：IDA 以 float 上下文显示 4.17e-08/0.0，实为 **double 0.3/0.5**
   （disasm `fmul ds:dbl_465018` 8 字节操作数）。FPU 常量必须按 disasm 操作数宽度读。
2. **拍卖按钮 wParam = 悬停索引原值（0..6）**，不是 `af` 自减后的错觉——`byte_48C4AF` 存 i+1、
   LBUTTONUP 先 `--` 再 Post（=i）。7 按钮=PASS/5 档加价/Give up；`g_auctionBidSteps[1..5]` 才对齐
   （[0]/[6]/[7] 是相邻表垃圾）。
3. **PASS≠出列**：`word_48C436=1` 仅"不再竞争"计数（状态5 判定 v32−v33==1 落槌），玩家仍在轮转
   名单；放弃（wParam=6）才 `word_48C434=0` 出列。
4. **成交台词表 12 角色**：`g_auctionWinLines` 表长 = off_475B54..0x475B84 = 48B = **12 指针**
   （#0136..#0147，含 4 隐藏角色烏咪/孫小美/小丹尼/金貝貝）；dump 指针表必须读到下一符号地址为止。
5. **mkf list 列 ≠ 帧数**：panel[26] "2220" = 头长度（12×185 → 184 帧）；smp.py 导出编号与帧头
   编号一致（已用尺寸核对）。
6. **赢家表情 blit 原版笔误**：`byte_48C4B0` 分支擦除/绘制传 `Rect.right/bottom=(223,95)`，
   应为 `(183,65)`（同医院复位帧 bug 类）；重写按 (183,65) 修正。

## 6. 重写映射（✅ 2026-09-26 实现，待实机验收）

| 原版 | 重写 |
|---|---|
| `runAuction` 0x43BDE5 | `src/app/auction_dialog.cpp` `runAuction` |
| `auctionWndProc` 0x43A2DD | `auctionHandler`（runModal 模态，tick 化状态机） |
| `auctionDrawRow` 0x43A155 | `drawRow` |
| `auctionAiMaxBid` 0x439F0D | `auctionAiMaxBid` |
| `sub_41D2C6` 转账 | `transferMoney`（economy.cpp，已有） |
| `sub_41906A` 强制重绘 | `renderGameFrame+renderFrame` |

## 7. 验收清单

- [ ] 起拍价公式（estate/corp × (1+level×0.5) × M）与缩略图帧（4 地图 × 2 模式）
- [ ] 人类：7 按钮悬停/按下态、PASS/5 档加价/放弃、现金不足自动放弃
- [ ] AI：预算分档、领先者现金+500 封顶、单人强制最低加价、出价头像翻滚+气泡
- [ ] 落槌：帧21/24/25/17 覆盖序列、{29} 音、#0135、赢家表情翻滚、12 角色台词 #0136..#0147
- [ ] 成交链：视口对准 → 产权转移（地权期限）→ 小地图 → `transferMoney`（魔法屋=收款给被罚玩家；
      新闻 idx7 → 公库）；流标 #0148
- [ ] 新闻 idx 7 激活（`Ctrl+Shift+A` 多次抽到）；魔法屋惩罚 11（`Ctrl+Shift+0`）
