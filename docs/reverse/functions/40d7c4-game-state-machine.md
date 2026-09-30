# 游戏内回合/移动状态机（0x40D7C4）与回合推进

## 总览

进入游戏后（`enterGameLoop` 0x401981 压入 `sub_417E26`），WinMain 内层循环
（`0x401B9C`）每帧执行：

```
while (!PeekMessageA(...)) {
    if (byte_46CB01) {                       // 游戏内
        if (dword_475110) sub_4192F7();      // 脏区刷新/渲染
        if (byte_46CAFA)  sub_40D7C4();      // ★ 回合/移动状态机
        if (当前玩家状态 == 0 && byte_498EA0 < 0) {
            byte_498EA0 &= 0x7F;
            sub_418C55();                    // 回合开始
        }
        if (g_sceneRequest) { ... }          // 场景切换
    }
}
```

## 每玩家行动字段（按 52 字节步进，索引 = 玩家号）

| 字段 | 地址 | 语义 |
|------|------|------|
| `byte_498EA0` | 0x498EA0 | 行动标志位（bit7=待开始回合，0x30=状态效果，0x0F=动画组） |
| `byte_498EA1` | 0x498EA1 | 移动动画组（0=普通 1=特殊/载具） |
| `byte_498EA2` | 0x498EA2 | 行动状态 0/1/2/3 |
| `byte_498EA3` | 0x498EA3 | 移动动画帧/已走步数 |
| `byte_498EA5` | 0x498EA5 | 状态 0 倒计时（负数 = 直接下一位） |
| `word_496B76` | 0x496B76 | 上一步所在 cellEnt（移动时排除回头路） |
| `g_playerCellEntId` | 0x496B74 | 当前所在 cellEnt |
| `byte_496B7A` | 0x496B7A | 骰子个数 |

全局：`byte_46CAFB`（状态机激活）、`byte_46CAFA`（本帧运行标志，入口置 0）、
`dword_48BAF8`（剩余步数 = 骰子点数）、`byte_48BB00`（待处理落地事件）。

## `sub_40D7C4` 状态机（0x40D7C4）

`switch (byte_498EA2[当前玩家])`：

- **case 0（回合收尾倒计时）**：`byte_498EA5` 递减；减到 `(v & 0x7F) == 0` 后：
  - `v >= 0` → `sub_418E7F()`（重算，普通玩家）或置 1（`v0`，玩家 8）
  - `v < 0` → `byte_46CAFB = 0; sub_418EBD()`（下一位玩家）
- **case 1（移动中）**：
  - `dword_48BAF8 == 0` → 初始化收尾：`byte_498EA2=0; byte_498EA3=0; byte_498EA5=5`
  - `byte_48BB00` → `onPlayerActionPhase()`（落地事件），清标志
  - `dword_48BAF8 && sub_40C05C()` → 走一格成功：`byte_48BB00=1; --dword_48BAF8`
  - `sub_416E6D(1)`（小地图/面板刷新）
- **case 2（掷骰动画）**：`byte_498EA3` 帧计数达到骰子资源帧数 →
  `dword_48BAF8 = sub_419572()`（点数）、`dword_4749D4` 设移动音效、`byte_498EA2=1`
- **case 3（特殊移动）**：`byte_4749E0` 停留帧；按 `dword_498ECC`（行走动画资源帧数）
  逐步移动，`sub_40B066` 查目标物件坐标，`byte_498EA2=3` 流程用于遥控骰子/传送等

## 回合切换 `sub_418EBD`（0x418EBD）

- `dword_48BE18 = 0`（取消手动视角）
- 玩家 8 / 有状态 0x30 → 走特殊分支（`sub_41906A` / `sub_40F381` / `sub_448A7E`）
- 否则循环 `++g_currentPlayer`：越过 `g_playerCount` 回绕（置 `v1=1` 表示过天）；
  跳过 `!alive && spriteX` 的玩家；事件槽 4..8 检查 `byte_498DF2[16*i]`（=槽记录 `busy`，同一字节；
  busy==0 才进槽——在押 NPC 不产生回合；见 `498df0-event-slot-npc.md` §2.1）
- `v1` → `advanceDay()`（过天）
- `sub_41C84F(当前玩家)`（状态效果倒计时）
- `v1 && byte_49715C` → `saveGameToSlot(0)`（自动存档）
- 末尾 `byte_498EA0[52*玩家] |= 0x80`（标记新回合开始）

