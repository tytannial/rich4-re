#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game/render/fli.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {

// [RE 0x46CB3C] g_newGameOptions 选人界面（0x404E44）配置结果
// 依据: 0x406DE7 中 dword_46CB3C[0..5] 为 6 项选项索引、dword_46CB54 为地图索引;
//       dword_48A35C[3*k] 为玩家 k 的角色 ID（bit31 = AI，状态 1 随机补齐时置位）
struct NewGameConfig {
    int playerCountIndex = 2; // 0=二人 1=三人 2=四人
    int startMoneyIndex = 1;  // kStartMoney 索引
    int travelMode = 0;       // 0=步行 1=機車 2=汽車
    int landPermIndex = 0;    // kLandPermText 索引
    int gameTimeIndex = 0;    // kGameTimeText / kGameDays 索引
    int winCondIndex = 0;     // kWinMoneyMul 索引
    int mapIndex = 0;         // 0-3
    int charId[4] = {-1, -1, -1, -1}; // 玩家槽角色 ID（-1 = 未选）
    bool isAi[4] = {false, false, false, false}; // 对应原版 charId bit31（AI 标志）

    // [RE 0x4990F4=2] 上一局 AI 角色遗留标记（跨局保留）
    // 依据: 0x406DE7 结尾清理把"本轮已渲染 AI"（状态 4）转 2、其余转 0;
    //       下次进入选人界面时状态 2 = 灰度 + 红X + 不可选（原版 byte_4990F4）
    bool aiUsed[12] = {};

    int playerCount() const { return playerCountIndex + 2; }
};

// [RE 0x496B68] g_players 玩家数据（原版 104 字节/项，字段按原偏移标注）
// 依据: 0x406DE7 memcpy(&g_players[26*k], &g_charData + 104*charId, 104);
//       0x417E26/0x40829D/0x415F69 等以 g_playerCash/g_playerSpriteX/... 偏移访问
// ⚠️ 字段注释中的 `+N` 为**原版 32 位 pack(1) 布局偏移**（g_players 每记录 104 字节）。
// 重写为 64 位且 `name` 为 8 字节指针，实际偏移与原版不同（姓名之后整体后移 4+ 对齐差），
// 代码一律按成员名访问；存档映射见 `save_data.cpp`（按原版偏移手工序列化）。
// padN 字段用于维持字段相对次序（便于按原版布局推理），不参与逻辑。
struct Player {
    const char* name = nullptr; // +0  角色名（BIG5→UTF-8）
    uint32_t color = 0;         // +4  g_playerColor 0x496B6C（u32 RGB888，绘制前经 convertColor 转 555）
    uint16_t spriteX = 0;       // +8  g_playerSpriteX 0x496B70（52 步进 word 数组）
    uint16_t spriteY = 0;       // +10 g_playerSpriteY 0x496B72
    uint16_t cellEntId = 0;     // +12 g_playerCellEntId 0x496B74
    uint16_t prevCellEnt = 0;   // +14 word_496B76（上一步所在 cellEnt，移动排除回头路）
    uint8_t dir = 0;            // +16 byte_496B78（绘制朝向 0-7）
    uint8_t travel = 0;         // +17 byte_496B79（行进方式 0-3）
    uint8_t diceCount = 1;      // +18 byte_496B7A（骰子个数）
    uint8_t charIndex = 0;      // +19 g_playerCharIndex 0x496B7B（角色 ID）
    uint8_t byte20 = 0;         // +20
    uint8_t alive = 0;          // +21 g_playerAlive 0x496B7D（bit0 人类/bit1 电脑/bit2 托管/0x10,0x20 状态）
    uint8_t aiCardItem = 0;     // +22 byte_496B7E（bit0 使用卡片、bit1 使用道具）
    uint8_t aiPersonality = 0;  // +23 byte_496B7F（0=乖寶寶 1=普通人 2=大老奸）
    uint8_t aiLoanPct = 0;      // +24 byte_496B80（AI 贷款比例，总资产 %）
    uint8_t aiCashPct = 0;      // +25 byte_496B81（AI 现金比例，余额 %）
    uint8_t aiStockPct = 0;     // +26 byte_496B82（AI 股票比例，总资产 %）
    uint8_t byte27 = 0;         // +27 byte_496B83（状态恢复朝向）
    int32_t cash = 0;           // +28 g_playerCash 0x496B84
    int32_t bank = 0;           // +32 g_playerBank 0x496B88
    int32_t loan = 0;           // +36 g_playerLoan 0x496B8C
    // [RE 0x496B90] dword_496B90 週轉（预支）金额：週轉現金存入时累加、归还时递减
    // 依据: 0x435260 sub_434492 週轉現金（bank += v; dword_496B90 += v）；0x436B0A 银行资金准备/垫付
    int32_t bankAdvance = 0;    // +40
    uint32_t loanDate = 0;      // +44 g_playerLoanDate 0x496B94
    uint16_t points = 0;        // +48 g_playerPoints 0x496B98
    uint32_t stateFlags = 0;    // +50 dword_496B9A（BYTE0-3 各状态效果剩余回合）
    uint8_t byte54 = 0;         // +54 byte_496B9E（额外状态）
    uint8_t state37 = 0;        // +55 g_playerState37 0x496B9F
    uint8_t skipMove = 0;       // +56 byte_496BA0（跳过移动）
    uint8_t fixedStep = 0;      // +57 byte_496BA1（固定步数）
    uint8_t byte58 = 0;            // +58
    // [RE 0x496BA3] byte_496BA3 银行拒绝往来剩余天数（非 0 显示"銀行拒絕往來 還剩%d天！"）
    // 依据: 0x4379DA sub_4379C9（(b&0x7F)+1 → 0x464BED）；0x43667B sub_436668 停留守卫
    uint8_t bankRefuseDays = 0;    // +59
    // [RE 0x496BA4] byte_496BA4 银行融资状态：bit0-6 特别融资期数（+1 显示）、bit7 标志
    // 依据: 0x4351CE（(b&0x7F)+1 期数消息 0x464AD5）；0x4341E7 週轉/0x4368CE AI 贷款守卫
    uint8_t bankFinanceFlags = 0;  // +60
    // [RE 0x496BA5] byte_496BA5 同盟生效标记（clearAllyPair 0x40CC1A 解除时清双方）
    uint8_t allyActive = 0;        // +61
    // [RE 0x496BA6] g_playerCardCnt（**保险期**剩余天数；原误名 cardCount）
    // 依据: 0x41AC6E 保險收费 += 轮盘天数；0x44BA63 非 0 时保险理赔；0x41CAE3 到期清 0
    uint8_t insuranceDays = 0;  // +62
    // [RE 0x496BA7] 附身神明物件 ID（cellTable 槽+1；0=无）。寿命计数在
    //   cellTable[槽].life（+4，死神13/其余7），每回合 updatePlayerStates 递减
    uint8_t cellTableIdx = 0;   // +63 byte_496BA7
    // [RE 0x496BA8] 挂身道具物件 ID（路障/定時炸彈，cellTable 槽+1；0=无）。
    //   寿命 cellTable[槽].life（炸弹38），每次落地 onPlayerActionPhase 递减
    uint8_t cellNo = 0;         // +64 g_playerCellNo 0x496BA8
    // [RE 0x496BA9] g_playerVehicle 实为同盟对象（1-based 玩家号，0=无）
    // 依据: 0x41D559 用 g_playerVehicle[地主] == 当前玩家+1 → "與%s同盟中\n\n免收%s！";
    //       0x40DF69 记账后命中同盟 → sub_40CC1A 解除同盟（清双方 +65 与 byte_496BA5）;
    //       0x416256 面板用 pieceSprites[+65-1] 帧 2 画同盟对象棋子头像
    uint8_t ally = 0;           // +65 同盟对象（重写原误名 vehicle）
    uint8_t byte66 = 0;         // +66 byte_496BAA（月度结息排名累计，见 monthSettleA）
    uint8_t pad67 = 0;          // +67
    // [RE 0x496BAC/496BAE/496BB0] 附身神明三属性修正（attachObject 0x40EAD7 加 /
    //   deleteMapObject 0x40E14D 减；表 kLuckA/B/C 0x4749E2/474A06/474A2A）
    // 语义（读取点）：A = 悲情/领先排名 10×A（0x437D1A）；B = AI 建设门槛 ≥0 +
    //   角色台词吉凶（0x41FACC/0x44B896）；C = 另一类台词（0x44B896 a3）
    int16_t luckA = 0;          // +68 word_496BAC（原误名 bonusAtk）
    int16_t luckB = 0;          // +70 word_496BAE（原误名 bonusDef）
    int16_t luckC = 0;          // +72 word_496BB0（原误名 bonusMove）
    uint16_t stayCorpIdx = 0;   // +74 word_496BB2（住宿的旅館 corp 索引，走进/走出移动目标）
    uint8_t pad76[16] = {};     // +76..+91 未建模（原版字段占位）
    // [RE 0x496BC4/0x496BC8] 月度结息累计（sub_437D1A 排名：2500×M×byte66 + A − B + 10×bonusAtk）
    // 依据: 0x437D4A sub_437D1A；0x439E5B 月初面板结束逐玩家清零 —— 语义待查
    int32_t monthSettleA = 0;   // +92 dword_496BC4
    int32_t monthSettleB = 0;   // +96 dword_496BC8
    uint8_t kind = 0;           // +100 g_playerKind 0x496BCC（1=人类 2=AI）
    // [RE 0x496BCC/0x496BCD 工程车复用] 原版工程车（0x4479D2）把「原载具」写回 kind(+100)、
    //   「原骰子数」写 byte_496BCD(+101)，到期由 sub_41C84F 恢复；重写用独立字段
    //   保持 kind 的 AI 判定语义（行为等价）
    uint8_t vehicleRestore = 0;  // 工程车暂存原载具 travel
    uint8_t diceRestore = 1;     // 工程车暂存原骰子数
    uint8_t pad103 = 0;          // +103
    // [RE 0x48BE24] dword_48BE24 每玩家右側资讯面板页签（0=資金 1=地產 2=股票 3=其他；
    //   0x417E26 点击 x∈[616,640) y∈[0,280) 时 = y/70 并重绘；0x4C 查詢面板复用同值）
    // 依据: 0x418313..0x418355（tab = y/70 → dword_48BE24[cur] = tab）；
    //       0x415F69 case 分派读该值；独立运行时全局数组，不入存档
    uint8_t panelTab = 0;
};

