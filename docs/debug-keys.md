# 调试工具（--debug）

> [NEW] 重写新增的测试辅助功能，**无原版对应**（原版无此热键）。
> 实现：`src/app/debug_keys.cpp` / `include/game/app/debug_keys.h`；
> 开关 `GameState.debugMode`（`src/main.cpp` 解析 `--debug`）；
> 接入点 `src/app/game_loop.cpp` 的 `SDL_EVENT_KEY_DOWN` 分支。

## 启用

```powershell
./build/rich4.exe --game resources/game --debug
```

未加 `--debug` 时全部热键不生效（`debugMode == false`）。

## 键位

按住 **Ctrl** + 数字键（主键盘 `1`..`9`）：

| 键 | 功能 | 细节 |
|----|------|------|
| `Ctrl+1` | 鼠标指向地块 **owner = 当前回合玩家** | 支持地产(estate)/公司(corp)/特殊点(specPt)；同时重建小地图/大地图色块 |
| `Ctrl+2` | 指向地块 **等级 +1** | 地产 `level` / 公司 `sub`，上限 5 |
| `Ctrl+3` | 指向地块 **等级 -1** | 下限 0 |
| `Ctrl+4` | 指向地块 **改建卡效果切换** | estate：`level!=0` 时 `type^=1`（住宅↔连锁店）；corp：`sub!=0` 时弹设施选择变更 `type`（公園/加油站降为 1 级）——对齐 0x44309B |
| `Ctrl+5` | **全图住宅用地归当前回合玩家** | 遍历 estates + corps + specPts |
| `Ctrl+6` | **全图有主住宅用地 level = 1** | 仅 `owner != 0` 的地产（便于快速铺建筑测收租） |
| `Ctrl+7` | 当前回合玩家 **现金 +100000** | 直接改 `players[cur].cash` |
| `Ctrl+8` | **打印指向地块详情**（日志） | estate：名字/type/owner/level/flag/dir/priceAdd/priceBase/fees/price/expire；corp/specPt/evtCell 类似；cellEnt：type/objId/sprite/occMask（事件格定位用） |
| `Ctrl+9` | 指向地块的 **联动组归当前回合玩家** | 住宅用地 = 同街同名全部；连锁店 = 全部连锁店（对齐原版收租联动组定义）|

### Ctrl+Shift（难测机制快捷构造）

**`Ctrl+Shift+数字`：商業用地一键布置**（指向 corp；owner=当前玩家、sub=1）

| 键 | 设施 |
|----|------|
| `Ctrl+Shift+1` | 公園（type0，不收过路费） |
| `Ctrl+Shift+2` | 旅館（type1，收费走**转盘**天数 + 住宿状态） |
| `Ctrl+Shift+3` | 購物中心（type2，收费走**转盘**倍数） |
| `Ctrl+Shift+4` | 加油站（type3，载具×步数；需先 `Ctrl+Shift+V` 切载具） |
| `Ctrl+Shift+5` | 研究所（type4，owner 停留研发入口） |

**`Ctrl+Shift+字母`**

| 键 | 功能 | 细节 |
|----|------|------|
| `Ctrl+Shift+G` | **瞬移到指向地块并立即结算** | 指向道路格/任何物件 → 传送到对应 cellEnt → 直接调用 `landingEvent`（买地/升级/收租/转盘/事件一次性触发，省去走路） |
| `Ctrl+Shift+V` | **切换载具** | 步行→機車→汽車→步行；骰子数同步 `travel+1`（测试加油站/汽车/石油收费） |
| `Ctrl+Shift+T` | **循环携带神明** | 无→小財神→大財神→小窮神→大窮神→小衰神→大衰神→死神（测试神明调整租金/阻止购地/地主死神免收） |
| `Ctrl+Shift+S` | **循环玩家状态** | 无→住宿→出國→監獄→醫院→冬眠→夢遊（测试 `ownerCanCollectRent` 免收租；每回合自动倒计时结束并恢复行动） |
| `Ctrl+Shift+B` | 银行存款 +100000 | 测试收款入银行/贷款 |
| `Ctrl+Shift+J` | **当前玩家入狱 3 天** | `jailPlayer` 0x43D593：移到監獄格（綠島）/sprite/stateFlags BYTE2/`jailFlags`/FLC 538/保险理赔；**入狱后按原版 `sub_40DEFE` 机制推进回合**（`playerActionFlags\|=0x80` → 显示"坐牢中"并跳过）；FLC 播放中忽略重复触发 |
| `Ctrl+Shift+L` | **当前玩家住院 3 天** | `hospitalizePlayer` 0x43EC3F：醫院格/stateFlags BYTE3/`hospitalFlags`/FLC 524 |
| `Ctrl+Shift+K` | 买入指向 specPt 的股票 100 股 | 测试控股/经营権转移（先 `Ctrl+7` 备现金；持股超过他人 → owner 变更/小地图刷新；日志含 shares/owner/control） |
| `Ctrl+Shift+M` | 打开**股市面板** | `stockMarketDialog`（0x42B58F）；测行情/持股切换、買/賣、走势图、休市 |
| `Ctrl+Shift+D` | 过一天 | `advanceDay`（日期/到期回收/每日事件/股票行情） |
| `Ctrl+Shift+C` | 清空玩家状态+神明 | `stateFlags`/`byte54`/`state37`/`cellTableIdx` 归零 |

