# 0x44101D 研究所研发道具（labDevelopDialog）

> 商业用地「研究所」设施（`corp.type==4`）的研发入口：owner 停留 →（先走通用升级询问）
> → 选 1 个道具 → 记 `researchItem`/`researchLeft=5` → 每回合开始倒计时，归零时产出该道具。
> 这是 13 道具里 5 个"非卖品"（機器工人/時光機/傳送機/工程車/核子飛彈）的**唯一获取入口**，
> 也是道具系统的最后一块缺口。帮助依据 `[HELP 22]`（商業用地·研究所"逐级研发道具"）。

## 基本信息

| 项 | 值 |
|----|-----|
| 地址 | `0x44101D` `labDevelopDialog`（大小 ~0x1E7） |
| 选择框 | `0x4402D7`（窗口过程 `labSelectWndProc`，runModal） |
| 触发判定 | `0x41B0B3..0x41B106`（landingEvent 收尾 **loc_41B077** 内，见下） |
| 产出 | `0x41C84F` 尾段 corp 循环（`0x41CD8C..0x41CE2F`） |
| 调用者 | 收尾 `0x41B108`（landingEvent 0x41982D）；产出 `0x41CE25`→`0x445A4D` |
| 被调用 | `0x450441`(读 panel[11]) / `0x451A5A`(allocUiElement) / `0x456280`(blitBackground) / `0x4562A5`(blitElement 色键) / `0x4553FE`(区域灰度) / `0x456418`(色键 blit) / `0x44F9D8`(setTextFont) / `0x44FABC`(drawText) / `0x4018E7`(runModal) / `0x451E7E`(overlay) / `0x451EDB` / `0x456E11`(free) |
| 重写符号 | `src/app/lab_dialog.cpp` `labDevelopDialog` / `labEventHandler`；触发 `turn_system.cpp` `labDevelopAfterLanding`（landingTail 内）；产出 `turn_system.cpp` `updatePlayerStates` 尾 |
| 状态 | 已实现 + 实机验证通过（帧号/灰度/颜色 2026-09-27 修正） |

## 功能与完整流程（**顺序是语义**）

owner 停留自己的商業用地（`type` 与 `sub` 决定分支）时，原版流程（`0x41A1B3` 起）：

```
owner == 自己:
  state37 != 0 → 直接收尾（0x41A1DE）
  sub == 0 → 建设施（付款 → selectFacilityDialog / AI rand%4+1 → ++sub）
             → godBlessUpgrade → **收尾**
  sub != 0 → 升级（0x41A2B3，上限 g_facilityMaxLevel[type]，type4=5）：
             sub >= 上限 → 收尾
             现金不足 → showMessage("您的現金不足！") → 收尾
             人类 askDialog("%s\n\n升級費用:%d元\n\n是否升級？")，拒绝 → 收尾
             升级成功 →（sub==5 台词+FLC523 / 否则 estateChainSpeech+godBlessUpgrade）→ 收尾
收尾 loc_41B077: landAfterMove → sub_448A7E → refreshGameUi(0,0,1)
             → 研究所判定（objId∈(4000,6000) && owner==cur+1 && state37==0
                && type==4 && sub!=0 && (flag&0x0F)==0）→ labDevelopDialog
```

关键点：
- **type4 也走通用升级分支**（不是特例）→ 玩家先看到「是否升級？」（即"先提示加盖"），
  拒绝/成功/现金不足/满级后**都继续**弹研究面板；
- **首次建成研究所立即弹研究面板**：sub==0 建设施（选到研究所 4）→ ++sub=1
  → godBlessUpgrade → 收尾 → type4 判定成立 → labDevelopDialog（sub=1 仅機器工人可点）；
- 已在研发中（`researchLeft>0`）再停留会重开选择覆盖 `researchItem`（无守卫，照抄）。

选择框内：5 个道具图标（panel[11] 帧 10..14），**可选数量 = 研究所等级 `corp.sub`**
（index ≥ sub 的图标**灰度**不可选，逐级解锁）。人类点击 → `researchItem = index+1`、
`researchLeft = 5`；右键/ESC 取消（不改动）。AI（`alive != 1`）恒选 `index = sub-1`
（即 `researchItem = sub`，不弹框）。

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
| `0x46396D` | `25 73 0a 0a a4 c9 af c5 b6 4f a5 ce 3a 25 64 a4 b8 0a 0a ac 4f a7 5f a4 c9 af c5 a1 48` | %s\n\n升級費用:%d元\n\n是否升級？ |

