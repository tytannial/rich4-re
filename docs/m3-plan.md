# M3 执行计划（终局闭环 + 现代化操作 + 跨平台冻结）

> 前置状态（2026-09-29）：**M2 全部功能 ✅**（帮助 99/99、AI 专项完成——道具/卡片/回合动作链
> 三链闭环，见 `docs/reverse/functions/420e9a-item-ai.md`、`441baa-card-effects.md`、
> `418c55-ai-turn-actions.md`；headless 全量矩阵 84/84 PASS + L0 ctest）。
> 本计划为交接文档，执行前先读 `AGENTS.md` 与 `docs/reverse/functions/victory-flow.md`（权威逆向档案）。
>
> **范围重定义（2026-09-29）**：M3 = **Windows 原版内容/体验对等（AVI 视频除外）**；
> 跨平台 → **M4**、现代化（真宽屏/自定义键/窗口全屏选项）→ **M5**。除终局链 A 外，
> 另纳 B 功能/演出/音频拉齐（节日链/房地产AI/卡门槛/OGG续播/失焦暂停/生日演出/出监出院资源/
> 公佈欄交互/乐透截断等），详见 AGENTS 与 docs 盘点。
>
> **进度**：A2 `checkVictory`、A4 `defeatFlow`、A6 run scene 分派、A1 clearedMaps 生命周期、
> A3 `gameClearFlow` + `mapSelectDialog`(0x4060E9) 选图面板 ✅（`src/app/victory.cpp`+`application.cpp`，
> 测试 280/282/286）；**A2 选人目标两档 UI 经核实已于 M2 实现**（`applyListSelection` list4/5）。
> M3-A 终局链**功能闭环达成**。
> **✅ 2026-09-29 A 终局链观感修正完成**（实机问题驱动，详见 `victory-flow.md`）：playLine
> 视口切说话玩家（0x44EFBD）、破产致胜切胜者+认输台词位序订正（0x40D060/0x40D237）、
> 选图界面全屏演出重做（滚动预览+角色行走+装饰，消重影）、**蓝星(帧10)/红勾(帧8)预合成**
> （`UiImage::blitIntoFrame` 0x4562A5 语义）、multiple 仅大字无面板（多人回主菜单=原版行为）、
> `player.bankrupt` 调试命令 + `283_victory_last_stand`（全矩阵 89/89 绿）。
> 余：284 连通关闭环（实机）；A5 AVI 不实现。转 B 组功能缺口。

## 1. M3-A：终局链（最高优先，档案 victory-flow.md 已备齐依据）

| # | 任务 | 原版地址 | 要点 |
|---|------|----------|------|
| A1 | 通关进度 `GameState.mapClearFlags[4]` | `dword_4990F0` | 回主菜单清零（0x401B9C LABEL_5）；**进存档**（0x402AC5/0x402FD1 字段序核对） |
| A2 | 胜利判定 `checkVictory` 接入 advanceDay | `sub_41D89E` @0x41CF67 | 时间限制 `dword_49911C`/胜利资金 `dword_499108`（选人界面配置传入，均 0=无限不判）；赢家资产最高 `sub_4239B9`；四分支 sceneRequest 1/2/3 + `charState[AI角色]=2` 遗留灰名单 |
| A3 | 通关结算+地图选择面板 | `sub_4075C1` + `sub_4060E9` | 标记当前图通关 → g_jumpUi 帧 11-14/15-20 预览滑入、已通关不可选、红勾选定 `word_4991B8`；4 图全通 → END/THANKS AVI + 退出 |
| A4 | 失败界面 | `sub_407842` + `sub_406B14` | 10s 倒计时模态；确认→sceneRequest=4（读档继续：`byte_496B7D[0]=1`+其他玩家 charState 清 0）/超时→回主菜单。替换现 `defeatFlow` 的 scene=1 近似 |
| A5 | AVI 播放 | `0x45144F` AVI 分支 / `END.AVI`/`THANKS.AVI` | 重写需新增 **AVI(MJPEG) 解码器**（或降级：FLC 截图静态图——与用户确认取舍） |
| A6 | 场景收尾 | WinMain `byte_46CAF8` switch | scene 2/3 的 `newGameInit(lParam=1)`（保留通关进度）/回主菜单路径；现 `sceneRequest` 语义扩展 |
| A7 | 测试 | — | `280_victory_time`（时间到判胜）、`282_victory_money`、`284_clear_progress`（4 图连通关+灰名单）、`286_defeat_screen` headless |

## 2. M3-B：现代化操作（项目目标级特性）

| # | 任务 | 说明 |
|---|------|------|
| B1 | 宽屏适配 | SDL3 窗口缩放/letterbox 或真扩展（640×480 逻辑 → 任意窗口）；与用户确认方案 |
| B2 | 快捷键体系 | 原版 `word_49717C..17E` 全局键位（前进/骰子数）→ 扩展自定义键位表（存档 cfg，参考 0x401010 钩子） |
| B3 | 设置面板增强项 | 音量/速度/按键映射 UI（沿用 `411b53-settings-dialog.md`） |

## 3. M3-C：平台与冻结

- C1 Linux 构建验证 ✅ **2026-09-30 完成**：CMake/SDL3/FreeType Linux 构建 + `run_tests.py` 全量
  （两平台 92/93 一致，唯一 TIMEOUT 为 `84_ai_marathon` wall-cap 截断）；跨平台改造全量落地
  （GDI→FreeType 文本、cp950 内嵌表、自带 PRNG、可写路径回退、资源大小写、CI build 主程序），
  详见 **`docs/cross-platform.md`**；「窗口分辨率高清文本层」列后续专项（同档 §9）。
- C2 `--save-selftest` + 全量矩阵作为 release 冻结基线；版本号与 CHANGELOG。
- C3 帮助 99 条终验 + 实机清单（`docs/gameplay-map-mechanics.md` 验收表收尾）。

## 4. 顺序与提交粒度

A1→A2→A4→A3→A5→A6→A7（A3/A5 依赖 A1/A2 的 scene 流），随后 B、C。
每个可测功能一个 `wip:` 提交；逆向双落（源码 `[RE]` + 档案 + IDB）；提交前 `python tools/re_map.py`。

## 5. 遗留/待拍板

- AVI 资源播放：真解码 vs 静态近似（A5 前问用户）。
- 宽屏方案：letterbox 保真 vs 视野扩展改渲染（B1 前问用户）。
- 破产债务清算矩阵 `addPlayerDebt` 不入存档的终局结算（原版行为已归档，M3-A2 实现时复核）。