拾取与右键物件提示同一套：鼠标位置 → `pickMapObject`（复用渲染期 `mapHitRegions`，
含 SPR 像素掩码精确命中）。日志前缀 `debug:`。

## 行为说明

- **不受 `gamePlayerControl` 限制**：AI 回合/掷骰/移动中也可改地块（便于布置测试场景）。
- **跳伞入场期间被屏蔽**（与其他输入一致，原版跳伞阻塞语义）。
- 修改后地图与小地图立即刷新（`buildMiniMapMarks`）。
- 部分操作会写入 `RICH4_LOGI` 日志（`logs/`），`Ctrl+8` 是主要诊断手段。

## 典型测试流程

**一键测试任意地块机制**（推荐，省去走路与等待）：
1. 布置场景：`Ctrl+1`（指向地块归当前玩家）/ `Ctrl+9`（整条街）/ `Ctrl+Shift+2..5`（corp 一键设施）
2. 鼠标指向目标地块 → **`Ctrl+Shift+G`** → 立即触发结算（买地/升级/收租/转盘/事件）
3. 日志核对：`debug: Ctrl+Shift+G landingEvent -> wait=N`

**商業用地快速流程**（转盘/住宿/加油站/神明/免收）：
1. 指向無主城市地块 → `Ctrl+Shift+2`（直接建成**旅館** 1 级且归自己）
2. 轮到 AI 时指向该地块 → `Ctrl+1`（归 AI）
3. 指向该地块 → `Ctrl+Shift+G` → 立即收租：**转盘动画**（**点击左键可提前停止**）→
   「休息N天 費用N元！」→ **走进旅館**（角色移动至旅館建筑后消失）→ **住宿 N 天**
   （每回合倒计时并显示「住宿中 還剩N天」）→ **走出旅館回到道路**后恢复行动（不重复收费）
4. 加油站：先 `Ctrl+Shift+V` 切到機車/汽車 → `Ctrl+Shift+4` 建加油站 → `Ctrl+Shift+G`
5. 神明调整：`Ctrl+Shift+T` 携带小財神 → `Ctrl+Shift+G` → 租金减半提示（大財神免付/小窮神+50%/大窮神加倍）
6. 免收租：`Ctrl+Shift+S` 设地主状态（住宿/出國/監獄/醫院/冬眠/夢遊）→ `Ctrl+Shift+G` → 免收提示
7. 購物中心同 1-3（`Ctrl+Shift+3`）；公園/研究所不收费（对照验证）

**行業設施點收费 / 控股 / 股票行情**：
1. 指向地图上的行業設施點 → `Ctrl+1` 归当前玩家 → 再 `Ctrl+1`（AI 回合时）归 AI
2. `Ctrl+7` 备现金 → 指向该 specPt → `Ctrl+Shift+G` → 收费提示（航空/保險走**转盘**；
   汽車/石油需先 `Ctrl+Shift+V` 切载具）→ **认购股份**确认框 → **数字输入框**（键盘/滑块/最大/确定）
3. `Ctrl+7` + `Ctrl+Shift+K` 反复买入 → 持股超过他人 → **owner 变更**（日志 + 小地图刷新）
4. `Ctrl+Shift+D` 过天 → 日志 `stockTick: market=...`（股价随机涨跌 + 大盘指数）