// MAPDAT 五表（docs/formats/mapdat.md）；原版各表 index 0 保留
// [RE 0x498E80] g_cellEnts 每项 40 字节
struct CellEnt {
    int16_t x = 0;              // +0  像素坐标
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint16_t exits[4] = {};     // +24..+30 连接道路的 cellEnt（出生点候选）
    uint16_t special = 0;       // +32 特殊标记（8001/8002）
    uint16_t sprite = 0;        // +34 图块索引（dword_474949 + 12*(idx-1)）
    uint32_t occMask = 0;       // +36 占用位掩码（256<<player）
};

// [RE 0x498E84] g_estates 每项 52 字节
struct Estate {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    char name[19] = {};         // +4..+22 住宅用地名（BIG5，原版 sprintf "%s\n\n" 显示）
    uint8_t flag = 0;           // +23 旗帜（&1）
    uint8_t type = 0;           // +24 类型
    uint8_t owner = 0;          // +25 拥有者（玩家号+1）
    uint8_t level = 0;          // +26 等级
    uint8_t dir = 0;            // +27 朝向
    uint16_t priceAdd = 0;      // +28 u16 地价附加（购地价 = level*priceBase + priceAdd）
    uint16_t priceBase = 0;     // +30 u16 地价基数（RE 0x41982D loc_41A013: level*word[+30]+word[+28]）
    uint16_t fees[6] = {};      // +32..+43 各等级费用/租金（u16 × 6，原版 sub_452793 显示 +32+2*level）
    int32_t price = 0;          // +44 收购价（sub_41982D）
    uint32_t expireDate = 0;    // +48 到期日期（dword_497160 比较，sub_41CF67）
};

// [RE 0x498E88] g_corps 每项 56 字节（商業用地及其上设施）
struct Corp {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23 地名（BIG5）
    uint8_t type = 0;           // +24 设施类型 0=公園 1=旅館 2=購物中心 3=加油站 4=研究所
    uint8_t owner = 0;          // +25 拥有者+1
    uint8_t sub = 0;            // +26 设施等级（0=无设施）
    uint8_t dir = 0;            // +27 朝向
    uint8_t flag = 0;           // +28 旗帜/查封（非 0 收费翻倍；&0x0F 非 0 时研究所不研发）
    uint8_t researchItem = 0;   // +29 研究所研发项目+1（0=未研发）
    uint8_t researchLeft = 0;   // +30 研发剩余（原版 =5）
    uint8_t pad31[3] = {};      // +31..+33
    uint16_t buildPrice = 0;    // +34 建设价（购地/建设施费用）
    uint16_t feeTable[6] = {};  // +36..+47 各等级费用表（等级 0..5）
    int32_t lastFee = 0;        // +48 最近收费（收租时写入；原名 price 误）
    uint32_t expireDate = 0;    // +52 到期日期（sub_41CF67）
};

