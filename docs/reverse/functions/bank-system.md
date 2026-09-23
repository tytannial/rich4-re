# 银行系统（case 14）专项研究

> P2 经济基石主线的研究档案（2026-09-23）：保险/贷款/週轉/月度结息/股东大会分红。
> 关联：`41982d-p2-events.md` §8（case 14 入口）、`pricing-formulas.md` §1.1（物價指數）、
> `ui-controls.md` §25（面板底座：FloatMessage/语音/音乐栈）、`help-checklist.md` idx 2/14/35。
> 原版地址均已 IDA 核实；「待查」项为实现时需补逆向的点。

## 0. 总览与调用链

| 场景 | 入口 | 人类分支 | AI 分支 |
|------|------|---------|---------|
| **路过银行** | `sub_4379C9`（0x4379C9，landingEvent 0x41B396 / onPlayerActionPhase 0x41B5AB） | `runModal(sub_436EF8)` UI（panel.mkf[24]） | 按 `aiCashPct` 调整现金/存款比例 |
| **停留银行** | `sub_436668`（0x436668，case 14 0x41B3AF） | `runModal(sub_435062)` 主 UI（panel.mkf[23]，music scene 4） | 贷款/提前还款（`aiLoanPct`） |
| **每日贷款到期** | `sub_436A5A`（0x436A5A，由 `sub_41C84F` 回合开始状态更新 0x41C86D 调用） | 到期前 3 天提醒 → 强制执行 | 同 |
| **月初结息** | `sub_439BFA`（0x439BFA，advanceDay 新月 0x41D09E） | `runModal(sub_437E61)` 结息面板（panel.mkf[25]） | 同（全员面板） |
| **15 号股东大会** | `sub_42BA97`（0x42BA97，advanceDay 0x41D08F） | `runModal(sub_42B3EB)`（panel.mkf[76]） | 同 |
| **15 号乐透开奖** | `sub_431712`（0x431712，advanceDay 0x41D094） | **后置**（依赖 case 9 投注，TODO(P2 樂透)） | 同 |

## 1. 数据与字段

### 1.1 Player 相关（104B 结构；`+offset` 除注明外为 Player 内偏移）

| 字段 | 地址 | 语义 |
|------|------|------|
| `loan` / `loanDate` | +36 / +44 | 贷款本金 / 到期日（`sub_433B7E` 贷款时 = 当前日期 +90 天，跳过特殊日期） |
| `dword_496B90[26*p]` | +40 | **週轉（预支）金额**：週轉現金存入时累加、归还时递减（`sub_434492`/`sub_436B0A`） |
| `byte_496BA3[104*p]`（重写 `bankRefuseDays`） | +59 | **银行拒绝往来剩余天数**（非 0 显示"銀行拒絕往來 還剩%d天！"，`sub_4379C9` 0x4379DA）；**命运事件 `sub_44C229` `+= 30`**；回合递减 0→0x80→清 0（`sub_41C84F` 0x41CAA6/0x41CB94） |
| `byte_496BA4[104*p]`（重写 `bankFinanceFlags`） | +60 | **特别融资剩余天数**：bit0-6 天数（+1 显示第 N 期）、bit7 到期（週轉現金按钮拒绝、AI 贷款守卫 `!byte_496BA4`）；回合递减（0x41CAB7/0x41CBB5） |
| `dword_496BC4/BC8[26*p]` | +92 / +96 | 月度结算累计（结息面板排名用，`sub_437D1A`；语义待查） |
| `byte66`（`byte_496BAA`） | +66 | 结算排名用累计计数（×2500×M，`sub_437D1A`；与 jailPlayer 的 `+=days` 共址，语义待查） |

### 1.2 全局

| 项 | 地址 | 说明 |
|----|------|------|
| `byte_497159` | 0x497159 | 游戏设置项（`loadConfig` 0x411E8F 写入；jailPlayer FLC / 小游戏 / 银行 UI 演出条件）——银行申请贷款按钮分支用（0x435200 → off_475830 消息），确切语义待 1.4 逆向 |
| `dword_497160` | 0x497160 | 当前日期（y<<16\|m<<8\|d；`sub_4521AA` 日期差、`sub_452117` 推进） |
| `g_moneyMul` | 0x4990E8 | 物价指数（结息排名 2500×M、`updateMoneyIndex`） |

### 1.3 常量/表