> ⚠️ **踩坑**：IDA 反编译把产出消息显示为 `"%s"`（`aS_13`），实为 `"%s開發成功！"`——
> BIG5 双字节尾含 `a1 49`（全形！）未截断，但 `get_string` 视图截断。照抄会丢"開發成功！"。

## 逆向依据（逐段）

### 触发判定 `0x41B0B3..0x41B106`（收尾内，非 type4 分支特例）

```c
// loc_41B077 收尾：landAfterMove(0x41B086) → sub_448A7E(0x41B09D) → refreshGameUi(0,0,1)
if (objId > 0xFA0 && objId < 0x1770) {           // 4000..6000 商業用地
  if (corp.owner == g_currentPlayer + 1 &&
      g_playerState37[cur] == 0 &&
      corp.type == 4 && corp.sub != 0 && (corp.flag & 0x0F) == 0)
    labDevelopDialog(corp);                       // 0x41B108
}
```

所有 owner 路径（购地→建设施/升级询问成功与失败/state37 早退）都 jmp 到此收尾，
因此**先升级后研究**、**建研究所立即研究**都是这条汇聚的必然结果。

### `labDevelopDialog 0x44101D`

```c
if (g_playerAlive[cur] == 1) {                      // 人类
  v1 = panel[11];                                   // sub_450441(idx 11)
  canvas = allocUiElement(g_tipFrame[帧7].w, .h);   // dword_48C508；帧7 = 400×89
  blitBackground(canvas, g_tipFrame+96=帧7, 0, 0);  // 面板底图（**不透明**）
  for (i=0..4) {
    blitElementToCanvas(canvas, panel[11]+132+12*i, (15+76*i)+33, 44);  // 图标帧 = 10+i（色键）
    if (i >= corp.sub) sub_4553FE(canvas, 15+76*i, 17, 66, 54);         // 未解锁 → **灰度**
  }
  sub_451E7E({0,40,440,480});                       // overlay
  blit(backbuffer, g_tipFrame+72=帧5, 220,140);     // 标题框（色键 stack 帧5 249×170）
  drawText("請選擇欲開發道具", 220,122);
  blit(backbuffer, canvas, 20, 280);                // 5 图标合成块
  idx = runModal(sub_4402D7, corp.sub);             // 参数 = 等级 = 可选数
} else {                                            // AI
  idx = corp.sub - 1;                               // 恒选最高解锁项
}
if (idx != -1) { corp.researchItem = idx+1; corp.researchLeft = 5; }
```

> ⚠️ **帧号公式**：`g_tipFrame` = `sub_450441` 返回的**资源基址**，帧 n 帧头 = `12 + 12*n`。
> `+72` = 帧5（249×170 提示框）、`+96` = **帧7（400×89 五格木纹面板）**。
> 旧实现把 `+96` 当成帧8（35×43 数字 '0'）并错用帧6（271×199 云朵气泡）当标题框 →
> 面板裸奔成黑块 + 大气泡，2026-09-27 修正。

### `sub_4402D7`（选择框窗口过程）

- `WM_INIT(1025)`：`dword_47FF22[2*i]` 名表有效数 `dword_48C534 = 参数(=sub)`，悬停 `=-1`，光标归位 (220,320)。
- `WM_MOUSEMOVE(512)`：命中 `x∈[0x23,0x196]=[35,406], y∈[0x129,0x15F]=[297,351]`，`idx=(x-35)/76`；
  音效 hover(0)；双描边 `drawRectBorder(surface,76*idx+32,294,0x47,59)` + `(…+33,295,0x45,57)`
  （**颜色 = `push 0FFFF00h` = RGB888 黄**）；名 `drawText(off_47FF22[2*idx], 220,154)`。
- `WM_LBUTTONDOWN(0x201)`：`dword_48C530 != -1 && < sub` → click 音效 +`highlightRect(76*idx+35,297,76*idx+101,351)`
  （`0x451B9E`：内容右下移 1px + 顶/左边亮度减半 = 按下下沉；**抬起无音效**）。
- `WM_LBUTTONUP(514)`：`dword_48C530 != -1 && < dword_48C534` → `postModalExit(idx)`，否则忽略。
- `WM_0x205`（右键抬起）：cancel 音效 + `postModalExit(-1)`。
- ⚠ `word_46CAEC`（值 640）是 **640×480 surface 描述符**（首 word=宽），不是颜色；
  `drawRectBorder` 的颜色是调用点压栈的第 6 参（`0x45620F` 内经 `convertColor` 转 RGB555）。

### 区域灰度 `sub_4553FE`（未解锁图标）

```c
v5 = canvas.data + 2*(x + y*w);        // (15+76i,17)
v7 = 2*(w - 66);                       // 行跨距
for (54 行) funcs_4553F1[dword_47637C](66, src, dst=src);
```