// [RE 0x498E7C] g_specPts 每项 52 字节
struct SpecPt {
    int16_t x = 0;                // +0
    int16_t y = 0;                // +2
    uint8_t pad4[20] = {};        // +4..+23 名称（BIG5，nameAt 读取）
    uint8_t owner = 0;            // +24 经营権者（= 最大股东，sub_4294D5 更新）
    uint8_t stockNo = 0;          // +25 股票号（0=非上市；sub_428CAF 与股票表互查）
    uint8_t costType = 0;         // +26 收费类型（g_specPtCostMap 索引 0..15；landingEvent 分派）
    uint8_t dir = 0;              // +27
    uint8_t shareholders[4] = {}; // +28..+31 前 4 大股东（1-based 玩家号，0=空）
    uint16_t sprite = 0;          // +32 图块索引
    uint16_t feeBase = 0;         // +34 费用基数（收费公式 +34）
    int32_t capital = 0;          // +36 股本（每股价格 = capital / 10000，sub_41D1A9）
    int32_t fund = 0;             // +40 公库资金（sub_41D2C6 收支累计，分红来源）
    int32_t fundPaid = 0;         // +44 累计支出
    int32_t sharesLeft = 0;       // +48 可售股数（sub_41D1A9 认购上限）
};

// [RE 0x498E78] g_evtCells 每项 28 字节
struct EvtCell {
    int16_t x = 0;              // +0
    int16_t y = 0;              // +2
    uint8_t pad4[20] = {};      // +4..+23
    uint8_t dir = 0;            // +24
    uint8_t pad25 = 0;          // +25
    uint16_t sprite = 0;        // +26 图块索引
};

// [RE 0x498E28] g_miscTable80 事件槽记录（5 槽 × 16 字节，槽索引 i = p-4；i=0..3 事件槽玩家 4..7，
//   i=4 機器娃娃槽 8）。IDA 变量名逐字段对齐：
//     +0 word_498E28 pixelX / +2 word_498E2A pixelY / +4 word_498E2C cell / +6 word_498E2E prevCell
//     +8 byte_498E30 bailer / +9 byte_498E31 dir
//     +A byte_498E32 **busy 行动状态**（0=自由游走 1=監獄 2=醫院；槽8=3）
//        注意：绝对视角的 `byte_498DF2[16*p]`（p>=4）即本字段（0x498DF0+16*4+2 == 0x498E32），
//        旧文档误标为"独立槽占用/轮转位"——原版无第二字段（无任何写入点）。
//     +B byte_498E33 status（1=監獄 2=醫院；bit7=抓回闩锁，release 起点匹配即置位）
//     +C byte_498E34 timerA / +D byte_498E35 timerB（slotFlag5）/ +E byte_498E36 timerC /
//        +F byte_498E37 timerD：回合开始递减（1→0x80，下回合清 0）；timerA 归零重置动画。
//   开局关押 = newGameInit memcpy 自 unk_47ECEC（槽4/5 busy=1、槽6/7 busy=2、槽8 busy=3）。
struct NpcSlot80 {
    uint16_t pixelX = 0;
    uint16_t pixelY = 0;
    uint16_t cell = 0;
    uint16_t prevCell = 0;
    uint8_t bailer = 0;
    uint8_t dir = 0;
    uint8_t busy = 0;
    uint8_t status = 0;
    int8_t timerA = 0;
    int8_t timerB = 0;
    int8_t timerC = 0;
    int8_t timerD = 0;
};
static_assert(sizeof(NpcSlot80) == 16, "NpcSlot80 必须与 g_miscTable80 记录一致（16 字节）");

// [RE 0x4967E0] g_miscTable336 玩家交易挂单（工具条 case 9「公佈欄」；每玩家 7 槽 × 12 字节）
// 依据: 0x4246C5 queueTradeOrder 写入 / 0x42483E 清理 / 0x4255DA 成交 /
//       0x4284BE 入口（人类 UI / AI 自动）/ 0x428475 每日 age++ / 存档 0x403146/0x402CB8；
//   +0 类型 1=股票 2=地产 3=道具 4=卡片（旧文档 3/4 写反，见 4284be-trade-market.md）；
//   +2 objId（股票号 / estate+2000 / corp+4000 / 道具 id 1..13 / 卡片 id）；
//   +4 售价（现金）；+8 数量（仅股票）；+10/+11 地产 type/level 快照（失效检测）
struct TradeSlot {
    uint8_t type = 0;      // +0
    uint8_t age = 0;       // +1（每日 ++，原版无读取点）
    uint16_t objId = 0;    // +2
    int32_t price = 0;     // +4
    uint16_t count = 0;    // +8
    uint8_t snapType = 0;  // +10
    uint8_t snapLevel = 0; // +11
};
static_assert(sizeof(TradeSlot) == 12, "TradeSlot 必须与 g_miscTable336 记录一致（12 字节）");

// [RE 0x44808A 保存 / 0x448544 恢复] 時光機回合快照
//   原版布局 = g_playerMapBlocks[2502*i]（10008B：valid/date/players416/miscTable80/cellTable/
//   card/item/prop/gift/turnCounter/stockHistory/shares/stocks/miscTable336/标量/lottery/jail/
//   hosp/news·fate）+ g_playerMapDatCopies[2502*i]（mapDat 副本 dwSize）。
//   重写持有原版**字节布局**（block/mapDatCopy），与 SAVE%d.DAT 尾段共用同一序列化内核
//   （save_data.cpp writeSnapshotBlock/readSnapshotBlock）；存档 tail 直接搬运 block/mapDatCopy，
//   读旧档 tail 即灌入 snapshots[i] → 时光机读档后立即可用（用户方案：用存档喂时光机）。
//   保存点 = startPlayerMove 掷骰前（人类玩家）；恢复 = 时光机全状态回滚。
struct TurnSnapshot {
    bool valid = false;
    std::vector<uint8_t> block;       // 10008B g_playerMapBlocks 布局（0x44808A）
    std::vector<uint8_t> mapDatCopy;  // mapDat blob 副本（dwSize；地块状态含五表 + 占用位）
};