| 项 | 地址 | 值/说明 |
|----|------|---------|
| 贷款期限 | `sub_433B7E` | **+90 天**（`sub_45218F(date,90)` → 0x45201F；`isSpecialDate` 跳过假日） |
| 日期工具 | 0x451F8C / 0x4521AA / 0x45218F | `daysSince1998` / 日期差 / 日期+天数 → ✅ `date_util.h`（1.2 已实现） |
| 存款月息 | `dbl_464EA0` | **0.1（10%/月）** |
| AI 现金比例 | `flt_464C08`=100.0f；`dbl_464C10`=1.5（月初 ≤7 天）；`dbl_464C18`=0.5（月末 ≥26 天）；阈值 `dbl_464C20/C28`=±0.25 | `sub_4379C9` AI 分支 |
| 排名比例阈值 | `dbl_464D58` = 0.4 | `sub_437D1A` 第一名需比第二名高 40% |
| 结息头像帧表 | `word_475918` | 按存活玩家数（4/3/2/1）取 {55,56,57}/{52,53,54}/{49,50,51}/{48,48,48} |
| 结息音效表 | `dword_475B17` | {27,28,60,...} |
| 保释/週轉消息 | off_475830/840/850、47585C/60/68/6C/70、475860/68/6C | 文本待 dump（BIG5） |

## 2. 路过银行（`sub_4379C9`）

```
BYTE1 = byte_496BA3[cur]                       // 拒绝往来天数
if (BYTE1) { showMessage("銀行拒絕往來\n\n還剩%d天！" 0x464BED, 1000); return; }
if (人类 alive==1) {
    panel[24] 加载（dword_48C3F4）; byte_46CB00=1
    // 面板暂存/恢复：dword_48BE24[cur] 非 0 → 暂存 → sub_41D433 刷新
    runModal(sub_436EF8, 0);                   // 柜员机 UI
    // 恢复暂存 + sub_45285E(360) 延时 + refreshGameUi
} else {
    // AI：调整现金/存款比例到目标 aiCashPct（月初×1.5 / 月末×0.5，clamp 0.1..0.9）
    //     差异 ≥0.25（或现金为 0）才调整：cash = total×目标；bank = total - cash
}
```

### 2.1 柜员机 UI（`sub_436EF8`，0xAD1）

- Windows 消息回调（147 块）；panel.mkf[24] 资源
- **数字滚动动画 `sub_436D3A`**：金额字符串逐位绘制（`dword_48C3F4 + 12 + 12*(digit-29)`），
  进度条 `v7 = 6*(len/总额*flt_464BD0)` 动态裁剪 @(118..324, 172..236)，`InvalidateRect(118,172,324,236)`
- 存款/取款/离开按钮（`sub_436B0A` 入口）
- **待查**：按钮布局、输入框调用、`sub_436B0A` 调用时机

### 2.2 週轉金结算（`sub_436B0A`，0x230）

```
v2 = specPt 中 type(+26)==7（銀行）且 owner(+24)!=0 的 owner-1   // 银行经营者（董事长）
if (a1 == 1) {                                // 单人对董事长：週轉欠款 > 其他玩家存款总额
    v12 = dword_496B90[26*v2]                 // 董事长週轉欠款
    v6  = Σ其他玩家 bank
    if (v6 < v12) {
        // 临时切到董事长视角（g_currentPlayer 换 + sub_41906A(1) 刷新）
        showMessage("銀行資金準備\n\n不足%d元\n\n由經營者%s墊" 0x464B75, 2500)
        sub_433BD8(v2, v12 - v6)              // 从董事长存款/现金扣垫
        dword_496B90[26*v2] -= (v12-v6)
    }
} else {                                      // a1 == 0：全员週轉欠款催缴
    for (每个 i != v2, alive, dword_496B90[i]) {
        showMessage("銀行經營權易主！" 0x464B9E, 1500) + "欠款%s %d元"
        sub_433BD8(i, dword_496B90[i]); dword_496B90[26*i] = 0;
    }
}
```

- 调用者：`sub_4379C9`（路过）/`sub_436EF8`/`sub_418C55`（跳伞入场）/`stockPanelMain`/
  `sub_4284BE`（15 号链）/`sub_44C7EF`、`sub_44C91F`（新闻效果）
- **重写要点**：董事长视角切换 = 原版临时改 `g_currentPlayer`；重写建议直接传玩家参数

## 3. 停留银行（`sub_436668` → `sub_435062`）

### 3.1 停留入口（`sub_436668`）