## 回合开始 `sub_418C55`（0x418C55）

`sub_40C912(0)` 返回行动类型：

- `0` → `sub_418E7F()`（`byte_498EA5 = sub_41982D(当前格)` 落地事件）+ `byte_46CAFB=1`
- `1` → 等待玩家操作（`sub_4196F1` + `setPauseDraw(1)`）
- `2/5` → `sub_40DD1F()`（AI/自动移动）

`sub_40C912` 内：有状态（`dword_496B9A`/`byte_496B9E`）且 `alive&0x30` → `sub_40DD1F()`；
`g_playerState37` → `sub_40DD1F(); return -1`。

### 跳伞入场（`dword_475114`）

待入场玩家（`dword_475114`）→ `sub_45144F(资源, 0, 40, flags=1)` **阻塞播放**跳伞 FLC →
落地初始化（`g_playerSpriteX/Y = cellEnt 坐标`、`g_playerAlive = g_playerKind`、
`loadWalkResources`、`dword_475114 = 0`）。

**不可中断**：`flcOpen`（0x450CED）中 `g_flcInterruptible = flags & 2`；跳伞 `flags=1` → 0，
播放期间 `PeekMessage` 只取走消息不派发 → 落地初始化完成前无任何交互
（骰子 `flags=0x1E000001` 同样不可中断）。

### 交互锁定（`g_playerControl` 0x46CAFD）

`enablePlayerControl`(0x4196F1) / `disablePlayerControl`(0x419703) 控制 `byte_46CAFD`；
`gameWndProc` 中多处 `cmp byte_46CAFD, 0 → jz 出口` 锁定交互
（0x4186CB/0x41889D/0x418910/0x418B93/0x418151）：

- **人类回合等待输入** → `enablePlayerControl`（=1，可交互）
- **按 GO / 开始移动** → `disablePlayerControl`（=0）→ **掷骰、移动、结算全程锁定**
- **AI 回合**：`beginPlayerTurn` 未 enable → 保持 0 → 锁定
- **例外**：地图区物件提示（0x4186BE）在 `byte_46CAFD` 检查**之前**，锁定期间仍可按住查看

重写：`gameEventHandler` 在 `!gamePlayerControl` 期间消费鼠标移动/抬起/右键
（左键按下仅保留地图区物件提示）；跳伞期间（`pendingSpawnPlayer/parachuteActive`）全屏蔽。

**重写差异（M4-B 2026-09-30 收口）**：原版 `byte_46CAFD` 的生命周期 =「进入 `beginPlayerTurn`
即 0（0x418C55）→ 人类等待输入才 1（0x4196F1）→ 按 GO/交棒回 0」；重写此前只在工具条收尾按
「`playerActionState==0 && sceneRequest==0`」置 true，且 `beginPlayerTurn`/`nextPlayer` 从不置 0
→ **一旦置 true 就跨回合、跨演出保持**，表现为「GO 面板出现在認輸演出/事件动画上」。
现补：`beginPlayerTurn` 起手、`nextPlayer` 起手、`surrenderPlayer` 起手显式
`disablePlayerControl(app)`，`topBarFinish` 的 `canResume` 追加 `!blockingPerf(app)`。
详见 `docs/reverse/ui-controls.md` §39 ⑤ 与 `docs/m4-plan.md` §14（含修前/修后实测）。

## 开始移动 `sub_40DD1F`（0x40DD1F）

- 人类（<4）无状态效果 → `byte_498EA2 = 2`（进入掷骰）
- 人类有 `byte_496BA0` → `byte_498EA5=2` 跳过；`byte_496BA1` → `dword_48BAF8=1, state=1`
- AI（4..7）→ `dword_48BAF8 = rand()%9+2`，`state=1`
- 玩家 8 → `dword_48BAF8=9, state=1`
- 末尾 `byte_498EA3=0; byte_46CAFB=1`

## 掷骰 `sub_419572`（0x419572）

