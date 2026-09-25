# 0x44101D 研究所研发道具（labDevelopDialog）

> 商业用地「研究所」设施（`corp.type==4`）的研发入口：owner 停留 → 选 1 个道具 →
> 记 `researchItem`/`researchLeft=5` → 每回合开始倒计时，归零时产出该道具。
> 这是 13 道具里 5 个"非卖品"（機器工人/時光機/傳送機/工程車/核子飛彈）的**唯一获取入口**，
> 也是道具系统的最后一块缺口。帮助依据 `[HELP 22]`（商業用地·研究所"逐级研发道具"）。

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x44101D` `labDevelopDialog`（大小 ~0x1F5） |
| 选择框 | `0x4402D7`（窗口过程，runModal） |
| 产出 | `0x41C84F` 尾段 corp 循环（`0x41CD8C..0x41CE2F`） |
| 调用者 | `0x41B106`（landingEvent 0x41982D 商业用地 owner 分支 type4）；产出给道具 `0x41CE25`→`0x445A4D` |
| 被调用 | `0x450441`(读 panel[11]) / `0x451A5A`(allocUiElement) / `0x456280`(blitBackground) / `0x4562A5`(blitElementToCanvas) / `0x4553FE`(变暗) / `0x456418`(色键 blit) / `0x44F9D8`(setTextFont) / `0x44FABC`(drawText) / `0x4018E7`(runModal) / `0x451E7E`(overlay) / `0x451EDB` / `0x456E11`(free) |
| 重写符号 | `src/app/lab_dialog.cpp` `labDevelopDialog` / `labSelectHandler`；触发 `turn_system.cpp` type4 分支；产出 `turn_system.cpp` `updatePlayerStates` 尾 |
| 状态 | 已实现待验证 |

## 功能

owner 停留在自己的研究所（`type4`，`sub!=0` 已建设施，`flag(+28) & 0x0F == 0` 未被查封，
[RE 0x41B0FC]）→ 弹「請選擇欲開發道具」面板。面板显示 5 个道具图标（panel[11] 帧 10..14），
**可选数量 = 研究所等级 `corp.sub`**（index ≥ sub 的图标变暗不可选，逐级解锁）。
人类点击某图标 → `researchItem = index+1`、`researchLeft = 5`；右键/ESC 取消（不改动）。
AI（`alive != 1`）恒选最高解锁项 `index = sub-1`（即 `researchItem = sub`）。

此后每个**回合开始**（`updatePlayerStates`，仅处理 `owner == currentPlayer` 的研究所）：
- 若 `researchItem > corp.sub`（研究途中设施被拆到等级下降）→ `researchLeft = 0` 作废；
- 否则 `researchLeft--`；归零时 `refreshGameUi(1)` + `showMessage("%s開發成功！")` +
  `givePlayerItem(currentPlayer, researchItem + 8)` 产出道具。

## 关键结构

### Corp 字段（`0x498E88` 每项 56 字节；见 `game_state.h:156` `struct Corp`）

| 偏移 | 字段 | 研发用途 |
|------|------|----------|
| +24 `type` | 设施类型（**4 = 研究所**） | 判定 |
| +25 `owner` | 拥有者+1 | 仅处理 `owner==currentPlayer+1` |
| +26 `sub` | 设施等级（0=无设施，建设施即 1，上限 5） | **可选道具数 = sub** |
| +28 `flag` | 查封（`&0x0F != 0` 时不研发；非 0 收费翻倍） | 触发守卫 |
| +29 `researchItem` | 研发项目 1..5（0=未研发） | 写入/读取 |
| +30 `researchLeft` | 研发剩余（写入恒 =5） | 倒计时 |

### 5 道具映射表（**铁律：`item id = researchItem + 8`**）

| 选择框 index (`i`) | `researchItem`(`i+1`) | `item id`(`+8`) | 道具 | panel[11] 图标帧(`i+10` = `id+1`) |
|---|---|---|---|---|
| 0 | 1 | 9 | 機器工人 | 10 |
| 1 | 2 | 10 | 時光機 | 11 |
| 2 | 3 | 11 | 傳送機 | 12 |
| 3 | 4 | 12 | 工程車 | 13 |
| 4 | 5 | 13 | 核子飛彈 | 14 |

- 道具名表（悬停/产出消息用）：原版 `off_47FF22[2*i]`（悬停，`i`=index）与 `off_47FF1A[2*ri]`
  （产出，`ri`=researchItem）是**同一组 5 个名字**，仅索引基准差 1。重写直接复用
  `kItemBagNames[id]`（`0x47FEDA`，`map_tables.cpp:613`，`id 9..13` = 機器工人/時光機/傳送機/工程車/核子飛彈，
  与本表逐一吻合，已交叉验证）。

### 字符串（BIG5→UTF-8，`get_bytes` 原始字节复核）

| 地址 | 原始 | UTF-8 |
|------|------|-------|
| `0x465298` | `bd d0 bf ef be dc b1 fd b6 7d b5 6f b9 44 a8 e3` | 請選擇欲開發道具 |
| `0x463B68` | `25 73 b6 7d b5 6f a7 b9 a6 a8 a1 49` | %s開發成功！ |

> ⚠️ **踩坑**：IDA 反编译把产出消息显示为 `"%s"`（`aS_13`），实为 `"%s開發成功！"`——
> BIG5 双字节尾含 `a1 49`（全形！）未截断，但 `get_string` 视图截断。照抄会丢"開發成功！"。

## 逆向依据（逐段）

### `labDevelopDialog 0x44101D`

```c
if (g_playerAlive[cur] == 1) {                      // 人类
  v1 = panel[11];                                   // sub_450441(idx 11)
  canvas = alloc(g_tipFrame[帧8].w, .h);            // dword_48C508
  blitBackground(canvas, g_tipFrame+96=帧8, 0, 0);  // 面板底图
  for (i=0..4) {
    blitElementToCanvas(canvas, panel[11] + 132 + 12*i, (15+76*i)+33, 44);  // 图标帧 = 10+i
    if (i >= corp.sub) darken(canvas, 15+76*i, 17, 66, 54);                 // sub_4553FE 变暗
  }
  sub_451E7E({0,40,440,480});                       // overlay
  blit(backbuffer, g_tipFrame+72=帧6, 220,140);     // 标题框（色键）
  drawText("請選擇欲開發道具", 220,122);
  blit(backbuffer, canvas, 20, 280);                // 5 图标合成块
  idx = runModal(sub_4402D7, corp.sub);             // 参数 = 等级 = 可选数
} else {                                            // AI
  idx = corp.sub - 1;                               // 恒选最高解锁项
}
if (idx != -1) { corp.researchItem = idx+1; corp.researchLeft = 5; }
```

### `sub_4402D7`（选择框窗口过程）

- `WM_INIT(1025)`：`dword_47FF22[2*i]` 名表有效数 `dword_48C534 = 参数(=sub)`，悬停 `=-1`，光标归位 (220,320)。
- `WM_MOUSEMOVE(512)`：命中 `x∈[35,407], y∈[297,351]`，`idx=(x-35)/76`；音效 hover(0)；
  双描边 `drawRectBorder(76*idx+32,294,0x47,59)` + `(76*idx+33,295,0x45,57)`；
  名 `drawText(off_47FF22[2*idx], 220,154)`。
- `WM_LBUTTONUP(514)`：`idx < sub` 才 `postModalExit(idx)`，否则忽略。
- `WM_0x205`（右键）：`postModalExit(-1)`（音效 cancel）。
- 描边色 = `word_46CAEC`（与 `selectFacilityDialog` 同 RGB555 绿 0x0280）。

### `updatePlayerStates 0x41C84F` 尾段（产出，0x41CD8C..）

```c
// 位于 if (g_playerAlive[a1] && !g_sceneRequest) 内，a1<4（玩家）分支末尾
for (each corp) {
  if (corp[24]==4 && corp[30] /*researchLeft*/ && corp[25]==g_currentPlayer+1) {
    if (corp[29] /*item*/ > corp[26] /*level*/) { corp[30] = 0; }      // 等级门槛作废
    else if (--corp[30] == 0) {
      sub_41906A(1);                                                    // refreshGameUi(0,0,1)
      sprintf(txt, "%s開發成功！", off_47FF1A[2*corp[29]]);             // = kItemBagNames[item+8]
      showMessage(txt, 1500);
      givePlayerCard(=givePlayerItem)(g_currentPlayer, corp[29] + 8);   // item id = researchItem+8
    }
  }
}
```

> IDB 里 `0x445A4D` 旧名 `givePlayerCard` 系误名，实为 **givePlayerItem**（`0x49915C` 道具库存），
> 已在 `map_objects.cpp:1110` 订正实现。

## 关键常量与坐标

| 项 | 值 | 依据 |
|----|-----|------|
| 图标 X0 / 步进 / 尺寸 | 48 / 76 / ~66×54（canvas 内） | `blitElementToCanvas(..., v3+33, 44)`，`v3=15+76*i` |
| 图标场景命中区 | x∈[35,407]（`35+76*i`）, y∈[297,351] | `sub_4402D7` 512 |
| 面板底图 | `g_tipFrame`（estateTiles/data.mkf[517]）帧8 | `+96` |
| 标题框 | estateTiles 帧6 @ (220,140) | `+72`，色键 sub_456418 |
| 标题文字 | "請選擇欲開發道具" @ (220,122) font16 | `0x465298` |
| 名文字 | @ (220,154) | `off_47FF22[2*idx]` |
| 描边 | 双框 71×59 / 69×57 @ (32+76*idx, 294) | `word_46CAEC` 0x0280 |
| 音效 | hover=0 / click=1 / cancel=4 | `g_uiSoundHover/Click/Cancel` |
| 面板合成块落点 | canvas → 场景 (20,280) | `sub_456418(...,canvas,20,280)` |
| `researchLeft` 初值 | 5 | `0x4411FB` |

## 重写要点

- 模态框架直接复用 `runModal(app, handler, &st)`（同 `facility_dialog.cpp`），无需复刻
  canvas/overlay 中间层——逐图标 `blitElement` 到场景即可（图标帧 `10+i`，落点命中反编译场景坐标，
  实机 `frame_align.py` 校准）；描边/标题/名沿用 facility 同款。
- 变暗（`index >= sub`）：重写若无现成 darken helper，按 `ui-controls.md` 变暗表用 RGB555 亮度
  偏移；最简可跳过未解锁项的描边/点击并在图标上叠半透明，但应保留"禁用不可选"的视觉（待补 helper）。
- 触发接线：`turn_system.cpp` type4 分支（现仅日志 stub，`0x41B106`）替换为
  `labDevelopDialog(app, cp)`；守卫 `cp.sub != 0 && (cp.flag & 0x0F) == 0`。
- 产出接线：`updatePlayerStates`（`turn_system.cpp:2711`，[RE 0x41C84F]）末尾加 corp 遍历循环，
  逐行对齐 `0x41CD8C..`（含等级门槛作废分支 + `refreshGameUi`(=renderGameFrame) + showMessage + givePlayerItem）。
- 存档：`researchItem/researchLeft` 属运行态，随 corp 动态数据整体入档（P5 存档完整恢复），本功能不强耦合。

## 差异 / 待深入

- 原版每次停留研究所（含已在研 `researchLeft>0`）都重开选择覆盖 `researchItem`（无 `researchLeft` 守卫）——照抄。
- `0x41B0FC` 触发前置是否还有"未查封"以外的守卫（如满 9 道具拒绝）：反编译未见，产出 `givePlayerItem` 内部上限 9 自处理。
- panel[11] 帧 10..14 与道具栏帧一致性已确认；实机仍需目视图标对位。
- AI 研究所是否会主动升级 `sub` 以解锁更高道具：AI 升级走通用设施升级分支（`0x41A310` 自动升级），
  与研发独立。

## 验收

1. `--debug` 建研究所（`Ctrl+Shift+设施` 造 type4，`Ctrl+2` 升级），owner 停留 → 弹选择框，
   `sub=1` 仅機器工人可点、其余 4 变暗；右键取消不改状态。
2. 选機器工人 → `researchItem=1, researchLeft=5`（`Ctrl+8` 打印 corp research=%u/%u 验证）。
3. 过 5 个自己的回合 → 第 5 回合开始弹"機器工人開發成功！" + 道具栏出现機器工人。
4. 研究中途把研究所拆到 `sub < researchItem` → 下次回合 researchLeft 清 0，不产出。
5. AI/托管玩家停留研究所自动研发最高解锁项并如期产出。