```
if (byte_496BA3[cur]) return;                 // 拒绝往来中不处理
dword_48C3B0 = playerTotalAssets(cur)         // ★ 贷款上限基准
if (人类) {
    panel[23]（dword_48C3C0）+ panel[2]（dword_48C3BC）
    allocUiElement(200,280,0,0)（dword_48C3B8，右上面板）@(440,0)
    allocUiElement(200,200,0,0)（dword_48C3B4，右下面板）@(440,280)
    if (cellEntId 6000..8000 且该 specPt 是银行 && owner==cur) v2 = 1   // 自己经营的银行
    musicPlayScene(4); runModal(sub_435062, v2); musicStackPopRestore();
} else { // AI
    if (loan != 0) {                          // 提前还款：2×loan < bank 或 loan×1.3 ≤ 总资产 → 还清
        sub_433BD8(cur, loan); showMessage("%s\n\n還款%d元"); loan = 0;
    } else {
        if ((rand()%10 == 0 || bank+cash < 30000) && byte_496BA4==0 && aiLoanPct) {
            loan = totalAssets × aiLoanPct / 100;
            bank += loan; sub_433B7E(cur);   // 记录 +90 天期限
            showMessage("%s\n\n貸款%d元");
        }
    }
}
```

### 3.2 主 UI（`sub_435062`，0xFD2，137 块）

资源布局：`sub_434186`（0x2F0 主绘制）——上窗 panel[23] + 右下两 panel；按钮/标签：

| 标签文本 | 地址 | 位置 | 说明 |
|---------|------|------|------|
| 申請貸款 / 償還貸款 | 0x464A81 / 0x464A8A | (345,345)/(530,345) | 顶部两个操作页签 |
| 特別融資 | 0x464A93 | (443,427)/(492,245 灰) | 董事长特别融资入口 |
| 週轉現金 | 0x464A9C | (67,324) | 打开 `sub_434492` |
| 歸還款項 | 0x464AA5 | (67,382) | 週轉归还 |
| 客戶存款總額 / 目前融資金額 / 尚可融資金額 | 0x464AAE/ABB/AC8 | (78,147/195/243) | 存款总额（他人存款合计）/当前贷款/可贷额度 |

状态机 `byte_48C3DD`（0 空闲 / 1 停业消息 / 2 等待 / 3 贷款输入 / 4.. ? / 7 贷款完成 /
8 额度不足 / 9 现金不足）；消息 off_475830（停业）/840（贷款成功）/850（现金不足）/47585C…（週轉）。

**申请贷款**（0x435228；数字框调用 0x435245）：

```
if (byte_497159) { 停业消息; return; }
if (g_playerLoan[cur] >= dword_48C3B0) { 状态=8 "尚可融资金额不足"; return; }
v = sub_453544(dword_48C3B0 - g_playerLoan[cur]);   // 输入上限 = 总资产 − 已贷
if (!v) { 状态=8; return; }
g_playerBank[cur] += v; g_playerLoan[cur] += v;
sub_433B7E(cur);                                     // 到期日 = 今天+90 天
sub_433D6E(); sub_433F24();                          // 刷新双面板数字
重绘 (0,0)-(640,480)：dword_48C3B8 + dword_48C3D5 元素 + dword_48C3B4@(0,0x118)
状态=7; floatMsgShow(off_475840); byte_48C3E2 = 1;  // 本回合已贷
```

> **帮助 idx 35 称"最高 100 万、3 个月"**：IDA 实际为 `playerTotalAssets − 已有贷款`，
> 期限 90 天确认；"100 万"疑为帮助过时口径（实现以 IDA 为准，实机对照）。

**偿还贷款**（0x435360；数字框调用 0x435367）：

```
v = sub_453544(g_playerLoan[cur]);                   // 上限 = 贷款余额
if (!v) { 状态=8; return; }
if (v > bank + cash) { 状态=9 + off_475850; return; }
bank -= v; if (bank < 0) { cash += bank; bank = 0; } // 存款优先，不足补现金
loan -= v; 刷新; 状态=7(消息); byte_48C3E2 = 1;
```

- **特别融资**（0x4351A0 附近）：`byte_496BA4` 低 7 位 +1 → `sprintf(unk_464AD5, N)` +
  `showMessage(…, 8000)`；`byte_497159` 时停业。**待查**：期数含义、发放逻辑、与经营权的联动
- **待查**：页面切换（`byte_48C3DD` 4/5/6 分支）、`sub_433D6E`/`sub_433F24` 数字刷新细节、
  `dword_48C3D5` 元素内容、"贷款到账"演出