**收租·整条街联动**（停留结算 `landingEvent` 0x419A67；金额 = 同街 `fees[level]` 之和 × `moneyMul`）：
1. `Ctrl+5` 全图归自己 → `Ctrl+6` 全图 level=1（建筑可见）
2. 轮到 AI 玩家时，鼠标指向目标街的任一块按 **`Ctrl+9`**（该**整条街**归 AI）；
   或逐块 `Ctrl+1`
3. 自己回合走到该街任一块 → **整条街地块先闪烁**（原版 `highlightBlink`，16 帧约 0.9 秒）
   → **再弹出**「請付N元過路費」；**付款方现金减少、地主存款（银行）增加**
   （`transferMoney` flags=0 = 收款入银行）；角色面向地块片刻后**恢复原朝向**
4. 日志核对（`estateRouteRent` 诊断）：
   ```
   routeRent: owner=2 cur=(台北市 type=0) matched=4 sum=4800 total=4800 (M=1)
   ```
   `matched` = 参与联动的地块数（整条街 4 块应 = 4）；若为 1 说明该街其他块 owner 不属于同一地主
5. 神明调整验证：AI 携带小/大財神（槽 1/2）或小/大窮神（槽 5/6）时金额变化并弹出神明提示

**收租·连锁店**（金额 = 同地主全部连锁店数 × 2000 × `moneyMul`）：
1. `Ctrl+5` 归自己 → `Ctrl+6` 铺 level=1
2. 轮到 AI 时，指向若干地块按 **`Ctrl+4`**（变连锁店；需 `level!=0`），再按 `Ctrl+9`（全部连锁店归 AI）
3. 自己走到任一连锁店 → 金额 = 连锁店数 × 2000 × M（例：3 家 = 6000），**全部连锁店闪烁**（跨街道）
4. 连锁店不可加盖（升级分支 `type!=0` 跳过）；外观为连锁店旗帜 + 地主色

> 状态机 case 3 仅在日志打印 `landing due: player ... owes ...`（原版只记欠款矩阵、不转账），
> 不再直接扣款；实际收租统一在停留结算完成。
> 联动组定义（原版收租循环）：住宅 = 同 owner + 同名 + `type==0`；连锁店 = 同 owner + `type!=0`。

**商業用地（corp）**（购地 `+34×M` / 建设施 `+34×M` / 升级 `+36×M` / 收费按 type）：
1. `Ctrl+1` 指向無主商業用地（城市名地块，如花蓮市）→ 走到 → 弹「費用N元 是否買下此地?」
2. 再走一次（自己的）→ 弹「請選擇設施類別」→ 点图标选 公園/旅館/購物中心/加油站/研究所 → 建成（sub=1）
3. 再走 → 弹「升級費用N元 是否升級?」（公園/加油站上限 1 级不可升；旅館/購物/研究所可升 5 级）
4. 他人收费：`Ctrl+1` 在 AI 回合给 AI → 自己走到：
   - 旅館：先**转盘动画**（panel.mkf[69]，指针滚动→减速→停在休息天数，约 5 秒）→
     「休息N天 費用N元！」→ 付款后**住宿 N 天**（住宿中免收租）
   - 購物中心：先**转盘动画**（panel.mkf[70]，指针停在消費倍數）→
     「您的消費金額為 NxM倍=N元」
   - 加油站：无转盘（直接按载具×步数计算）；需有载具（非步行）→
     「加油站 董事長X 請付N元加油費」（步行免付）
5. `Ctrl+4` 指向有设施的 corp → 弹设施选择变更类型（改建卡效果）

**事件格·得點券**（`landingEvent` case 10/11/12，FLC 537 + 50/30/10 點券）：
1. 鼠标指向地图上的得點券格（type 10/11/12，图块 19/21/23；`Ctrl+8` 可打印类型确认）
2. **`Ctrl+Shift+G`** → 立即结算：FLC 537（31x39 透明动画，14 帧约 1 秒，Effect.mkf[98]）→
   「得點券ＮＮ點」（1 秒）→ 玩家 `points` +50/30/10
3. 日志核对：`landingEvent: cellType 10 得點券 +50 (total N) RE 0x41982D case 10`
   （角色语音 `sub_44EF41` 待 P4；case 12 原版无语音）

