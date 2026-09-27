#pragma once

namespace rich4 {

class Application;

// [RE 0x48123A] 卡片台词表：12 角色 x 90 槽（角色 stride 360 字节），由
//   tools/gen_card_lines.py 生成（src/app/card_lines.cpp）；文本含 '#NNNN' 语音前缀 /
//   '#NNNN@MM' 表情帧（经 item_lines.h playLine 渲染）。
//   逐卡槽映射见 docs/reverse/functions/441baa-card-effects.md §台词。
extern const char* const kCardSpeech[12][90];
// [RE 0x48089E] 受害台词（价值表 0x48084A 第 21 列；梦游受害者台词）
extern const char* const kCardVictimLines[12];

// 槽常量（使用者槽 = 卡 id - 1，见 kCardSpeech 表）
constexpr int kSpeechTurnSelf = 35;        // 转向：被转向者为自己
constexpr int kSpeechStaySelf = 43;        // 停留：对自己
constexpr int kSpeechTurtleSelf = 59;      // 烏龜：对自己
constexpr int kSpeechEqualPoorTarget = 61; // 均貧：目标
constexpr int kSpeechBuyLandSeller = 62;   // 購地：原主
constexpr int kSpeechSwapLandOther = 63;   // 換地：对方
constexpr int kSpeechSwapHouseOther = 64;  // 換屋：对方
constexpr int kSpeechTurnTarget = 65;      // 轉向：目标
constexpr int kSpeechAuctionOwner = 67;    // 拍賣：原主
constexpr int kSpeechMonsterOwner = 70;    // 怪獸：原主
constexpr int kSpeechDemolishOwner = 71;   // 拆除：原主
constexpr int kSpeechStealTarget = 72;     // 搶奪：目标
constexpr int kSpeechStayTarget = 73;      // 停留：目标
constexpr int kSpeechFrameTarget = 76;     // 陷害：目标
constexpr int kSpeechRevengeUser = 77;     // 復仇：使用者（被反伤方）
constexpr int kSpeechPassOnTarget = 78;    // 嫁禍：被嫁祸者
constexpr int kSpeechFreeOther = 79;       // 免費：对方（收费方）
constexpr int kSpeechTaxTarget = 85;       // 查稅：目标
constexpr int kSpeechAllyTarget = 88;      // 同盟：目标
constexpr int kSpeechTurtleTarget = 89;    // 烏龜：目标

// [RE 0x44EF41] playCardLine：按槽位播角色气泡台词（playLine 语义：语音/表情/状态守卫/去重；
//   exprFrame = 头像表情帧索引，见 item_lines.h playLine）
void playCardLine(Application& app, int player, int slot, int exprFrame = 0);

} // namespace rich4
