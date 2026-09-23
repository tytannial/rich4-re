# AGENTS.md

## 项目简介

《大富翁4》(Rich4) 逆向重建工程。原始 `rich4.exe`（1999 年，i386 PE，DirectDraw/DirectSound）
已无法在现代系统运行且无法跨平台。本项目通过逆向分析，用 **C++17 + SDL3** 重写引擎，
目标是 Windows/Linux 跨平台运行，并支持宽屏、快捷键等现代化操作。

游戏内码为 **BIG5（繁体）**；引擎内部统一使用 UTF-8。

## 当前进度

**M1（基本交互）已完成**：主菜单 → 选人/选地图 → 开局（跳伞入场，期间锁定交互）→
回合循环（掷骰三阶段：角色扔骰/骰子 FLC/点数停留 → 移动 → 过天）→
工具条（帮助/设置/大地图/读档/存档/托管AI）→ 右侧面板（玩家信息/日历/小地图/前进面板）→
地图物件（神明事件点/地标/住宅用地/商業用地/行業設施點/事件格）与**左键按住查看物件信息**。
非玩家控制期（AI 回合/掷骰/移动/结算）锁定交互（`g_playerControl`）。

**M2（进行中）**：P0 买地 ✅、P1 升级 ✅ / **他人住宅用地收租 ✅**（`landingEvent` 0x419A67：
同路段联合租金/连锁×2000/查封翻倍/免收状态/神明调整/showMessage/收款入银行；
同盟分账/死神代付/卡片抵用 → P4）、收费公式全部逆向归档 ✅（`pricing-formulas.md`）；
**商業用地 corp 全流程 ✅**（购地/建设施 `selectFacilityDialog`/升级/他人收费/旅館住宿，
`facility_dialog.cpp`+`roulette_dialog.cpp`）、**行業設施點 specPt 收费 ✅**（6 公式 + 公库 `fund`）、
**连锁店 ✅**（改建卡 0x44309B + `Ctrl+4`）；Debug 工具（Ctrl+1..9 + Ctrl+Shift 扩展）✅、
大地图填色 ✅、买地面板背景 ✅；**仅剩建造菜单 ❌**（任务 F，`sub_423B3B/4225A3/423CF3`
已调研，**入口已确认 = 工具条 case 6（`sub_424492`→`sub_423CF3`）**）；**P2 事件格推进中**
（得點券 case 10/11/12 ✅、卡片格 case 13 ✅、監獄/醫院 case 4/5 ✅ 含保释·出院面板+护士动画
+ **开局默认在押 NPC**（小偷·強盜/流氓·間諜，0x40734F）+ **NPC 释放动画**（panel[64] 全身像
+ 答谢消息，0x43CFDB/0x43DE4C；事件槽路面行走待 P5）；事件格 FLC 阻塞播放模板 `playEventFlc` + `drawEventFlcFrame` +
`showCardGet` + `jailPlayer`/`hospitalizePlayer`/`insurancePayout`；面板底座
`FloatMessage`/语音 `playVoice`/音乐栈见 `ui-controls.md` §25）。
**M2 股市 ✅**：`stock_system.cpp`（stockTick/认购/控股/买卖交易量）+ `spec_pt_dialog.cpp`
（0x41D1A9 认购 + 数字输入框）+ `stock_market_dialog.cpp`（0x42B58F/0x42AAFF 行情/持股切换·買/賣·
走勢圖饼+折线·休市，`panel.mkf[75]` 帧0/1/2）+ `advanceDay` 每日维护（停牌/新闻倒计时·休市·
交易量·stockTick）；`kStockNames[8][12]`；详见 `42b58f-stock-panel.md`。
**M2 银行/月度经济 ✅（2026-09-23）**：`bank_dialog.cpp`（柜员机 0x436EF8）+ `bank_stay_dialog.cpp`
（停留 0x435062/週轉 0x434492/催收 0x436034）+ `month_settle_dialog.cpp`（月初结息
0x439BFA/0x437E61：存款 ×1.1 + 悲情/冠军排行演出 + FLC 全身像）+ `dividend_dialog.cpp`
（15 号分红 0x42BA97/0x42B3EB）；`advanceDay` 接入 15 号分红/新月结息/卡片格·銀行格换位；
AI 买地 0x41D7D4；`transferMoney` 累计 monthSettleA/B；详见 `bank-system.md`。