**事件格·卡片**（`landingEvent` case 13，FLC 536 + 赠卡池抽卡，`sub_441E12`）：
1. 鼠标指向地图上的卡片格（type 13，图块 25；`Ctrl+8` 可打印类型确认）
2. **`Ctrl+Shift+G`** → 立即结算：FLC 536（28x40 透明动画）→ 抽卡 → 「得到ＸＸ卡！」+
   卡片图（165×256 @ (138,200)，显示 1.5 秒，音效槽 23）
3. 日志核对：`landingEvent: card cell -> 得到ＸＸ卡！` + `card get: id N -> ...`
4. 边界：卡包满 15 张 → 先舍弃价格最低的一张（`kCardPrices`）；赠卡池（`propStock`）空 → 不弹提示；
   `Ctrl+7` 可多次触发观察不同卡

**監獄/醫院**（入狱 `jailPlayer` 0x43D593 / 住院 `hospitalizePlayer` 0x43EC3F）：
1. `Ctrl+Shift+J` 入狱 → 角色消失并移到監獄格（綠島），播 FLC 538（`settings[1]` 动画过程开启时）
2. `Ctrl+Shift+D` 过天 3 次 → 日志 `player states: ... flags=00030000`（BYTE2 递减）→ 到期后
   自动出狱（`flags=00800000` → 下回合恢复占用/朝向，角色重现）
3. 收租验证：入狱期间他人踩其地 → 「ＸＸ坐牢中 免收過路費！」（`ownerCanCollectRent`）
4. 保险理赔：先踩 specPt 保險公司（转盘投保天数）→ `Ctrl+Shift+J` → 弹「保險期間 得到理賠金 N元」
   （N = 2000×M×天数，由保险公司付款；无保险则不弹不扣）
5. `Ctrl+Shift+L` 住院同理（FLC 524）；`Ctrl+Shift+S` 可辅助验证免收租状态
6. 保释验证：入狱者之外另一玩家回合 → `Ctrl+Shift+G` 传送到監獄格（type 4）→ 保释面板
   （在押者头像 + 悬停「保釋點數 30點」）→ 点选 + YES → 扣点券、格子擦除、犯人 BYTE=0x80
   （下回合走出出狱）；点券不足 → 「抱歉！你的點數不足！」浮动消息（语音 #0002）；
   NPC 格（i≥4，P5 前一般不会出现）点击同显示点数不足（占位）
7. 出院验证：`Ctrl+Shift+L` 住院 → 另一玩家 `Ctrl+Shift+G` 传送到醫院格（type 5）→ 办理出院面板
   （开场问候语音 #0127 → 两列头像格 → 确认「OK！您的朋友已经可以出院了！」→ 扣点券出院；
   右键「要保重身體喔！」告别退出；上窗偶发护士行走、下窗小像驻留微动画）
8. 场景音乐：监狱面板 track25、医院 track26，退出恢复原曲（音乐栈）

**升级/加盖**（`priceBase × moneyMul`）：
1. `Ctrl+1` 指向地块归己（或 `Ctrl+5` 全归己）
2. 走到该格 → 弹出「升級費用N元 是否升級?」→ 确认后 `level + 1`
3. `Ctrl+2/3` 可直接调等级验证各级外观与费用

**大地图/小地图填色**：
1. `Ctrl+1` 或 `Ctrl+5` 改所属
2. 工具条「地圖」按钮 → 大地图应出现玩家色块（与小地图一致）

## 已知限制

- 卡片系统完整流程（选卡 UI/卡片消耗/`useCardFlow`）留 P4；`Ctrl+4` 只提供调试直调
  （estate `type^=1` / corp `selectFacilityDialog(1)` 均已接入）。
- 大地块（`estate.type != 0`）不能通过升级分支加盖，应走**建造菜单**（任务 F，未实现）。
- 不修改 `dword_496BB4` 欠款矩阵（重写未建模，见任务 D 说明）。
- 联动高亮不含**同盟地块**（`g_playerAlly`，P4）。
- 事件格已实现得點券/卡片格/監獄·醫院状态系统；監獄·醫院门外保释/办出院 UI、新聞/命運/小游戏等尚未实现（P2 进行中）。