### 3.3 週轉現金（`sub_434492`，0xBBd，**已完整逆向**）

```
初始化(0x401)：额度 dword_48C3C8 = Σ其他玩家(0..3) bank；SetTimer 100ms；double 缓冲面板
点击命中表 word_475818/47581A/47581C/47581E（3 按钮）：
  按钮1 週轉現金：byte_496BA4[cur] 非 0 → 拒绝；否则 状态=… → PostMessage(0x409)
      存款：v = sub_453544(额度 − dword_496B90[cur])   // 上限 = 他人存款总额 − 已预支
            bank += v; dword_496B90[cur] += v; byte_48C3D0 = 1（退出码=1）
  按钮2 歸還款項：dword_496B90[cur] == 0 → 拒绝；否则 PostMessage(0x40A)
      还款：v = sub_453544(dword_496B90[cur])
            if (v > bank + cash) { 不足消息(off_47586C); return; }
            bank -= v;（不足扣现金）；dword_496B90[cur] -= v
  按钮3 離開 → 状态=7 → postModalExit(byte_48C3D0)
消息：off_47585C 开场 / off_475860 额度不足 / off_475870 告别（状态机 byte_48C3CC）
```

### 3.4 银行 UI 动画（复用医院/监狱动画模式）

| 动画 | 位置 | 机制 |
|------|------|------|
| **柜员微动画**（停留） | `sub_436034` | **50ms SetTimer**（0x32）；`byte_48C3E7` 消息阶段 1→2→3；`byte_48C3E9` 动画位：上窗帧表 `byte_475880`={7,6,7,5}/{12,11,12} @(140,114)-(220,154)；下窗 (140,152)-(220,182) 帧 `+108`/`9,10`（`12*((rand&1)+9)`）；`rand>>10` 随机触发、`rand>>11>=4` 概率驻留 1..7 拍 |
| **数字滚动**（路过） | `sub_436D3A` | 金额逐位 + 进度条裁剪 @(118..324, 172..236) |
| **週轉现窗**（子界面） | `sub_434492` | **100ms SetTimer**（0x64）；帧表 `byte_475884` @(496,162)-(566,197)（0x30 复位）；小像帧 13/14 @(496,197)-(562,222) 随机 1..8 拍 |
| 开场/退出 | `sub_45285E(360)` | 路过银行退出后的 360ms 延时（同医院出院节奏） |

## 4. 贷款到期（`sub_436A5A`，每日由 `sub_41C84F` 调用）

```
v = sub_4521AA(当前日期, g_playerLoanDate[cur])     // 到期天数差
if (v <= 3) {
    0：showMessage("貸款到期日\n\n強制執行！" 0x464B2C, 1500)
       sub_433BD8(cur, loan)（存款→现金扣，不足破产）; loan = 0; loanDate = 0;
    1：showMessage("距貸款到期日\n\n還剩１天！" 0x464B43, 1500)
    2：showMessage("距貸款到期日\n\n還剩２天！" 0x464B5C, 1500)
    3：sub_43695E()                                  // 催收演出：panel[23] + runModal(sub_436034)
                                                     //   （50ms 柜员动画窗 + 消息阶段机）
}
```

- 调用位置：`sub_41C84F` 开头 0x41C86D（在 alive/sceneRequest 检查**之前**；重写
  `updatePlayerStates` 已接入，`turn_system.cpp` `checkLoanDue`）
- `sub_433BD8`（0x48）：从 **bank 扣**，不足由 cash 补，再不足 → `eliminatePlayer`（0x40CD87）
  → ✅ 重写 `payFromBank`（`economy.cpp`，1.2）

## 5. 月初结息（`sub_439BFA` → `sub_437E61`）

`sub_439BFA`（0x313，advanceDay 新月 0x41D09E）：

```
panel[25] 加载；全屏底图；逐行玩家：头像帧 word_475918[4×存活数 + i] @x=600
  "存款：" + bank
  "利息：" + bank × 0.1            （dbl_464EA0）
  loan 非 0 时：红字"貸款："（byte_464E97）替换利息行
floatMsgSetup(190,10)；musicPlayScene(9)；runModal(sub_437E61, 0)；musicStackPopRestore
结束后逐玩家清零：byte_496BAA/byte66、dword_496BC4、dword_496BC8
```