**M2 P3 地图物件生命周期 ✅（2026-09-24）**：`map_objects.cpp`（attachObject 0x40EAD7 附身/
老虎机 0x440706/送丢卡/死神没收 + attachEnd 0x40E32C 飘走动画 + relocatePlayer 0x40CC56 +
damagePlayer/demolish/givePlayerItem）；`economy.cpp` deleteMapObject **配对轮替**（小↔大、惡犬↔土地公…）；
`onPlayerActionPhase` 物件分派（神明/惡犬/禮物/寶箱/路障/地雷/炸彈挂身·转移·爆炸 + 乞丐施捨）；
`updatePlayerStates` 附身寿命 7·13 天递减；渲染 8 方向帧/跟随玩家/浮空寿命数字/弹飞 flt；
`Player.luckA/B/C` + `GameState.trapStock`；详见 `map-object-refresh.md`。
[TODO P4] 道具放置入口（0x446BAA/C88/D69/拆除卡 0x446AFB 已留桩）+ 角色台词语音；[TODO P5] NPC 分支。

**功能总览**：`docs/help-checklist.md`（帮助文档 99 条 → 实现状态，当前 ✅ 19 / 🟡 19 / ❌ 61）。

**M2 执行计划**：见 `docs/m2-plan.md`

**M2 待办**（详见 `docs/reverse/functions/` 各档案「差异」段，按选定路线排序）：

- P2 事件格实现（研究已完成，见 `41982d-p2-events.md`；**得點券/卡片格/監獄/醫院/銀行 ✅
  2026-09-23**（银行全链含週轉/催收/月度结息/15 号分红），继续：物價面板 → 新聞/命運 → 小游戏/樂透/百貨/魔法屋；
  面板底座 `ui-controls.md` §25，事件槽 NPC 路面行走待 P5）
- 任务 F 建造菜单（大地块特殊建筑外观 + 5 页签；**入口已确认 = 工具条 case 6
  `sub_424492`→`sub_423CF3`**；`g_miscTable336` 建造队列每日计数 sub_428475 待接入）
- 经济系统剩余：物價指數面板显示（idx 5）、股市交易市场/挂单、卡片与道具使用（P4）
- AI 决策：AI 买地 ✅（`sub_41D7D4`，2026-09-23）；`alive & 6` 电脑/托管自动行动
  （`landingEvent` 0x41982D 回合主流程）
- 存档完整恢复：`loadGameFromSlot` 目前仅恢复地图/模式/玩家数
- 事件槽 NPC（玩家 4-7：流氓/间谍/小偷等）—— 保释机制/犯人表（0x498E28）已研究

## 目录结构

| 路径 | 说明 |
|------|------|
| `resources/game/` | 原始游戏文件（exe + mkf + dat + cfg），**只读，不提交** |
| `docs/formats/` | 资源格式规范（已完整解析，见 `README.md`） |
| `docs/reverse/` | 逆向规范与函数档案（注释格式、地址映射表、逐函数笔记） |
| `tools/` | Python 逆向与资源解析工具 |
| `include/game/` | C++ 头文件 |
| `src/` | C++ 源码 |
| `build/` | 构建输出（已忽略） |

## 构建

工具链：**MSVC (VS 18 Community) + CMake + Ninja**。

### 准备 SDL3

从 https://github.com/libsdl-org/SDL/releases 下载 `SDL3-devel-<ver>-VC.zip`，
解压到 `third_party/`（如 `third_party/SDL3-3.4.16/`）。CMake 会自动查找该目录。
若未提供，则回退到 FetchContent 从源码构建（较慢）。

### 编译

