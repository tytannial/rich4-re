# 0x452946 copyNameNoSpaces — 名字/公司名空格双轨制

> 2026-09-29 专项核查。解决「角色名中间带空格」观感问题的权威依据：
> **原版数据就带空格，去空格只发生在拼进消息文本时**。

## 1. 数据源

| 数据 | 原版地址 | 内容 | 空格 |
|------|----------|------|------|
| 角色名表 | `g_charData[i]+0` @ `0x47E80C` → 串 @ `0x4665C4..0x46662C` | 約 翰 喬 / 沙隆巴斯 / 忍 太 郎 / 錢 夫 人 / 阿 土 伯 / 莎拉公主 / 宮本寶藏 / 糖  糖 / 烏  咪 / 孫 小 美 / 小 丹 尼 / 金 貝 貝 | **有**（`0x20` 半角，3 字名每字间 1 个、2 字名中间 2 个 → 统一 4 字宽排版） |
| 公司（股票）名 | `g_stocks[i]+0` ← `off_47F072` | 台 積 電 / 震 旦 行 / 萊 爾 富 / 聯 合 報 … | **有**（同 4 字宽排版） |
| NPC 名 | `dword_47ED5A`（小偷/強盜/流氓/間諜） | — | 无 |
| 玩家结构 | `g_players[26*p]+0` | `newGameInit` `0x4072E4` memcpy `g_charData` 项、读档 `0x402BAE` 重定位指针 | 即上述指针 |

重写：`kCharNames[12]`（`new_game_tables.cpp:25`，生成表照抄原版）→ `Player.name` 原样携带空格。

## 2. 原版双轨规则

- **`drawText(name)` 直绘 → 保留空格**：选人界面名字条、右侧/前进玩家面板（`0x4162A9`/`0x4168AC`）、
  物件提示 showObjectTip、查詢面板、股票行情/走勢圖、分红、抽奖中奖名、交易掛单、
  监狱/医院保释列表。
- **`copyNameNoSpaces(dst, src)`（`0x452946`，逐字节复制跳过 `0x20`）→ 去空格后进 `sprintf` 消息**。
  注释语义 = 「玩家名**或公司名**」。

### 2.1 copyNameNoSpaces 全 44 调用点矩阵

| src 类别 | 调用点 | 宿主函数 / 消息 | 重写位置 |
|----------|--------|-----------------|----------|
| 玩家名 | `0x40C997` | checkPlayerActionStatus「%s住宿中/坐牢中…還剩%d天」 | turn_system `playerNameNoSpace` ✅ |
| 玩家名 | `0x419CA0`/`0x419CF9` | landingEvent 收租「此地屬%s」/「屬%s與%s」 | turn_system 2235 区 ✅（本次补） |
| 玩家名 | `0x419EED`/`0x41AE57`/`0x41AFBF` | 三收费管线「死神顯靈 由%s賠償%s」 | turn_system `resolveFeePayer` ✅（本次补） |
| 玩家名 | `0x41A3FC` | landingEvent corp 收費/旅館·購物中心轮盘地主名 | turn_system 2458 区 ✅（本次补） |
| 玩家名 | `0x41A6C5` | landingEvent specPt 收費「董事長%s」 | turn_system 2568 区 ✅（本次补） |
| 玩家名 | `0x41B9F3`…`0x41C639`（9 处） | onPlayerActionPhase 神明/恶行/NPC 事件消息 | turn_system 173–497 ✅（原有） |
| 玩家名 | `0x41D585` | ownerCanCollectRent 免收租提示 | turn_system 1441 ✅（本次补） |
| 玩家名 | `0x436185` | sub_436034 催收「%s您好」 | bank_stay_dialog 895 ✅（本次补） |
| 玩家名 | `0x436BF2` | sub_436B0A 垫款「由經營者%s墊付」 | bank_dialog 396 ✅（本次补） |
| 玩家名 | `0x43D524`/`0x43EBD0` | jailBailDialog / hospitalVisitDialog「保釋%s」 | jail_dialog 375/832 ✅（本次补） |
| 玩家名 | `0x441A7D` | selectCardOrItemDialog 收卡消息 | card_bag_dialog ✅（原有） |
| 玩家名 | `0x444824`/`0x4449B3` | passOnCardDialog 嫁祸卡 | card_bag_dialog ✅（原有） |
| 玩家名 | `0x4453BB` | cardEffectTaxAudit「%s被課稅%d元」 | card_effects 763 ✅（原有） |
| 玩家名 | `0x44998C`…`0x44B2D1`（8 处） | newsEvt08/09/10/11/12/13/23/29 | news_dialog ✅（原有） |
| **公司名** | `0x42C755`/`0x42D05B` | aiStockBuy/Sell「買進%s%d張」（**消息里玩家名仍带空格直引** `0x42C769`/`0x42D06F`） | stock_system ✅（本次订正：玩家名回退空格、公司名 `nameNoSpaces`；「張」订正） |
| **公司名** | `0x44B113`/`0x44B20A` | newsEvt27/28「%s股票暫停/恢復」 | news_dialog ✅（本次补） |
| **公司名** | `0x444FA8`/`0x445121` | cardEffectRed/BlackStock **AI 分支**「對%s使用紅卡/黑卡！」 | card_effects ✅（本次补齐消息，重写原缺） |
| 标签串 | `0x41805D`/`0x41808A` | gameWndProc，src=`byte_463920`「現  金」 | 待 M3 终局/窗口消息排查 |
| 玩家名 | `0x44C4CE` | 命运生日顶行「向%s收一張卡片」（重写简化为直接弹面板，未演该顶行） | 备忘 |
| 玩家名 | `0x4526A5` | 节日给卡「%s得到%s！」（`0x4661C4..203`，重写未实现） | 备忘 |

