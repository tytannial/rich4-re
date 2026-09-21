# M2 执行计划（Debug 工具 + 测试修复 + 收租/建造）

> 本计划为交接文档，可在新会话中直接按序执行。所有原版依据均已用 IDA 核实
> （地址、公式、常量、字段偏移）。执行前请阅读 AGENTS.md 与
> `docs/reverse/functions/gameplay-map-mechanics.md`（机制总览与验收检查表）。

## 0. 上下文与工作流

- **分支**：`feat/m2-game-logic`（M1 已合入 `main`；P0 买地闭环完成，最近提交 `e004eb5`）
- **编译**：
  ```powershell
  cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build build'
  ```
- **提交前**：`python tools/re_map.py`（校验逆向标注 + 更新 `docs/reverse/address-map.md`）
- **逆向双落**：源码 `[RE 0xXXXXXX]` 注释（`依据:` 行只写在 `.cpp`）+ `docs/reverse/functions/` 档案 + IDB 重命名/注释并保存
- **提交粒度**：每个可测功能一个 `wip:` 提交；实机验证由用户执行
- **文档检查表**：`docs/reverse/functions/gameplay-map-mechanics.md`（每完成一项打勾并记录差异）

## 1. 任务 A：`--debug` 工具集（Ctrl+1..8）

**新文件** `src/app/debug_keys.cpp`（+ `include/game/app/debug_keys.h`）。
`Application` 解析命令行 `--debug` → `GameState.debugMode`；仅该模式生效。

实现要点：
- 在 `src/app/game_loop.cpp` 的 `SDL_EVENT_KEY_DOWN` 分支（约 427 行）插入处理；`SDL_GetMouseState` 取指针坐标
- 拾取复用 `state.mapHitRegions`（与 `showObjectTip` 同款命中逻辑），id：2000+ 地产 / 4000+ 公司 / 6000+ 特殊点
- 每次操作 `RICH4_LOGI`；标注 `[NEW]`（无原版依据）

| 键 | 功能 | 实现 |
|----|------|------|
| `Ctrl+1` | 指向地块 owner = 当前回合玩家 | `estates[i].owner = currentPlayer+1` + `buildMiniMapMarks` |
| `Ctrl+2` | level+1（≤5） | `level = min(level+1, 5)` |
| `Ctrl+3` | level-1（≥0） | `level = max(level-1, 0)` |
| `Ctrl+4` | 设为连锁店 | **待任务 F3 探明后接入（先留空）** |
| `Ctrl+5` | 全图地产归当前玩家 | 遍历 `estates` |
| `Ctrl+6` | 全图地产 level=1 | 遍历 |
| `Ctrl+7` | 当前玩家现金 +100000 | `players[p].cash += 100000` |
| `Ctrl+8` | 打印指向地块详情 | 日志：objId/type(+24)/owner(+25)/level(+26)/flag(+23)/priceAdd(+28)/priceBase(+30) |

**验收**：`--debug` 启动后各键生效；无 `--debug` 时不响应。

## 2. 任务 B：大地图填色修复

- **根因**：`src/app/map_dialog.cpp:23` 底图用 `state.miniMapRaw.frame(1)`（未填色原图）；小地图正常（用 `miniMapBuffer`）
- **原版**：`rebuildMiniMap`(0x40A4E1) 双分支——a1=0 小地图（5696 缩放）/ a1=1 大地图（11392 缩放）
- **修复**：
  1. `GameState` 增 `bigMapBuffer`（同 `miniMapRaw` 帧 1 尺寸，400x400）
  2. `buildMiniMapMarks`（`src/app/map_render.cpp`）同时重建两份
  3. `map_dialog.cpp` 改用 `bigMapBuffer`
- **验收**：大地图出现玩家色块（与小地图一致）

## 3. 任务 C：买地面板背景（提示框）

- **原版**：`askDialog`(0x440BA8) 先画 `g_tipFrame + 72`（帧 5，`sub_456418(dst, g_tipFrame+72, 220, 140)`）再画文本
- **待查**：`g_tipFrame`(0x48BAD8) 在 `loadMapData`(0x40808F) 的加载资源索引（mkf + index）
- **修复**：`GameState` 增 `tipFrame` + 加载；`confirmDialog(text)` 先 blit `frame(5)` 于 (220,140) 再画文本
- **验收**：询问框有宝石装饰背景（对齐原版截图）