```powershell
# 加载 MSVC 环境
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> 注意：Strawberry Perl 自带的 cmake/ninja 在本机已损坏（缺 DLL），
> 请使用 VS 自带或独立安装的 CMake/Ninja。

## 运行

```powershell
./build/rich4.exe --game resources/game
```

## 调试

```powershell
./build/rich4.exe --game resources/game --debug
```

`--debug` 启用 **Ctrl+1..8** 测试热键（改地块所属/等级、批量铺地、加现金、打印地块详情），
完整键位与测试流程见 `docs/debug-keys.md`。

## 测试

当前骨架阶段以手动运行为主。后续加入单元测试（格式解析往返）后在此补充命令。

## Python 工具

```bash
# 一键解包全部资源 + 生成索引
python tools/extract_all.py resources/game --out ./assets --manifest docs/formats/resource-manifest.csv

python tools/mkf.py list resources/game/Data.mkf
python tools/spr.py extract <spr.bin> <outdir> [--rgb565]
python tools/smp.py extract <smp.bin> <outdir> [--rgb565]

# 逆向标注校验 + 生成地址映射表
python tools/re_map.py
```

## 代码约定

- **C++17**，根命名空间 `rich4`
- 文件/目录 `snake_case`；类型 `PascalCase`；函数 `camelCase`；私有成员 `m_`
- 头文件用 `#pragma once`
- 平台层不抛异常，错误以 `bool` / `std::optional` 返回
- 内部字符串 UTF-8；BIG5 仅在资源加载期转换
- **不要添加无关注释**；格式相关说明可引用 `docs/formats/*.md`
- **逆向标注强制**：每个重写函数/全局都要标注原始地址与依据
  （`// [RE 0xXXXXXX] 原符号名` + `依据:` 行；平台迁移用 `[PORT ...]` + `替换依据:`），
  格式见 `docs/reverse/README.md`，提交前跑 `python tools/re_map.py` 校验

## 重要参考

- `docs/formats/README.md` — 资源格式总览与工具索引
- `docs/formats/lzhuf.md` — 压缩算法（含易错点说明）
- `docs/reverse/README.md` — 逆向注释规范与工作流
- `docs/reverse/address-map.md` — 地址↔重写符号映射表（自动生成）
- `include/game/formats.h` — 全部格式的 C 结构定义
- `docs/help-checklist.md` — **游戏内帮助 99 条 → 功能实现检查表**（机制描述权威索引）
- `docs/reverse/functions/gameplay-map-mechanics.md` — 地块机制总览与验收检查表
- `docs/reverse/functions/pricing-formulas.md` — **收费/价格公式总汇**（物价指数、轮盘、住宅用地/商業用地/行業設施點、到期清算、MAPDAT 数值）
- `docs/reverse/functions/41982d-p2-events.md` — P2 事件格（case 2..16）专项研究
- `docs/reverse/functions/41982d-estate-rent.md` — 收租/收费专项（case 0 他人住宅用地收租全流程）
- `docs/reverse/functions/stock-system.md` — **股市系统**（行情 stockTick/控股/认购/行情界面/交易市场/数字输入框）
- `docs/reverse/functions/42b58f-stock-panel.md` — **股市面板**（0x42B58F 行情/持股/买卖/走勢圖/休市 精确实现）
- `docs/reverse/ui-controls.md` — **游戏内 UI 控件指南**（浮雕按钮/变暗表/文本对齐/数字框/模态框架/…；**发现新控件必须更新此文件**）
- `docs/reverse/frame-anchor.md` — **帧锚点 offset 与落点对齐**（原版落点=实参−offset、
  像素对齐校验流程 `tools/frame_align.py`、非零 offset 资源清单；元素错位类问题先查此文件）

## 工作经验（踩坑与约定）

### 逆向方法

- **不猜测，查 IDA**：音效索引、帧号、结构偏移、表内容一律先用 IDA 验证
  （`get_bytes` 读原始表、`decompile` 看调用链与常量），再写代码。
  凭猜测写 id/帧号是「音效对不上 / 贴图错帧」类问题的首要原因。