`dword_47637C` = `detectPixelFormat` 的像素格式索引；RGB555 = `0x455442`：
`gray = (R+G+B+16)>>2`，`0` 像素保持透明。重写 = `grayscaleImage` 逐行调用（**不是**通道减半变暗）。

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
| 图标 X0 / 步进 | 实参 (68+76i, 324)（帧头 offset 后 ≈ (35+76i, 294)） | `blitElementToCanvas(..., v3+33, 44)`，`v3=15+76i` |
| 图标帧 | panel[11] 帧 10..14（`+132+12i`） | `v1[12*v2+132]` |
| 图标场景命中区 | x∈[35,406], y∈[297,351] | `sub_4402D7` 512 |
| 面板底图 | `g_tipFrame+96` = **帧7** 400×89 @(20,280)，**不透明**（blitBackground） | `0x441082` |
| 标题框 | `g_tipFrame+72` = **帧5** 249×170 @ (220,140)，色键 | `0x441161`，`sub_456418` |
| 标题文字 | "請選擇欲開發道具" @ (220,122) font16 | `0x465298` |
| 名文字 | @ (220,154) | `off_47FF22[2*idx]` |
| 未解锁灰度区 | (35+76i, 297) 66×54（canvas (15+76i,17)） | `sub_4553FE(...,66,54)` |
| 描边 | 双框 71×59 / 69×57 @ (32+76*idx, 294) | `0x4404A8..` push **0xFFFF00** |
| 按下反馈 | (35+76i, 297) 66×54 下沉 1px | `0x4405E5 → 0x451B9E` |
| 音效 | hover=0 / click=1(按下) / cancel=4 | `g_uiSoundHover/Click/Cancel` |
| 面板合成块落点 | canvas → 场景 (20,280) | `sub_456418(...,canvas,20,280)` |
| `researchLeft` 初值 | 5 | `0x4411FB` |

## 重写要点

- 模态框架复用 `runModal(app, handler, &st)`；逐层直接画到场景：
  底图 `blitElementOpaque(frame(7))` → 图标 `blitElement`（panel[11]）→ 未解锁逐行
  `grayscaleImage(66)` → 标题 `frame(5)` + 文字 → 悬停黄双框 + 名 → 按住 `pressDown(1px)`。
- 触发接线：`turn_system.cpp` `landingTail`（`landAfterMove` 后）调用
  `labDevelopAfterLanding`；type4 **不再**在分支内特例直呼，与升级询问共用流程。
- 产出接线：`updatePlayerStates`（`turn_system.cpp`，[RE 0x41C84F]）末尾 corp 遍历已实现。
- 存档：`researchItem/researchLeft` 属运行态，随 corp 动态数据整体入档（P5 存档完整恢复），本功能不强耦合。

## 差异 / 待深入

- 原版每次停留研究所（含已在研 `researchLeft>0`）都重开选择覆盖 `researchItem`——照抄。
  （2026-09-30 曾按实机反馈加 `researchLeft==0` 守卫，**随后用户裁定恢复 IDA 逻辑**：
  触发点条件仅 `owner/type4/sub!=0/(flag&0x0F)==0`；查封卡置 `flag` 低半字节=1 时才禁研究。）
- `0x41B0FC` 触发前置无"满 9 道具"守卫；产出 `givePlayerItem` 内部上限 9 自处理。
- 升级询问的封顶（`off_480886` 列15 expr0）与非封顶（0x40ECDE 列8 expr3）台词 ✅ P4-C 已接入。
- AI 研究所是否会主动升级 `sub`：AI 升级走通用设施升级分支（自动，无询问），与研发独立。

## 验收（2026-09-27 实机验证通过）

1. `--debug` 建研究所（造 type4），owner 停留 → **先**弹「升級費用…是否升級？」，
   拒绝/同意后 → 弹选择框；`sub=1` 仅機器工人可点、其余 4 灰度；右键取消不改状态。
2. 首次在商業用地选建「研究所」→ 建设施完成后**立即**弹选择框（sub=1）。
3. 选機器工人 → `researchItem=1, researchLeft=5`（调试打印 corp research=%u/%u 验证）。
4. 过 5 个自己的回合 → 第 5 回合开始弹"機器工人開發成功！" + 道具栏出现機器工人。
5. 研究中途把研究所拆到 `sub < researchItem` → 下次回合 researchLeft 清 0，不产出。
6. AI/托管玩家停留研究所自动研发最高解锁项并如期产出。
7. 面板视觉：棕木标题框（帧5）+ 400×89 木纹五格（帧7）；未解锁格灰度；悬停黄框 + 名称；按下格下沉。
