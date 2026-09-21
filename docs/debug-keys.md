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

按住 **Ctrl** + 数字键（主键盘 `1`..`8`）：

| 键 | 功能 | 细节 |
|----|------|------|
| `Ctrl+1` | 鼠标指向地块 **owner = 当前回合玩家** | 支持地产(estate)/公司(corp)/特殊点(specPt)；同时重建小地图/大地图色块 |
| `Ctrl+2` | 指向地块 **等级 +1** | 地产 `level` / 公司 `sub`，上限 5 |
| `Ctrl+3` | 指向地块 **等级 -1** | 下限 0 |
| `Ctrl+4` | 指向地块 **设为连锁店** | **预留未实现**（待 F3 探明"改建卡→连锁店"流程） |
| `Ctrl+5` | **全图地产归当前回合玩家** | 遍历 estates + corps + specPts |
| `Ctrl+6` | **全图有主地产 level = 1** | 仅 `owner != 0` 的地产（便于快速铺建筑测收租） |
| `Ctrl+7` | 当前回合玩家 **现金 +100000** | 直接改 `players[cur].cash` |
| `Ctrl+8` | **打印指向地块详情**（日志） | estate：名字/type/owner/level/flag/dir/priceAdd/priceBase/fees/price/expire；corp/specPt/evtCell/cellEnt 类似 |

拾取与右键物件提示同一套：鼠标位置 → `pickMapObject`（复用渲染期 `mapHitRegions`，
含 SPR 像素掩码精确命中）。日志前缀 `debug:`。

## 行为说明

- **不受 `gamePlayerControl` 限制**：AI 回合/掷骰/移动中也可改地块（便于布置测试场景）。
- **跳伞入场期间被屏蔽**（与其他输入一致，原版跳伞阻塞语义）。
- 修改后地图与小地图立即刷新（`buildMiniMapMarks`）。
- 部分操作会写入 `RICH4_LOGI` 日志（`logs/`），`Ctrl+8` 是主要诊断手段。

## 典型测试流程

**收租**（状态机 case 3 = `30 × g_moneyMul`）：
1. `Ctrl+5` 全图归自己 → `Ctrl+6` 全图 level=1（建筑可见）
2. 轮到 AI 玩家时，鼠标指向某块按 `Ctrl+1`（该块归 AI）
3. 自己回合走到该块 → 现金减少，日志出现 `rent: player 0 -> 1 amount ...`

**升级/加盖**（`priceBase × moneyMul`）：
1. `Ctrl+1` 指向地块归己（或 `Ctrl+5` 全归己）
2. 走到该格 → 弹出「是否加蓋?」→ 确认后 `level + 1`
3. `Ctrl+2/3` 可直接调等级验证各级外观与费用

**大地图/小地图填色**：
1. `Ctrl+1` 或 `Ctrl+5` 改所属
2. 工具条「地圖」按钮 → 大地图应出现玩家色块（与小地图一致）

## 已知限制

- `Ctrl+4`（连锁店）未实现：连锁店由**小空地使用改建卡**创建，流程待逆向（见
  `docs/m2-plan.md` 任务 F3）。
- 大地块（`estate.type != 0`）不能通过升级分支加盖，应走**建造菜单**（任务 F，未实现）。
- 不修改 `dword_496BB4` 欠款矩阵（重写未建模，见任务 D 说明）。