- **帧头公式**：精灵/元素帧头 = `资源基址 + 12 + 12*帧号`
  （如 `+84`=帧6、`+72`=帧5、`+24`=帧1）；结构数组内偏移如
  `&dword_48231A + 8*控件 + 8`。误读会画错帧或错位。
- **帧锚点 offset**：SMP/SPR 帧头 `x/y` 是**锚点偏移**，原版与重写 blit 的落点都是
  **实参 − offset**（重写见 `blit.cpp` `clipBlit`）→ **移植直接抄原版实参，勿自行加减
  offset**（曾双重补偿踩坑）。元素错位用 `python tools/frame_align.py align` 做像素差扫描
  验证原版实参，**勿肉眼判断**；原版实参偶有笔误（医院复位帧 8/13 传了 Rect.right/bottom）。
  方法/清单见 `docs/reverse/frame-anchor.md`。
- **时序精确到调用点**：同一功能在不同阶段表现不同（例：掷骰——扔骰动画无音效 →
  骰子 FLC 第30帧与结束各播一次落地槽 → 转移动才播载具槽）；必须看反编译中
  调用发生的**具体位置**，不能想当然。
- **区分资源格式**：`UiImage.load` 仅支持 SPR/SMP；RAW 无头位图用
  `decodeRawBitmap`（按字节数推断尺寸）；FLC 用 `FliDecoder`。
- **单 buffer 语义**：原版每个音效槽是单 DirectSound buffer
  （`sub_4542E9` 停 + `sub_4542CE` 播）→ 重写用同槽替换，避免多声部叠加。
- **帮助文档是第一手机制依据**：`help.mkf` 99 条（8 分类，分类/标题表 `[RE 0x4761B4]`/`0x4761B8`）
  已提取为 `docs/help-checklist.md`；研究/实现时用 `[HELP idx]` 标注引用。
  但**帮助文本可能错**（idx 58 冬眠卡误用红卡文本）或与实现不符
  （医院罚款帮助写 1000/天，代码 `g_moneyMul*2000*days`）→ 以 IDA/实测为准。
- **术语以原文为准，简繁同字可自由转换**：专有名词（地块/设施/费用名、卡片/道具/神明名）
  对照 `help.mkf` 原文、MAPDAT 格名与 exe 名表（`g_costNames`/`g_facilityNames`/`kObjectNames` 等）；
  文档与注释**允许简体**（如 商業用地/商业用地、載具/载具 均可），
  **已处理过的繁体文本不再回改**；但同字转换勿改错字（"商場"实为住宅等级名 `kBuildingNames[3]`、
  设施名应为"購物中心"——帮助列表段与详述段用词不一时以 exe 表为准）。
- **MAPDAT 实测定类型**：cellEnt `+4` 名称 / `+36` 低字节类型 / `+34` 图块，
  用 Python 解析 `map.mkf[2*(4*mode+map)+1]` 逐项打印；比"猜测类型对应"可靠
  （特殊地点 16 类与帮助页逐项吻合，一次确定）。
- **switch case 读跳转表**：`get_bytes` 读 jpt（如 `landingEvent` 的 `0x4197E9` 17 项）
  比反编译 switch 更直接，能精确定位每个 case 的代码地址与合并的 default。
- **指针表解码**：`off_XXXX`/`funcs_XXXX` 是指针数组，用 Python 读 exe 解码
  （VA→文件偏移 = `VA - 0x401A00`，BIG5 用 cp950；`get_string` 遇 BIG5 常失败）。
- **图块交叉验证索引**：类型/名称索引用 `data.mkf[type+395]` 图块内容验证
  （407=土地公、408=禮物、410=死神、411=路障 → 确认 `kObjectNames[类型值]` 为 0-based，
  而非"类型-1"）。

### 常见坑

- **FLC 解码越界**：部分 FLC 数据尾部含超出声明帧数的 chunk，必须按
  `m_frameCount` 封顶，否则会多播一帧（回绕成第0帧，表现为「闪回首帧」）。