### 2.2 原版「不去空格」例外（sprintf 直引，重写必须保留空格）

| 地址 | 宿主 / 消息 | 重写位置 |
|------|-------------|----------|
| `0x431CE7`…`0x432488`（12 处） | applyMagicPenalty「%s\n\n<惩罚>」全部 case | magic_house_dialog 273/338（本次回退） |
| `0x4446D1` | triggerRevengeCard「%s\n\n復仇卡生效！」 | card_effects（本次回退 + 文本补全） |
| `0x444AE9` | applyFreeCard askDialog「%s\n\n是否使用免費卡？」 | card_effects（本次回退） |
| `0x444B18` | applyFreeCard 生效 = **「使用免費卡」**（`0x465305`+`off_47FE8A`，无玩家名） | card_effects（本次订正） |
| `0x444BF2` | applyExemptCard「%s\n\n免罪卡生效！」 | turn_system 1296（本次回退） |
| `0x436865`/`0x436939` | sub_436668 AI 銀行「%s\n\n償還/向銀行貸款%d元」 | bank_stay_dialog 1247（保留 ✅） |
| `0x436CEB` | sub_436B0A else「%s\n\n強制償還…週轉欠款」 | bank_dialog 412（保留 ✅） |
| `0x42C769`/`0x42D06F` | aiStockBuy/Sell 消息**玩家名** | stock_system（保留 ✅） |
| `0x44B2EF` | newsEvt29 第一参 specPt 记录名（`v6+4`） | news_dialog 942（保留 ✅） |
| drawText 各处 | 见 §2 首条 | 保留 ✅ |

## 3. 重写 API

- `nameNoSpaces(const char*)` / `playerNameNoSpace(const GameState&, int)`（`event_common.h`，
  `[RE 0x452946]`）。
- 规则速查：**进 showMessage/sprintf/askDialog 文本 → `playerNameNoSpace` / `nameNoSpaces(公司名)`；
  `drawText` 上屏 → `p.name` / `kStockNames` 原样**。
- §2.2 例外清单内的消息**保持原样带空格**（原版即如此，勿"顺手优化"）。

## 4. 验证要点（实机）

1. 收租/免收租/死神代付/ corp/specPt /轮盘 消息：名字无空格；右侧面板、按住物件提示：仍显示
   「約 翰 喬」原版排版。
2. 银行「%s您好」「由經營者X墊付」无空格；AI 还/借款、「強制償還」消息带空格（原版）。
3. AI 买/卖股票消息：玩家名带空格 + 公司名无空格 +「%d張」。
4. AI 红/黑卡：弹「對X使用紅卡！」（公司名无空格）——本次补齐。
5. 魔法屋惩罚、復仇/免費/免罪卡消息：名字带空格（原版）。
6. 新聞停牌/复牌：公司名无空格。