## 4. 任务 D：收租（P1 核心）

### D1 状态机 case 3 重做（`src/app/turn_system.cpp` `updateGameState`）

原版 `playerActionStateMachine` case 3（0x40DA4B）：
- `byte_4749E0`（重写 `stepDwell`）等待递减；`byte_498EA3`（动画帧）非 0 时先播动画
- 读 `cellEnt+32` objId：2000..4000 地产 / 4000..6000 公司
- `owner != 0 && owner != currentPlayer+1` → **`sub_40DF69(cur, owner-1, 30 * g_moneyMul)`**
- 之后：`g_playerRestoreDir = dir`、面向目标格（`sub_454FB4`）、后续动画

### D2 `landAfterMove`（0x40F381）实现

- 调用点：`nextPlayer`(0x418EBD @0x418F59) + 买地路径(0x41B086)
- 守卫：`stateFlags(player) & 0xFF` 非 0 或未存活 → 返回
- 按 `byte_496BA7[player]`（重写 `cellTableIdx`，携带物件槽）：
  - **槽 9**：公司相关提示（`showMessage(off_47ED9A, 1500)` + `sub_40B110`）
  - **槽 12**：完整流程——地产/公司 owner 检查；他人 → `sub_40DF69(owner-1, player, g_moneyMul * word[+28] * (level + 2.0f) / 5.0f)`（公司用 `corp+34`）；无主 → `owner = player+1`（自动购地）；`g_cfgLandPerm` 补到期日；`rebuildMiniMap(0)`；随后 `showMessage(byte_4634F2, 1500)` + `sub_44EF41`（角色语音）
  - **槽 10**：地产/公司 `level != 0` → `sub_40DF69(owner-1, player, 30 * g_moneyMul)` → `showMessage(byte_4634D7, 1500)` + `sub_40AB4A` + FLC 526 + 音效 `dword_4823DA`
  - 其他槽 → 无
- 常量：`flt_46350C = 2.0f`、`flt_463510 = 5.0f`（0x46350C/0x463510）

### D3 关键说明

- **`sub_40DF69` 只记账**（`dword_496BB4[26*a1+a2]` 玩家间往来账，clamp 0），**不直接扣钱**；真正转账是 `sub_41D2C6`（重写 `transferMoney`）——实现时需确认原版何时结算记账（可能回合末/过天），**列为实现时的分析点**
- 提示文本 `byte_4634D7` / `byte_4634F2` 用 `get_bytes` 解码（BIG5；注意 `get_string` 会在 `\n` 截断）

**验收**：`Ctrl+5/6` 批量铺地后，走到他人有建筑地块扣款 + 提示；公司同理。

## 5. 任务 E：升级/加盖

- **原版**：`sub_41982D` case 0 自己地产分支（0x419911..0x419A52）：
  - 守卫：`level < 5`、`type == 0`、`state37 == 0`
  - 费用 `word[estate+30] * g_moneyMul`；现金检查 → `askDialog`（格式串约 0x41995C）→ `level++` → 音效/动画
  - 用户提示"可以选择分支建设"——实现时确认升级是否含建筑选择（可能仅大地块 `type != 0` 走建造菜单）
- **验收**：`Ctrl+6` 铺 level=1 后停留自己的地可升级；`Ctrl+2/3` 调整后复测

## 6. 任务 F：建造菜单 + 大地块 + 连锁店

### F1 建造菜单（结构已探明）

| 原版 | 作用 |
|------|------|
| `sub_423B3B(player, tab)` | 5 页签筛选：0=我的地产+公司 / 1=我的地产 / 2=我的公司 / **3=普通地块（owner=me && level≠0 && type==0）** / **4=大地块（owner=me && level≠0 && type≠0）** |
| `sub_4225A3(player, dir)` | 列表绘制（地产名/建筑名/费用 `g_moneyMul × (word[+28] + word[+30] × level)`/到期日），5 个 case 对应页签 |
| `sub_423CF3` | 外层对话框（页签/翻页/确认）；**入口待查**（疑似工具栏"锤子"按钮）|