- `v2 = byte_496B7A[当前玩家]`（骰子个数）
- `v14[i] = rand() % 6 + 1`（i < v2），返回 `sum(v14)`
- 绘制骰子精灵到 `(v7, v8)`（朝向 + 地图旋转），`sub_45285E(500)` 延时
- **音效**（掷骰共 4 次）：
  1. 角色扔骰动画（`case2` 帧递增到 `perDir`）——**无音效**
  2. `sub_450CDA(&dword_48235A,0)` 预设 FLC 触发音效；`sub_450F04` 播到 `dword_48C850=0x1E`
     （第 30 帧）→ 播**槽 2**（`dword_48235A`，落地①）
  3. `sub_45144F` 播放结束（`0x41962F`）→ 再播**槽 2**（落地②）
  4. 返回后转移动前：播载具槽 `dword_4749D4`（特殊组 15 / 否则 `(载具&3)+11`）+ **槽 3**
     （`dword_482362`，当前玩家<4 且 `g_playerCellNo` 非 0）

## 走一格 `sub_40C05C`（0x40C05C）

普通玩家（<4）：

1. `dword_4749DC == 0`（开始新一步）：
   - 收集当前 cellEnt 的 `exits[4]` 中非来向（`word_496B76`）、未被占用的出口
   - 随机选下一格 `v10`；`word_496B76 = 当前格; g_playerCellEntId = v10`
   - 占用掩码 `256 << 玩家`：旧格清、新格置
   - 起点 = 当前格坐标，终点 = 新格坐标
   - 帧数：有状态/`byte_498EA1` → `dist * 0.125`（`flt_4631DC`）；
     否则 `dist / byte_4749D8[travel & 3]`（步行 8 / 机车 12 / 汽车 16）
   - `flt_498EA8/EAC = 当前位置`，`flt_48BAEC/F0 = 每帧位移`
2. `--dword_4749DC`；`<= 0` → 位置 = 终点，返回 1；否则浮点累加更新
   `g_playerSpriteX/Y`
3. **选路后立即** `byte_496B78 = sub_407A8C(来向, 当前)`（移动中即朝本步方向，避免转角滞后）；
   到达后 `byte_498EA3` 帧计数回绕
4. 行走动画组切换（`sub_40B93B`）：仅**组切换**时重置 `byte_498EA3`（同组跨格连续）；
   移动音效同槽替换（`sub_4542E9` 停旧 + `sub_4542CE` 播新，槽 11-15）；
   **水上/渡水（cellEnt+39 bit31=1）→ 槽 15**（`dword_4749D4 = 15`），否则 `travel&3`+11

`g_playerSpriteX/Y` 是玩家逻辑像素坐标；`sub_40829D` 每帧等距投影绘制棋子，
动画帧 = `byte_498EA3 + 方向 * 帧数`（`dword_498EB4` 行走资源）。

## 落地事件（M2）

- `sub_41982D`（0x41982D）：按 cellEnt 物件 ID（2000+ 住宅用地 / 4000+ 商業用地 / 6000+ 行業設施點）
  与 `cellEnt+36` 的格子类型（1..18）处理购地/升级/收租/税/卡片/机会等
- `onPlayerActionPhase`（0x41B42D）：格子类型 11..18 的落地事件分发（161 基本块）

## 重写现状（M1）

- 已实现（`src/app/turn_system.cpp`）：`updateGameState`（状态 0/1/2）、
  `nextPlayerTurn`、`advanceDay`、`beginPlayerTurn`、`startPlayerMove`、
  `rollDice`、`moveOneStep`
- 差异：
  - `beginPlayerTurn` 仅区分人类/AI，未接入 `sub_40C912` 状态效果与 `sub_41982D` 落地事件
  - `nextPlayerTurn` 事件槽 4..7：**已实现 ✅ 2026-09-26**（依次进槽、busy!=0 跳过；全在押回绕过天；
    见 `498df0-event-slot-npc.md`）——旧注"本作 M1 无流氓/间谍等 NPC"已过时
  - `advanceDay` 未接入 `checkVictory`（0x41D89E，M3）；已接入音乐切换计时器
    `byte_46CB06`（低 4 位递减到 0 → `sub_454D91(0)` 切下一首）
  - 骰子已实现三阶段（角色扔骰 → 骰子滚动 FLC → 点数停留 500ms）+ 落地(槽2×2)/载具槽/槽3 音效；
    FLC 越界用 `m_frameCount` 封顶（原版按帧数停）
  - 状态 3（遥控骰子/传送）待接入；行走资源加载（`sub_40B93B`）已接入（组切换才重置帧）