// 全局游戏状态，对应原版 DGROUP 段的全局句柄区（0x48A0xx / 0x4990xx）。
struct GameState {
    // [RE 0x48A0E4] dword_48A0E4
    // 依据: 0x4015D6 中 sub_4502FE("data.mkf") 的返回值; 0x450441 以其为资源库句柄
    MkfArchive data;

    // [RE 0x48A054] dword_48A054
    // 依据: 0x4015D6 中 sub_4502FE("speaking.mkf") 的返回值
    MkfArchive speaking;

    // [RE 0x48A05C] dword_48A05C
    // 依据: 0x4015D6 中 sub_4502FE("panel.mkf") 的返回值
    MkfArchive panel;

    // [RE 0x48A058] dword_48A058
    // 依据: 0x4015D6 中 sub_4502FE("effect.mkf") 的返回值
    MkfArchive effect;

    // [RE 0x497158] byte_497158 设置区（16 字节，格式见 docs/formats/cfg.md）
    // 依据: 0x411E8F CFG 不存在时初始化 byte_497158=1/497159=1/49715A=4/49715B=4/
    //       49715C=1/49715D=1；存在时由 CFG 前 16 字节覆盖
    uint8_t settings[16] = {1, 1, 4, 4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // [RE 0x46CB05] byte_46CB05
    // 依据: 0x401815 清理函数的幂等标志（非 0 时跳过清理）
    bool shutdownDone = false;

    // [RE 0x4991B6] word_4991B6
    // 依据: WinMain case 4 设置（新模式）; 0x406DE7 中参与资源索引 4*mode+map
    int gameMode = 0;

    // [RE 0x4991B8] word_4991B8
    // 依据: 0x406DE7 中地图索引; 0x407AD2 中参与 MAP.MKF 索引
    int mapIndex = 0;

    // [RE 0x499114] dword_499114
    // 依据: 0x406DE7 中 dword_499114 = dword_46CB3C + 2（玩家数）
    int playerCount = 4;

    // [RE 0x46CAFC] byte_46CAFC 是否已进入过游戏（新游戏配置保留标志）
    // 依据: WinMain case 0/4 调 sub_406DE7(byte_46CAFC)；进入游戏后置 1，
    //       再次进入时 lParam!=0 → 0x406DE7 保留上次选人配置并自动確定
    bool gameInited = false;

    // 选人界面配置（0x404E44 结果，跨"重新开始"保留）
    NewGameConfig newGameConfig;

    // [RE 0x49910C] dword_49910C
    // 依据: 0x417E26 中 *dword_49910C 为当前玩家索引
    int currentPlayer = 0;

    // 读档对话框选择的槽位（-1 无）；对应 0x40257A case 1 经 postModalExit 回传的索引
    int pendingLoadSlot = -1;

    // [RE 0x47493C] 完整存档数据（0x402AC5 读取的全部状态，供后续恢复）
    std::vector<uint8_t> saveData;

    // [RE 0x497160] dword_497160 游戏内日期（BYTE0=日, BYTE1=月, HIWORD=年）
    // 依据: 0x411E8F loadConfig 尾部 dos_getdate 钳位 [1998,2010] 初始化（尾跳转 0x411A7C）;
    //       0x4119E3 日期更改确认后写入; 0x411AA3 重新遊戲=系统日期; 0x402AC5 读档恢复;
    //       0x41CF67 advanceDay 过天。0x406DE7 新开局**不**重置本值。
    //       内存上与 byte_497158+8 重叠（RICH4.CFG 设置区第 8..11 字节随设置一并存盘）
    uint32_t gameDate = 0;

    // [RE 0x48BB50] dword_48BB50 上次手动确认的日期（dateDialog 工作初值，0x410AC3 case 7 初值）
    // 依据: 0x4119E3 确认分支 0x411A77 与 dword_497160 双写；loadConfig 尾跳转跳过此写，
    //       故启动后本值仍为 0（原版如此），仅日期更改对话框使用
    uint32_t lastConfirmedDate = 0;

    // [RE 0x497164] byte_497164 日历显示模式（0=大数字日历, 1=月历网格）
    // 依据: 0x411E8F loadConfig 读 cfg 第12字节(默认0); 0x417E26 gameWndProc 点击
    //       x∈[448,474]→0、x∈[478,504]→1（日历区左右箭头切换），切换后 sub_4169BC(1) 重绘
    int calendarMode = 0;

    // [RE 0x46CB06] 游戏内音乐切换计时器 g_musicTimer → **迁入 Audio::switchDays**
    //   （2026-09-29 音乐机制修正：原版 playSceneMusic/PopRestore 门控与过天周期切歌
    //   都读该全局，收口到 Audio；见 audio.h / 41cf67 音乐段）

    // [RE 0x451387] 骰子 FLC 落地音效标志：FLC 第30帧触发一次 + 播放结束一次（共两次槽2）
    bool diceLandPlayed = false;

    // [RE 0x48A354] dword_48A354 地图预览缓冲（614400 = 640*480*2 全屏 RGB555）
    // 依据: 0x406DE7 分配 0x96000 字节并由 JUMP.MKF 地图图填充
    std::vector<uint8_t> mapPreview;

    // JUMP.MKF 句柄（0x406DE7 临时打开；保留以复用地图预览图）
    MkfArchive jump;

    // MAP.MKF 句柄（0x407AD2 临时打开；地图数据指针在游戏期间常驻）
    MkfArchive map;

    // ===== 游戏内状态（0x406DE7 确认后 / 0x407AD2 / 0x4190CF）=====

    // [RE 0x496B68] g_players（原版 9 槽，仅 g_playerCount 有效）
    Player players[9];

    // [RE 0x48CB80] 時光機回合快照（每玩家一份；0x44808A 保存 / 0x448544 恢复）
    TurnSnapshot snapshots[4];

    // [RE 0x498E80/0x498E84/0x498E88/0x498E7C/0x498E78] MAPDAT 五表（index 0 保留）
    std::vector<CellEnt> cellEnts;
    std::vector<Estate> estates;
    std::vector<Corp> corps;
    std::vector<SpecPt> specPts;
    std::vector<EvtCell> evtCells;

    // [RE 0x474945] g_gnd GND 地图数据（MAP.MKF[2*(4*mode+map)]）
    // 布局: +16 调色板 u16[256] / +528 格索引 u16[5184] / +10896 图块 8bit[5184*1024]
    std::vector<uint8_t> gnd;
    // [RE 0x48B6B4] g_gndPalette（= gnd + 16）
    const uint16_t* gndPalette = nullptr;
    // [RE 0x48BAC4] g_gndCellIndex（= gnd + 528）
    const uint16_t* gndCellIndex = nullptr;
    // [RE 0x48BACC] g_gndBitmaps（= gnd + 10896）
    const uint8_t* gndBitmaps = nullptr;

    // 地图图块资源（map.mkf）
    UiImage cellEntTiles;         // [RE 0x474949] map.mkf[24]
    UiImage flagTiles;            // [RE 0x47494D] map.mkf[26]
    UiImage pieceTiles;           // [RE 0x48AEA8] map.mkf[25]
    UiImage estateTiles;          // [RE 0x48BAD8] data.mkf[517]（住宅用地/商業用地标記）
    UiImage mapTiles519;          // [RE 0x48BAD4] data.mkf[519]
    UiImage miniMap;              // [RE 0x48BADC] map.mkf[map+4*mode+16]
    UiImage miniMapRaw;           // [RE 0x48BAD0] map.mkf[map+4*mode+16]
    // [RE 0x48BADC] dword_48BADC 小地图工作副本（200x200，叠加住宅用地/商業用地标記）
    std::vector<uint16_t> miniMapBuffer;
    // [RE 0x48BADC] dword_48BADC 大地图工作副本（= g_miniMapWork 帧 1，400x400；
    //   原版 rebuildMiniMap(1) 的产物，帧 26/28/28 标记 + 11392 缩放）
    std::vector<uint16_t> bigMapBuffer;
    UiImage specPointTiles[5];    // [RE 0x48AE4C] map.mkf[n+5*(map+4*mode)+39]
    // [RE 0x48AE4C] 特殊点/事件格图块缓存（索引 = sprite 值 134..259）
    //   原版 dword_48AE4C[sprite] = map.mkf[sprite + 38]（loadMapData 按需加载）
    std::vector<UiImage> specTiles;
    UiImage estateFlag;           // [RE 0x48AE60] map.mkf[map+79+4*mode]
    // [RE 0x48AE64] 商業用地图块组（17 项 = map.mkf[87..103]；mode1 = 17*map+104..120）
    //   注意 dword_48AE78/0x48AE90 不是独立数组，而是本数组 +5/+11 项别名：
    //   購物中心用 [5+sub]、加油站用 [11]、研究所用 [11+sub]（见 map_render 绘制）
    UiImage corpTiles[17];
    UiImage pieceSprites[9];      // [RE 0x498EB0] map.mkf[charIndex+27]
    UiImage pieceAnim[9];         // [RE 0x498EB4] data.mkf[21*charIndex+128+3*travel]
    UiImage cellTypeSprites[46];  // [RE 0x49692C] data.mkf[cellType+395]（索引 1-20 有效）

    // 面板资源（panel.mkf）
    UiImage panelFrame0;          // [RE 0x48BE0C] panel.mkf[0]
    UiImage panelTopBar;          // [RE 0x475118] panel.mkf[1]
    UiImage panelFrame2;          // [RE 0x48BE10] panel.mkf[2]
    UiImage panelFrame3;          // [RE 0x48BE14] panel.mkf[3]
    // [RE 0x48BDD0] 节日背景缓存（data.mkf[kCalendarRes[base]+idx] RAW 200x200），idx 变化才重载
    std::vector<uint8_t> holidayBgData;
    int holidayBgIdx = -1;
    UiImage panelFrame7;          // [RE 0x48BE04] panel.mkf[7]
    // [RE 0x48BE08] panel.mkf[8] 是 72x67 无头字节控件图（非 SPR/SMP），
    //   命中 ctrl = 像素值 + 10：11=骰子数、12=拖动、13=GO
    std::vector<uint8_t> panel8Mask;
    // [RE 0x48BDF8/0x48BDF4] 骰子滚动动画 panel.mkf[4/5/6]（FLC，按骰子数 1/2/3）
    //   原版 sub_419572 用 sub_45144F 播放；非 UiImage（无 SPR/SMP 魔数，loadUiImage 会失败）
    // [RE 0x40D7C4 case2] 掷骰三阶段：0=角色扔骰动画, 1=骰子滚动FLC, 2=点数停留
    int dicePhase = 0;
    FliDecoder diceAnim;          // 当前掷骰滚动动画（panel[骰子数+3]）
    bool diceAnimActive = false;  // 阶段1 滚动播放中
    bool diceAnimOpened = false;  // 阶段1 已 open（防每 tick 重载；角色保持扔骰末帧）
    uint64_t diceAnimLastMs = 0;  // [NEW M4-C2] 骰子 FLC 帧节拍：帧延时 = 10*dword_475264[settings[0]]（50/30/20ms）
    uint64_t diceHoldStartMs = 0; // 阶段2 点数停留起始（原版 sub_45285E(500)）
    uint64_t walkAnimLastMs = 0;  // 走路动画换帧计时（限帧率，避免 60fps tick 下偏快）

    // [RE 0x496D08] g_cellTable 每项 24 字节（+0 = 类型）
    std::vector<uint8_t> cellTable;

    // [RE 0x417559] 右键物件提示：每帧渲染记录物件屏幕矩形（已含 offset），右键命中后
    //   显示名称/地主/价值等；左键或其他操作清除
    struct MapHitRegion {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        // 同原版拾取缓冲 ID：cellEnt 1..N、2000+ 地产、4000+ 公司、6000+ 特殊点、
        // 8000+ 事件格、槽号<<8 cellTable 物件、0xF000| 玩家
        uint16_t id = 0;
        // [RE 0x409B18] 拾取形状（SPR 像素掩码，精确命中；null = 用矩形近似）
        const UiImage* shape = nullptr;
        int shapeFrame = 0;
        int anchorX = 0; // 形状绘制锚点（blit 的 x/y）
        int anchorY = 0;
        // [PORT 2026-09-26] 来源格 id（仅"格命中"记录；item/玩家/物件命中 = 0）。
        //   地块格归一化后 id 变对象 id（2000+），查看提示借源格显示 cellEnt+4 地名（城市名）
        uint16_t cellId = 0;
        // [RE 0x409B18/0x451985] 高亮形状（原版拾取掩码 g_pickMask：estate 帧 0/1、
        //   corp 帧 2/3，落点 = 绘制锚点 y-40）；拾取形状 shape 为体感改用帧 4，
        //   highlightBlink 用 hl* 恢复原版形状/位置；无主空地 hlShape=null（原版不写缓冲）
        const UiImage* hlShape = nullptr;
        int hlFrame = 0;
        int hlAnchorX = 0;
        int hlAnchorY = 0;
    };
    std::vector<MapHitRegion> mapHitRegions;
    // [RE 0x48BE58/0x48BE5C] AI 用卡预选目标（aiCardSelect 写、卡效果 AI 分支 sub_41E6F2(0/1) 读）
    //   语义随卡：玩家掩码 0x8000|mask / 地块 objId / 股票 0-based 索引 / 改建设施号；不入存档（原版同）
    int aiCardTarget = 0;
    int aiCardTarget2 = 0;
    // [RE 0x48BE64] AI 用道具预选（aiItemSelect 写、道具效果 AI 分支 sub_420EEE(idx) 读；
    //   语义随道具：目标格 id（cellEnt）/ 遥控骰子点数 1..6 / 核弹 objId；不入存档）
    int aiItemTarget = 0;
    std::string objectTipText; // 非空时显示提示框
    int objectTipX = 0;
    int objectTipY = 0;

    // [RE 0x451985] 收租联动高亮（整条街/连锁店闪烁）
    // 依据: 0x419A67 收租时标记同 owner 同名住宅（或全部连锁店）→ v141>1 时
    //       sub_451985 播放 16 帧亮度闪烁（byte_476380，30ms/帧）+ 400ms 停留；
    //       sub_4554FC 按 g_pickBuffer 0xFFFF 标记重绘高亮像素
    // 迁移: 重写无 pickBuffer，改用 mapHitRegions 的 estate 绘制区域做 RGB555 亮度偏移；
    //       帧推进/停留由阻塞播放 playHighlightBlink 控制，drawEstateHighlight 仅渲染
    std::vector<int> highlightEstates; // 联动组 estate idx（含自身）
    // [RE 0x451985] corp 单块高亮（新闻 idx 6/14/18/19 的商業用地分支；原版 markPickBuffer(corp)
    //   单块闪烁；重写高亮支持 estate+corp 两类，二选一使用）
    std::vector<int> highlightCorps;   // corp idx
    // [RE 0x451985] 高亮形状快照（原版 pickBuffer 标记语义：启动时从已渲染场景提取，
    //   播放期间不随地图数据变化重建——拆除后的地块仍可高亮，如新闻 idx 19 山洪）
    struct HighlightShape {
        const UiImage* shape = nullptr; // g_pickMask（estate 帧 0/1 / corp 帧 2/3）
        int frame = 0;
        int x = 0; // 落点左上角（已含帧 offset；原版 = 锚点 y-40）
        int y = 0;
    };
    std::vector<HighlightShape> highlightShapes;
    int highlightFrame = -1;           // -1=未激活；0..15=闪烁帧；16=400ms 停留期

    // ===== 经济/回合全局 =====
    int32_t startMoneyVal = 0; // [RE 0x49908C] g_startMoneyVal
    int32_t gameDaysLimit = 0; // [RE 0x49911C] g_gameDaysLimit
    int32_t winMoney = 0;      // [RE 0x499108] g_winMoney
    int32_t humanCount = 0;    // [RE 0x499104] g_humanCount
    int32_t moneyMul = 1;      // [RE 0x4990E8] g_moneyMul
    int32_t dayCount = 0;      // [RE 0x4990E4] g_dayCount
    int32_t turnCounter = 0;   // [RE 0x499100] g_turnCounter
    int32_t cfgTravel = 0;     // [RE 0x499118] g_cfgTravel
    int32_t cfgLandPerm = 0;   // [RE 0x499110] g_cfgLandPerm

    // [RE 0x496980] g_stocks（每支 9×u32=36B 的 2D 视图，字段按原版偏移）：
    //   [0] 名称 VA（(&g_stocks)[9*i]，股票名 BIG5 指针；显示用 kStockNames）
    //   [1] specPt 索引（word_496984[18*i]，sub_428CAF 反查；0=非上市）
    //   [2] 保留股份/交易量（word_496988 低 u16 + word_49698A 高 u16 的原始位模式；
    //       显示用下方独立 stockReserved/stockVolume 数组）
    //   [3] 基准价  [4] 昨收  [5] 当前价  [6] 波动率  [7] 动量  [8] 扰动
    float stocks[12][9] = {};
    uint8_t propStock[30] = {};      // [RE 0x499198] g_propStock
    int32_t playerShares[4][12] = {}; // [RE 0x4971A0] g_playerShares（持股数，原版 [24*p+2*s]）
    // [RE 0x4971A4] flt_4971A4 持股均价（买入加权平均）
    float playerAvgCost[4][12] = {};
    int32_t stockHistory[12][144] = {}; // [RE 0x497328] g_stockHistory（int32 存 float 位模式）
    uint8_t stockHalted[12] = {};    // [RE 0x496986] byte_496986 停牌剩余天数
    uint8_t stockNews[12] = {};      // [RE 0x496987] byte_496987 新闻（高4位涨基数/低4位剩余）
    int32_t stockReserved[12] = {};  // [RE 0x496988] word_496988 保留股份（买减卖加，loadMapData 初值）
    int32_t stockVolume[12] = {};    // [RE 0x49698A] word_49698A 交易量（sub_42915A 每日生成）
    int32_t stockMarketClosed = 0;   // [RE 0x4990DC] dword_4990DC 休市剩余天数（bit7=到期标志）
    int32_t stockDivPeriod = 1;      // [RE 0x499084] dword_499084 分红期数（走势图累積盈餘除数）
    // [RE 0x499080] dword_499080 系统公库：卖股入系统 + 意外损失（transferMoney to=-1）
    //   + 乐透投注/奖金池（sub_4315CC +1000 / 开奖发放清零）
    int32_t publicFund = 0;
    // [RE 0x4990F0] g_clearedMaps 通关进度（每地图 1 字节；0x4075C1 标记、进存档、回主菜单清零）
    //   通关结算界面属 M3，本字段先随存档字段序读写保偏移
    uint8_t clearedMaps[4] = {};
    int32_t stockTotalValue = 0;     // [RE 0x49907C] dword_49907C 股票总市值（Σ基准价×10）
    int32_t marketIndex = 0;         // [RE 0x499078] dword_499078 大盘指数（Σ价格×10）
    float stockGlobalDrift = 0.0f;   // [RE 0x4990EC] dword_4990EC 全局扰动
    uint8_t cardState60[60] = {};    // [RE 0x499120] g_cardState60 卡片卡包（4 玩家 × 15 槽，存卡 id）
    // [RE 0x49915C] byte_49915C / g_playerCards 玩家**道具库存**（4 × 15 槽，索引 = 道具 id 1..9，
    //   每槽数量上限 9；givePlayerCard 0x445A4D 写/ takePlayerCard 0x445AA2 减；原误名 cardPool60）
    uint8_t itemStock[60] = {};
    // [RE 0x499090] g_newsOrder **新闻事件顺序表**（36 项，newGameInit 洗牌；newsEvent 0x44B6DF 抽取）
    uint8_t newsOrder[36] = {};
    uint8_t fateOrder[37] = {};      // [RE 0x496B38] g_fateOrder 命运事件顺序表（37 项，newGameInit 0x44BAEA 洗牌；fateEvent 0x44DB81 抽取）
    int32_t newsPos = 0;             // [RE 0x4990E0] g_newsPos 当前新闻索引（0..35 循环）
    int32_t fatePos = 0;             // [RE 0x4990B4] g_fatePos 当前命运索引（0..36 循环）
    // [RE 0x4990B8] g_miscTable36A 乐透号码归属（36 号，0=未售，N=玩家 N-1；
    //   投注 sub_4315CC/42F7FC 写、开奖 431712 读、破产 sub_40CD87 清该玩家号）
    uint8_t lotteryNumbers[36] = {};

    // [RE 0x498E28] g_miscTable80 事件槽记录（槽 4..8 各 16 字节，见 NpcSlot80）
    NpcSlot80 npcSlots[5] = {};
    // [RE 0x4967E0] g_miscTable336 交易挂单（工具条 case 9「公佈欄」；玩家 × 7 槽，见 TradeSlot）
    TradeSlot tradeSlots[8][7] = {};
    // [RE 0x497320] byte_497320 **礼物卡池**（8 种可赠卡库存；drawGiftCard 0x445ADA 展开随机抽；
    //   死神没收卡片 0x445B3F 归还；kMisc8Init 初值。其中 [4]/[5]（0x497324/25）兼作
    //   机车/汽车回收槽：damagePlayer 0x40CD3B/43 载具损毁 ++、newGameInit 初始载具 --、
    //   百货 AI 0x42F1B1/0x42F211 购买条件）
    uint8_t misc8A[8] = {};
    // [RE 0x497321] byte_497321..23 路障/地雷/定時炸彈 回收计数（deleteMapObject 0x40E14D
    //   删除对应类型 +1；[TODO P4] 消耗点未定位——放置函数 0x446BAA/446C88/446D69 仅扣
    //   玩家道具库存 byte_49915D/E/F，未递减本组）
    uint8_t trapStock[3] = {};
    // [RE 0x496BB4] g_playerDebts 欠款矩阵：playerDebt[creditor][debtor]（addPlayerDebt 0x40DF69
    //   累加 clamp≥0、记账解除同盟；原版行长 26 仅前 8 列使用；清偿点 = 破产 sub_40CD87 /
    //   最大债主查询 sub_40D2D3，结算接入 [TODO]）行距 26 照原版；不入存档（原版
    //   回合快照 sub_44808A 与 SAVE 流均无本表）
    int32_t playerDebt[8][26] = {};
    // [RE 0x496B30 / 0x496B60] 監獄/醫院在押标志（8 字节；索引 0..3 玩家 / 4..7 事件槽 NPC）
    uint8_t jailFlags[8] = {};
    uint8_t hospitalFlags[8] = {};
    // [RE 0x48BAE0 / 0x48BAE2] loadMapData 扫描 special 8002（監獄）/ 8001（醫院）的 cellEnt
    uint16_t jailCellEntId = 0;
    uint16_t hospitalCellEntId = 0;

    // [RE 0x499088] dword_499088 地图旋转方向（0-7）
    int mapRotation = 0;
    // [RE 0x48B2AC/0x48B2B0] 地图视口中心像素坐标（上次绘制值）
    int viewX = 0;
    int viewY = 0;
    // [RE 0x475118] dword_475118 面板资源已加载标志（0x4190CF 幂等判断）
    bool panelLoaded = false;

    // ===== 游戏内交互状态（0x417E26）=====
    // [RE 0x48BE18] dword_48BE18：非 0 时用 dword_48BE1C/BE20 作为视口中心（手动视角）
    bool manualView = false;
    // [RE 0x48BE1C/0x48BE20] 手动视口中心（小地图拖拽设置，限幅 220..2084）
    int viewTargetX = 0;
    int viewTargetY = 0;
    // [NEW] 小地图点击平滑跳转：当前平滑视口中心与进行中标志（原版为直接跳转）
    int viewSmoothX = 0;
    int viewSmoothY = 0;
    bool viewScrolling = false;
    // [RE 0x48BE29] byte_48BE29：小地图拖拽中
    bool dragMiniMap = false;
    // [RE 0x48BE28] byte_48BE28：待处理操作（1/2=旋转，>=100=工具条按钮 index-100）
    int pendingAction = 0;
    // [RE 0x48BDE4] dword_48BDE4：工具条悬停按钮索引（-1 无）
    int topBarHover = -1;
    // ===== 游戏内系统菜单结果（0x411B53 / 0x401B9C 内层循环）=====
    // [RE 0x46CAF8] g_sceneRequest：场景切换请求（重新遊戲 → 1，回主菜单）
    int sceneRequest = 0;
    // [RE 0x46CAF9] g_quitGame：退出请求（結束遊戲）
    bool quitGame = false;
    // [RE 0x411AE0] 認輸投降请求（当前玩家出局）
    bool surrenderRequest = false;
    // [NEW] debug 脚本用：下一游戏循环 tick 触发 eliminatePlayer(p)（0x40CD87 全链含演出，
    //   经 tick 处理避免脚本 step 内嵌套 runModal/playEventFlc，同 surrenderRequest 模式）
    int bankruptRequest = -1;

    // ===== 回合/移动状态机（0x40D7C4）=====
    // 每玩家 52 字节步进数组（0x498EA0 起，索引 = 玩家号 0..8）
    // [RE 0x498EA0] byte_498EA0 行动标志位（bit7=待开始回合 0x30=状态效果 0x0F=动画组）
    uint8_t playerActionFlags[9] = {};
    // [RE 0x498EA1] byte_498EA1 移动动画组（0=普通 1=特殊/载具）
    uint8_t playerMoveGroup[9] = {};
    // [RE 0x498EA2] byte_498EA2 行动状态: 0=收尾倒计时 1=移动中 2=掷骰 3=特殊移动
    uint8_t playerActionState[9] = {};
    // [RE 0x498EA3] byte_498EA3 移动动画帧/已走步数
    uint8_t playerMoveFrame[9] = {};
    // [RE 0x498EA4] byte_498EA4 行走动画帧偏移（到达后循环）
    uint8_t playerActionExtra[9] = {};
    // [RE 0x498EA5] byte_498EA5 状态 0 倒计时（负数 = 直接下一位玩家）
    int8_t playerActionWait[9] = {};
    // [RE 0x498EA8/0x498EAC] flt_498EA8/EAC 每玩家浮点像素坐标
    float playerStepX[9] = {};
    float playerStepY[9] = {};

    // [RE 0x46CB01] byte_46CB01 游戏内运行标志
    bool gameLoopActive = false;
    // [RE 0x46CAFA] byte_46CAFA 本帧需运行状态机
    bool gameFrameTick = false;
    // [RE 0x46CAFB] byte_46CAFB 状态机激活
    bool gameStateActive = false;
    // [RE 0x46CAFD] byte_46CAFD 玩家可操作（前进面板可用）
    bool gamePlayerControl = false;
    // [RE 0x46CAFE] byte_46CAFE 系统菜单/暂停态
    bool gamePauseMenu = false;
    // [RE 0x46CAFF] byte_46CAFF 取消键触发跳过回合
    bool gameSkipRequest = false;
    // [NEW] 调试模式（命令行 --debug 启用；测试辅助，无原版对应）
    // 用途: Ctrl+1..8 修改地块所属/等级/连锁店、批量铺地、加现金、打印地块详情
    bool debugMode = false;
    // [NEW] --quickstart（headless 测试开局，docs/testing.md；无原版对应）：
    //   run() 跳过主菜单模态、newGameInit 跳过选人对话框，直接用下列配置开局。
    //   quickHumans: 前 N 人为人类（其余 AI）；headless L1 场景=1、L2 长跑=0（全员 AI 自动）
    bool debugQuickstart = false;
    int quickMap = 0;
    int quickPlayers = 4;
    int quickHumans = 1;

    // [RE 0x475110] dword_475110 脏区标志（bit1=地图区）
    uint8_t dirtyFlags = 0;
    // [RE 0x475118] dword_475118 工具条资源已加载（游戏内标志）
    bool gamePanelReady = false;

    // [RE 0x48BAF8] dword_48BAF8 剩余移动步数（= 骰子点数）
    int remainingSteps = 0;
    // [RE 0x48BAFC] g_diceValue 本次行走总步数（掷骰定格时记录，供加油費/修車費等
    //   "依行走步數收費"公式使用；移动中不递减）
    int diceValue = 0;
    // [RE 0x48BB00] byte_48BB00 待处理落地事件
    bool stepActionPending = false;
    // [RE 0x4749DC] dword_4749DC 单格移动剩余帧
    int stepFrames = 0;
    // [RE 0x48BAE4/0x48BAE8] dword_48BAE4/E8 目标像素坐标
    int stepTargetX = 0;
    int stepTargetY = 0;
    // [RE 0x48BAEC/0x48BAF0] flt_48BAEC/F0 每帧位移
    float stepDeltaX = 0.0f;
    float stepDeltaY = 0.0f;
    // [RE 0x48BAF4] dword_48BAF4 半步点（载具状态切换）
    int stepHalf = 0;
    // [RE 0x4749E0] byte_4749E0 到达后停留帧
    int stepDwell = 0;
    // [NEW] 走进旅館(0x20)/走出旅館(0x10)移动标志（半程清状态用；0=普通移动）
    uint8_t stepExitState = 0;
    // [NEW] 走出旅館完成 → 跳过落地结算（不重复收旅館费）
    bool stepSkipLanding = false;

    // [RE 0x4749D4] dword_4749D4 当前移动音效索引
    int moveSoundIndex = 0;
    // [RE 0x475284] qword_475284 前进面板屏幕位置
    int advancePanelX = 180;   // [RE 0x475284] 前进面板左上角初值 (180,120)
    int advancePanelY = 120;
    int advancePanelW = 72;    // [RE 0x48BDD8] 面板宽 = panel[7] 帧0 宽
    int advancePanelH = 67;    // [RE 0x48BDDC] 面板高 = panel[7] 帧0 高
    // [RE 0x48BDD4] dword_48BDD4 前进面板闪烁帧（0/1，GO 金色高亮）
    int advancePanelBlink = 0;
    // [RE 0x419572] 骰子点数（最多 3 个）与显示标志
    int diceValues[3] = {0, 0, 0};
    bool showDice = false;
    // [RE 0x475DD8] byte_475DD8 遥控骰子强制点数（一次性：rollDice a2 = sub_447285() 读后清零；
    //   非 0 时骰子数强制为 1、点数 = 该值；case2 骰子 FLC 也按单骰选择）
    uint8_t forcedDice = 0;
    // [RE 0x419572 v2] 本次掷骰显示的点数精灵个数（遥控单骰 = 1，否则 = diceCount）——
    //   rollDice 写入、map_render 停留期画点数用（原版 v2 = forced ? 1 : byte_496B7A）
    uint8_t diceShowCount = 1;

    // [RE 0x498E68..0x498E77] 機器娃娃（虚拟槽 8）**无独立字段**——使用槽记录 `npcSlots[4]`
    //   （基址 0x498E68 = 0x498E28+16*4）：pixelX/Y(498E68/6A)、cell(498E6C)、prevCell(498E6E)、
    //   bailer=发起玩家(498E70)、dir(498E71)、busy(498E72：0=使用中；结束 0→3)；0x446AFB 写入。

    // 事件槽（玩家 4..8）专用状态见上方 GameState::npcSlots（[RE 0x498E28] g_miscTable80）。

    // [RE 0x498EB4] dword_498EB4 行走动画资源（每玩家 13 槽：EB4[13n+2*state+group]）
    UiImage walkRes[9][13];

    // [RE 0x475114] dword_475114 待入场玩家（+1；0 = 无）
    int pendingSpawnPlayer = 0;    // [RE 0x45144F] 跳伞动画（data.mkf[charIndex+559] FLC）
    FliDecoder parachute;
    bool parachuteActive = false;
    int parachuteTimer = 0;
    uint32_t parachuteLastMs = 0;
    // [RE 0x45144F / 0x4506C7] 事件 FLC 叠加（得點券等；透明模式：索引 0 保留目标像素）
    FliDecoder eventFlc;
    bool eventFlcActive = false;
    int eventFlcX = 0;
    int eventFlcY = 0;
};

} // namespace rich4