实现：新文件 `src/app/build_menu.cpp`（参照 `ai_dialog.cpp` 对话框模式）。
**验收**：菜单 5 页签正确筛选 → 选中大地块建造特殊建筑（公園/旅館/購物中心/加油站/研究所）→ 地图显示对应图块。

### F2 大地块购买诊断

- **判定字段**：`estate.type`（+24）≠ 0（普通地块 = 0）
- 购买路径同普通地块（`loc_41A013` 无 type 检查）→ "无法购买"用 **`Ctrl+8`** 打印现场诊断
- 可能修复点：重写 `landingEvent` 的 objId 范围 / 价格计算 / `estates.size()` 边界

### F3 连锁店（改建卡）

- 用户描述：**小空地（level 0）使用改建卡 → 连锁店；过路费 = 数量 × 2000**
- 探索线索：卡片系统（`givePlayerCard` 0x445A4D、`sub_445ADA` 抽卡、卡片名表 `off_47FEDA`）；`estate.flag`(+23) 或 `estate.type`(+24) 取值；过路费公式中的"数量×2000"
- 结论回填文档 + IDB；随后接入 `Ctrl+4`

## 7. 关键原版依据速查

| 项 | 地址/值 |
|----|---------|
| 买地流程 | `sub_41982D` case 0 → `loc_41A013`；价格 `(level*priceBase + priceAdd) * moneyMul`；返回 0x88（重写用 `kLandingWait=0x90`）|
| 询问框 | `askDialog` 0x440BA8（文本 (220,140) 字体16/0xF0F0F0/0x101010/阴影+粗体 + `yesNoDialog(220,320)`）；`yesNoDialog` 0x453A32（data.mkf[440]）|
| 询问文本 | 0x4639E1（BIG5）`"%s\n\n費用:%d元\n\n是否買下此地?"` |
| 现金不足 | `showMessage(byte_46398B="現金不夠支付！", 1500)` |
| 购地音效 | `g_effectSlots[17]`（0x4823D2）|
| 格子音效表 | `g_cellTypeSound` 0x475299（17 项）|
| estate 绘制 | 0x4091DF：`level==0` 有主 → `pieceTiles`(map.mkf[25]) 帧=角色 ID、**不改调色板**；`level>0` → `dword_48AE48[level]`（type==0）/ `estateFlag`（type≠0），owner 色 |
| 调色板 | `g_drawListPal`(0x48A852)：0xFF=不改 / 0=黑 / 1..8=`g_playerColor[owner-1]`（u32 RGB888 → `rgb888To555`）|
| 收租（移动结束） | case 3：`30 * g_moneyMul` |
| 收租（落地后） | `landAfterMove` 0x40F381；公式 `g_moneyMul * word[+28] * (level + 2.0f) / 5.0f`；提示 `byte_4634D7`/`byte_4634F2`；FLC 526；音效 `dword_4823DA` |
| 记账 | `sub_40DF69` → `dword_496BB4[26*a1+a2]`（不直接扣钱）|
| 转账 | `sub_41D2C6`（重写 `transferMoney`）|
| 建造费用 | `g_moneyMul * (word[estate+28] + word[estate+30] * level)` |
| 大地图 | `rebuildMiniMap` a1=0 小(5696) / a1=1 大(11392) |
| 吐槽 | `estateChainSpeech` 0x44F627（同名地产≥3 → `sub_44EF41`）|
| 神明阻止 | `checkCarriedGod` 0x40FA61（槽 7/8/15）|

## 8. 已知差异清单（P0 遗留，随 D/E/F 一并处理）

- AI 买地决策 `sub_41D7D4`（未实现，AI 不买地）
- 现金不足/神明阻止的提示文本未显示
- 询问框缺 `g_tipFrame` 装饰背景（任务 C）
- 落地后处理 `sub_40F381`/`sub_448A7E`、连锁奖励 `sub_44101D`（任务 D）
- 事件格 case 2..16（P2，未开始）

## 9. 执行顺序与待确认

**建议顺序**：A（debug，测试必需）→ B → C → D（收租）→ E → F

**待确认**：
1. 优先级是否按上述顺序？
2. 建造菜单入口若是工具栏"锤子"按钮，是否同批实现按钮与菜单？
3. `Ctrl+4`（连锁店）在 F3 探明前留空，可接受？