- **`rewind()` 只重置偏移不清像素**；播完应冻结末帧（不回绕）。
- **视口**：`manualView=true` 且 `viewSmooth=0` → 场景跳左上角，并触发重选出生点/跳伞；
  小地图旋转只改 `mapRotation`，**不要**切视角模式。
- **事件槽（玩家 4-7）**：无激活 NPC 时不能进入（否则 `currentPlayer` 指向无数据槽）。
- **音乐**：取消对话框**不改动**音乐；確定仅在音量 0↔非0 时停/重播（非0 之间只调音量）。
- **重新开局**：`newGameInit` 需重置 `mapRotation`/骰子/回合/音乐计时器等残留状态。
- **音效 id 空间**：UI 音效（`g_uiSoundHover/Click/Confirm/Cancel` = Effect.mkf 0/1/2/4）
  与游戏音效槽（`g_effectSlots[slot]`）是两套，勿混用。
- **多套顺序勿混用**：同一批数据可能同时存在 type 顺序 / 图块顺序 / 帮助页顺序三套
  （特殊地点 type 7/8 = 七彩氣球/喜從天降，但图块 sprite 15/13 互换、帮助页顺序又不同）；
  重写时按用途查对应表，不要假设一致（见 `gameplay-map-mechanics.md` §3）。
- **帮助页标题与内容需交叉验证**：页面标题表在 exe（`[RE 0x4761B8]`），
  与 `help.mkf` 页内容可能错位/重复（idx 58）；发现异常时在文档标注，不照抄。
- **重写命名要对照逆向**：如 `GameState.cardShuffle` 实为**新闻事件顺序表**
  `g_newsOrder`（0x499090，`newsEvent` 用）——错误命名会误导后续开发；
  发现后先记文档，实现时随功能改名。
- **PowerShell 环境**：Python 输出中文前设 `$env:PYTHONIOENCODING="utf-8"`
  （否则 GBK 控制台报 `UnicodeEncodeError`）；本机无 `rg`，搜索用 grep/glob 工具。

### 工作流

- 每项修正单独 `wip:` 提交；用户实机验证后再推进；**不要**把 `wip:` 当里程碑。
- 实机问题优先加**诊断日志**（限帧/状态变化）定位，再改代码，避免盲改。
- 编译前确保游戏已退出（否则 `LNK1104` 文件占用）；`build.cmd` 不提交。
- 逆向成果需**双落**：源码注释 + `docs/reverse/functions/` 档案 + IDB 重命名/注释
  （IDB 修改后保存）；改动后跑 `python tools/re_map.py` 校验。
- **大功能先出专项研究文档再实现**：如 P2 事件格 → `41982d-p2-events.md`
  （处理函数/数据结构/实现顺序/待深入清单），避免重复逆向与遗漏依赖。
- **状态同步**：完成功能后更新 `help-checklist.md`（对应帮助条目状态）与
  `gameplay-map-mechanics.md`（阶段勾选）；IDB 重命名后同步文档中的符号名。
- **控件沉淀**：实现中用到/发现任何可复用 UI 控件（浮雕按钮、变暗表、数字框、列表框、
  滑块、光标、模态叠加…），**必须**总结进 `docs/reverse/ui-controls.md`（按文末模板），
  记录原版地址/资源/交互/重写位置/踩坑经验，供后续功能复用、避免重复逆向。
- IDB 批量操作：`rename`（函数/全局）、`set_comments`、`get_bytes`（表/jpt）、
  `analyze_batch`（多函数概览）→ 完成后 `idb_save`。

## 注意事项

- `resources/game/` 下的文件**不要修改或提交**（版权 + 体积）
- `resources/game/rich4.exe.i64` 是权威逆向数据库（重命名/注释成果），**修改后需保存**
- `src/resource/lzhuf_tables.cpp` 由 `tools/gen_lzhuf_tables.py` 从 exe 生成，**勿手改**
- LZHUF 解压器每次解压需重置工作区（自适应树有状态）