- `sub_437E61`（**0x1D99，169 块，全游戏最大 UI 之一**）：结息面板主体；语音
  `#0092/#0093/#0095/#0108/#0109/#0122`；`SetTimer` 动画；**待逐步逆向**
- `sub_437D1A`（0xE4）：排名 = `2500×M×byte66 + dword_496BC4 − dword_496BC8 + 10×bonusAtk(+68)`，
  取第一名；若第一名比第二名高 >40%（dbl_464D58）返回其索引，否则 −1（用途待查）
- **待查**：利息是否实际发放（`sub_437E61` 内）、`byte_496BAA/dword_496BC4/BC8` 语义

## 6. 15 号股东大会分红（`sub_42BA97` → `sub_42B3EB`，**逻辑已明**）

```
panel[76] 加载；标题"股東大會"（296,25）/"分紅"（104,82）
逐玩家名 @x=160+i*98
对 12 家上市公司（word_496984[18*i] 非 0 → specPt 索引）：
    公司名 @(62,y)；总持股 = Σ存活 g_playerShares[24*p + 2*i]
    每玩家分红 = specPt+40（累积盈余）× (个人持股 / 总持股)，int 截断
    显示累积盈余 @(572,y)；v20 += 24
    ★ 若总持股非 0：specPt+40 = 0（分配后清零）
逐玩家合计分红 @(198+i*98, 404)
runModal(sub_42B3EB)                    // 确认界面（动画待查）
逐玩家：g_playerBank += 分红；<0 → 扣现金 → 不足 eliminatePlayer
```

- `g_specPts + 40` = **公司累积盈余（分红池）**（收费入账累积，见 `spec_pt` 收费研究）
- **待查**：`sub_42B3EB` 确认界面细节；上市公司判定（`word_496984` 表来源）；
  分红与"经营权/董事长"联动

## 7. 资源与文本

| 资源 | 内容 |
|------|------|
| panel.mkf[23] 1,757,022B SMP 未压缩 | 停留主 UI + 柜员帧（+108/9,10/12,11/13,14 等） |
| panel.mkf[24] 275,912B | 路过柜员机 UI |
| panel.mkf[25] 3,603,204B | 月初结息面板（含头像帧 48..57） |
| panel.mkf[76] 511,512B | 股东大会分红 |
| panel.mkf[13/15/16/17] | 乐透（后置） |

消息文本（BIG5，已解码）：0x464B2C 强执 / 0x464B43/5C 剩余 1/2 天 / 0x464B75 經營者墊
/ 0x464B9E 銀行經營權易主 / 0x464BED 銀行拒絕往來 / 0x464A81.. 界面标签。

## 8. 实现顺序（wip 划分）与待办

1. **物价指数**：`updateMoneyIndex` + advanceDay 每日（独立，先行）
2. **银行数据层**：Player 补字段（+40/+59/+60/+92/+96）+ `sub_436A5A` 每日到期检查 +
   假日停业 `byte_497159`/`byte_496BA3` + AI 路过/停留决策（`sub_4379C9` AI / `sub_436668` AI）
3. **路过 UI**：panel[24] + `sub_436EF8` + 数字滚动 `sub_436D3A` + `sub_436B0A` 週轉结算
4. **停留主 UI**：panel[23] + `sub_434186` 布局 + 柜员动画 `sub_436034` + 申请/偿还贷款 + 特别融资
5. **週轉現金**：`sub_434492`（逻辑已全，动画 100ms + 帧表 byte_475884）
6. **月初结息**：panel[25] + `sub_439BFA` + `sub_437E61`（待逐步逆向）+ `sub_437D1A`
7. **15 号分红**：panel[76] + `sub_42BA97` + `sub_42B3EB`（乐透开奖留 TODO）

### 待查清单（实现时补逆向）

- [ ] `sub_435062` 状态机 4/5/6 分支、页面切换、特别融资完整逻辑
- [ ] `sub_436EF8` 按钮布局/输入/`sub_436B0A` 调用时机
- [ ] `sub_433D6E`/`sub_433F24`/`sub_433C20` 数字刷新细节；`dword_48C3D5` 元素
- [ ] `sub_43695E`（到期第 3 天调用）
- [ ] `sub_437E61` 完整体（169 块）；利息发放位置；`byte_496BAA/dword_496BC4/BC8` 语义
- [ ] `byte_496BA4` 特别融资位定义；`sub_42B3EB`；`word_496984` 上市公司表
- [ ] 帮助"最高 100 万"与 IDA 公式的差异实机核对
