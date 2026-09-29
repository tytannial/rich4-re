#include "game/app/turn_system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "game/application.h"
#include "game/app/ai_item.h"  // aiDiceAdjustPerTurn [RE 0x4221C0]
#include "game/app/bank_dialog.h"
#include "game/app/bank_stay_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/card_effects.h"
#include "game/app/card_lines.h"
#include "game/app/confirm_dialog.h"
#include "game/app/date_util.h"
#include "game/app/dividend.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/facility_dialog.h"
#include "game/app/fate_event.h"
#include "game/app/game_loop.h"
#include "game/app/game_panel.h"
#include "game/app/item_bag_dialog.h"
#include "game/app/item_lines.h"
#include "game/app/jail_dialog.h"
#include "game/app/lab_dialog.h"
#include "game/app/lottery.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/minigame.h"
#include "game/app/victory.h"
#include "game/app/month_settle.h"
#include "game/app/news_dialog.h"
#include "game/app/roulette_dialog.h"
#include "game/app/shop_dialog.h"
#include "game/app/spec_pt_dialog.h"
#include "game/app/stock_system.h"

#include "game/app/save_data.h"
#include "game/app/target_select_dialog.h"
#include "game/app/trade_market.h"
#include "game/core/encoding.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
#include "game/core/clock.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x4749D8] byte_4749D8 每格移动帧数（步行/机车/汽车/第四）
constexpr int kMoveSpeed[4] = {8, 12, 16, 8};
// [RE 0x4631DC] flt_4631DC = 0.125（载具/特殊状态固定速度系数）
constexpr float kMoveScale = 0.125f;
// [RE 0x498EC4] 骰子/掷骰动画帧数（原版取资源头 >>3；B4 接入资源前用固定值）
constexpr int kDiceAnimFrames = 12;
// [RE 0x419572] 掷骰后停留帧数（原版 sub_45285E(500) 约 500ms 显示点数）
constexpr int kDiceHoldFrames = 30;
// [RE 0x482414] byte_482414 方向映射表
constexpr uint8_t kDirTable[8] = {2, 3, 4, 5, 6, 7, 0, 1};

int daysInMonth(int year, int month) {
    static const int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4) == 0) {
        return 29;
    }
    return kDays[month];
}

// [RE 0x41D7D4] AI 买地决策：现金+存款−地价 > 物價指數 × min(初始资金×5%, 7000) → 购买
// 依据: 0x41D7D4 反编译（dbl_463CC8=0.05，上限 7000；g_startMoneyVal/g_moneyMul/g_currentPlayer）
bool aiBuyDecision(const GameState& st, const Player& pl, int32_t price) {
    const int32_t reserve = std::min(static_cast<int32_t>(st.startMoneyVal * 0.05), 7000);
    return (pl.bank + pl.cash - price) > st.moneyMul * reserve;
}

// [RE 0x452117] 日期推进；返回是否跨月（原版返回 v2）
int advanceDate(GameState& st) {
    int year = static_cast<int>((st.gameDate >> 16) & 0xFFFF);
    int month = static_cast<int>((st.gameDate >> 8) & 0xFF);
    int day = static_cast<int>(st.gameDate & 0xFF);
    bool newMonth = false;
    if (++day > daysInMonth(year, month)) {
        day = 1;
        newMonth = true;
        if (++month > 12) {
            month = 1;
            ++year;
        }
    }
    st.gameDate = (static_cast<uint32_t>(year) << 16) | (static_cast<uint32_t>(month) << 8) |
                  static_cast<uint32_t>(day);
    return newMonth ? 1 : 0;
}

// [RE 0x454FE1] sub_454FE1 角度计算（atan2 定点化，0..0xFFFF = 0..360°）
// [RE 0x454FB4] sub_454FB4 朝向 = byte_482414[((sub_454FE1(-dy)>>12)+1)>>1 & 7]
// 依据: 0x454FE1 查表 word_48242C（atan(min/max)），0x454FB4 映射 8 方向
// 差异: 用标准 atan2 等价替代查表（结果一致，避免硬编码 2049 项表）
int facingFromDelta(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    constexpr double kPi = 3.14159265358979;
    double a = std::atan2(-static_cast<double>(dy), static_cast<double>(dx));
    if (a < 0.0) {
        a += 2.0 * kPi;
    }
    const int angle = static_cast<int>(a / (2.0 * kPi) * 65536.0) & 0xFFFF;
    const int idx = (((angle >> 12) + 1) >> 1) & 7;
    return kDirTable[idx];
}

// [RE 0x41B42D LABEL_88 0x41C17A..0x41C7A5] 事件槽 NPC 恶行（cur>=4；busy==0 且 timerB==0 才执行）
// 依据: 0x41B42D 反编译（LABEL_88 + LABEL_102 抢银行 + cur6/7 停留段）:
//   cur 4/5（小偷/強盜，路过/停留均触发）：presentMask 排除 bailer 后取最低位且存活玩家 —
//     小偷 = 偷其一半点券（points>>1）给 bailer（消息 0x463AE4「偷取%s\n\n%d點點券！」）；
//     強盜 = discardRandomCard 抢卡 → giveCardToBag(bailer)（消息 0x463AF7「奪取%s%s！」）；
//   随后強盜且落地格 cellType==14（銀行）→ 遍历玩家 (int)(bank×0.2) transferMoney(→bailer, 5)
//     （消息 0x463B02）。两段步骤中 stepsRemaining!=0 时恢复循环移动音（0x41C328/0x41C43A）。
//   cur 6/7（流氓/間諜，**仅停留** steps==0）：refreshGameUi → cellEnt+32 special 分类：
//     (2000,4000) estate / (4000,6000) corp / (6000,8000) specPt；owner 非 bailer 时 —
//     流氓(6)：estate = 同 owner 同路段名全部 (+28 priceAdd) 求和 ×moneyMul（0x463B21「勒索%s\n\n%d元保護費！」）；
//              corp = (+34 buildPrice) ×moneyMul；
//     間諜(7)：estate = (+44 price 最近租金，0 跳过)；corp = (+48 lastFee，0 跳过)；
//              specPt = (+40 fund，0 跳过，转账至 100+idx 资金池)（0x463B36/0x463B49）。
//     转账均 transferMoney(owner-1 → bailer, 金额, 0)。见 498df0-event-slot-npc.md §2.7。
static void npcVillainPhase(Application& app, int p) {
    GameState& st = app.gameState();
    const int i = p - 4;
    const NpcSlot80& slot = st.npcSlots[i];
    if (slot.busy != 0 || slot.timerB != 0) {
        return; // [RE 0x41C187] DF2(busy)!=0 || DF5(timerB)!=0 → 跳过恶行
    }
    const uint16_t cell = st.players[p].cellEntId;
    if (cell == 0 || cell >= st.cellEnts.size()) {
        return;
    }
    const CellEnt& ce = st.cellEnts[cell];
    const uint8_t cellType = static_cast<uint8_t>(ce.occMask & 0xFF);
    const uint8_t presentMask = static_cast<uint8_t>((ce.occMask >> 8) & 0xFF);
    const int bailer = slot.bailer;
    if (bailer >= 4) {
        return;
    }
    char text[256];

    if (p == 4 || p == 5) {
        // [RE 0x41C19C] 同格非 bailer 玩家（掩码最低位；死者跳过，不尝试下一位）
        const uint32_t mask = static_cast<uint32_t>(~(1u << bailer)) & presentMask;
        if (mask != 0) {
            const int victim = bitScanPlayer(mask); // [RE 0x41C1C9 sub_40D293]
            if (victim >= 0 && victim < 4 && st.players[victim].alive != 0) {
                if (p == 4) {
                    // [RE 0x41C210] 小偷：偷一半点券（>>1，0 则跳过）
                    const int half = st.players[victim].points >> 1;
                    if (half != 0) {
                        app.audio().stopEffectSlot(st.moveSoundIndex);
                        std::snprintf(text, sizeof(text), "偷取%s\n\n%d點點券！",
                                      playerNameNoSpace(st, victim).c_str(), half);
                        showMessage(app, text, 1000);
                        st.players[victim].points =
                            static_cast<uint16_t>(st.players[victim].points - half);
                        st.players[bailer].points =
                            static_cast<uint16_t>(st.players[bailer].points + half);
                        if (st.remainingSteps != 0) { // [RE 0x41C328] 移动中恢复循环音
                            app.audio().playEffectSlotLooping(st.moveSoundIndex);
                        }
                    }
                } else {
                    // [RE 0x41C299] 強盜：discardRandomCard → 给 bailer（0 跳过）
                    const int card = discardRandomCard(app, victim);
                    if (card != 0) {
                        app.audio().stopEffectSlot(st.moveSoundIndex);
                        std::snprintf(text, sizeof(text), "奪取%s%s！",
                                      playerNameNoSpace(st, victim).c_str(), kCardNames[card]);
                        showMessage(app, text, 1000);
                        giveCardToBag(st, bailer, card); // [RE 0x41C307 sub_4412E4]
                        if (st.remainingSteps != 0) {
                            app.audio().playEffectSlotLooping(st.moveSoundIndex);
                        }
                    }
                }
            }
        }
        // [RE 0x41C345 LABEL_102] 強盜 && cellType==14（銀行）→ 全体玩家银行 ×0.2 给 bailer
        if (p == 5 && cellType == 14) {
            int total = 0;
            for (int v = 0; v < st.playerCount; ++v) {
                if (st.players[v].alive == 0 || v == bailer) {
                    continue;
                }
                const int amount =
                    static_cast<int>(static_cast<double>(st.players[v].bank) * 0.2); // dbl_463B60
                transferMoney(app, v, bailer, amount, 5); // [RE 0x41C39B] 银行优先+入现金
                total += amount;
            }
            app.audio().stopEffectSlot(st.moveSoundIndex);
            std::snprintf(text, sizeof(text), "強盜搶奪銀行\n\n得款%d元\n\n給%s！", total,
                          playerNameNoSpace(st, bailer).c_str());
            showMessage(app, text, 2000);
            if (st.remainingSteps != 0) { // [RE 0x41C43A]
                app.audio().playEffectSlotLooping(st.moveSoundIndex);
            }
        }
        return;
    }

    // cur 6/7：仅停留（steps==0）
    if (st.remainingSteps != 0) {
        return; // [RE 0x41C44E]
    }
    renderGameFrame(app); // [RE 0x41C458 refreshGameUi(0,0,1)]
    app.renderFrame();
    const uint16_t special = ce.special; // [RE 0x41C46D cellEnt+32]
    if (special > 2000 && special < 4000) {
        const int idx = special - 2000; // [RE 0x41C49B]
        if (idx > 0 && idx < static_cast<int>(st.estates.size())) {
            const Estate& e = st.estates[idx];
            const int owner = (e.owner != 0) ? e.owner - 1 : -1;
            if (owner >= 0 && owner != bailer) {
                int amount = 0;
                if (p == 6) {
                    // [RE 0x41C4EC] 流氓：同 owner 同路段名全部 estate priceAdd(+28) 求和 ×moneyMul
                    for (size_t k = 1; k < st.estates.size(); ++k) {
                        const Estate& o = st.estates[k];
                        if (o.owner == e.owner && std::strcmp(o.name, e.name) == 0) {
                            amount += o.priceAdd;
                        }
                    }
                    amount *= st.moneyMul;
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    std::snprintf(text, sizeof(text), "勒索%s\n\n%d元保護費！",
                                  playerNameNoSpace(st, owner).c_str(), amount);
                } else {
                    // [RE 0x41C597] 間諜：estate+44 最近租金，0 跳过
                    amount = e.price;
                    if (amount == 0) {
                        return;
                    }
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    std::snprintf(text, sizeof(text), "取走過路費\n\n%d元！", amount);
                }
                showMessage(app, text, 1500);
                transferMoney(app, owner, bailer, amount, 0); // [RE 0x41C56E..0x41C5D2]
            }
        }
        return;
    }
    if (special > 4000 && special < 6000) {
        const int idx = special - 4000; // [RE 0x41C5FD]
        if (idx > 0 && idx < static_cast<int>(st.corps.size())) {
            const Corp& c = st.corps[idx];
            const int owner = (c.owner != 0) ? c.owner - 1 : -1;
            if (owner >= 0 && owner != bailer) {
                int amount = 0;
                if (p == 6) {
                    // [RE 0x41C666] 流氓：corp+34 buildPrice ×moneyMul
                    amount = st.moneyMul * c.buildPrice;
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    std::snprintf(text, sizeof(text), "勒索%s\n\n%d元保護費！",
                                  playerNameNoSpace(st, owner).c_str(), amount);
                } else {
                    // [RE 0x41C6BD] 間諜：corp+48 lastFee，0 跳过
                    amount = c.lastFee;
                    if (amount == 0) {
                        return;
                    }
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    std::snprintf(text, sizeof(text), "取走過路費\n\n%d元！", amount);
                }
                showMessage(app, text, 1500);
                transferMoney(app, owner, bailer, amount, 0); // [RE 0x41C692/0x41C6B6]
            }
        }
        return;
    }
    if (special > 6000 && special < 8000) {
        const int idx = special - 6000; // [RE 0x41C6FE]
        if (idx > 0 && idx < static_cast<int>(st.specPts.size())) {
            const SpecPt& s = st.specPts[idx];
            const int owner = (s.owner != 0) ? s.owner - 1 : -1;
            if (s.owner != 0 && owner != bailer && p == 7) {
                // [RE 0x41C73E] 間諜：specPt+40 fund，0 跳过；转账至 100+idx 资金池
                const int amount = s.fund;
                if (amount != 0) {
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    std::snprintf(text, sizeof(text), "取走盈餘\n\n%d元！", amount);
                    showMessage(app, text, 1500);
                    transferMoney(app, 100 + idx, bailer, amount, 0); // [RE 0x41C79E]
                }
            }
        }
        return;
    }
}

// [RE 0x41B42D] onPlayerActionPhase 每格落地事件（路过/停留双判定；物件分派主体）
// 依据: 0x41B42D 反编译 + docs/reverse/functions/map-object-refresh.md §5。变量：
//   cellType = cellEnt+36 低字节、玩家在场掩码 = +36 bit8..15、objId = +36 bit16..23
// 已实现: 路过銀行格柜员机（0x41B540，处理后仍续行物件 LABEL_22 流程）；
//   同格死亡玩家=乞丐施捨（0x41B4D4 段：停留 +1000×M 给银行 + 乞丐随机换位 0x40CC56）；
//   挂身道具(cellNo)每步寿命递减、归零爆炸（删物件+拆该格地产一级+damagePlayer+
//   爆炸 FLC525+停止+住院5）、同格他人转移（0x41B68E 段）；switch(物件类型)：
//   神明 1..10/12 停留 attachObject（0x41B82D）、惡犬11（0x41B847 停留删→轮替土地公；
//   步行=咬伤 FLC532+住院3、有车=碾过 FLC552 无事）、禮物13（0x41B8A2 drawGiftCard 道具）、
//   寶箱14（0x41B960 删+500点券）、路障16（0x41BAF9 路过删+强制停止）、
//   地雷17（0x41BB0A 停留删+炸+住院3）、炸彈18（0x41BC2B 停留无挂件→挂身 life38）
// 台词（P4-A/C ✅）：路障/地雷/炸彈踩中（kItemTouchLines 列14/15/16）、神明哭/笑
//   （kMoneyLines 列22/8）、抽卡 sub_44F230（playValueLine）；事件槽 NPC（p≥4：小偷/強盜/流氓/間諜物件分支 +
//   LABEL_88 恶行 + 抓回）✅ 2026-09-26（见 498df0-event-slot-npc.md §2.6/§2.7/§2.8；
//   機器娃娃 p==8 沿路踢物件 ✅（0x446AFB 写槽8 + onPhase bounceObject/deleteMapObject）；
//   乞丐施捨在原版为无条件检查（bankVisit 标志仅影响音效恢复）
void onPlayerActionPhase(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    // [RE 0x41B43E] 到达（步数尽）统一停移动音（原版函数首行 `if (!g_stepsRemaining)
    //   audioStopEffect(&g_effectSlots[2*dword_4749D4])`；含事件槽 4..7 与本回合末的娃娃 8）
    if (st.remainingSteps == 0) {
        app.audio().stopEffectSlot(st.moveSoundIndex);
    }
    if (p == 8) {
        // [RE 0x41B42D p==8] 機器娃娃沿路踢除物件：bounceObject(槽, 起始格, 上一格) 弹飞 +
        //   deleteMapObject（0x41B4F1..0x41B531）；无物件格直接返回
        Player& w = st.players[8];
        if (w.cellEntId == 0 || w.cellEntId >= st.cellEnts.size()) {
            return;
        }
        const CellEnt& ce = st.cellEnts[w.cellEntId];
        const uint8_t slotHi = static_cast<uint8_t>((ce.occMask >> 16) & 0xFF);
        if (slotHi == 0) {
            return;
        }
        // [RE 0x41B4FF] 弹飞基准 = 娃娃当前格/上一格（word_498E6C/6E 即槽 8 的
        //   cellEntId/prevCellEnt，随移动逐步更新）——不是玩家起始格
        const uint16_t fromCell = w.cellEntId;
        const uint16_t toCell = (w.prevCellEnt != 0) ? w.prevCellEnt : w.cellEntId;
        bounceObject(st, slotHi, fromCell, toCell);  // [RE 0x40FAFD]
        releaseCellTableSlot(app, slotHi);           // [RE 0x40E14D]
        renderGameFrame(app);
        app.renderFrame();
        RICH4_LOGI("worker kick: slot %u at cell %u (RE 0x41B42D)", slotHi, w.cellEntId);
        return;
    }
    if (p < 0 || st.players[p].cellEntId == 0) {
        return;
    }
    if (p >= 4) {
        // [RE 0x41B42D NPC 分支 0x41B9D6../0x41C84E] 事件槽 NPC 物件分派（cur 4..7，槽 8 已在上方返回）。
        //   与原版逐 case 对齐：`byte_498E35`(timerB) 非 0 时小偷类恶行跳过（特殊动画中）；
        //   objType 13/14/16/17/18 仅小偷(p==4)触发"偷道具给 bailer"，5/6/7 走各自差异分支；
        //   神明 1..10/12 不附身；每 case 结束后汇入 LABEL_88 恶行 + LABEL_145 抓回。
        const int i = p - 4;
        NpcSlot80& nslot = st.npcSlots[i];
        Player& np = st.players[p];
        const uint16_t ncell = np.cellEntId;
        CellEnt& nce = st.cellEnts[ncell];
        const uint8_t cellType = static_cast<uint8_t>(nce.occMask & 0xFF);
        const int objId = static_cast<int>((nce.occMask >> 16) & 0xFF);
        uint8_t objType = 0;
        if (objId > 0 && objId <= 46) {
            objType = st.cellTable[static_cast<size_t>(24 * (objId - 1))];
        }
        const bool stopped = st.remainingSteps == 0;
        char text[256];

        auto npcRestoreMoveLoop = [&]() { // [RE LABEL_68/LABEL_87] 移动中恢复循环移动音
            if (st.remainingSteps != 0) {
                app.audio().playEffectSlotLooping(st.moveSoundIndex);
            }
        };
        switch (objType) {
            case 1: case 2: case 3: case 4: case 5: case 6:
            case 7: case 8: case 9: case 10: case 12:
                break; // 神明：NPC 不附身（原版 `if (cur<4 && !steps)`）
            case 11: { // [RE 0x41B847] 惡犬：路过无视；停留 → 删(槽11) + FLC532/音93 + 住院
                if (!stopped) {
                    break;
                }
                releaseCellTableSlot(app, 11); // [RE deleteMapObject(11) 硬编码槽]
                playEventFlc(app, 532, 0, 40, 93, false, 3); // [RE LABEL_44]
                st.remainingSteps = 0;
                hospitalizePlayer(app, p, 3); // [RE LABEL_45]
                break;
            }
            case 13: { // [RE 0x41B99E] 禮物：仅小偷且 timerB==0 → 替 bailer 抽道具
                if (p != 4 || nslot.timerB != 0) {
                    break;
                }
                releaseCellTableSlot(app, 13);          // [RE deleteMapObject(13)]
                renderGameFrame(app);                   // [RE 0x41B9C6 refreshGameUi(0,0,1)]
                app.renderFrame();
                std::snprintf(text, sizeof(text), "小偷偷得禮物\n\n給%s！",
                              playerNameNoSpace(st, nslot.bailer).c_str());
                app.audio().stopEffectSlot(st.moveSoundIndex); // [RE 停移动音]
                showMessage(app, text, 1500);
                const int item = drawGiftCard(app, nslot.bailer); // [RE drawGiftCard(bailer)]
                if (item != 0) {
                    app.audio().playEffectSlot(6);      // [RE dword_48237A 礼物音效]
                    renderGameFrame(app);               // [RE refreshGameUi(bailer.x, bailer.y, 0)]
                    app.renderFrame();
                    std::snprintf(text, sizeof(text), "得到%s！", kItemNames[item - 1]);
                    showMessage(app, text, 1500);
                    playValueLine(app, nslot.bailer, kItemPrice[item - 1]);
                    renderGameFrame(app);               // [RE LABEL_54 sub_41D546]
                    app.renderFrame();
                }
                npcRestoreMoveLoop();
                break;
            }
            case 14: { // [RE 0x41BBA6] 寶箱：仅小偷且 timerB==0 → bailer +500 点券
                if (p != 4 || nslot.timerB != 0) {
                    break;
                }
                app.audio().playEffectSlot(7);          // [RE dword_482382 宝箱音效]
                releaseCellTableSlot(app, 14);          // [RE deleteMapObject(14)]
                renderGameFrame(app);                   // [RE 0x41BBDD refreshGameUi(0,0,1)]
                app.renderFrame();
                std::snprintf(text, sizeof(text), "小偷偷得寶箱\n\n給%s！",
                              playerNameNoSpace(st, nslot.bailer).c_str());
                app.audio().stopEffectSlot(st.moveSoundIndex);
                showMessage(app, text, 1500);
                renderGameFrame(app);                   // [RE refreshGameUi(bailer.x, bailer.y, 0)]
                app.renderFrame();
                showMessage(app, "得到５００點券！", 1500); // [RE byte_463AD3]
                st.players[nslot.bailer].points =
                    static_cast<uint16_t>(st.players[nslot.bailer].points + 500);
                playValueLine(app, nslot.bailer, 101); // [RE sub_44EF41(bailer,0,off_48084A 列0) 笑]
                renderGameFrame(app);                   // [RE LABEL_54 sub_41D546]
                app.renderFrame();
                npcRestoreMoveLoop();
                break;
            }
            case 16: { // [RE 0x41BCEB] 路障
                if (p == 4) {
                    if (nslot.timerB != 0) {
                        break;
                    }
                    releaseCellTableSlot(app, objId);   // [RE deleteMapObject(v63)]
                    renderGameFrame(app);               // [RE 0x41BD9C refreshGameUi(0,0,1)]
                    app.renderFrame();
                    std::snprintf(text, sizeof(text), "小偷偷得路障\n\n給%s！",
                                  playerNameNoSpace(st, nslot.bailer).c_str());
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    showMessage(app, text, 1500);
                    givePlayerItem(st, nslot.bailer, 2); // [RE givePlayerCard(bailer,2)]
                    npcRestoreMoveLoop();
                } else { // 5/6/7：删 + 强制停下（原版无消息/台词）
                    app.audio().stopEffectSlot(st.moveSoundIndex); // [RE 0x41BD08]
                    releaseCellTableSlot(app, objId);
                    renderGameFrame(app);               // [RE 0x41BD26 refreshGameUi(0,0,1)]
                    app.renderFrame();
                    st.remainingSteps = 0;
                }
                break;
            }
            case 17: { // [RE 0x41BE64] 地雷
                if (p != 4 && stopped) {                // 5/6/7 停留 → 炸伤住院
                    releaseCellTableSlot(app, objId);   // [RE deleteMapObject(v63)]
                    playEventFlc(app, 525, 0, 40, 82, false, 3);
                    st.remainingSteps = 0;
                    hospitalizePlayer(app, p, 3);       // [RE LABEL_45]
                    break;
                }
                if (p == 4 && nslot.timerB == 0) {      // 小偷 → 替 bailer 拿地雷道具
                    releaseCellTableSlot(app, objId);
                    renderGameFrame(app);               // [RE 0x41BF4D refreshGameUi(0,0,1)]
                    app.renderFrame();
                    std::snprintf(text, sizeof(text), "小偷偷得地雷\n\n給%s！",
                                  playerNameNoSpace(st, nslot.bailer).c_str());
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    showMessage(app, text, 1500);
                    givePlayerItem(st, nslot.bailer, 3); // [RE givePlayerCard(bailer,3)]
                    npcRestoreMoveLoop();
                }
                break;
            }
            case 18: { // [RE 0x41BFD2] 定時炸彈：仅小偷 → 替 bailer 拿道具（不挂身）
                if (p == 4 && nslot.timerB == 0) {
                    releaseCellTableSlot(app, objId);   // [RE deleteMapObject(v63)]
                    renderGameFrame(app);               // [RE 0x41C0A9 refreshGameUi(0,0,1)]
                    app.renderFrame();
                    std::snprintf(text, sizeof(text), "小偷偷得定時炸彈\n\n給%s！",
                                  playerNameNoSpace(st, nslot.bailer).c_str());
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                    showMessage(app, text, 1500);
                    givePlayerItem(st, nslot.bailer, 4); // [RE givePlayerCard(bailer,4)]
                    npcRestoreMoveLoop();
                }
                break;
            }
            default:
                break;
        }
        // [RE 0x41C17A LABEL_88] 恶行（小偷/強盜同格行窃、強盜抢银行、流氓/間諜勒索）
        npcVillainPhase(app, p);
        // [RE 0x41C7A6 LABEL_145] 抓回（每步无条件）：
        //   status&0x7F==1（監獄）且 cellType==4、或 ==2（醫院）且 cellType==5 →
        //   未置 bit7 则置位（防出门即抓）；已置 → jail/hospitalize + 停步
        const uint8_t status = static_cast<uint8_t>(nslot.status & 0x7F);
        if ((status == 1 && cellType == 4) || (status == 2 && cellType == 5)) {
            if ((nslot.status & 0x80) != 0) {
                if (status == 1) {
                    jailPlayer(app, p, 0);
                } else {
                    hospitalizePlayer(app, p, 0);
                }
                st.remainingSteps = 0;
            } else {
                nslot.status = static_cast<uint8_t>(nslot.status | 0x80);
            }
        }
        return;
    }
    Player& pl = st.players[p];
    const uint16_t cellEntId = pl.cellEntId;
    CellEnt& ce = st.cellEnts[cellEntId];
    const uint8_t cellType = static_cast<uint8_t>(ce.occMask & 0xFF);
    const uint8_t presentMask = static_cast<uint8_t>((ce.occMask >> 8) & 0xFF);
    const int objId = static_cast<int>((ce.occMask >> 16) & 0xFF);
    uint8_t objType = 0;
    if (objId > 0 && objId <= 46) {
        objType = st.cellTable[static_cast<size_t>(24 * (objId - 1))];
    }
    const bool stopped = st.remainingSteps == 0;  // 停留（步数走完）

    if (stopped) {
        app.audio().stopEffectSlot(st.moveSoundIndex);  // [RE 0x41B447] 到达（步数尽）停移动音
    }
    if (cellType == 14 && pl.state37 == 0 && !stopped && objType != 0x10) {
        // [RE 0x41B540] 路过銀行格 → 柜员机（可提存款；离开即继续移动）
        app.audio().stopEffectSlot(st.moveSoundIndex);
        if (pl.cellNo != 0) {
            app.audio().stopEffectSlot(3);  // [RE g_effectSlot3] 炸弹挂身循环音
        }
        bankVisitDialog(app);
        if (st.sceneRequest != 0) {
            return;
        }
        if (!stopped) {
            app.audio().playEffectSlot(st.moveSoundIndex);
            if (pl.cellNo != 0) {
                app.audio().playEffectSlot(3);
            }
        }
    }

    // [RE 0x41B5B6..0x41B669] 同格死亡玩家 = 乞丐：停留付 1000×M 打发、其随机换位
    const int beggar = bitScanPlayer(~(1u << p) & presentMask);
    if (beggar != -1 && beggar < 9 && st.players[beggar].alive == 0 && stopped) {
        const int32_t alms = 1000 * st.moneyMul;
        char text[64];
        std::snprintf(text, sizeof(text), "施捨給乞丐%d元", alms);
        showMessage(app, text, 1500);
        transferMoney(app, p, -1, alms, 0);
        relocatePlayer(st, beggar);
    }

    // [RE 0x41B66A..0x41B79B] 挂身道具（炸彈/路障 cellNo）每步寿命递减
    if (pl.cellNo != 0) {
        uint8_t& life = st.cellTable[static_cast<size_t>(24 * (pl.cellNo - 1)) + 4];
        RICH4_LOGI("carried bomb: p%d cellNo=%u life=%u stopped=%d (RE 0x41B697)", p, pl.cellNo,
                   life, stopped ? 1 : 0);
        if (life != 0) {
            --life;
            if (life == 0) {
                // 爆炸：删物件、拆该格地产一级、车毁受伤、爆炸插图、停止、住院5
                RICH4_LOGI("carried bomb EXPLODE: p%d obj %u (RE 0x41B6CD)", p, pl.cellNo);
                app.audio().stopEffectSlot(3);
                const int boom = pl.cellNo;
                pl.cellNo = 0;
                releaseCellTableSlot(app, boom);
                const uint16_t cellObj = ce.special;
                if (cellObj != 0) {
                    demolishAtObjId(app, cellObj, 0);
                }
                damagePlayer(app, p);
                playEventFlc(app, 525, 0, 40, 82, false, 3);  // [RE 爆炸插图+音效82，帧3切换]
                st.remainingSteps = 0;
                hospitalizePlayer(app, p, 5);
                return;
            }
        }
        // [RE 0x41B700..0x41B79B] 同格存活且未挂件的玩家 → 炸弹转移
        const int to = bitScanPlayer(~(1u << p) & presentMask);
        if (to != -1 && to < 4 && st.players[to].alive != 0 && st.players[to].cellNo == 0) {
            const int carried = pl.cellNo;
            st.cellTable[static_cast<size_t>(24 * (carried - 1)) + 5] =
                static_cast<uint8_t>(to + 1);
            st.players[to].cellNo = static_cast<uint8_t>(carried);
            pl.cellNo = 0;
            app.audio().stopEffectSlot(3);
            RICH4_LOGI("bomb transferred p%d -> p%d (RE 0x41B42D)", p, to);
        }
    }

    char text[256];
    switch (objType) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 12:
            // [RE 0x41B82D] 神明：仅停留触发附身
            if (stopped) {
                attachObject(app, p, cellEntId, objId);
            }
            break;
        case 11: {  // [RE 0x41B847] 惡犬：停留触发（路过无视）
            if (!stopped) {
                break;
            }
            releaseCellTableSlot(app, 11);  // 删恶犬 → 轮替土地公
            if (pl.travel == 0) {
                damagePlayer(app, p);
                playEventFlc(app, 532, 0, 40, 93, false, 3);  // [RE 咬伤插图+音效93，帧3切换]
                st.remainingSteps = 0;
                hospitalizePlayer(app, p, 3);       // 咬伤住院 3 天 [HELP 51]
            } else {
                playEventFlc(app, 552, 0, 40, 85, false, 1);  // [RE 有车碾过无事，帧1切换]
            }
            break;
        }
        case 13: {  // [RE 0x41B8A2] 禮物：停留得 1 件道具
            if (!stopped) {
                break;
            }
            const int item = drawGiftCard(app, p);
            if (item != 0) {
                app.audio().playEffectSlot(6);  // [RE dword_48237A] 礼物音效
                releaseCellTableSlot(app, 13);  // 删除（月度重定位）
                renderGameFrame(app);           // [RE 0x41B946] refreshGameUi(0,0,1)：删除先重绘
                app.renderFrame();              //   → 消息显示在物件已消失的新画面上
                std::snprintf(text, sizeof(text), "得到%s！", kItemNames[item - 1]);
                showMessage(app, text, 1500);
                playValueLine(app, p, kItemPrice[item - 1]); // [RE sub_44F230 道具价值台词]
            }
            break;
        }
        case 14:  // [RE 0x41B960] 寶箱：停留 +500 点券
            if (!stopped) {
                break;
            }
            app.audio().playEffectSlot(7);      // [RE dword_482382] 宝箱音效
            releaseCellTableSlot(app, 14);
            renderGameFrame(app);  // [RE 0x41BB41] refreshGameUi(0,0,1)：宝箱先消失再弹效果
            app.renderFrame();
            showMessage(app, "得到５００點券！", 1500);  // [RE byte_463AD3]
            pl.points = static_cast<uint16_t>(pl.points + 500);
            playValueLine(app, p, 101); // [RE sub_44EF41(p,0,off_48084A 列0) 笑]
            break;
        case 16:  // [RE 0x41BAF9] 路障：路过即触发 → 删除 + 强制停止 + 被挡台词
            app.audio().stopEffectSlot(st.moveSoundIndex);
            releaseCellTableSlot(app, objId);
            renderGameFrame(app);  // [RE 0x41BD20] refreshGameUi(0,0,1)：先删物件再弹台词
            st.remainingSteps = 0;
            // [RE 0x41BD56] 踩中者被挡台词 off_480D92（列14、表情帧1）；玩家4-7/娃娃由 playLine 守卫跳过
            if (p >= 0 && p < 4) {
                const int ci = st.players[p].charIndex;
                if (ci >= 0 && ci < 12) {
                    playLine(app, p, kItemTouchLines[ci][0], 1);
                }
            }
            break;
        case 17:  // [RE 0x41BB0A] 地雷：停留触发 → 炸 + 车毁 + 住院3
            if (!stopped) {
                break;
            }
            releaseCellTableSlot(app, objId);
            damagePlayer(app, p);
            playEventFlc(app, 525, 0, 40, 82, false, 3);  // [RE 地雷 flags 0x30001 帧3]
            // [RE 0x41BEFF] 踩中者哭台词 off_480D96（列15、表情帧1）；玩家4-7/娃娃不播
            if (p >= 0 && p < 4) {
                const int ci = st.players[p].charIndex;
                if (ci >= 0 && ci < 12) {
                    playLine(app, p, kItemTouchLines[ci][1], 1);
                }
            }
            st.remainingSteps = 0;
            hospitalizePlayer(app, p, 3);
            break;
        case 18:  // [RE 0x41BC2B] 定時炸彈：停留且无挂件 → 挂身（life=38 步）
            if (pl.cellNo != 0 || !stopped) {
                break;
            }
            pl.cellNo = static_cast<uint8_t>(objId);
            st.cellTable[static_cast<size_t>(24 * (objId - 1)) + 5] =
                static_cast<uint8_t>(p + 1);
            st.cellTable[static_cast<size_t>(24 * (objId - 1)) + 4] = 38;
            ce.occMask &= ~0x00FF0000u;  // 本格占用让位给挂身物件
            app.audio().playEffectSlot(3);  // [RE g_effectSlot3] 滴答循环
            renderGameFrame(app);           // [RE 0x41BC2B] refreshGameUi(0,0,1)：立即显示挂身
            // [RE 0x41C063] 踩中者台词 off_480D9A（列16 被炸、表情帧2）——原实现误用放置表列3
            if (p >= 0 && p < 4) {
                const int ci = st.players[p].charIndex;
                if (ci >= 0 && ci < 12) {
                    playLine(app, p, kItemTouchLines[ci][2], 2);
                }
            }
            RICH4_LOGI("bomb attached: p%d obj %d life=38 (RE 0x41BC2B)", p, objId);
            break;
        default:
            break;
    }
    RICH4_LOGI("onPlayerActionPhase: player %d cellEnt %u cellType %u obj %d/%u (RE 0x41B42D)", p,
               cellEntId, cellType, objId, objType);
}

} // namespace

// 行走资源加载/释放（供 updatePlayerStates/attachEnd/damagePlayer 等跨文件复用，
// 定义于 map_objects.h / 本文件；map_objects 引用）

// [RE 0x40B93B] 行走资源加载（定义见下方 loadWalkResources；本地辅助）
void loadWalkRes(GameState& st, int p, int slot, size_t index) {
    auto blob = st.data.read(index);
    if (blob) {
        st.walkRes[p][slot].load(std::move(*blob));
    } else {
        RICH4_LOGW("loadWalkRes: data.mkf[%zu] unavailable (RE 0x40B93B)", index);
    }
}

// [RE 0x40B8D8] releaseWalkResources
// 依据: 0x40B8D8 反编译; 释放玩家 a1 的动画组 a2（slot = a2 + 2*i, i<4），
//       并清 byte_498EA0 的动画组位（a2=1 → 低 4 位；a2=0 → 高 4 位）
void releaseWalkResources(GameState& st, int p, int group) {
    for (int i = 0; i < 4; ++i) {
        st.walkRes[p][group + 2 * i] = UiImage{};
    }
    if (group) {
        st.playerActionFlags[p] &= 0x0F;
    } else {
        st.playerActionFlags[p] &= 0xF0;
    }
}

// [RE 0x40B93B] loadWalkResources
// 依据: 0x40B93B 反编译; 普通玩家(0..3):
//   v1 = 21*charIndex + 128
//   state37 → 组5: slot0=v1+16, slot2=v1+17, slot4=v1+2
//   cellEnt+39 最高位（有路）→ 组1: slot1=v1+13, slot3=v1+14, slot5=v1+15
//   travel → slot0=3*travel+v1, slot2=+1, slot4=+2, slot6=+3（travel==3）
// 差异: 槽 4..7（事件槽 NPC）分支待补
void loadWalkResources(GameState& st, int p) {
    if (p < 0 || p >= 9) {
        return;
    }
    if (p >= 4 && p < 8) {
        // [RE 0x40B93B 事件槽 NPC] 行走资源 = data.mkf[4*p+364..367]（非角色 charIndex 系列）。
        //   drawPiece/动画帧按 slot=2*state+group 取图（state 0停/1移动/2掷骰/3收租）：
        //   走路组(group0) 用 travel 系 slot{0站,2走,4,6}，渡水组(group1) 用渡水系 slot{1,3走,5,7}；
        //   NPC 无多载具，停格/收租态(slot4/6/5/7)复用站立(364)或渡水(366)静态帧——
        //   否则每回合末进 state3 取未加载槽 → 棋子消失数帧=闪烁（2026-09-26 修）。
        const int i = p - 4;
        if (st.npcSlots[i].timerB) {
            // 特殊/恶行动画组：停格 364，移动 367
            if ((st.playerActionFlags[p] & 0x0F) != 5) {
                releaseWalkResources(st, p, 0);
                st.playerActionFlags[p] =
                    static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) | 5);
                loadWalkRes(st, p, 0, static_cast<size_t>(4 * p + 364));
                loadWalkRes(st, p, 2, static_cast<size_t>(4 * p + 367));
                loadWalkRes(st, p, 4, static_cast<size_t>(4 * p + 364));
                loadWalkRes(st, p, 6, static_cast<size_t>(4 * p + 364));
            }
            st.playerMoveGroup[p] = 0;
            if (st.playerMoveFrame[p]) {
                st.playerActionExtra[p] = 0;
            }
            st.playerMoveFrame[p] = 0;
            return;
        }
        const uint16_t cell = st.players[p].cellEntId;  // 真源=moveOneStep 推进的当前格
        const bool road = cell > 0 && cell < st.cellEnts.size() &&
                          (st.cellEnts[cell].occMask & 0x80000000u) != 0;
        if (!road) {
            // 普通走路组（cellEnt+39 最高位=0）：站0/6=364，走2=365，音效11
            if ((st.playerActionFlags[p] & 0x0F) != 1) {
                releaseWalkResources(st, p, 0);
                st.playerActionFlags[p] =
                    static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) | 1);
                loadWalkRes(st, p, 0, static_cast<size_t>(4 * p + 364));
                loadWalkRes(st, p, 2, static_cast<size_t>(4 * p + 365));
                loadWalkRes(st, p, 4, static_cast<size_t>(4 * p + 364));
                loadWalkRes(st, p, 6, static_cast<size_t>(4 * p + 364));
            }
            if (st.playerMoveGroup[p] == 1) {
                st.playerMoveFrame[p] = 0;
            }
            st.playerMoveGroup[p] = 0;
            st.moveSoundIndex = 11;  // [RE 0x40B93B NPC 普通路 dword_4749D4=11]
            return;
        }
        // 渡水/走进组（bit31=1）：slot{1,3走,5,7}=366，音效15（坐船）
        if ((st.playerActionFlags[p] & 0x30) != 0x10) {
            releaseWalkResources(st, p, 1);
            st.playerActionFlags[p] =
                static_cast<uint8_t>((st.playerActionFlags[p] & 0xCF) | 0x10);
            loadWalkRes(st, p, 1, static_cast<size_t>(4 * p + 366));
            loadWalkRes(st, p, 3, static_cast<size_t>(4 * p + 366));
            loadWalkRes(st, p, 5, static_cast<size_t>(4 * p + 366));
            loadWalkRes(st, p, 7, static_cast<size_t>(4 * p + 366));
        }
        if (st.playerMoveGroup[p] == 0) {
            st.playerMoveFrame[p] = 0;
        }
        st.playerMoveGroup[p] = 1;
        st.moveSoundIndex = 15;  // [RE 0x40B93B NPC 渡水 dword_4749D4=15]
        return;
    }
    if (p >= 8) {
        // [RE 0x40B93B 槽8] 機器娃娃专用行走资源：data.mkf[521]（站立/走路 slot0）+
        //   data.mkf[522]（移动 slot2）——不是玩家角色资源
        if ((st.playerActionFlags[p] & 0x0F) != 1) {
            releaseWalkResources(st, p, 0);
            st.playerActionFlags[p] =
                static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) | 1);
            loadWalkRes(st, p, 0, 521);
            loadWalkRes(st, p, 2, 522);
        }
        if (st.playerMoveGroup[p] == 1) {
            st.playerMoveFrame[p] = 0;
        }
        st.playerMoveGroup[p] = 0;
        return;
    }
    Player& pl = st.players[p];
    const int v1 = 21 * pl.charIndex + 128;

    if (!pl.alive || (pl.alive & 0x40)) {
        if ((st.playerActionFlags[p] & 0x30) != 0x20) {
            releaseWalkResources(st, p, 1);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xCF) | 0x20);
            loadWalkRes(st, p, 1, static_cast<size_t>(v1 + 18));
        }
        st.playerMoveGroup[p] = 1;
        if (st.playerActionState[p] == 1) {
            st.playerActionWait[p] = 5;
        }
        st.playerActionState[p] = 0;
        st.playerMoveFrame[p] = 0;
        return;
    }
    if (pl.state37) {
        if ((st.playerActionFlags[p] & 0x0F) != 5) {
            releaseWalkResources(st, p, 0);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) | 5);
            loadWalkRes(st, p, 0, static_cast<size_t>(v1 + 16));
            loadWalkRes(st, p, 2, static_cast<size_t>(v1 + 17));
            loadWalkRes(st, p, 4, static_cast<size_t>(v1 + 2));
        }
        st.playerMoveGroup[p] = 0;
        if (st.playerMoveFrame[p]) {
            st.playerActionExtra[p] = 0;
        }
        st.playerMoveFrame[p] = 0;
        return;
    }
    // cellEnt+39 最高位（bit31）置位 = 有路 → 组1；否则 travel（组0）
    const bool hasRoad = pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size() &&
                         (st.cellEnts[pl.cellEntId].occMask & 0x80000000u) != 0;
    if (hasRoad) {
        if ((st.playerActionFlags[p] & 0x30) != 0x10) {
            releaseWalkResources(st, p, 1);
            st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xCF) | 0x10);
            loadWalkRes(st, p, 1, static_cast<size_t>(v1 + 13));
            loadWalkRes(st, p, 3, static_cast<size_t>(v1 + 14));
            loadWalkRes(st, p, 5, static_cast<size_t>(v1 + 15));
        }
        if (st.playerMoveGroup[p] == 0) {
            st.playerMoveFrame[p] = 0; // 从普通组切入渡水/特殊组时重置动画帧
        }
        st.playerMoveGroup[p] = 1;
        // [RE 0x40B93B] 特殊移动（水上/渡水，cellEnt+39 bit31）→ 音效槽 15
        //   （原版 dword_4749D4 = 15 + audioPlayEffect(&dword_482382[16])）
        st.moveSoundIndex = 15;
        return;
    }
    const int travel = pl.travel & 3;
    if ((travel + 1) != (st.playerActionFlags[p] & 0x0F)) {
        releaseWalkResources(st, p, 0);
        st.playerActionFlags[p] = static_cast<uint8_t>((st.playerActionFlags[p] & 0xF0) |
                                                       (travel + 1));
        const int v10 = 3 * travel + v1;
        loadWalkRes(st, p, 0, static_cast<size_t>(v10));
        loadWalkRes(st, p, 2, static_cast<size_t>(v10 + 1));
        loadWalkRes(st, p, 4, static_cast<size_t>(v10 + 2));
        if (travel == 3) {
            loadWalkRes(st, p, 6, static_cast<size_t>(v10 + 3));
        }
    }
    // 仅在从特殊移动组(渡水/机车 group=1)切回普通组时重置动画帧；
    //   同组内跨格保持连续，避免每格走路动画重启（原版 sub_40B93B 组0 末尾逻辑）
    if (st.playerMoveGroup[p] == 1) {
        st.playerMoveFrame[p] = 0;
    }
    st.playerMoveGroup[p] = 0;
    st.moveSoundIndex = travel + 11;
}

// [RE 0x418C55] dword_475114 跳伞入场落地（定义见 beginPlayerTurn）
// 说明: 落地后 g_playerSpriteX/Y = cellEnt 坐标, g_playerAlive = g_playerKind,
//       sub_40B93B 加载行走资源, dword_475114 = 0
void finishParachute(Application& app) {
    GameState& st = app.gameState();
    const int p = st.pendingSpawnPlayer - 1;
    if (p >= 0 && p < st.playerCount) {
        Player& pl = st.players[p];
        if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
            // [RE 0x40829D] 落地仅设坐标；朝向/来向已在放置时设定，跳伞不改
            pl.spriteX = static_cast<uint16_t>(st.cellEnts[pl.cellEntId].x);
            pl.spriteY = static_cast<uint16_t>(st.cellEnts[pl.cellEntId].y);
        }
        pl.alive = pl.kind;
        loadWalkResources(st, p);
    }
    st.pendingSpawnPlayer = 0;
    st.parachuteActive = false;
    st.parachute = FliDecoder{};
}

// [RE 0x45144F] 跳伞动画播放（FLI/FLC 逐帧；原版阻塞循环，重写用状态机）
void updateParachute(Application& app) {
    GameState& st = app.gameState();
    const int p = st.pendingSpawnPlayer - 1;
    if (!st.parachuteActive) {
        const int charIndex = (p >= 0 && p < 9) ? st.players[p].charIndex : 0;
        auto blob = st.data.read(static_cast<size_t>(charIndex + 559));
        if (blob && st.parachute.open(std::move(*blob))) {
            st.parachuteActive = true;
            st.parachuteTimer = 0;
            st.parachuteLastMs = 0;
            st.parachute.nextFrame(); // 跳过纯调色板首帧，立即显示首幅画面
        } else {
            RICH4_LOGW("parachute: data.mkf[%d] unavailable (RE 0x418C55)", charIndex + 559);
            finishParachute(app);
        }
        return;
    }
    // 按真实时间推进（原版 sub_45144F 用 timeGetTime，与帧率无关）
    const uint32_t now = static_cast<uint32_t>(nowMs());
    const uint32_t interval = static_cast<uint32_t>(std::max(1, st.parachute.speedMs()));
    if (st.parachuteLastMs == 0) {
        st.parachuteLastMs = now;
    }
    if (now - st.parachuteLastMs >= interval) {
        st.parachuteLastMs = now;
        // 帧数据播完时 nextFrame 返回 false（frameCount 含 0xF100 占位帧，不可用作结束条件）
        if (!st.parachute.nextFrame()) {
            finishParachute(app);
        }
    }
}

// [RE 0x419744] estateRouteRent：同路段/连锁店联合租金（导出：landingEvent 收租 + object_tip 显示）
// 依据: 0x419744 反编译; 普通住宅用地（type==0）→ 遍历全部地产，owner 相同且地名相同
//       （strcmp）的 fees[level]（+32+2*level）累加；连锁店（type!=0）→ owner 相同的
//       连锁店每处 +2000；返回 g_moneyMul × 总和
int32_t estateRouteRent(const GameState& st, uint8_t owner, const Estate& es) {
    int32_t sum = 0;
    int count = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& e = st.estates[i];
        if (es.type == 0) {
            if (e.type == 0 && e.owner == owner &&
                std::strncmp(e.name, es.name, sizeof(e.name)) == 0) {
                const int lv = e.level < 6 ? e.level : 0;
                sum += e.fees[lv];
                ++count;
                RICH4_LOGD("  routeRent match estate[%zu] owner=%u level=%u fee=%u", i, e.owner,
                           e.level, e.fees[lv]);
            }
        } else if (e.type != 0 && e.owner == owner) {
            sum += 2000;
            ++count;
            RICH4_LOGD("  routeRent match chain estate[%zu] owner=%u", i, e.owner);
        }
    }
    const std::string curName = big5ToUtf8(es.name, sizeof(es.name));
    RICH4_LOGI("routeRent: owner=%u cur=(%s type=%u) matched=%d sum=%d total=%d (M=%d)",
               owner, curName.c_str(), es.type, count, sum, sum * st.moneyMul, st.moneyMul);
    return sum * st.moneyMul;
}

// [RE 0x407A8C] 朝向 = sub_454FB4(cellEnt[to]-cellEnt[from])，8 方向 0..7
// 依据: 0x407A8C→0x454FB4（atan2(-dy,dx) 量化）→byte_482414[2,3,4,5,6,7,0,1]
int facingBetween(GameState& st, int fromId, int toId) {
    if (fromId <= 0 || toId <= 0 || fromId >= static_cast<int>(st.cellEnts.size()) ||
        toId >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    const int dx = st.cellEnts[toId].x - st.cellEnts[fromId].x;
    const int dy = st.cellEnts[toId].y - st.cellEnts[fromId].y;
    return facingFromDelta(dx, dy);
}

// [RE 0x419744] estateRouteRent 定义见文件后部（导出供 object_tip 复用）

// [RE 0x43F7C6] 轮盘读数与 UI 已移至 src/app/roulette_dialog.cpp（roulettePrompt，完整转盘动画）

// [RE 0x419A67] startRouteHighlight：收租联动高亮收集与启动
// 依据: 0x419A67 高亮循环（rebuildPickBuffer(1) + sub_456C0A 标记同组地块 →
//       v141 > 1 时 sub_451985 播放 16 帧闪烁）；组定义：
//       住宅用地 = 同 owner + 同名 + type==0；连锁店 = 同 owner + type!=0（跨街道）；
//       **地主同盟者（+65，v140）的同组地块一并标记**（0x419C44/0x419BE2），
//       两组计数合计 > 1 才闪烁
void startRouteHighlight(GameState& st, const Estate& es) {
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    const uint8_t ally = st.players[es.owner - 1].ally; // [RE 0x419AAF] v140
    auto collect = [&](uint8_t owner) {
        if (owner == 0) {
            return;
        }
        for (size_t j = 1; j < st.estates.size(); ++j) {
            const Estate& e = st.estates[j];
            if (es.type == 0) {
                if (e.type == 0 && e.owner == owner &&
                    std::strncmp(e.name, es.name, sizeof(e.name)) == 0) {
                    st.highlightEstates.push_back(static_cast<int>(j));
                }
            } else if (e.type != 0 && e.owner == owner) {
                st.highlightEstates.push_back(static_cast<int>(j));
            }
        }
    };
    collect(es.owner);
    collect(ally); // [RE 0x419C44/0x419BE2] 同盟者同组地块
    st.highlightFrame = (st.highlightEstates.size() > 1) ? 0 : -1;
    trace::logf("hl owner=%u ally=%u type=%u estates=%d", es.owner, ally, es.type,
                static_cast<int>(st.highlightEstates.size()));
}

// [RE 0x451985] playHighlightBlink：阻塞播放联动闪烁（先闪烁、后弹收费提示）
// 依据: 0x451985 逐帧 byte_476380[i] 重绘（sub_4554FC；**byte_476380[i] 是
//       unk_485D68 亮度变换表的索引 k**，非线性查表变换——2026-09-27 修正误用的 >>2
//       线性偏移），每帧 sub_4528B9(30)，循环后 sub_4528B9(400) 停留；
//       原版在 showMessage(收费) 之前完成
// 迁移: 逐帧 renderGameFrame（画 surface）+ renderFrame（present/音频）+ 分段延时，
//       期间 pumpEvents 保持窗口响应（收租时 gamePlayerControl=0，事件被吞/仅物件提示）
// [PORT] 音频续喂：kTargetQueued=1024 字节（8bit mono @44.1kHz ≈ 23ms）——阻塞循环的
//       30ms 间隔会超过缓冲导致音乐 underrun（卡顿）→ 延时拆为 5ms 小段逐段 audio.update()
// preRedraw=false（整路段类卡片 天使/恶魔/漲價/查封）：原版闪烁 sub_4554FC 每帧从
//   **上次绘制的地图表面**复制（= 数据修改前的画面）+ 高亮变换，新等级/拆除后的画面要等
//   收尾 refreshGameUi(0,0,1)；重写对应 = 进入时快照整屏 → 每帧恢复快照 + 高亮变换
//   （不 renderGameFrame，否则会画出刚改完的新数据）。调用方须已渲染过（目标选择模态
//   退出时的画面即修改前画面，mapHitRegions 亦为当时记录）。
void playHighlightBlink(Application& app, bool preRedraw) {
    GameState& st = app.gameState();
    if (st.highlightFrame < 0) {
        return; // 联动组 ≤ 1（原版 v141 > 1 才播放）
    }
    // [RE 0x451985] 形状快照（原版 pickBuffer 标记语义：启动时提取，播放期间不随地图数据
    //   重建——拆除后的地块仍显示高亮，如新闻 idx 19 山洪归公）
    Surface& dst = app.surface();
    std::vector<uint16_t> snap;
    if (preRedraw) {
        // 防御: 先按当前视口重建 mapHitRegions（部分事件路径可能自上次渲染后未重绘，
        //   否则形状快照为空 → 高亮不可见）
        renderGameFrame(app);
        captureHighlightShapes(app);
    } else {
        captureHighlightShapes(app);  // 用调用方现有记录（修改前画面）
        snap.assign(dst.pixels(), dst.pixels() + static_cast<size_t>(Surface::kWidth) *
                                                        Surface::kHeight);
    }
    RICH4_LOGI("route highlight: %zu estates + %zu corps -> %zu shapes (RE 0x451985)",
               st.highlightEstates.size(), st.highlightCorps.size(), st.highlightShapes.size());
    auto waitAudio = [&app](int ms) {
        for (int t = 0; t < ms; t += 5) {
            app.audio().update();
            delayMs(5);
        }
    };
    auto presentFrame = [&](int f) {
        st.highlightFrame = f;
        if (!preRedraw) {
            std::memcpy(dst.pixels(), snap.data(), snap.size() * sizeof(uint16_t));
            drawEstateHighlight(app);  // 快照（修改前画面）+ 高亮变换
        } else {
            renderGameFrame(app);  // 内部已含 drawEstateHighlight
        }
        app.renderFrame();
        app.pumpEvents();
    };
    for (int f = 0; f < 16; ++f) {
        presentFrame(f);
        waitAudio(30);
    }
    // 停留期（原版 sub_4528B9(400)：最后一帧 byte_476380[15]=0 无偏移）
    presentFrame(16);
    waitAudio(400);
    st.highlightFrame = -1;
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    st.highlightShapes.clear();
}

// [RE 0x45144F] playEventFlc：阻塞播放事件 FLC 帧（得點券 data.mkf[537] 等）
// 依据: 0x45144F flcPlay(res, x, y, flags, soundId)：flcOpen（flags&1 透明 = 调色板索引 0；
//       见 0x4506C7）→ 阻塞逐帧 flcDecodeFrame，帧间隔 = FLC 头 speed（flags BYTE1 = 0 时）；
//       首帧解码后 sub_45434F 播放 Effect.mkf[soundId]（-1 不播）；播完释放资源。
// 迁移: 逐帧 renderGameFrame（经 eventFlc 叠加）+ renderFrame + pumpEvents + 分段延时，
//       音频续喂同 playHighlightBlink
// interruptible = 原版 flcOpen flags bit1（g_flcInterruptible 0x48C880）：
//   2026-09-29 全量回验原版 55 处 sub_45144F 调用点 flags——**仅开场 playIntro
//   （0x415A04/0x415B60/0x415C7E，flags=3/0x14000003）与月结悲情/冠军全身像
//   （0x43894E/0x439050，flags 表 dword_475A0B/0x4759F7 值 3/515/1027/1539 全含 bit1，
//   重写= month_settle_dialog.cpp playSettleFlc）可打断**；游戏事件动画
//   （神明插图 540..551/施工 523/拆除 526/入狱 538/住院 524/新闻 527..539/命运/
//   魔法屋 553/529/得點券 537/卡片 536/骰子/跳伞 559/破产 555/556…）flags 低 2 位
//   恒 01 → 全部**不可打断**（保持原版）；本参数默认 false 即 1:1，无需逐点改动
// freeze=true: 播完**冻结末帧**持续显示（原版 backbuffer 保留，直到下一次全量重绘/
//       showGodNarration 释放）——神明插图 + 旁白叠加需要
// switchFrame（原版 flcOpen flags BYTE2 = dword_48C85C 场景切换帧，[RE 0x450CED]）：
//   >0 进入"保帧"模式——动画背景保持**开始前**的场景表面（每帧回卷区域背景再叠加当前帧，
//   = 原版透明 blit 于保留表面），解到第 switchFrame 帧执行一次**视口锁定**的全量重绘
//   （原版 sub_40829D(-1,0) 用上次视口 dword_48B2AC，不随传送后的玩家位置重居中）：
//   视口原地不动、仅重建绘制列表——角色经棋子守卫 0x4086A1 此刻从画面消失、建筑显示
//   降级；动画**播完返回主循环后**视口才切到医院/监狱。原版实证：524=帧30（抬进医院）、
//   538=帧18（警车到中间）、525/532/526=帧3、552=帧1；0=不切换（插图期无状态变化的
//   动画，逐帧重绘与原版表面静止等价）。2026-09-24 三轮实机修正（跳视口→保帧→锁视口）
void playEventFlc(Application& app, int mkfIndex, int x, int y, int soundId, bool freeze,
                  int switchFrame, bool interruptible) {
    GameState& st = app.gameState();
    auto blob = st.data.read(mkfIndex);
    if (!blob || !st.eventFlc.open(std::move(*blob))) {
        RICH4_LOGW("eventFlc: data.mkf[%d] unavailable (RE 0x45144F)", mkfIndex);
        return;
    }
    // [RE 0x4528B9] 清除进入前残留的打断输入（防上一次界面点击误打断本次动画）
    app.consumeSkipInput();
    st.eventFlcX = x;
    st.eventFlcY = y;
    st.eventFlcActive = true;
    trace::logf("flc idx=%d x=%d y=%d sound=%d freeze=%d switch=%d (RE 0x45144F)", mkfIndex, x, y,
                soundId, freeze ? 1 : 0, switchFrame);
    const bool preserve = switchFrame > 0;
    // 动画开始前的视口中心（= 原版 dword_48B2AC"上次绘制视口"，传送不改它）
    const int lockViewX = st.viewX;
    const int lockViewY = st.viewY;
    std::vector<uint16_t> bg;  // 回卷区域（保帧模式）：FLC 覆盖矩形与屏幕交集
    int rx0 = 0;
    int ry0 = 0;
    int rw = 0;
    int rh = 0;
    Surface& dst = app.surface();
    auto snapBg = [&]() {
        bg.resize(static_cast<size_t>(rw) * rh);
        for (int r = 0; r < rh; ++r) {
            std::memcpy(&bg[static_cast<size_t>(r) * rw],
                        dst.pixels() + static_cast<size_t>(ry0 + r) * Surface::kWidth + rx0,
                        static_cast<size_t>(rw) * sizeof(uint16_t));
        }
    };
    if (preserve) {
        rx0 = std::max(0, x);
        ry0 = std::max(0, y);
        rw = std::max(0, std::min(Surface::kWidth, x + st.eventFlc.width()) - rx0);
        rh = std::max(0, std::min(Surface::kHeight, y + st.eventFlc.height()) - ry0);
        if (rw > 0 && rh > 0) {
            snapBg();
        }
    }
    const int interval = std::max(1, st.eventFlc.speedMs());
    bool soundPlayed = false;
    int frameNo = 0;       // 1 基（对应原版解码后 dword_48C874）
    bool switched = false;
    // [RE 0x45144F] g_flcInterruptible（flcOpen flags bit1）：原版延时循环内
    //   PeekMessage 检测 514/517/257 → v6=1 提前退出（停在当前帧、不重绘）
    //   光标隐藏由 Application::renderFrame 按 eventFlcActive 统一驱动
    bool aborted = false;
    while (!aborted && st.eventFlc.nextFrame()) {
        ++frameNo;
        if (preserve && rw > 0 && rh > 0) {
            if (!switched && frameNo >= switchFrame) {
                // [RE 0x450F04] 切换帧 = sub_40829D(-1,0)：**以动画开始前的视口**全量重绘
                //   （视口原地不动，仅绘制列表重建——角色经 0x4086A1 守卫此刻从画面消失、
                //   拆除建筑显示降级）；借 manualView+viewSmooth 临时锁定实现。
                //   重绘时**暂停动画叠加**得到纯净新场景 → 以其更新回卷基准（此后动画
                //   透明区透出的即新场景，不再残留切换前旧画面/上一帧像素=影子）→
                //   再回卷+叠加当前帧呈现（对应原版"重绘→回写当前帧"）。
                const bool mv = st.manualView;
                const int osx = st.viewSmoothX;
                const int osy = st.viewSmoothY;
                st.manualView = true;
                st.viewSmoothX = lockViewX;
                st.viewSmoothY = lockViewY;
                st.eventFlcActive = false;
                renderGameFrame(app);
                st.eventFlcActive = true;
                st.manualView = mv;
                st.viewSmoothX = osx;
                st.viewSmoothY = osy;
                snapBg();
                switched = true;
            }
            // 回卷基准（切换前=开始前画面；切换后=切换帧纯净场景）+ 叠加当前动画帧
            for (int r = 0; r < rh; ++r) {
                std::memcpy(dst.pixels() + static_cast<size_t>(ry0 + r) * Surface::kWidth + rx0,
                            &bg[static_cast<size_t>(r) * rw],
                            static_cast<size_t>(rw) * sizeof(uint16_t));
            }
            overlayEventFlcFrame(app);
        } else {
            renderGameFrame(app);
        }
        app.renderFrame();
        app.pumpEvents();
        if (!soundPlayed && soundId >= 0) {
            // [RE 0x45434F] 首帧解码后播放 FLC 音效（Effect.mkf[soundId]）
            app.audio().playEffect(soundId);
            soundPlayed = true;
        }
        // [RE 0x45144F] 打断检测（g_flcInterruptible=flags bit1）：原版在帧间延时循环内
        //   PeekMessage 检测 514/517/257 → v6=1 提前结束，表面停在当前帧（不重绘）
        for (int t = 0; t < interval; t += 5) {
            if (interruptible && app.consumeSkipInput()) {
                aborted = true;
                break;
            }
            app.audio().update();
            delayMs(5);
        }
        if (aborted) {
            trace::logf("flc skip idx=%d frame=%d (RE 0x45144F g_flcInterruptible)", mkfIndex,
                        frameNo);
            break;
        }
    }
    if (!freeze) {
        st.eventFlcActive = false;
    }
    trace::logf("flc done idx=%d frames=%d", mkfIndex, frameNo);
    RICH4_LOGI("eventFlc: data.mkf[%d] played @(%d,%d)%s%s (RE 0x45144F)", mkfIndex, x, y,
               freeze ? " frozen" : "", switchFrame > 0 ? " switch" : "");
}

// ===== 卡片卡包（P2 卡片格 case 13；0x4412E4/0x441262/0x44128F/0x441343/0x441E12）=====
// 数据结构: g_cardState60（0x499120，4 玩家 × 15 槽，槽内存卡 id，0 = 空）；
//           g_propStock（0x499198，byte_499197+1，30 种卡的赠卡池剩余数量）

// [RE 0x441262] cardBagCount：卡包非空槽数量
int cardBagCount(const GameState& st, int player) {
    int n = 0;
    for (int i = 0; i < 15; ++i) {
        if (st.cardState60[15 * player + i] != 0) {
            ++n;
        }
    }
    return n;
}

// [RE 0x44128F] cardBagLowestValue：卡包中价格最低（byte_47FDEF 最小）的卡 id（舍弃候选）
int cardBagLowestValue(const GameState& st, int player) {
    int best = 10000;
    int card = 0;
    for (int i = 0; i < 15; ++i) {
        const int c = st.cardState60[15 * player + i];
        if (c != 0 && best > kCardPrices[c]) {
            best = kCardPrices[c];
            card = c;
        }
    }
    return card;
}

// [RE 0x441343] cardBagRemove：移除指定卡（后槽前移 + 尾槽清 0 + 赠卡池恢复）
void cardBagRemove(GameState& st, int player, int card) {
    const size_t off = static_cast<size_t>(15 * player);
    for (int i = 0; i < 15; ++i) {
        if (st.cardState60[off + i] == card) {
            std::memmove(&st.cardState60[off + i], &st.cardState60[off + i + 1], 14 - i);
            st.cardState60[off + 14] = 0;
            ++st.propStock[card - 1]; // [RE 0x4413A2] ++byte_499197[card]
            return;
        }
    }
}

// [RE 0x4413AD] cardBagHas：卡包是否存在指定卡（免罪/嫁祸判定用）
bool cardBagHas(const GameState& st, int player, int card) {
    if (player < 0 || player >= 4 || card <= 0) {
        return false;
    }
    for (int i = 0; i < 15; ++i) {
        if (st.cardState60[15 * player + i] == card) {
            return true;
        }
    }
    return false;
}

// [RE 0x441210] resolvePenaltyTarget：惩罚目标解析（免罪卡 21 → -1；嫁祸卡 19 → 新目标）
// 依据: 0x441210 反编译; 免罪 sub_444BB2（showCardGet(21,"%s\n\n免罪卡生效！") + 移除卡）;
//   嫁祸 sub_44476A(p,0,0)（详见 passOnCardDialog 0x44476A）
int resolvePenaltyTarget(Application& app, int player) {
    GameState& st = app.gameState();
    if (cardBagHas(st, player, 21)) {
        char text[160];
        std::snprintf(text, sizeof text, "%s\n\n免罪卡生效！",
                      st.players[player].name ? st.players[player].name : "");
        showCardGet(app, 21, text); // [RE 0x444BB2 sub_441F73(21,...); 0x444BF2 sprintf 直引 g_players 不去空格]
        cardBagRemove(st, player, 21);
        playCardLine(app, player, 20);  // [RE 0x444BB2 使用者台词 免罪]
        RICH4_LOGI("resolvePenaltyTarget: p=%d 免罪卡抵消 (RE 0x441210/0x444BB2)", player);
        return -1;
    }
    if (cardBagHas(st, player, 19)) {
        const int t = passOnCardDialog(app, player); // [RE 0x44476A]
        if (t != -1) {
            return t;
        }
    }
    return player;
}

// [RE 0x4412E4] giveCardToBag：放入玩家卡包（满 15 → 先舍弃最低价；赠卡池递减）
void giveCardToBag(GameState& st, int player, int card) {
    if (cardBagCount(st, player) == 15) {
        cardBagRemove(st, player, cardBagLowestValue(st, player));
    }
    const size_t off = static_cast<size_t>(15 * player);
    for (int i = 0; i < 15; ++i) {
        if (st.cardState60[off + i] == 0) {
            st.cardState60[off + i] = static_cast<uint8_t>(card);
            trace::logf("card.give p=%d id=%d slot=%d", player, card, i);
            --st.propStock[card - 1]; // [RE 0x44133B] --byte_499197[card]
            return;
        }
    }
}

// [RE 0x441E12] drawFreeCard：从赠卡池按数量展开后 rand() 抽一张放入卡包（0 = 池空）
int drawFreeCard(GameState& st, int player) {
    uint8_t list[136]; // 原版栈缓冲 136（kPropStockInit 总和 = 100 ≤ 136）
    int n = 0;
    for (int v4 = 0; v4 < 30; ++v4) {
        for (int i = 0; i < st.propStock[v4] && n < 136; ++i) {
            list[n++] = static_cast<uint8_t>(v4);
        }
    }
    if (n == 0) {
        return 0;
    }
    const int card = list[dbg::roll(dbg::SlotCard, n)] + 1;
    trace::logf("card.draw p=%d id=%d pool=%d", player, card, n);
    giveCardToBag(st, player, card);
    return card;
}

// [RE 0x441F73] showCardGet：卡片获得显示（卡片图 data.mkf[570+id] @ (138,200) 不透明 +
//   提示框 g_tipFrame 帧 5 @ (220,129) + 文本；音效槽 23；停留 1500ms）
// 依据: 0x441F73 反编译; 0x441204 内嵌元素头 = 0x010000A5（w=165,h=256）；
//       blitElementFullscreen(dst, elem, 138, 200)；audioPlayEffect(dword_482402 = 槽 23)；
//       sub_4528B9(0x5DC = 1500)；原版另保存/恢复 (0,40,440,480) 背景（重写逐帧重绘等价）
namespace {
struct CardGetCtx {
    Application* app = nullptr;
    const char* text = nullptr;
    std::vector<uint16_t> pixels;
    int w = 0;
    int h = 0;
    uint64_t startMs = 0;
};

void cardGetDraw(CardGetCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    renderGameFrame(app); // 地图背景
    if (ctx.w > 0 && ctx.h > 0) {
        uint16_t* d = dst.pixels();
        for (int row = 0; row < ctx.h; ++row) {
            const int py = 200 + row; // [RE 0x441F73] (138,200)（元素偏移 0）
            if (py < 0 || py >= Surface::kHeight) {
                continue;
            }
            for (int col = 0; col < ctx.w; ++col) {
                const int px = 138 + col;
                if (px < 0 || px >= Surface::kWidth) {
                    continue;
                }
                d[static_cast<size_t>(py) * Surface::kWidth + px] =
                    ctx.pixels[static_cast<size_t>(row) * ctx.w + col];
            }
        }
    }
    const UiImage& tip = app.gameState().estateTiles; // [RE 0x48BAD8] g_tipFrame
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(5), 220, 129, false); // [RE 0x456418]
    }
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, ctx.text, 220, 129, 4); // [RE 0x44FABC]
}

bool cardGetHandler(const SDL_Event* event, void* user) {
    CardGetCtx& ctx = *static_cast<CardGetCtx*>(user);
    if (!event) {
        ctx.startMs = nowMs();
        cardGetDraw(ctx);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (nowMs() - ctx.startMs >= 1500) {
            ctx.app->events().requestExit(0);
        } else {
            cardGetDraw(ctx);
        }
        return true;
    }
    // [RE 0x4528B9(1500)] 原版延时消息泵检测 514/517/257 提前结束（吞掉不穿透）：
    //   卡片获得展示可被左键按下/Esc/Enter/Space 打断（2026-09-29 订正"纯延时"误标）
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        ctx.app->events().requestExit(0);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN &&
        (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_RETURN ||
         event->key.key == SDLK_SPACE)) {
        ctx.app->events().requestExit(0);
        return true;
    }
    return true;
}
} // namespace

void showCardGet(Application& app, int cardId, const char* text) {
    GameState& st = app.gameState();
    trace::logf("cardget id=%d text=\"%s\"", cardId, text ? text : "");
    CardGetCtx ctx;
    ctx.app = &app;
    ctx.text = text;
    constexpr int kCardW = 165; // [RE 0x441204] 内嵌元素头 0x010000A5（w=0x00A5,h=0x0100）
    constexpr int kCardH = 256;
    auto blob = st.data.read(static_cast<size_t>(570 + cardId));
    if (blob && blob->size() >= static_cast<size_t>(kCardW) * kCardH * 2) {
        ctx.w = kCardW;
        ctx.h = kCardH;
        ctx.pixels.resize(static_cast<size_t>(kCardW) * kCardH);
        std::memcpy(ctx.pixels.data(), blob->data(), ctx.pixels.size() * sizeof(uint16_t));
    } else {
        RICH4_LOGW("card get: data.mkf[%d] unavailable (RE 0x441F73)", 570 + cardId);
    }
    app.audio().playEffectSlot(23); // [RE 0x482402] 槽 23（Effect.mkf[62]）
    RICH4_LOGI("card get: id %d -> %s (RE 0x441F73)", cardId, text);
    runModal(app, &cardGetHandler, &ctx, 16);
}

// [RE 0x41D559] ownerCanCollectRent：地主状态免收租检查（false = 免收，已显示提示）
// 依据: 0x41D559 反编译; 依次判断（命中即 showMessage(文本, 1500) 并返回 false）：
//   flag(+23) 高/低半字节均非 0 = 房屋查封中; 地主 +65 同盟对象 == 付款人+1 = 與%s同盟中;
//   地主携带死神（cellTableIdx==15）= 死神顯靈; stateFlags BYTE0/1/2/3 = 住宿/消失/坐牢/住院中;
//   byte54 = 冬眠中; state37 = 夢遊中
bool ownerCanCollectRent(Application& app, int ownerIdx, uint8_t flag, const char* feeName) {
    GameState& st = app.gameState();
    const Player& owner = st.players[ownerIdx];
    // [RE 0x41D585] copyNameNoSpaces：免收租提示消息名字去空格
    const std::string ownerName = playerNameNoSpace(st, ownerIdx);
    char text[192];
    if ((flag & 0xF0) != 0 && (flag & 0x0F) != 0) {
        std::snprintf(text, sizeof(text), "房屋查封中\n\n免收%s！", feeName);
    } else if (owner.ally == static_cast<uint8_t>(st.currentPlayer + 1)) {
        std::snprintf(text, sizeof(text), "與%s同盟中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if (owner.cellTableIdx == 15) {
        std::snprintf(text, sizeof(text), "死神顯靈\n\n免收%s！", feeName);
    } else if ((owner.stateFlags & 0xFFu) != 0) {
        std::snprintf(text, sizeof(text), "%s住宿中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if ((owner.stateFlags & 0xFF00u) != 0) {
        std::snprintf(text, sizeof(text), "%s消失中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if ((owner.stateFlags & 0xFF0000u) != 0) {
        std::snprintf(text, sizeof(text), "%s坐牢中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if ((owner.stateFlags & 0xFF000000u) != 0) {
        std::snprintf(text, sizeof(text), "%s住院中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if (owner.byte54 != 0) {
        std::snprintf(text, sizeof(text), "%s冬眠中\n\n免收%s！", ownerName.c_str(), feeName);
    } else if (owner.state37 != 0) {
        std::snprintf(text, sizeof(text), "%s夢遊中\n\n免收%s！", ownerName.c_str(), feeName);
    } else {
        return true;
    }
    showMessage(app, text, 1500);
    return false;
}

// [RE 0x41D709] applyGodRentModifier：付款方携带神明对租金的调整
// 依据: 0x41D709 反编译; switch(cellTableIdx)：1 小財神=减半 / 2 大財神=免付 /
//   5 小窮神=加付 50% / 6 大窮神=加倍；金额有变化时 showMessage(提示, 1500)
int32_t applyGodRentModifier(Application& app, int payer, int32_t rent, const char* feeName) {
    const uint8_t slot = app.gameState().players[payer].cellTableIdx;
    int32_t v = rent;
    char text[192];
    switch (slot) {
        case 1:
            std::snprintf(text, sizeof(text), "小財神顯靈\n\n%s減免一半！", feeName);
            v = rent / 2;
            break;
        case 2:
            std::snprintf(text, sizeof(text), "大財神顯靈\n\n免付%s！", feeName);
            v = 0;
            break;
        case 5:
            std::snprintf(text, sizeof(text), "小窮神顯靈\n\n%s加付50％！", feeName);
            v = rent + rent / 2;
            break;
        case 6:
            std::snprintf(text, sizeof(text), "大窮神顯靈\n\n加倍付%s！", feeName);
            v = 2 * rent;
            break;
        default:
            return rent;
    }
    if (v != rent) {
        showMessage(app, text, 1500);
    }
    // [RE 0x41D7B4/0x41D7C1] 大財神免付（v==0）→ 意外之财台词（sub_44F567，原金额档 expr3）
    if (v == 0 && rent > 0) {
        playWindfallLine(app, payer, rent);
    }
    return v;
}

// 神明消息格式串（以 exe 原始字节 cp950 解码为准；IDA 串视图对 0x4634C0/0x463514
// 截断显示成 "%s"，完整文本见 AGENTS 踩坑）
constexpr char kFmtGodBuild[] = "%s顯靈\n\n加蓋一層房屋！";   // [RE 0x4634C0] aS_7
constexpr char kFmtGodInvest[] = "%s顯靈\n\n投資失敗！";     // [RE 0x463514] aS_14
constexpr char kNameAngel[] = "天使";                        // [RE off_47ED9A] 0x466678

// [RE 0x40FA61] checkCarriedGod：附身 idx 7 小衰神/8 大衰神/15 死神 阻止买地/升级/建设施
// → showMessage(sprintf kFmtGodInvest, 1500)。依据: 0x40FA61 反编译；调用点 = landingEvent
//   五处付费守卫（0x41A013 购地 / 0x4199AE 升级 / 0x41A261 建设施 / 0x41A86B corp 购地 /
//   设施升级），原版统一进本函数
bool checkCarriedGod(Application& app, const GameState& st, uint8_t carried) {
    if (carried != 7 && carried != 8 && carried != 15) {
        return false;
    }
    char text[96];
    std::snprintf(text, sizeof(text), kFmtGodInvest, kObjectNames[carried]);
    trace::logf("god.block carried=%u", carried);
    showMessage(app, text, 1500);
    return true;
}

// [RE 0x40B110] angelUpgrade：神明对落地格的**免费**建设/加盖（天使与福神共用）。返回位标志：
// 依据: 0x40B110 反编译；estate：type==0 且 level<5、或 type==1(连锁店) 且 level==0 → ++level
//   （达 5 附带 0x80）；corp：有设施且 sub<kFacilityMaxLevel[type] → ++sub（旧值 4 附带 0x80）；
//   无设施 → 直接建设（AI alive&6 = rand%4+1 随机设施 [RE 0x40B1D7]、人类 =
//   selectFacilityDialog(0) [RE 0x40B1EC]；原版无取消检查，取消回落 0=公園），sub=1。
//   bit0=已建设；bit7(0x80)=封顶（调用方播 FLC 523 施工动画 sub_40B0CD）
int angelUpgrade(Application& app, uint16_t objId) {
    GameState& st = app.gameState();
    int ret = 0;
    if (objId > 2000 && objId < 4000) {
        const int idx = objId - 2000;
        if (idx > 0 && idx < static_cast<int>(st.estates.size())) {
            Estate& es = st.estates[idx];
            if ((es.type == 0 && es.level < 5) || (es.type == 1 && es.level == 0)) {
                ++es.level;
                ret = 1;
                if (es.level == 5) {
                    ret |= 0x80;
                }
            }
        }
    } else if (objId > 4000 && objId < 6000) {
        const int idx = objId - 4000;
        if (idx > 0 && idx < static_cast<int>(st.corps.size())) {
            Corp& cp = st.corps[idx];
            if (cp.sub != 0) {
                const uint8_t maxLv = cp.type < 5 ? kFacilityMaxLevel[cp.type] : 1;
                if (cp.sub < maxLv) {
                    const uint8_t old = cp.sub;
                    cp.sub = static_cast<uint8_t>(old + 1);
                    ret = 1;
                    if (old == 4) {
                        ret |= 0x80;
                    }
                }
            } else {
                Player& pl = st.players[st.currentPlayer];
                if ((pl.alive & 6) != 0) {
                    cp.type = static_cast<uint8_t>(dbg::roll(dbg::SlotCorp, 4) + 1);
                } else {
                    const int r = selectFacilityDialog(app, 0);
                    cp.type = static_cast<uint8_t>(r >= 0 ? r : 0);
                }
                cp.sub = 1;
                ret = 1;
            }
        }
    }
    return ret;
}

// [RE 0x40F8BE] godBlessUpgrade：普通升级/建设施**成功后**，附身福神（idx 3 小福神/4 大福神）
//   免费追加一级。调用点 = landingEvent 三条付费成功路径（estate level++ 未满级 0x419A48 /
//   corp 建设施 0x41A2AE / corp 升级未满 5 0x41A36B）。
// 依据: 0x40F8BE 反编译；corp 无设施先 showMessage(福神名)+2048 抑制 → angelUpgrade →
//   消息 = kObjectNames[idx] → 音效槽 18 [dword_4823DA]；封顶(bit7) → 台词 off_480886[角色]
//   （sub_44EF41 [TODO P4]）+ FLC 523 [RE 0x40FA26]；未封顶 → 随机祝福台词
//   off_48084A[角色][rand&1]（[TODO P4]）
void godBlessUpgrade(Application& app, int p, uint16_t cellEntId) {
    GameState& st = app.gameState();
    const uint8_t idx = st.players[p].cellTableIdx;
    if (idx != 3 && idx != 4) {
        return;
    }
    if (cellEntId >= st.cellEnts.size()) {
        return;
    }
    const uint16_t objId = st.cellEnts[cellEntId].special;
    RICH4_LOGI("godBlessUpgrade: p%d god=%u cell=%u objId=%u (RE 0x40F8BE)", p, idx, cellEntId,
               objId);
    // [RE 0x40F8EE] 进入先重绘：显示买地/升级后的**即时状态**（买地=空地+购买人棋子标记、
    //   付费升级=升级 1 级后的建筑）。随后 angelUpgrade 只改内存，消息叠加在此画面上；
    //   消息后 [RE 0x40F9EF] 再重绘，加盖后的建筑此时才出场（原版表面保留时序）
    renderGameFrame(app);
    char text[128];
    int flags = 0;
    if (objId > 4000 && objId < 6000) {
        const int ci = objId - 4000;
        if (ci > 0 && ci < static_cast<int>(st.corps.size()) && st.corps[ci].sub == 0) {
            std::snprintf(text, sizeof(text), kFmtGodBuild, kObjectNames[idx]);  // [RE 0x4634C0]
            showMessage(app, text, 1500);
            flags = 2048;
        }
    }
    flags |= angelUpgrade(app, objId);
    RICH4_LOGI("godBlessUpgrade: objId %u -> flags %d (RE 0x40F8BE)", objId, flags);
    if ((flags & 1) == 0) {
        return;
    }
    if ((flags & 0x800) == 0) {
        std::snprintf(text, sizeof(text), kFmtGodBuild, kObjectNames[idx]);  // [RE 0x4634C0]
        showMessage(app, text, 1500);
    }
    app.audio().playEffectSlot(18);  // [RE 0x40F9E1] dword_4823DA 升级音效
    renderGameFrame(app);            // [RE 0x40F9EF] 消息后重绘：加盖后的建筑出场
    const int ci = st.players[p].charIndex;
    if ((flags & 0x80) != 0) {
        // [RE 0x40FA13] 封顶台词 off_480886（列15、expr0）+ FLC 523
        if (ci >= 0 && ci < 12) {
            playLine(app, p, kMoneyLines[ci][6], 0);
        }
        playEventFlc(app, 523, 0, 40, 90);
    } else {
        // [RE 0x40FA49 → 0x40ECDE] 未封顶：大笑台词 off_48086A（列8、expr3）
        if (ci >= 0 && ci < 12) {
            playLine(app, p, kMoneyLines[ci][2], 3);
        }
    }
}

// [RE 0x40FBB8] findDeathGodCarrier：付款人外、存活且附身 idx∈{14,15} 的首位玩家（代付者）。
// 依据: 0x40FBB8 反编译（g_playerAttachedObj == 14 || == 15）。初版表 slot13=寶箱→idx14、
//   slot14=死神→idx15——**原版即包含寶箱携带者**，照抄；文本用"死神顯靈"(0x4639CC)
int findDeathGodCarrier(const GameState& st, int payer) {
    for (int i = 0; i < st.playerCount && i < 9; ++i) {
        if (i != payer && st.players[i].alive != 0 &&
            (st.players[i].cellTableIdx == 14 || st.players[i].cellTableIdx == 15)) {
            return i;
        }
    }
    return -1;
}

// [RE 0x419E58/0x419EB8/0x419ECC（0x41A62E/0x41A683/0x41A6A4、0x41AF20/0x41AF75/0x41AF99 同构）]
//   resolveFeePayer：收费金额定案（>0）后、转账前的三步：**免費卡(20) 抵用 → 嫁祸卡(19) 转嫁
//   → 死神/寶箱代付**（三处收费管线：住宅過路費/商業收費/行業設施收費）。
//   返回 -1 = 免費卡已抵用（调用方免付）；否则返回实际付款玩家。
int resolveFeePayer(Application& app, int payer, int32_t fee, const char* feeName) {
    if (fee == 0) {
        return payer;
    }
    GameState& st = app.gameState();
    // [RE 0x419E58/0x419EB8] 卡片抵用/转嫁门槛：fee ≥ 2000×物价 或 fee > 付款人现金+存款；
    //   小额费用不再弹询问（原版對免費/嫁祸同门槛，此前重写对所有 fee>0 直接询问=差异）
    const bool cardGate = (fee >= 2000 * st.moneyMul) ||
                          (fee > st.players[payer].cash + st.players[payer].bank);
    // [RE 0x419E58] 免費卡(20)：抵用成功 → 免付
    if (cardGate && cardBagHas(st, payer, 20) && applyFreeCard(app, payer, fee) == 1) {
        RICH4_LOGI("resolveFeePayer: p%d free card covers fee %d (RE 0x444A60)", payer, fee);
        return -1;
    }
    // [RE 0x419EB8] 嫁祸卡(19)：转嫁他人（原版 0x44476A(payer, 2, 0)）
    if (cardGate && cardBagHas(st, payer, 19)) {
        const int r = passOnCardDialog(app, payer);
        if (r != -1) {
            payer = r;
        }
    }
    const int c = findDeathGodCarrier(st, payer);
    if (c < 0) {
        return payer;
    }
    char text[192];
    std::snprintf(text, sizeof(text), "死神顯靈\n\n由%s賠償%s",
                  playerNameNoSpace(st, c).c_str(), feeName); // [RE 0x419EED/0x41A6A4/0x41AF99 管线 sprintf 前 copyNameNoSpaces]
    showMessage(app, text, 1500);
    RICH4_LOGI("death god pays: player %d covers %d fee %d (RE 0x40FBB8)", c, payer, fee);
    return c;
}

// [RE 0x40B455] 房地产公司 AI 预选（selectTargetDialog 0x2090086 的 AI 分支）：返回自己产业中
//   「当前等级租金最高(estate type0 level<5 → fees[level]) / buildPrice 最高(corp sub<上限)」
//   的地块 objId(2000+/4000+)，无自有产业 → 0（调用方回落 1000×M）。
static int realtyAiPick(const GameState& st, int a1) {
    int bestObj = 0;
    int bestVal = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& e = st.estates[i];
        if (e.owner == static_cast<uint8_t>(a1 + 1) && e.type == 0 && e.level < 5) {
            const int val = e.fees[e.level]; // [RE 0x40B4A0] +32+2*level
            if (bestVal < val) {
                bestVal = val;
                bestObj = static_cast<int>(i) + 2000;
            }
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        const Corp& c = st.corps[i];
        if (c.owner == static_cast<uint8_t>(a1 + 1) && bestVal < c.buildPrice && c.type < 5 &&
            c.sub < kFacilityMaxLevel[c.type]) { // [RE 0x40B4E4] +34
            bestVal = c.buildPrice;
            bestObj = static_cast<int>(i) + 4000;
        }
    }
    return bestObj;
}

// [RE 0x40F381] landAfterMove：landingEvent 公共末尾（loc_41B077）的**神明落地持续效果**。
// 依据: 0x40F381 反编译 + 调用点 0x41B077（estate/corp/specPt 各路径——含买地失败/拒绝等
//   jmp loc_41B074 路径——汇聚后统一执行；事件格 case 直接返回不经过）。前置：
//   stateFlags.BYTE0(住宿)==0 且 alive [RE 0x40F3AB]。按附身 cellTableIdx 分派：
//   9 天使 → angelUpgrade 免费加盖（corp 无设施先消息"天使"+2048 抑制 [RE 0x40F455]；
//     成功 → 消息"天使"[off_47ED9A=0x466678] + 音效槽 18 [dword_4823DA] + 封顶 → FLC 523
//     [RE 0x40B0CD]，其前置 sub_40829D(-1,0) 全屏重绘=重写逐帧重绘不迁移）；
//   10 惡魔 → 落地格有建筑（estate.level/corp.sub）→ 有主者记债 30×M（原版含自己，
//     addPlayerDebt 自欠跳过 [RE 0x40F547/0x40F5A2]）→ 消息(0x4634D7) → demolishAtObjId
//     [RE 0x40F61B] → FLC 526 @ 建筑屏幕坐标−(55,55) 音效 95 [RE 0x40F636]；
//   12 土地公 → estate/corp 占为本有：owner==自己 → 无动作 [RE 0x40F6B4]；他人地记债
//     M×(estate+28/corp+34)×(level+2.0)/5.0（flt_46350C=2.0/flt_463510=5.0）；原无主且
//     cfgLandPerm → 到期日 [RE 0x40F7F2]；owner=我+1 + 小地图 + 消息(0x4634F2)
//     + 大笑台词（0x40F8AB 价值列0 = kValueLines 首列，P4-C）；
//   其余附身值（含 3/4 福神=在付费升级路径 godBlessUpgrade）无持续动作。
// 差异: 另一调用点 sub_418EBD（回合尾、alive&0x30 临时载具分支）依赖载具计时系统 [TODO]；
//       原版开头的 refreshGameUi(视口同步) 省略（重写逐帧重绘）
void landAfterMove(Application& app, int p, uint16_t cellEntId) {
    trace::logf("landmove p=%d cell=%u god=%u", p, cellEntId,
                app.gameState().players[p].cellTableIdx);
    GameState& st = app.gameState();
    if (p < 0 || p >= 9 || cellEntId >= st.cellEnts.size()) {
        return;
    }
    Player& pl = st.players[p];
    if ((pl.stateFlags & 0xFFu) != 0 || pl.alive == 0) {
        return;
    }
    const uint16_t objId = st.cellEnts[cellEntId].special;
    char text[128];
    switch (pl.cellTableIdx) {
        case 9: {  // 天使：所到之地免费加盖/建设
            // [RE 0x40F427] 进入先重绘（视口对准玩家；落地后状态 = 棋子停在格上/旧建筑）。
            //   随后 angelUpgrade 只改内存，消息叠加在此画面上；音效后 [RE 0x40F506]
            //   再重绘（refreshGameUi(0,0,1)），加盖后的建筑此时才出场（与 godBlessUpgrade 同）
            renderGameFrame(app);
            int flags = 0;
            if (objId > 4000 && objId < 6000) {
                const int ci = objId - 4000;
                if (ci > 0 && ci < static_cast<int>(st.corps.size()) &&
                    st.corps[ci].sub == 0) {
                    std::snprintf(text, sizeof(text), kFmtGodBuild, kNameAngel);
                    showMessage(app, text, 1500);  // "天使顯靈\n\n加蓋一層房屋！" [RE 0x40F46D]
                    flags |= 2048;
                }
            }
            flags |= angelUpgrade(app, objId);
            if ((flags & 1) != 0) {
                if ((flags & 0x800) == 0) {
                    std::snprintf(text, sizeof(text), kFmtGodBuild, kNameAngel);
                    showMessage(app, text, 1500);  // [RE 0x40F4C7]
                }
                app.audio().playEffectSlot(18);      // [RE 0x40F4F8] dword_4823DA
                renderGameFrame(app);                // [RE 0x40F506] refreshGameUi(0,0,1)
                if ((flags & 0x80) != 0) {
                    playEventFlc(app, 523, 0, 40, 90);  // [RE 0x40F517 → 0x40B0CD]
                }
            }
            break;
        }
        case 10: {  // 惡魔：所到之地拆毀一层
            if (objId <= 2000 || objId >= 6000) {
                return;
            }
            int objX = 0;
            int objY = 0;
            uint8_t owner = 0;
            bool hasBuilding = false;
            if (objId < 4000) {
                const int idx = objId - 2000;
                if (idx <= 0 || idx >= static_cast<int>(st.estates.size())) {
                    return;
                }
                const Estate& es = st.estates[idx];
                hasBuilding = es.level != 0;  // [RE 0x40F53C]
                owner = es.owner;
                objX = es.x;
                objY = es.y;
            } else {
                const int idx = objId - 4000;
                if (idx <= 0 || idx >= static_cast<int>(st.corps.size())) {
                    return;
                }
                const Corp& cp = st.corps[idx];
                hasBuilding = cp.sub != 0;    // [RE 0x40F59C]
                owner = cp.owner;
                objX = cp.x;
                objY = cp.y;
            }
            if (!hasBuilding) {
                return;
            }
            if (owner != 0) {
                addPlayerDebt(st, owner - 1, p, 30 * st.moneyMul);
            }
            showMessage(app, "小惡魔顯靈\n\n拆毀一層房屋！", 1500);  // [RE 0x40F610/0x4634D7]
            demolishAtObjId(app, objId, 0);
            int sx = 0;
            int sy = 0;
            if (projectMapPoint(st, objX, objY, sx, sy)) {
                playEventFlc(app, 526, sx - 55, sy - 55, 95, false, 3);  // [RE 0x40F675 帧3=拆后场景]
            }
            break;
        }
        case 12: {  // 土地公：所到之地强占为己有
            if (objId <= 2000 || objId >= 6000) {
                return;
            }
            const bool isEstate = objId < 4000;
            const int idx = isEstate ? objId - 2000 : objId - 4000;
            if (idx <= 0) {
                return;
            }
            if (isEstate && idx >= static_cast<int>(st.estates.size())) {
                return;
            }
            if (!isEstate && idx >= static_cast<int>(st.corps.size())) {
                return;
            }
            uint8_t prevOwner;
            uint8_t levelField;
            uint16_t priceField;
            uint32_t* expire;
            if (isEstate) {
                Estate& es = st.estates[idx];
                prevOwner = es.owner;
                levelField = es.level;
                priceField = es.priceAdd;
                expire = &es.expireDate;
            } else {
                Corp& cp = st.corps[idx];
                prevOwner = cp.owner;
                levelField = cp.sub;
                priceField = cp.buildPrice;
                expire = &cp.expireDate;
            }
            if (prevOwner == static_cast<uint8_t>(p + 1)) {
                return;  // [RE 0x40F6B4] 已是自己的地：无动作
            }
            if (prevOwner != 0 && prevOwner <= 9) {
                // [RE 0x40F6CD] 赔偿 = M×priceAdd×(level+2)/5（記入欠款矩阵）
                const int32_t comp = static_cast<int32_t>(
                    static_cast<double>(st.moneyMul * priceField) *
                    (static_cast<double>(levelField) + 2.0) / 5.0);
                addPlayerDebt(st, prevOwner - 1, p, comp);
            }
            if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3 && prevOwner == 0) {
                // [RE 0x40F7F2] 强占无主地也按设定记到期日
                static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
                int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
                if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                    d += 0xF400;
                }
                *expire = static_cast<uint32_t>(d);
            }
            if (isEstate) {
                st.estates[idx].owner = static_cast<uint8_t>(p + 1);
            } else {
                st.corps[idx].owner = static_cast<uint8_t>(p + 1);
            }
            buildMiniMapMarks(app);  // [RE 0x40F834] rebuildMiniMap(0)
            showMessage(app, "土地公顯靈\n\n強佔土地！", 1500);  // [RE 0x40F878/0x4634F2]
            // [RE 0x40F8AB] 大笑台词（off_48084A 列0 = kValueLines 第一列、expr0）
            if (st.players[p].charIndex >= 0 && st.players[p].charIndex < 12) {
                playLine(app, p, kValueLines[st.players[p].charIndex][0], 0);
            }
            RICH4_LOGI("god land-grab: player %d seizes obj %u (prev owner %u, RE 0x40F381)", p,
                       objId, prevOwner);
            break;
        }
        default:
            break;
    }
}

// [RE 0x41AA3C/0x41ACD8] 房地產公司（specPt index11）选地施工公共段：视口对准目标地块
//   → angelUpgrade（times 次；第 1 次封顶则不再追加）→ FLC 553 施工（switchFrame=44
//   全量重绘时显示结果）→ 封顶播 FLC 523。返回第 1 次 flags（bit7=封顶）。
//   拥有者停留：times=2（0x41AAE8 + 0x41AAFC 两次）；他人收费：times=1（0x41AD7E）后按地价收费
//   ownerPath=true（拥有者）封顶时另有台词（0x41AB2F 列15 expr0，FLC 523 前）
int realtyPickUpgrade(Application& app, int sel, int times, bool ownerPath) {
    GameState& st = app.gameState();
    int tx = 0;
    int ty = 0;
    getObjectPosition(st, sel, tx, ty);  // [RE 0x40AF12]
    st.manualView = true;                // [RE refreshGameUi(x,y,0)] 视口对准
    st.viewSmoothX = tx;
    st.viewSmoothY = ty;
    st.viewScrolling = false;
    renderGameFrame(app);
    // 施工前场景快照（angelUpgrade 可能弹设施选择面板残留；FLC553 保帧基准 = 旧建筑）
    Surface& dst = app.surface();
    std::vector<uint16_t> preUpgrade(
        dst.pixels(), dst.pixels() + static_cast<size_t>(Surface::kWidth) * Surface::kHeight);
    int flags = angelUpgrade(app, sel);  // [RE 0x40B110]
    if ((flags & 0x80) == 0) {
        for (int i = 1; i < times; ++i) {
            angelUpgrade(app, sel);
        }
    }
    std::memcpy(dst.pixels(), preUpgrade.data(), preUpgrade.size() * sizeof(uint16_t));
    app.renderFrame();
    playEventFlc(app, 553, 0, 40, 91, false, 44);  // [RE 0x45144F flags 0x2C0001 音效 91]
    if ((flags & 0x80) != 0) {
        // [RE 0x41AB2F] 拥有者路径封顶台词（列15 expr0）+ playGodBuildFlc FLC 523
        if (ownerPath && st.currentPlayer >= 0 && st.currentPlayer < 4) {
            const int ci = st.players[st.currentPlayer].charIndex;
            if (ci >= 0 && ci < 12) {
                playLine(app, st.currentPlayer, kMoneyLines[ci][6], 0);
            }
        }
        playEventFlc(app, 523, 0, 40, 90, false, 0);
    }
    st.manualView = false;  // [RE sub_41D546] 视口复位
    renderGameFrame(app);
    return flags;
}

// [RE 0x41B0B3..0x41B106] landingEvent 收尾（loc_41B077）内的研究所研发判定：
//   objId ∈ (4000,6000)（商業用地）且 corp.owner == currentPlayer+1 且 state37 == 0
//   且 type == 4（研究所）且 sub != 0 且 (flag & 0x0F) == 0 → labDevelopDialog(0x44101D)。
//   位于 landAfterMove / sub_448A7E / refreshGameUi 之后（0x41B086→0x41B0A5→0x41B0B3）。
//   升级询问、建设施（首次建研究所）、购地失败等全部路径都汇聚到收尾 →
//   即「先提示升級(加蓋)后研究」；首次建成研究所立即弹研究面板（sub=1 仅機器工人）。
static void labDevelopAfterLanding(Application& app, int p, uint16_t objId) {
    if (objId <= 4000 || objId >= 6000) {
        return;
    }
    GameState& st = app.gameState();
    const int ci = static_cast<int>(objId) - 4000;
    if (ci <= 0 || ci >= static_cast<int>(st.corps.size())) {
        return;
    }
    Corp& cp = st.corps[ci];
    if (cp.owner == static_cast<uint8_t>(p + 1) && st.players[p].state37 == 0 && cp.type == 4 &&
        cp.sub != 0 && (cp.flag & 0x0F) == 0) {
        labDevelopDialog(app, cp);  // [RE 0x44101D] 人类选/未解锁灰度，AI 恒选最高解锁项
    }
}

// [RE 0x41982D] landingEvent
// 依据: 0x41982D 反编译; 停留格结算。switch(cellEnt+36 低字节 格子类型 0..16)：
//   入口先按 g_cellTypeSound[type]（0x475299）播放格子音效（type 2..16）
//   case 0 = 按 objId 分（2000..4000 住宅用地 / 4000..6000 商業用地 / 其他 → loc_41A168）
//   无主住宅用地购地（loc_41A013）：
//     守卫 state37 / cellTableIdx==12 → 价格=(level*priceBase+priceAdd)*moneyMul
//     → 现金检查（不足 → 0x41A159 showMessage"您的現金不足！"）→ sprintf("%s\n\n", estate.name)
//     → 人类 askDialog → sub_40FA61 携带神明检查（showMessage 神明名）→ owner=cur+1
//     → 音效槽 17（dword_4823D2）→ rebuildMiniMap(0) → 期限 expireDate（g_landPermDays）→ 扣现金
//   自己住宅用地升級（0x419911）：见下
//   他人住宅用地收租（loc_419A67）：音效槽 20 → ownerCanCollectRent（0x41D559 地主状态免收）
//     → estateRouteRent（0x419744 同路段 fees[level] 之和 / 连锁店数×2000）× moneyMul
//     → flag(+23) **仅地主侧**翻倍 → 同盟分账（地主 +65 对象的联合租金 v134 并入总额，
//       按 v132 = v134/(v8+v134) 拆分转账；消息 0x46399A「屬%s與%s」）→ applyGodRentModifier
//       （0x41D709 付款方神明调整，作用于双方总额）→ showMessage → transferMoney(flags=0
//       收款入银行)
//   事件格 case 10/11/12 得點券（0x41B184/0x41B21E/0x41B2A3）：data.mkf[537] FLC（阻塞）+
//     showMessage("得點券ＮＮ點") + 点券 50/30/10；case10 列0/1 随机、case11 列2
//     （kValueLines expr0，0x41B211；case 12 原版无）
//   事件格 case 13 卡片（0x41B302）：data.mkf[536] FLC + drawFreeCard（0x441E12 赠卡池抽卡，
//     满 15 舍弃最低价，见 cardBag* 辅助）+ showCardGet（0x441F73 卡片图/提示框/1500ms）
//     + 卡片价值语音（0x41B38C playValueLine(卡价)）
// 台词（P4-B ✅）：收租付款方 sub_44F4ED/sub_44F42D、收款方 sub_44F354（同盟分账仅地主份额）
int landingEvent(Application& app, uint16_t cellEntId) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9 || cellEntId >= st.cellEnts.size()) {
        return 0x80;
    }
    const CellEnt& ce = st.cellEnts[cellEntId];
    const uint16_t objId = ce.special; // +32
    const uint8_t cellType = static_cast<uint8_t>(ce.occMask & 0xFF); // +36 低字节
    trace::logf("land cell=%u obj=%u type=%u p=%d", cellEntId, objId, cellType, p);
    // [RE 0x475299] g_cellTypeSound[type] → 音效槽
    static const uint8_t kCellTypeSound[17] = {9, 0, 10, 10, 10, 10, 10, 10, 10,
                                               10, 16, 16, 16, 16, 10, 10, 10};
    if (cellType >= 2 && cellType <= 16) {
        app.audio().playEffectSlot(kCellTypeSound[cellType]);
    }
    if (cellType == 2) {
        // [RE 0x41B11E] case 2 新聞格：newsEvent（0x44B6DF）全屏事件，
        //   panel.mkf[66] + 插画 data.mkf[441+idx] + 2400ms 阻塞；返回 0x80
        RICH4_LOGI("landingEvent: case 2 新聞格 cellEnt=%u cur=%d (RE 0x41982D)", cellEntId, p);
        newsEvent(app);
        return 0x80;
    }
    if (cellType == 3) {
        // [RE 0x41B129] case 3 命運格：fateEvent（0x44DB81）全屏事件，
        //   panel.mkf[66] + 插画 data.mkf[477..516]（word_475FB4）+ 1600+800ms 阻塞；返回 0x80
        RICH4_LOGI("landingEvent: case 3 命運格 cellEnt=%u cur=%d (RE 0x41982D)", cellEntId, p);
        fateEvent(app);
        return 0x80;
    }
    if (cellType == 4) {
        // [RE 0x41B132→0x43D304] case 4 監獄格：保释面板（人类头像选择 / AI 按个性自动保释）
        RICH4_LOGI("landingEvent: case 4 監獄格 cellEnt=%u cur=%d(alive=%u) jailFlags=%u%u%u%u%u%u%u%u "
                   "(RE 0x41982D)",
                   cellEntId, p, st.players[p].alive, st.jailFlags[0], st.jailFlags[1],
                   st.jailFlags[2], st.jailFlags[3], st.jailFlags[4], st.jailFlags[5],
                   st.jailFlags[6], st.jailFlags[7]);
        jailBailDialog(app);
        return 0x80;
    }
    if (cellType == 5) {
        // [RE 0x41B13C→0x43E9A4] case 5 醫院格：办理出院面板（0x43DA27 阶段机+护士微动画）
        RICH4_LOGI("landingEvent: case 5 醫院格 cellEnt=%u cur=%d(alive=%u) hospitalFlags=%u%u%u%u%u%u%u%u "
                   "(RE 0x41982D)",
                   cellEntId, p, st.players[p].alive, st.hospitalFlags[0], st.hospitalFlags[1],
                   st.hospitalFlags[2], st.hospitalFlags[3], st.hospitalFlags[4],
                   st.hospitalFlags[5], st.hospitalFlags[6], st.hospitalFlags[7]);
        hospitalVisitDialog(app);
        return 0x80;
    }
    if (cellType >= 10 && cellType <= 12) {
        // [RE 0x41B184/0x41B21E/0x41B2A3] case 10/11/12 得點券（type 10/11/12 = 50/30/10 點）：
        //   data.mkf[537] FLC（31x39 @ (204,180)，透明，14 帧 @71ms，音效 Effect.mkf[98]）→
        //   showMessage("得點券ＮＮ點", 1000) → g_playerPoints += 50/30/10
        //   → 角色语音 sub_44EF41（case 10 槽 charIndex*27+rand&1 / case 11 每角色槽 +2；
        //     case 12 原版无语音）→ 返回 0x80（default）
        static const int kPoints[3] = {50, 30, 10};
        static const char* kText[3] = {"得點券５０點", "得點券３０點", "得點券１０點"};
        const int i = cellType - 10;
        // [RE 0x45144F(res, 0xCC, 0xB4, flags=1 透明, soundId=0x62)] 阻塞播放
        playEventFlc(app, 537, 0xCC, 0xB4, 98);
        showMessage(app, kText[i], 1000); // [RE 0x41B1BE] 3E8 = 1000ms
        st.players[p].points = static_cast<uint16_t>(st.players[p].points + kPoints[i]);
        // [RE 0x41B211] 角色语音（expr0）：case10 列0/1 随机、case11 列2；case12 原版无
        if (i < 2 && p < 4) {
            const int ci = st.players[p].charIndex;
            if (ci >= 0 && ci < 12) {
                playLine(app, p, kValueLines[ci][(i == 0) ? (std::rand() & 1) : 2], 0);
            }
        }
        RICH4_LOGI("landingEvent: cellType %u 得點券 +%d (total %u) RE 0x41982D case %u", cellType,
                   kPoints[i], st.players[p].points, cellType);
        return 0x80;
    }
    if (cellType == 14) {
        // [RE 0x41B396] case 14 銀行格：柜员机（sub_4379C9）→ sceneRequest==0 时停留处理
        //   （sub_436668：贷款/还款/週轉，1.4 实现，见 bank-system.md §3）
        RICH4_LOGI("landingEvent: case 14 銀行格 cellEnt=%u cur=%d (RE 0x41982D)", cellEntId, p);
        bankVisitDialog(app);
        if (st.sceneRequest == 0) {
            bankStayDialog(app, static_cast<int>(cellEntId)); // [RE 0x41B3AF]
        }
        return 0x80;
    }
    if (cellType == 13) {
        // [RE 0x41B302] case 13 卡片格：
        //   data.mkf[536] FLC（28x40 @ (208,180)，透明，音效 Effect.mkf[99]）→
        //   drawFreeCard（0x441E12：从赠卡池抽卡入卡包；池空返回 0 → 不弹提示直接下一位）→
        //   sprintf("得到%s！", 卡名) → sub_441F73 显示卡片图 → 语音 sub_44F230（P4）→ 0x80
        playEventFlc(app, 536, 0xD0, 0xB4, 99); // [RE 0x45144F(res, 0xD0, 0xB4, 1, 0x63)]
        const int cardId = drawFreeCard(st, p);
        if (cardId == 0) {
            return 0x80; // [RE 0x41B34F] 0 → jmp default（不弹提示）
        }
        char text[64];
        std::snprintf(text, sizeof(text), "得到%s！", kCardNames[cardId]); // [RE 0x463AA8]
        showCardGet(app, cardId, text);                                    // [RE 0x441F73]
        // [RE 0x41B38C] 卡片获得语音（sub_44F230，卡价 byte_47FDEF[8*card] = kCardPrices）
        playValueLine(app, p, kCardPrices[cardId]);
        RICH4_LOGI("landingEvent: card cell -> %s (RE 0x41982D case 13)", text);
        return 0x80;
    }
    if (cellType == 15) {
        // [RE 0x41B3B9] case 15 百貨公司：卡片店/道具店（0x42E931，见 shop_dialog.cpp）
        //   实参 = cellEnt+32 objId（special，6000+n；landingEvent 0x419854），非数组下标
        //   自家 → 董事长赠礼；人类 → 商店 UI；AI → 自动买卖；营业额入 specPt.fund/+44
        RICH4_LOGI("landingEvent: case 15 百貨公司 cellEnt=%u objId=%u cur=%d (RE 0x41982D)",
                   cellEntId, objId, p);
        shopDialog(app, static_cast<int>(objId));
        return 0x80;
    }
    if (cellType == 9) {
        // [RE 0x41B17A] case 9 樂透：人类投注界面（sub_4315CC→sub_42F7FC）/ AI 自动投注
        lotteryVisit(app);
        return 0x80;
    }
    if (cellType == 16) {
        // [RE 0x41B3CB] case 16 魔法屋：女巫随机条件 + 玩家选惩罚（0x43380A，
        //   见 magic_house_dialog.cpp / 43380a-magic-house.md）
        RICH4_LOGI("landingEvent: case 16 魔法屋 cellEnt=%u cur=%d (RE 0x41982D)", cellEntId, p);
        magicHouseVisit(app);
        return 0x80;
    }
    if (cellType >= 6 && cellType <= 8) {
        // [RE 0x41B146/0x41B15E/0x41B16C] case 6/7/8 企鵝挖寶/七彩氣球/喜從天降
        //   （0x415215/0x4154DC/0x4155FC）：人类+動畫過程开 → 交互小游戏；否则原版
        //   共享 else(0x415457) 兜底得分（rand()%20+50 点券）；四大恶人忽略。
        //   见 minigame.h / docs/reverse/functions/41982d-p2-events.md §6
        miniGameVisit(app, cellType);
        return 0x80;
    }
    if (cellType != 0) {
        RICH4_LOGI("landingEvent: cellType %u objId %u (RE 0x41982D, P2 TODO)", cellType, objId);
        return 0x80;
    }
    // case 0：普通格，按 objId 分（objId == 0 → 无事件返回 0x80；
    //   有 objId 的住宅用地/商業用地/行業設施點路径统一在 loc_41B111 返回等待值后下一位）
    // 差异: 原版 0x88 = 8 帧；重写帧率更高（约 2 倍），暂用 0x90 = 16 帧对齐观感
    constexpr int kLandingWait = 0x90;
    if (objId == 0) {
        return 0x80;
    }
    if (objId > 2000 && objId < 4000) {
        const int idx = static_cast<int>(objId) - 2000;
        if (idx <= 0 || idx >= static_cast<int>(st.estates.size())) {
            goto landingTail;
        }
        Estate& es = st.estates[idx];
        Player& pl = st.players[p];
        if (es.owner == 0) {
            // ---- 无主住宅用地购地（loc_41A013）----
            if (pl.state37 != 0 || pl.cellTableIdx == 12) {
                goto landingTail;
            }
            const int32_t price =
                (static_cast<int32_t>(es.level) * es.priceBase + es.priceAdd) * st.moneyMul;
            if (price > pl.cash) {
                // [RE 0x41A159] 现金不足：showMessage(byte_46398B="您的現金不足！", 1500)
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("buy land: cash %d < price %d (RE 0x41A159)", pl.cash, price);
                goto landingTail;
            }
            const std::string label = big5ToUtf8(es.name, sizeof(es.name));
            char text[160];
            // [RE 0x4639E1] 原版格式串（BIG5）："%s\n\n費用:%d元\n\n是否買下此地?"
            std::snprintf(text, sizeof(text), "%s\n\n費用:%d元\n\n是否買下此地?", label.c_str(),
                          price);
            bool buy = false;
            if ((pl.alive & 6) != 0) {
                buy = aiBuyDecision(st, pl, price); // [RE 0x41D7D4] AI 买地决策
            } else if (pl.alive == 1) {
                // [RE 0x440BA8] askDialog：文本居中 (220,140) + YES/NO 框 (220,320)
                buy = confirmDialog(app, text, 220, 320);
            }
            if (!buy) {
                goto landingTail;
            }
            // [RE 0x40FA61] 携带衰神/死神阻止 → "%s顯靈\n\n投資失敗！"
            const uint8_t carried = pl.cellTableIdx;
            if (checkCarriedGod(app, st, carried)) {
                RICH4_LOGI("buy land blocked by carried god %u (RE 0x40FA61)", carried);
                goto landingTail;
            }
            es.owner = static_cast<uint8_t>(p + 1);
            // [RE 0x41D476(0,0,1)] UI/地图刷新（重写每帧重绘，无需显式）
            app.audio().playEffectSlot(17); // [RE 0x4823D2] 买地音效
            buildMiniMapMarks(app);         // [RE 0x40A4E1] rebuildMiniMap(0)
            if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3) {
                // [RE 0x4751F0] g_landPermDays（打包日期偏移：年<<16 / 月<<8）
                static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
                // [RE 0x4521CB] 日期加法（月 > 12 时 +0xF400 进位）
                int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
                if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                    d += 0xF400;
                }
                es.expireDate = static_cast<uint32_t>(d);
            }
            pl.cash -= price;
            // [RE 0x41A13E] sub_44F627(estate.name, 0)：同路段住宅用地 ≥3 时角色感想（必播）
            playEstateChainSpeech(app, es.name, 0);
            // [RE 0x41A154 → 0x419A48] 购地成功也进**福神免费追加**：附身小/大福神 →
            //   estate 免费加盖一层（level 0→1）——原先只在付费升级/建设施路径调用（修）
            godBlessUpgrade(app, p, cellEntId);
            RICH4_LOGI("buy land: estate %d price %d owner %d (RE 0x41982D/0x41A013)", idx, price,
                       p + 1);
        } else if (es.owner == static_cast<uint8_t>(p + 1)) {
            // ---- 自己的住宅用地：升级/加盖（0x419911..0x419A52）----
            // 守卫: level >= 5 / type != 0（大地块走建造菜单）/ state37 != 0 → 无操作
            // 费用 = word[estate+30](priceBase) * g_moneyMul；现金不足 → showMessage("現金不夠支付！")
            // 人类 askDialog 询问（AI alive&6 自动升级）→ checkCarriedGod 阻止 → level++
            //   → 音效槽 18（dword_4823DA）；level==5 → 角色语音（sub_44EF41 索引 0）
            //   → 否则 estateChainSpeech(name, 1)（1/3 概率感想，P4）
            // 差异: 原版询问文本 sprintf("%s\n\n", name)（无费用），重写补费用行便于阅读；
            //       台词（P4-B ✅）：封顶列15 expr0 / 升级连锁吐槽 estateChainSpeech(name,1)
            if (es.level >= 5 || es.type != 0 || pl.state37 != 0) {
                goto landingTail;
            }
            const int32_t cost = static_cast<int32_t>(es.priceBase) * st.moneyMul;
            if (cost > pl.cash) {
                // [RE 0x419A52] 现金不足 → showMessage(byte_46398B="您的現金不足！", 1500)
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("upgrade: cash %d < cost %d (RE 0x419A52)", pl.cash, cost);
                goto landingTail;
            }
            const std::string label = big5ToUtf8(es.name, sizeof(es.name));
            char text[160];
            // [RE 0x46396D] 原版升级询问文本（estate/corp 共用 aS_3）
            std::snprintf(text, sizeof(text), "%s\n\n升級費用:%d元\n\n是否升級？", label.c_str(),
                          cost);
            bool upgrade = false;
            if ((pl.alive & 6) != 0) {
                upgrade = true; // [RE 0x41997D] AI 直接升级
            } else if (pl.alive == 1) {
                upgrade = confirmDialog(app, text, 220, 320); // [RE 0x419996] askDialog
            }
            if (!upgrade) {
                goto landingTail;
            }
            // [RE 0x4199AE] checkCarriedGod 阻止 → "%s顯靈\n\n投資失敗！"
            const uint8_t carried = pl.cellTableIdx;
            if (checkCarriedGod(app, st, carried)) {
                RICH4_LOGI("upgrade blocked by carried god %u (RE 0x40FA61)", carried);
                goto landingTail;
            }
            pl.cash -= cost;
            ++es.level;                          // [RE 0x4199D1] inc byte ptr [esi+1Ah]
            app.audio().playEffectSlot(18);      // [RE 0x4823DA] 升级音效
            if (es.level == 5) {
                // [RE 0x4199F1] 封顶台词 off_480886[角色]（列15、expr0）+ 施工 FLC 523
                //   （sub_40B0CD，经 loc_41B077）
                if (pl.charIndex >= 0 && pl.charIndex < 12) {
                    playLine(app, p, kMoneyLines[pl.charIndex][6], 0);
                }
                playEventFlc(app, 523, 0, 40, 90);
            } else {
                // [RE 0x419A2B] estateChainSpeech(name,1)：同路段≥3 感想（1/3 概率）
                playEstateChainSpeech(app, es.name, 1);
                godBlessUpgrade(app, p, cellEntId);  // [RE 0x419A48 → 0x40F8BE] 福神免费追加
            }
            RICH4_LOGI("upgrade: estate %d level %d cost %d (RE 0x419911)", idx, es.level, cost);
        } else {
            // ---- 他人住宅用地收租（loc_419A67）----
            // [RE 0x419A67] 收租音效 dword_4823EA = 槽 20 → sub_41D559 地主状态免收检查 →
            //   sub_419744 联合租金（同路段 fees[level] 之和 / 连锁店数×2000；地主同盟者
            //   同组另算 v134）→ flag(+23) 非 0 **仅翻倍地主侧** v8 → 高亮 →
            //   【同盟分账】v8 += v134、v132 = v134/v8 → sub_41D709 付款方神明调整（作用于
            //   双方总额）→ showMessage（同盟时 0x46399A「屬%s與%s」）→ 按 v132 拆分
            //   sub_41D2C6 转账（flags=0 收款入银行）→ estate+44 = 本次租金
            // 台词（P4-B ✅ 2026-09-27）：付款方 sub_44F4ED（欠债优先）/sub_44F42D、
            //   收款方 sub_44F354（按金额档选句的台词表，非浮动数字；同盟=地主份额）
            app.audio().playEffectSlot(20);
            static const char kFeeName[] = "過路費"; // [RE 0x47517C] off_47517C 初值
            if (ownerCanCollectRent(app, es.owner - 1, es.flag, kFeeName)) {
                const uint8_t ally = st.players[es.owner - 1].ally; // [RE 0x419AAF] 地主同盟对象
                const int32_t ownRent = estateRouteRent(st, es.owner, es); // [RE 0x419ACE/0x419AEE]
                const int32_t allyRent =
                    ally != 0 ? estateRouteRent(st, ally, es) : 0; // [RE 0x419ADC/0x419AFA]
                int32_t total = ownRent;
                if (es.flag != 0) {
                    total *= 2; // [RE 0x419B0F] 仅地主侧联合租金翻倍（同盟侧不翻）
                }
                total += allyRent; // [RE 0x419CBD]
                // [RE 0x419A67] 先播放联动闪烁（阻塞，原版 sub_451985 在收费提示之前完成；
                //   原版 v141 > 1 的判据含同盟地块，见 startRouteHighlight）
                startRouteHighlight(st, es);
                playHighlightBlink(app);
                // 收费提示（原版显示神明调整前的总额 v8；**同盟者无同路段地也显示「與B」**——
                //   原版以 ally 非 0 选格式而非 v134 > 0，照抄）
                if (total > 0) {
                    const std::string label = big5ToUtf8(es.name, sizeof(es.name));
                    const std::string ownerName = playerNameNoSpace(st, es.owner - 1); // [RE 0x419CA0]
                    char text[192];
                    if (ally != 0) {
                        const std::string mateName = playerNameNoSpace(st, ally - 1); // [RE 0x419CF9]
                        // [RE 0x46399A] "%s\n\n屬%s與%s\n\n請付%d元%s"
                        std::snprintf(text, sizeof(text), "%s\n\n屬%s與%s\n\n請付%d元%s",
                                      label.c_str(), ownerName.c_str(), mateName.c_str(), total, kFeeName);
                    } else {
                        // [RE 0x4639B3] "%s\n\n此地屬%s\n\n請付%d元%s"
                        std::snprintf(text, sizeof(text), "%s\n\n此地屬%s\n\n請付%d元%s",
                                      label.c_str(), ownerName.c_str(), total, kFeeName);
                    }
                    showMessage(app, text, 1500);
                }
                // [RE 0x419CDD] 同盟分成比例 = 同盟租金 / 总额（浮点；转账时截断）
                const double allyRatio =
                    (ally != 0 && total > 0)
                        ? static_cast<double>(allyRent) / static_cast<double>(total)
                        : 0.0;
                // [RE 0x41D709] 付款方神明调整（作用于双方总额，内部可能弹神明提示）
                int32_t fee = applyGodRentModifier(app, p, total, kFeeName);
                // [RE 0x41A003] 转账：付款人 → 地主/同盟（flags=0，收款入银行存款）
                if (fee > 0) {
                    // [RE 0x419DB1/0x419DD8] 记账（欠款矩阵）：付款人 → 地主/同盟，按神明调整后
                    //   金额拆分 /100（免费卡/嫁祸后仍保留；clamp≥0）
                    if (ally != 0) {
                        addPlayerDebt(st, p, es.owner - 1, (fee - allyRent) / 100);
                        addPlayerDebt(st, p, ally - 1, allyRent / 100);
                    } else {
                        addPlayerDebt(st, p, es.owner - 1, fee / 100);
                    }
                    // [RE 0x419E58/0x419EB8/0x419ECC] 免费卡/嫁祸卡/死神代付（见 resolveFeePayer）
                    const int payer = resolveFeePayer(app, p, fee, kFeeName);
                    // [RE 0x419F42] 付款人 = 地主/同盟（嫁祸卡转嫁收款人）→ 不转账、不更新租金
                    if (payer >= 0 && payer != es.owner - 1 &&
                        (ally == 0 || payer != ally - 1)) { // -1 = 免費卡抵用免付
                        // [RE 0x419F59/0x419F67] 付款方台词：欠债者面对最大债主（50%）优先，
                        //   否则按金额档（expr2）；同盟分账仅供地主收款台词
                        if (!playDebtorLine(app, payer, es.owner - 1, fee)) {
                            playPayerLine(app, payer, fee);
                        }
                        int32_t allyShare = 0;
                        if (ally != 0) {
                            // [RE 0x419F7D] 同盟份额 = trunc(费用 × 比例)，地主拿余数
                            allyShare = static_cast<int32_t>(
                                static_cast<double>(fee) * allyRatio);
                            // [RE 0x419FA1] 收款方台词（地主份额档位；同盟无收款台词）
                            playCollectorLine(app, es.owner - 1, fee - allyShare);
                            transferMoney(app, payer, es.owner - 1, fee - allyShare, 0);
                            transferMoney(app, payer, ally - 1, allyShare, 0);
                        } else {
                            // [RE 0x419FF0] 收款方台词
                            playCollectorLine(app, es.owner - 1, fee);
                            transferMoney(app, payer, es.owner - 1, fee, 0);
                        }
                        es.price = fee; // [RE 0x41A00B] estate+44 = 本次租金
                        RICH4_LOGI("rent: player %d -> owner %d %d / ally %u %d (estate %d, "
                                   "RE 0x419A67)",
                                   p, es.owner - 1, fee - allyShare, ally, allyShare, idx);
                    }
                }
            }
        }
    } else if (objId > 4000 && objId < 6000) {
        // ================= 商業用地 corp（0x41A168..0x41B074）=================
        const int idx = static_cast<int>(objId) - 4000;
        if (idx <= 0 || idx >= static_cast<int>(st.corps.size())) {
            goto landingTail;
        }
        Corp& cp = st.corps[idx];
        Player& pl = st.players[p];
        const std::string label =
            big5ToUtf8(reinterpret_cast<const char*>(cp.pad4), sizeof(cp.pad4));
        if (cp.owner == 0) {
            // ---- 无主商業用地购地（0x41A86B）：价格 = +34 buildPrice × M ----
            if (pl.state37 != 0 || pl.cellTableIdx == 12) {
                goto landingTail;
            }
            const int32_t price = static_cast<int32_t>(cp.buildPrice) * st.moneyMul;
            if (price > pl.cash) {
                showMessage(app, "您的現金不足！", 1500);
                RICH4_LOGI("buy corp: cash %d < price %d (RE 0x41A89D)", pl.cash, price);
                goto landingTail;
            }
            char text[160];
            // [RE 0x4639E1] "%s\n\n費用:%d元\n\n是否買下此地?"
            std::snprintf(text, sizeof(text), "%s\n\n費用:%d元\n\n是否買下此地?", label.c_str(),
                          price);
            bool buy = false;
            if ((pl.alive & 6) != 0) {
                buy = aiBuyDecision(st, pl, price); // [RE 0x41D7D4] AI 买地决策（与 estate 共用）
            } else if (pl.alive == 1) {
                buy = confirmDialog(app, text, 220, 320);
            }
            if (!buy) {
                goto landingTail;
            }
            const uint8_t carried = pl.cellTableIdx;
            if (checkCarriedGod(app, st, carried)) {  // "%s顯靈\n\n投資失敗！" [RE 0x40FA61]
                goto landingTail;
            }
            cp.owner = static_cast<uint8_t>(p + 1);
            app.audio().playEffectSlot(17); // [RE 0x4823D2] 买地音效
            buildMiniMapMarks(app);
            if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3) {
                static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
                int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
                if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                    d += 0xF400;
                }
                cp.expireDate = static_cast<uint32_t>(d);
            }
            pl.cash -= price;
            // [RE 0x41A993 → 0x419A48] 购地成功也进**福神免费追加**：附身小/大福神 → corp
            //   无设施时直接建设施（人类弹设施选择面板）/ 已有设施时升级（修）
            godBlessUpgrade(app, p, cellEntId);
            RICH4_LOGI("buy corp: corp %d price %d owner %d (RE 0x41A86B)", idx, price, p + 1);
        } else if (cp.owner == static_cast<uint8_t>(p + 1)) {
            // ---- 自己的商業用地 ----
            // [RE 0x41A1DE] state37（住院/監獄/出國等）非 0 → 跳过加盖/建设施交互，
            //   直接进收尾（收尾内的研究判定另有 state37 守卫，不会弹研究面板）
            if (pl.state37 != 0) {
                goto landingTail;
            }
            if (cp.type == 0) {
                // 建设施（0x41A1E0）：付 +34×M → selectFacilityDialog（AI rand%4+1）→ type、++sub
                const int32_t cost = static_cast<int32_t>(cp.buildPrice) * st.moneyMul;
                if (cost > pl.cash) {
                    showMessage(app, "您的現金不足！", 1500);
                    RICH4_LOGI("build facility: cash %d < cost %d (RE 0x41A216)", pl.cash, cost);
                    goto landingTail;
                }
                uint8_t facility = 0;
                bool chosen = false;
                if ((pl.alive & 6) != 0) {
                    facility = static_cast<uint8_t>(dbg::roll(dbg::SlotCorp, 4) + 1); // [RE 0x41A24D]
                    chosen = true;
                } else if (pl.alive == 1) {
                    const int r = selectFacilityDialog(app, 0); // [RE 0x41A22A]
                    if (r >= 0) {
                        facility = static_cast<uint8_t>(r);
                        chosen = true;
                    }
                }
                if (!chosen) {
                    goto landingTail;
                }
                const uint8_t carried = pl.cellTableIdx;
                if (checkCarriedGod(app, st, carried)) {  // "%s顯靈\n\n投資失敗！" [RE 0x40FA61]
                    goto landingTail;
                }
                cp.type = facility;
                ++cp.sub;                       // [RE 0x41A27C] ++sub（建设施即 1 级）
                app.audio().playEffectSlot(18); // [RE 0x4823DA]
                pl.cash -= cost;
                // [RE 0x41A2AE] 建设施成功也进福神追加路径（jmp loc_419A48）
                godBlessUpgrade(app, p, cellEntId);
                RICH4_LOGI("build facility: corp %d type %u sub %u cost %d (RE 0x41A1E0)", idx,
                           cp.type, cp.sub, cost);
            } else {
                // 设施升级（0x41A2B3）：+36 feeTable[0]×M，上限 kFacilityMaxLevel[type]
                //   **type4 研究所同样走此分支**（maxLevel=5）：先弹「是否升級？」询问，
                //   拒绝/满级/现金不足/升级成功后均汇聚收尾 loc_41B077
                //   → 收尾内研究所判定再弹研究面板（0x41B0B3）——即「先加盖(升級)后研究」
                const uint8_t maxLv = (cp.type < 5) ? kFacilityMaxLevel[cp.type] : 1;
                if (cp.sub >= maxLv) {
                    goto landingTail;
                }
                const int32_t cost = static_cast<int32_t>(cp.feeTable[0]) * st.moneyMul;
                if (cost > pl.cash) {
                    showMessage(app, "您的現金不足！", 1500);
                    RICH4_LOGI("upgrade facility: cash %d < cost %d (RE 0x41A2E6)", pl.cash, cost);
                    goto landingTail;
                }
                char text[160];
                // [RE 0x46396D] "%s\n\n升級費用:%d元\n\n是否升級？"
                std::snprintf(text, sizeof(text), "%s\n\n升級費用:%d元\n\n是否升級？",
                              label.c_str(), cost);
                bool up = false;
                if ((pl.alive & 6) != 0) {
                    up = true; // [RE 0x41A310] AI 直接升级
                } else if (pl.alive == 1) {
                    up = confirmDialog(app, text, 220, 320); // [RE 0x41A31E]
                }
                if (!up) {
                    goto landingTail;
                }
                const uint8_t carried = pl.cellTableIdx;
                if (checkCarriedGod(app, st, carried)) {  // "%s顯靈\n\n投資失敗！" [RE 0x40FA61]
                    goto landingTail;
                }
                pl.cash -= cost;
                ++cp.sub;                       // [RE 0x41A35D]
                app.audio().playEffectSlot(18); // [RE 0x4823DA]
                if (cp.sub == 5) {
                    // [RE 0x41A365 → 0x4199F1] 封顶台词（列15 expr0）+ 施工 FLC 523
                    const int ci = pl.charIndex;
                    if (ci >= 0 && ci < 12) {
                        playLine(app, p, kMoneyLines[ci][6], 0);
                    }
                    playEventFlc(app, 523, 0, 40, 90);
                } else {
                    // [RE 0x41A36B → 0x419A39] 福神免费追加
                    godBlessUpgrade(app, p, cellEntId);
                }
                RICH4_LOGI("upgrade facility: corp %d type %u sub %u cost %d (RE 0x41A2B3)", idx,
                           cp.type, cp.sub, cost);
            }
        } else {
            // ---- 他人商業用地收费（0x41A370）----
            // 守卫：无设施(sub==0)/公園(type0)/研究所(type4) 不收费
            if (cp.sub == 0 || cp.type == 0 || cp.type >= 4) {
                goto landingTail;
            }
            app.audio().playEffectSlot(20); // [RE 0x4823EA]
            const uint8_t costIdx = (cp.type < 5) ? kCorpCostMap[cp.type] : 0;
            const char* feeName = kCostNames[costIdx < 13 ? costIdx : 0];
            if (ownerCanCollectRent(app, cp.owner - 1, cp.flag, feeName)) {
                int32_t base =
                    static_cast<int32_t>(cp.feeTable[cp.sub < 6 ? cp.sub : 0]) * st.moneyMul;
                if (cp.flag != 0) {
                    base *= 2; // [RE 0x41A44C] flag 非 0 翻倍
                }
                const std::string ownerName = playerNameNoSpace(st, cp.owner - 1); // [RE 0x41A3FC]
                char text[192];
                int32_t rent = 0;
                int hotelDays = 0;
                if (cp.type == 1) {
                    // 旅館：休息天數 = 转盘(1)（完整 UI：panel[69] + 减速停止）；rent = base × days
                    hotelDays = roulettePrompt(app, 1, ownerName.c_str()); // [RE 0x41A458/0x44090E]
                    rent = base * hotelDays;
                    // [RE 0x4639FF] "休息%d天\n\n費用%d元！"
                    std::snprintf(text, sizeof(text), "休息%d天\n\n費用%d元！", hotelDays, rent);
                    showMessage(app, text, 1500);
                } else if (cp.type == 2) {
                    // 購物中心：消費倍數 = 转盘(2)（完整 UI：panel[70] + 减速停止）
                    const int mult = roulettePrompt(app, 2, ownerName.c_str()); // [RE 0x41A4B4/0x44090E]
                    rent = base * mult;
                    // [RE 0x463A14] "您的消費金額為\n\n%dx%d倍=%d元"
                    std::snprintf(text, sizeof(text), "您的消費金額為\n\n%dx%d倍=%d元", base, mult,
                                  rent);
                    showMessage(app, text, 1500);
                } else {
                    // 加油站（type3）：500 × 載具倍率 × 本次步數 × M（步行免付）
                    const int travel = pl.travel & 3;
                    if (travel == 0) {
                        goto landingTail; // [RE 0x41A4ED] 步行免付
                    }
                    const int mult = 1 << (travel - 1); // [RE 0x41A4FC]
                    rent = 500 * mult * st.diceValue * st.moneyMul;
                    // [RE 0x463A31] "加油站\n\n董事長%s\n\n請付%d元%s"
                    std::snprintf(text, sizeof(text), "加油站\n\n董事長%s\n\n請付%d元%s",
                                  ownerName.c_str(), rent, feeName);
                    showMessage(app, text, 1500);
                }
                // [RE 0x41A581] 通用后处理：神明调整 → 记账 → 转账（flags=0 收款入银行）→ corp+48
                rent = applyGodRentModifier(app, p, rent, feeName);
                int payer = -1;  // 付款人（原版 edi；住宿台词条件 payer==cur）
                if (rent > 0) {
                    // [RE 0x41A5C0] 记账（欠款矩阵）：付款人 → 地主 金额/100
                    addPlayerDebt(st, p, cp.owner - 1, rent / 100);
                    // [RE 0x41A62E/0x41A683/0x41A6A4] 免费卡/嫁祸卡/死神代付
                    payer = resolveFeePayer(app, p, rent, feeName);
                    if (payer >= 0) {  // -1 = 免費卡抵用免付
                        // [RE 0x41A710/0x41A71E/0x41A735] 付款/收款台词（嫁祸反弹付款人=地主
                        //   时原版 0x41A709 跳过台词）
                        if (payer != cp.owner - 1) {
                            if (!playDebtorLine(app, payer, cp.owner - 1, rent)) {
                                playPayerLine(app, payer, rent);
                            }
                            playCollectorLine(app, cp.owner - 1, rent);
                        }
                        transferMoney(app, payer, cp.owner - 1, rent, 0);
                    }
                    cp.lastFee = rent; // [RE 0x41A75E] corp+48 = 最近收费
                    RICH4_LOGI("corp fee: player %d -> %d amount %d (corp %d type %u, RE 0x41A370)",
                               p, cp.owner - 1, rent, idx, cp.type);
                }
                // [RE 0x41A761] 旅館住宿状态：BYTE0 = days-1（0→0x80）、累计天数、住宿欠款
                if (cp.type == 1 && hotelDays > 0 && pl.alive != 0 && st.sceneRequest == 0) {
                    // [RE 0x40D761] 设置新状态前清除旧状态（住宿/監獄/醫院）
                    pl.stateFlags = 0;
                    // [RE 0x41A7E0] 住宿天数台词（付款人=本人时；expr2）
                    if (payer == p) {
                        playStatusDaysLine(app, p, hotelDays);
                    }
                    const uint8_t stay = static_cast<uint8_t>(hotelDays - 1);
                    pl.stateFlags = static_cast<uint32_t>(stay != 0 ? stay : 0x80);
                    pl.byte66 = static_cast<uint8_t>(pl.byte66 + hotelDays); // byte_496BAA
                    // [RE 0x41A7BC] 住宿欠款（欠款矩阵）：当前玩家 → 地主 20×M×days
                    addPlayerDebt(st, p, cp.owner - 1, 20 * st.moneyMul * hotelDays);
                    RICH4_LOGI("hotel stay: player %d days %d owe %d (RE 0x41A7BC)", p, hotelDays,
                               20 * st.moneyMul * hotelDays);
                    // [RE 0x40D5A5] 走进旅館：清原格占用（消失）+ 保存朝向 + 面向旅館 +
                    //   记录旅館索引（word_496BB2）+ 启动走进移动（状态 1，慢速插值到旅館坐标）
                    if (p == st.currentPlayer && pl.cellEntId > 0 &&
                        pl.cellEntId < st.cellEnts.size()) {
                        st.cellEnts[pl.cellEntId].occMask &= ~(256u << p);
                        pl.stayCorpIdx = static_cast<uint16_t>(idx);
                        pl.byte27 = pl.dir;
                        pl.alive |= 0x20;
                        const CellEnt& ce = st.cellEnts[pl.cellEntId];
                        pl.dir = static_cast<uint8_t>(facingFromDelta(
                            static_cast<int>(cp.x) - static_cast<int>(ce.x),
                            static_cast<int>(cp.y) - static_cast<int>(ce.y)));
                        startPlayerMove(app); // [RE 0x40DD1F] alive&0x30 → 走进移动（含音效）
                        RICH4_LOGI("hotel stay: player %d walk into corp %d (RE 0x40D5A5)", p, idx);
                    }
                }
            }
        }
    } else if (objId > 6000 && objId < 8000) {
        // ========== 行業設施點 specPt（6000..7999，0x41A168..0x41B074）==========
        // [RE 0x41A8xx] 收费公式（pricing-formulas.md §5）：
        //   idx1 航空 = feeBase × 轮盘(0)天数 × M（0 → "不用出國！"）
        //   idx3 電腦 = feeBase × g_dayCount（不乘 M）
        //   idx4 保險 = feeBase × 轮盘(3)天数 × M（保险期 += days，M3）
        //   idx5/6 汽車/石油 = feeBase × 载具倍率 × 本次步數 × M（步行免付）
        //   idx11 房地產 = 抽一块地按其地價 × M（简化：无地分支 1000×M；选地 UI M3）
        //   idx12 = feeBase × 本次步數 × M；其余 index 不收費
        // 收款方 = objId-5900 的公库（transferMoney to>100 → specPts[to-100].fund，分红来源）
        const int si = objId - 6000;
        if (si > 0 && si < static_cast<int>(st.specPts.size())) {
            SpecPt& sp = st.specPts[si];
            // 名称 BIG5 → UTF-8（渲染字体为 UTF-8；pad4 为 20 字节 BIG5）
            const std::string spName =
                big5ToUtf8(reinterpret_cast<const char*>(sp.pad4), 20);
            RICH4_LOGI("specPt landing: si=%d owner=%u costType=%u stockNo=%u sharesLeft=%d "
                       "(RE 0x41A168)",
                       si, sp.owner, sp.costType, sp.stockNo, sp.sharesLeft);
            if (sp.owner != 0 && sp.owner != p + 1) {
                // ---- 他人 specPt：收费 ----
                const uint8_t spIdx = sp.costType; // +26 收费类型（g_specPtCostMap 索引）
                const std::string ownerName = playerNameNoSpace(st, sp.owner - 1); // [RE 0x41A6C5]
                const char* feeName = (spIdx < 16) ? kCostNames[kSpecPtCostMap[spIdx]] : "";
                char text[192];
                int32_t fee = 0;
                int airlineDays = 0; // [RE 0x41B05A v135] 航空轮盘天数（付款后出國用）
                if (spIdx == 1) {
                    // 航空公司：转盘(0) 天数；0 → 不用出國
                    airlineDays = roulettePrompt(app, 0, spName.c_str());
                    if (airlineDays == 0) {
                        showMessage(app, "不用出國！", 1500); // [RE 0x463A5F]
                        // [RE 0x41AC1F] 免出国台词（off_480852 列2、expr1）
                        const int ci = st.players[p].charIndex;
                        if (ci >= 0 && ci < 12) {
                            playLine(app, p, kValueLines[ci][2], 1);
                        }
                        goto landingTail;
                    }
                    fee = sp.feeBase * airlineDays * st.moneyMul;
                } else if (spIdx == 3) {
                    // 電腦公司：+34 × g_dayCount（不乘 M）
                    fee = sp.feeBase * st.dayCount;
                } else if (spIdx == 4) {
                    // 保險公司：转盘(3) 天数 → 保费；保险期 += days（M3）
                    const int days = roulettePrompt(app, 3, spName.c_str());
                    fee = sp.feeBase * days * st.moneyMul;
                } else if (spIdx == 5 || spIdx == 6) {
                    // 汽車/石油公司：载具倍率 × 本次步數（步行免付）
                    const int travel = st.players[p].travel & 3;
                    if (travel == 0) {
                        goto landingTail;
                    }
                    const int mult = 1 << (travel - 1);
                    fee = sp.feeBase * mult * st.diceValue * st.moneyMul;
                } else if (spIdx == 11) {
                    // 房地產公司（0x41ACD8..0x41AE18）：选一块地（mode 0x2090086）→ 免费加盖
                    //   1 层 + FLC553 → rent = 选中地块地价（estate+28 priceAdd / corp+34
                    //   buildPrice）× M；取消/无目标 → 1000×M
                    int sel = 0;
                    if (st.players[p].alive == 1) {
                        sel = selectTargetDialog(app, 0x2090086);  // [RE 0x446AE8]
                    } else {
                        sel = realtyAiPick(st, p);  // [RE 0x40B455] AI 预选自己产业中价值最高者加盖
                    }
                    if (sel != 0) {
                        realtyPickUpgrade(app, sel, 1, false);  // [RE 0x41AD7E] 加盖 1 层 + FLC553
                        if (sel > 2000 && sel < 4000) {
                            const int ei = sel - 2000;
                            fee = (ei > 0 && ei < static_cast<int>(st.estates.size()))
                                      ? st.estates[ei].priceAdd * st.moneyMul
                                      : 0;
                        } else if (sel > 4000 && sel < 6000) {
                            const int ci = sel - 4000;
                            fee = (ci > 0 && ci < static_cast<int>(st.corps.size()))
                                      ? st.corps[ci].buildPrice * st.moneyMul
                                      : 0;
                        }
                        RICH4_LOGI("specPt realty fee: obj %d -> %d (RE 0x41ADD5)", sel, fee);
                    } else {
                        fee = 1000 * st.moneyMul;  // [RE 0x41ADFF] 无目标
                        RICH4_LOGI("specPt realty fee: no target 1000*M (RE 0x41ADFF)");
                    }
                } else if (spIdx == 12) {
                    // 帮派/游乐：+34 × 本次步數 × M（文本用 "幫主%s"，[RE 0x463A6A]）
                    fee = sp.feeBase * st.diceValue * st.moneyMul;
                } else {
                    goto landingTail; // 其余 index 不收费
                }
                if (fee > 0) {
                    // [RE 0x463A31] "%s\n\n董事長%s\n\n請付%d元%s"
                    std::snprintf(text, sizeof(text), "%s\n\n董事長%s\n\n請付%d元%s",
                                  spName.c_str(), ownerName.c_str(), fee, feeName);
                    showMessage(app, text, 1500);
                    fee = applyGodRentModifier(app, p, fee, feeName);
                    if (fee > 0) {
                        // [RE 0x41AF20/0x41AF75/0x41AF99] 免费卡/嫁祸卡/死神代付
                        const int payer = resolveFeePayer(app, p, fee, feeName);
                        if (payer >= 0) {  // -1 = 免費卡抵用免付
                            // [RE 0x41B006] 付款人=本人 → 付款台词（金额档 expr2；公库无收款台词）
                            if (payer == p) {
                                playPayerLine(app, p, fee);
                            }
                            transferMoney(app, payer, objId - 5900, fee, 0); // 收款入公库
                        }
                        // [RE 0x41B05A] 航空付款后：付款人出國 airlineDays 天（免费卡免付时
                        //   = 当前玩家；条件：costType==1 && 天数≠0 && 付款人存活 && 非场景切换）
                        const int payTarget = (payer >= 0) ? payer : p;
                        if (spIdx == 1 && airlineDays != 0 &&
                            st.players[payTarget].alive != 0 && st.sceneRequest == 0) {
                            fateStartTravelState(app, st.players[payTarget], payTarget, airlineDays,
                                                 false);
                        }
                        RICH4_LOGI("specPt fee: player %d -> fund %d amount %d (specPt %d idx %u, "
                                   "RE 0x41A168)",
                                   p, objId - 5900, fee, si, spIdx);
                    }
                }
            } else if (sp.owner == static_cast<uint8_t>(p + 1)) {
                // ---- 自己的 specPt：拥有者停留效果（0x41A9C0 段）----
                //   costType 4 保險公司：轮盘(3) → 保险天数 += v（&0x7F 上限 127，0x41A9FE）
                //   costType 11 房地產：选一块地免费加盖（0x41AA3C）——angelUpgrade 两次
                //     （第 1 次即封顶则跳过第 2 次）+ FLC553 施工；封顶 → 台词 + FLC523
                //   其余类型无动作；取消选择 = 直接进认购/落尾
                const uint8_t ownIdx = sp.costType;
                if (ownIdx == 4) {
                    const int v = roulettePrompt(app, 3, spName.c_str());  // [RE 0x41AA01]
                    Player& pl = st.players[p];
                    pl.insuranceDays = static_cast<uint8_t>((pl.insuranceDays + v) & 0x7F);
                    RICH4_LOGI("specPt insurance: p%d +%d -> %d days (RE 0x41A9FE)", p, v,
                               pl.insuranceDays);
                } else if (ownIdx == 11) {
                    int sel = 0;
                    if (st.players[p].alive == 1) {
                        char text[128];
                        std::snprintf(text, sizeof(text), "%s\n\n", spName.c_str());  // [RE aS_0]
                        showMessage(app, text, 1500);
                        sel = selectTargetDialog(app, 0x2090086);  // [RE 0x446AE8] 住宅/商业+滚动
                    } else {
                        sel = realtyAiPick(st, p);  // [RE 0x40B455] AI 预选自己产业中价值最高者加盖
                    }
                    if (sel != 0) {
                        // [RE 0x41AAE8 + 0x41AAFC] 免费加盖两次（第 1 次封顶则一次）
                        const int flags = realtyPickUpgrade(app, sel, 2, true);
                        RICH4_LOGI("specPt realty: p%d upgrade obj %d flags %d (RE 0x41AA3C)", p,
                                   sel, flags);
                    }
                }
            }
            // [RE 0x41D1A9] 认购股份对话框（有主/无主均可；守卫在函数内：sharesLeft != 0）
            specPtBuyStockDialog(app, si);
        }
    }
landingTail:  // loc_41B077（estate/corp/specPt 各路径含失败/拒绝汇聚点）
    // [RE 0x41B077] 落地公共末尾：神明持续效果（天使加盖/惡魔拆毀/土地公強佔）
    landAfterMove(app, p, cellEntId);
    // [RE 0x41B0B3..0x41B106] 研究所研发判定：升级询问/建设施完成后统一在此弹研究面板
    labDevelopAfterLanding(app, p, objId);
    // loc_41B111：等待 kLandingWait 帧
    return kLandingWait;
}

// [RE 0x419572] rollDice
// 依据: 0x419572 反编译; v2 = byte_496B7A[当前玩家]（骰子个数）,
//       v14[i] = rand()%6+1，返回 sum(v14)；
//       a2 = sub_447285()（读 byte_475DD8 后清零）：非 0 → 单骰强制点数（遥控骰子）
// 差异: 骰子精灵绘制与 sub_45285E(500) 延时见 B4
int rollDice(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    const int forced = st.forcedDice;  // [RE 0x447285] 一次性读取
    st.forcedDice = 0;
    int sum = 0;
    if (forced != 0) {
        st.diceValues[0] = forced;
        sum = forced;
        st.diceShowCount = 1;  // [RE 0x419572 v2=1] 遥控单骰：只显示 1 个点数精灵
    } else {
        const int n = std::max(1, static_cast<int>(st.players[p].diceCount));
        for (int i = 0; i < n; ++i) {
            const int v = dbg::roll(dbg::SlotDice, 6) + 1;
            st.diceValues[i] = v;
            sum += v;
        }
        st.diceShowCount = static_cast<uint8_t>(n);
    }
    st.showDice = true;
    st.diceValue = sum; // [RE 0x48BAFC] g_diceValue = 本次行走总步数（供步数计费公式）
    RICH4_LOGI("roll dice: %d%s (RE 0x419572)", sum, forced ? " [remote]" : "");
    return sum;
}

// [RE 0x40C05C] moveOneStep
// 依据: 0x40C05C 反编译; 普通玩家: 从当前 cellEnt 的 exits[4] 中排除来向
//       （word_496B76）与占用出口，随机选下一格；占用掩码 256<<player；
//       速度 = byte_4749D8[travel&3]（或 flt_4631DC），浮点插值 g_playerSpriteX/Y；
//       到达后 byte_496B78 = sub_407A8C(上一步, 当前)，sub_40FC00 更新携带物件
// 差异: 载具状态 alive&0x30 的上下车分支、AI 槽 4..8 分支、占用位 0x40000000 判定待补
bool moveOneStep(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    Player& pl = st.players[p];
    if (st.stepFrames == 0) {
        if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
            return true;
        }
        // [RE 0x40C05C] 每步开始重载行走资源（原版 sub_40B93B）
        loadWalkResources(st, p);
        // [RE 0x4542CE + 0x48234A] 每步开始播放移动音效（moveSoundIndex 为游戏音效槽 11..15，
        //   经 dword_48234A 映射到 Effect.mkf）；槽 8（機器娃娃）由 startPlayerMove 循环播槽 9，
        //   不在此逐帧重播（原版 0x40DD1F p>=8 播一次循环音）
        if (st.moveSoundIndex > 0 && p != 8) {
            app.audio().playEffectSlot(st.moveSoundIndex);
        }
        int startX = st.cellEnts[pl.cellEntId].x;
        int startY = st.cellEnts[pl.cellEntId].y;
        st.stepExitState = 0;
        if ((pl.alive & 0x30) != 0) {
            // [RE 0x40C05C] 状态移动分支（不选出口、不改 cellEntId）：
            //   0x20 走进旅館：起点=当前格坐标、终点=住宿旅館坐标（word_496BB2 → corps）
            //   0x10 走出旅館：起点=当前 sprite（旅館坐标）、终点=当前格坐标
            st.stepExitState = static_cast<uint8_t>(pl.alive & 0x30);
            if ((pl.alive & 0x20) != 0) {
                const int ci = pl.stayCorpIdx;
                if (ci > 0 && ci < static_cast<int>(st.corps.size())) {
                    st.stepTargetX = st.corps[ci].x;
                    st.stepTargetY = st.corps[ci].y;
                } else {
                    st.stepTargetX = startX;
                    st.stepTargetY = startY;
                }
            } else {
                startX = pl.spriteX;
                startY = pl.spriteY;
                st.stepTargetX = st.cellEnts[pl.cellEntId].x;
                st.stepTargetY = st.cellEnts[pl.cellEntId].y;
            }
        } else {
            // 收集可走出口（排除来向）。cellEnt id 为 u16，候选必须用 uint16_t（原版存 word 数组）
            uint16_t cand[4];
            int n = 0;
            uint32_t exitBit = 0x40000000u; // [RE 0x40C05C] v6 起始位（bit30 起）
            for (int k = 0; k < 4; ++k) {
                const uint16_t exitId = st.cellEnts[pl.cellEntId].exits[k];
                if (exitId && exitId != pl.prevCellEnt && exitId < st.cellEnts.size() &&
                    (exitBit & st.cellEnts[pl.cellEntId].occMask) == 0) {
                    cand[n++] = exitId;
                }
                exitBit >>= 1;
            }
            uint16_t nextId = n ? cand[dbg::roll(dbg::SlotNpc, n)] : pl.prevCellEnt;
            if (nextId == 0 || nextId >= st.cellEnts.size()) {
                nextId = pl.cellEntId;
            }
            // 更新来向/当前格/占用掩码
            // [RE 0x40C05C] 占位掩码：玩家 = 256<<p；事件槽 4..7 = 4096<<(p-4)；
            //   **槽 8（機器娃娃）不占位**（v65==4 时原版跳过 &= / |=）——否则 bit16 与
            //   物件槽 1 的占用位冲突，娃娃每格都被误判为"有物件"而弹飞/删除 slot 1
            pl.prevCellEnt = pl.cellEntId;
            const uint32_t mask =
                (p < 4) ? (256u << p) : (p < 8 ? (4096u << (p - 4)) : 0u);
            st.cellEnts[pl.cellEntId].occMask &= ~mask;
            st.cellEnts[nextId].occMask |= mask;
            pl.cellEntId = nextId;
            // [RE 0x40C05C] 移动中即朝本步方向（原版每帧设 byte_496B78=sub_407A8C(来向,当前)），
            //   否则角色朝上一步方向走、到新格才转向 → 转角混乱
            pl.dir = static_cast<uint8_t>(facingBetween(st, pl.prevCellEnt, pl.cellEntId));
            updatePlayerCarriedObjects(st, p);
            // [RE 0x40C05C NPC] 同步事件槽记录（原版直接以 word_498E2C/2E 为真源；
            //   重写以 players 为运动真源，每步回写槽记录供存档/恶行/占位判据读取；含槽8）
            if (p >= 4 && p < 9) {
                NpcSlot80& slot = st.npcSlots[p - 4];
                slot.prevCell = pl.prevCellEnt;
                slot.cell = pl.cellEntId;
                slot.dir = pl.dir;
            }

            st.stepTargetX = st.cellEnts[nextId].x;
            st.stepTargetY = st.cellEnts[nextId].y;
        }
        const int dx = st.stepTargetX - startX;
        const int dy = st.stepTargetY - startY;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        double frames;
        if (st.playerMoveGroup[p] || (pl.alive & 0x30)) {
            frames = dist * static_cast<double>(kMoveScale);
        } else {
            frames = dist / static_cast<double>(kMoveSpeed[pl.travel & 3]);
        }
        st.playerStepX[p] = static_cast<float>(pl.spriteX);
        st.playerStepY[p] = static_cast<float>(pl.spriteY);
        st.stepFrames = static_cast<int>(frames);
        st.stepHalf = st.stepFrames >> 1;
        if (st.stepFrames == 0) {
            st.stepFrames = 1;
        }
        const double f = static_cast<double>(st.stepFrames);
        st.stepDeltaX = static_cast<float>(static_cast<double>(dx) / f);
        st.stepDeltaY = static_cast<float>(static_cast<double>(dy) / f);
    }
    // [RE 0x40C05C] 行走动画帧（byte_498EA3）递增，按当前资源每方向帧数回绕。
    //   原版游戏循环帧率低，走路动画约 16fps；按真实时间门控换帧，
    //   避免 16ms tick（~60fps）下动画循环偏快。
    {
        const uint64_t now = nowMs();
        if (now - st.walkAnimLastMs >= 60) {
            st.walkAnimLastMs = now;
            const int slot = 2 * st.playerActionState[p] + st.playerMoveGroup[p];
            int perDir = 8;
            if (slot >= 0 && slot < 13 && st.walkRes[p][slot].frameCount() > 0) {
                perDir = st.walkRes[p][slot].frameCount() / 8;
            }
            if (perDir <= 0) {
                perDir = 1;
            }
            st.playerMoveFrame[p] = (st.playerMoveFrame[p] + 1) % perDir;
        }
    }
    // [RE 0x40C05C] 半程清除状态标志（dword_4749DC < dword_48BAF4）：
    //   走进(0x20) → alive &= 0xF（到达旅館）；走出(0x10) → stateFlags=0（恢复行动）
    if (st.stepExitState != 0 && (st.stepExitState & 0x80) == 0 && st.stepFrames < st.stepHalf) {
        if ((st.stepExitState & 0x10) != 0) {
            pl.stateFlags = 0;
            RICH4_LOGI("stay exit: player %d states cleared at half (RE 0x40C05C)", p);
        } else {
            pl.alive &= 0x0F;
            RICH4_LOGI("stay enter: player %d reached hotel at half (RE 0x40C05C)", p);
        }
        st.stepExitState |= 0x80;
    }
    --st.stepFrames;
    if (st.stepFrames <= 0) {
        pl.spriteX = static_cast<uint16_t>(st.stepTargetX);
        pl.spriteY = static_cast<uint16_t>(st.stepTargetY);
        if ((st.stepExitState & 0x10) != 0) {
            st.stepSkipLanding = true; // 走出旅館完成 → 跳过落地结算（不重复收旅館费）
            RICH4_LOGI("stay exit: player %d back on road (RE 0x40C05C)", p);
        } else if (st.stepExitState == 0) {
            pl.dir = static_cast<uint8_t>(facingBetween(st, pl.prevCellEnt, pl.cellEntId));
        }
        // [RE 0x40C05C NPC] 到达同步槽像素坐标/朝向（原版到达将 g_miscTable80/word_498E2A
        //   写回本步起点坐标；重写以 players 为真源直接同步到期终坐标，供存档/小地图使用；含槽8）
        if (p >= 4 && p < 9) {
            NpcSlot80& slot = st.npcSlots[p - 4];
            slot.pixelX = pl.spriteX;
            slot.pixelY = pl.spriteY;
            slot.dir = pl.dir;
        }
        st.stepExitState = 0;
        return true;
    }
    st.playerStepX[p] += st.stepDeltaX;
    st.playerStepY[p] += st.stepDeltaY;
    pl.spriteX = static_cast<uint16_t>(static_cast<int>(st.playerStepX[p]));
    pl.spriteY = static_cast<uint16_t>(static_cast<int>(st.playerStepY[p]));
    // [RE 0x40C05C NPC] 移动每帧同步槽像素（原版每帧写 g_miscTable80/word_498E2A；供小地图白框；含槽8）
    if (p >= 4 && p < 9) {
        NpcSlot80& slot = st.npcSlots[p - 4];
        slot.pixelX = pl.spriteX;
        slot.pixelY = pl.spriteY;
    }
    return false;
}

// [RE 0x40C912] checkPlayerAction
// 依据: 0x40C912 反编译; a1=1 时返回可行动标志（alive）；
//       a1=0 时有状态效果则 sub_40DD1F（自动移动）或显示状态文本；g_playerState37 → -1
// 差异: 状态文本/语音（sub_44808A/sub_452946/sprintf/sub_440CAC/sub_44EF41）M2 接入
int checkPlayerAction(Application& app, int a1) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    int v1 = 0;
    if (p >= 4) {
        const int s = p - 4;
        // [RE 0x40C912 NPC] 行动状态 498E32(busy=坐牢/入院)/498E34(timerA)/498E36(timerC) 全 0 → 返回 2 可游走；
        //   否则（在押/计时中）返回 0 → 本回合不移动（见 498df0-event-slot-npc.md §2.2）
        if (st.npcSlots[s].busy == 0 && !a1 && st.npcSlots[s].timerA == 0 &&
            st.npcSlots[s].timerC == 0) {
            return 2;
        }
        return 0;
    } else {
        const uint8_t v2 = st.players[p].alive;
        if (v2) {
            if (a1) {
                if ((v2 & 0x30) == 0 && !st.players[p].stateFlags && !st.players[p].byte54) {
                    v1 = v2;
                }
            } else if (st.players[p].stateFlags || st.players[p].byte54) {
                if ((v2 & 0x30) != 0) {
                    startPlayerMove(app);
                } else {
                    // [RE 0x40C912] 状态文本（BIG5 0x4631E0..：%s住宿中/消失中/坐牢中/住院中/
                    //   冬眠中\n\n還剩%d天！；天数 = (编码 & 0x7F/0x3F) + 1）
                    //   坐牢/住院/冬眠另有 50% 台词（列19/20/21，expr2/2/1；台词先于消息）
                    char text[128] = {};
                    const std::string name = playerNameNoSpace(st, p); // [RE 0x40C997 copyNameNoSpaces]
                    const uint32_t sf = st.players[p].stateFlags;
                    const int ci = st.players[p].charIndex;
                    const auto statusLine = [&](int colIdx, int expr) {
                        if (ci >= 0 && ci < 12 && (std::rand() & 1)) {
                            playLine(app, p, kMoneyLines[ci][colIdx], expr);
                        }
                    };
                    if (sf & 0xFFu) {
                        std::snprintf(text, sizeof(text), "%s住宿中\n\n還剩%d天！", name.c_str(),
                                      static_cast<int>(sf & 0x7Fu) + 1);
                    } else if ((sf >> 8) & 0xFFu) {
                        std::snprintf(text, sizeof(text), "%s消失中\n\n還剩%d天！", name.c_str(),
                                      static_cast<int>((sf >> 8) & 0x3Fu) + 1);
                    } else if ((sf >> 16) & 0xFFu) {
                        std::snprintf(text, sizeof(text), "%s坐牢中\n\n還剩%d天！", name.c_str(),
                                      static_cast<int>((sf >> 16) & 0x7Fu) + 1);
                        statusLine(13, 2);  // [RE 0x40CA51] 列19「放我出去」
                    } else if ((sf >> 24) & 0xFFu) {
                        std::snprintf(text, sizeof(text), "%s住院中\n\n還剩%d天！", name.c_str(),
                                      static_cast<int>((sf >> 24) & 0x7Fu) + 1);
                        statusLine(14, 2);  // [RE 0x40CACA] 列20「我不要打針」
                    } else if (st.players[p].byte54) {
                        std::snprintf(text, sizeof(text), "%s冬眠中\n\n還剩%d天！", name.c_str(),
                                      static_cast<int>(st.players[p].byte54 & 0x7Fu) + 1);
                        statusLine(15, 1);  // [RE 0x40CB4C] 列21（playLine 冬眠守卫会拦，同原版）
                    }
                    if (text[0]) {
                        showMessage(app, text, 1500);
                    }
                }
            } else if (st.players[p].state37) {
                startPlayerMove(app);
                return -1;
            } else {
                v1 = v2;
            }
        }
    }
    return v1;
}

// [RE 0x418E7F] calcPlayerWait
// 依据: 0x418E7F 反编译; sub_40C912(1) 非 0 → byte_498EA5 = sub_41982D(当前格)，
//       否则 -125（0x83，bit7 置位 = 下一位玩家）
void calcPlayerWait(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    // [NEW] 走出旅館完成 → 跳过落地结算（原版走出移动由 sub_40DD1F 启动，不再收旅館费）
    if (st.stepSkipLanding) {
        st.stepSkipLanding = false;
        st.playerActionWait[p] = -125;
        RICH4_LOGI("calcPlayerWait: skip landing after stay exit (player %d)", p);
        return;
    }
    if (checkPlayerAction(app, 1)) {
        st.playerActionWait[p] = static_cast<int8_t>(landingEvent(app, st.players[p].cellEntId));
    } else {
        st.playerActionWait[p] = -125;
    }
}

// [RE 0x418C55] beginPlayerTurn
// 依据: 0x418C55 反编译; dword_475114 待处理玩家 → sub_40B93B/sub_40B93B 重置；
//       sub_40C912(0) 返回: 0/1 → 等待玩家操作（sub_4196F1 + setPauseDraw(1)）,
//       2/5 → sub_40DD1F（AI/自动）
// 差异: dword_475114（传送/移动待处理玩家）与 sub_41D546 面板刷新待补
void beginPlayerTurn(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    // sub_40C912(0)
    const int v5 = checkPlayerAction(app, 0);
    switch (v5) {
        case 0:
            // sub_418E7F + byte_46CAFB=1（原版 case 0 分支）
            calcPlayerWait(app);
            st.gameStateActive = true;
            break;
        case 1:
            // 等待玩家操作：sub_4196F1（byte_46CAFD=1 + 前进面板）+ setPauseDraw(1)
            enablePlayerControl(app);
            // AI 玩家自动前进（原版由 AI 决策逻辑触发 sub_40DD1F）
            if (st.players[p].kind == 2) {
                disablePlayerControl(app);
                startPlayerMove(app);
            }
            break;
        case 2:
        case 5: {
            // [RE 0x418C55 case2/5] 事件槽 NPC（>=4）直接移动，跳过 AI 用卡/道具/交易（R2）
            if (p >= 4) {
                startPlayerMove(app);
                break;
            }
            // [RE 0x418D61/0x418D69] turn 入口 drawMiniMap(1)+sub_41D546：原版空闲循环持续
            //   重绘，AI 回合开始视口早已跟随当前玩家；重写 tick 序 update→render，
            //   beginPlayerTurn 内 mapHitRegions 仍是**上一玩家视口**的陈旧记录
            //   （机器工人/传送机/卡片链「远处误选」根因，2026-09-28 实机）→ 先刷新视口
            renderGameFrame(app);
            // [RE 0x418DE6/0x418DF4] AI 股票买/卖（0x42BF03/0x42C79F，内部自判 aiStockPct/
            //   休市/贷款/概率；实现 stock_system.cpp aiStockBuy/aiStockSell）
            aiStockBuy(app, p);
            aiStockSell(app, p);
            // [RE 0x418DFE] 週轉资金准备结算（sub_436B0A(0)，柜员机 one=1 / 回合 one=0）
            bankAdvanceSettle(app, 0);
            if (st.sceneRequest != 0) {  // [RE 0x418E06] 场景切换保护
                break;
            }
            // [RE 0x418E13] 交易市场 AI（sub_4284BE：清挂单 + 随机挂卖/改价/低价买入）
            tradeAiTurn(app);
            // [RE 0x418E18..0x418E21] rand()&1 → 用卡(useCardFlow 0x441BAA)/道具(itemPanelFlow 0x447D97)
            //   两函数内部自判 alive&6 + aiCardItem bit0/1；AI 用卡链见 ai_card.cpp
            if (dbg::roll(dbg::SlotAi, 2) != 0) {
                useCardDialog(app);
            } else {
                itemBagDialog(app);
            }
            // [RE 0x418E31] 状态守卫：出国/绑架/冬眠等（stateFlags+50 ∨ byte54+54 ∨
            //   state37+55 任一非 0）→ 置待处理标记 0x80（下 tick 重新 beginPlayerTurn），
            //   本回合不掷骰不移动
            if (st.players[p].stateFlags != 0 || st.players[p].byte54 != 0 ||
                st.players[p].state37 != 0) {
                st.playerActionFlags[p] |= 0x80;
                break;
            }
            // [RE 0x418E6E/0x418E70] 非移动态才做骰子数动态调整，然后 sub_40DD1F 移动
            if (st.playerActionState[p] != 1) {
                aiDiceAdjustPerTurn(app);
            }
            startPlayerMove(app);  // [RE 0x40DD1F]
            break;
        }
        default:
            break;
    }
    RICH4_LOGI("begin player %d turn: type=%d (RE 0x418C55)", p, v5);
}

// [RE 0x4196F1] enablePlayerControl
// 依据: 0x4196F1 → byte_46CAFD = 1; sub_417191(1)（绘制前进面板）
void enablePlayerControl(Application& app) {
    app.gameState().gamePlayerControl = true;
    // sub_417191(1)：前进面板绘制（A6 接入，当前由 game_panel 每帧绘制）
}

// [RE 0x419703] disablePlayerControl
// 依据: 0x419703 → byte_46CAFD = 0（并跳转 sub_417191 尾部）
void disablePlayerControl(Application& app) {
    app.gameState().gamePlayerControl = false;
}

// [RE 0x44808A] saveTurnSnapshot：回合开始快照（時光機回滚用）
// 依据: 0x44808A 反编译; 仅 currentPlayer<4 且 alive&1 保存；原版写 g_playerMapBlocks
//   （2502 dword/玩家）+ mapDat 副本
void saveTurnSnapshot(GameState& st, int p) {
    if (p < 0 || p >= 4 || (st.players[p].alive & 1) == 0) {
        return;
    }
    // [RE 0x44808A] 全状态写入 10008B mapBlocks buffer + mapDat 副本（save_data.cpp 序列化内核）
    //   与 SAVE%d.DAT 尾段共用同一布局 → 读旧档即可喂时光机
    writeSnapshotBlock(st, p, st.snapshots[p]);
    RICH4_LOGI("saveTurnSnapshot: p%d date=%u (RE 0x44808A)", p, st.gameDate);
}

// [RE 0x448544] restoreTurnSnapshot：時光機全状态回滚（sub_448544）
// 依据: 0x448544 反编译; 恢复全部快照字段 + 全员 loadWalkResources + sub_40C03B/rebuildMiniMap
bool restoreTurnSnapshot(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 4) {
        return false;
    }
    // [RE 0x448544] 反序列化 10008B 快照 buffer 回灌（含 news/fate/mapDat 地块）
    if (!readSnapshotBlock(st, st.snapshots[p])) {
        return false;
    }
    rebuildEventNpcFromSlots(st);  // players[4..7] 镜像由 npcSlots 重建
    // [RE 0x448A53] 全员行走资源重载（j < playerCount 或 j >= 4 时）
    for (int j = 0; j < st.playerCount || j < 4; ++j) {
        if (j < 9) {
            loadWalkResources(st, j);
        }
    }
    buildMiniMapMarks(app);  // [RE 0x40C03B] rebuildMiniMap 等价
    RICH4_LOGI("restoreTurnSnapshot: p%d -> date=%u day=%d (RE 0x448544)", p, st.gameDate,
               st.dayCount);
    return true;
}

// [RE 0x40DD1F] startPlayerMove
// 依据: 0x40DD1F 反编译; 人类(<4): alive&0x30 → dword_48BAF8=1,state=1；
//       byte_496BA0 → state=0,wait=2；byte_496BA1 → dword_48BAF8=1,state=1；
//       否则 state=2（掷骰）。AI(4..7): dword_48BAF8=rand()%9+2,state=1。
//       玩家 8: dword_48BAF8=9,state=1。末尾 byte_498EA3=0, byte_46CAFB=1
void startPlayerMove(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 4) {
        if ((st.players[p].alive & 0x30) != 0) {
            st.remainingSteps = 1;
            st.playerActionState[p] = 1;
        } else {
            saveTurnSnapshot(st, p);  // [RE 0x44808A] 掷骰前回合快照（時光機回滚基准）
            if (st.players[p].skipMove) {
                st.playerActionState[p] = 0;
                st.playerActionWait[p] = 2;
            } else if (st.players[p].fixedStep) {
                st.remainingSteps = 1;
                st.playerActionState[p] = 1;
            } else {
                st.playerActionState[p] = 2;
            }
        }
        if (st.playerActionState[p] == 1) {
            st.moveSoundIndex = st.playerMoveGroup[p] ? 15 : (st.players[p].travel & 3) + 11;
        }
    } else if (p >= 8) {
        st.remainingSteps = 9;
        st.playerActionState[p] = 1;
        st.moveSoundIndex = 9;
        // [RE 0x40DD1F p>=8] `dword_4749D4=9; audioPlayEffect(&g_effectSlots[2*9], 1)`（循环）
        app.audio().playEffectSlotLooping(9);
    } else {
        const int s = p - 4;
        if (st.npcSlots[s].timerC) {
            st.playerActionState[p] = 0;
            st.playerActionWait[p] = -126;
        } else {
            st.remainingSteps = st.npcSlots[s].timerD ? 1 : (dbg::roll(dbg::SlotNpc, 9) + 2);
            st.playerActionState[p] = 1;
        }
        if (st.playerActionState[p]) {
            st.moveSoundIndex = st.playerMoveGroup[p] ? 15 : 11;
        }
    }
    st.playerMoveFrame[p] = 0;
    st.gameStateActive = true;
}

// [RE 0x40D6BE] endPlayerState：状态结束恢复
// 依据: 0x40D6BE 反汇编; alive |= 0x10（状态结束标志，nextPlayer 0x30 分支据此恢复朝向并让该
//       玩家重新开始回合）+ 朝向重算（面向所在格中心 sub_454FB4）+ 恢复格占用（cellEnt+36 bit8+p）
// 差异: 原版不含状态字节清零（清零见 updatePlayerStates 差异说明）
void endPlayerState(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 4) {
        return;
    }
    Player& pl = st.players[p];
    pl.alive |= 0x10;
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        const CellEnt& ce = st.cellEnts[pl.cellEntId];
        pl.dir = static_cast<uint8_t>(
            facingFromDelta(static_cast<int>(ce.x) - static_cast<int>(pl.spriteX),
                            static_cast<int>(ce.y) - static_cast<int>(pl.spriteY)));
        st.cellEnts[pl.cellEntId].occMask |= (256u << p);
    }
}

// [RE 0x44BA63] insurancePayout：保险期内费用由保险公司给付（入狱/住院罚款）
// 依据: 0x44BA63 反编译; g_playerCardCnt(+62 保险期) 非 0 → 找 costType==4 的保险公司 specPt
//       （第一个）→ showMessage("保險期間\n\n得到理賠金\n\n%d元" 0x4658FA, 2000) →
//       sub_41D2C6(保险公司索引+100, player, amount, 1)（收款入现金）
// 差异: 原版循环越界时仍取索引（未防护）；重写找不到返回 0
int32_t insurancePayout(Application& app, int player, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || st.players[player].insuranceDays == 0) {
        return 0;
    }
    int specIdx = 0;
    for (size_t i = 1; i < st.specPts.size(); ++i) {
        if (st.specPts[i].costType == 4) { // kSpecPtCostMap index4 = 保險費（保險公司）
            specIdx = static_cast<int>(i);
            break;
        }
    }
    if (specIdx == 0) {
        return 0;
    }
    char text[96];
    std::snprintf(text, sizeof(text), "保險期間\n\n得到理賠金\n\n%d元", amount); // [RE 0x4658FA]
    showMessage(app, text, 2000);
    transferMoney(app, 100 + specIdx, player, amount, 1); // [RE 0x41D2C6(i+100, a2, a3, 1)]
    RICH4_LOGI("insurance payout: p=%d +%d (specPt %d) (RE 0x44BA63)", player, amount, specIdx);
    return amount;
}

// [RE 0x43D593] jailPlayer：入狱（days 天）
// 依据: 0x43D593 反编译; 已入狱（BYTE2 非 0）→ 天数累加 &0x7F；首次：
//   sub_40D761 清旧状态（監獄/醫院标志 + stateFlags 全清）→ alive &= 0x0F →
//   释放当前格占用 → cellEntId = 監獄格（word_48BAE0）→ sprite = evtCells[2]（綠島）→
//   BYTE2 = days → g_jailFlags=1 → FLC 538 @(0,40)（settings[1] 动画过程）→ 语音（P4）→
//   保险理赔 M×2000×days → byte66 += days（累计）
// 天数语义（2026-09-26 IDA 复核，结果=非 bug）：显示/传入 X 天 = 错过 X 个完整回合
//   （入狱当回合余下时间也计入）；0x41C84F 每回合开始 n→n-1，1→0x80，**再下一次**回合
//   才 release+走出 → 从触发回合数起第 X+1 次回合开始恢复行动。sub_40C912 显示
//   (BYTE2&0x7F)+1（重写 checkPlayerAction 同步）。debug 直调补跳入狱当回合同语义。
// 差异: cellTable/cellNo 位置表（word_496D0A）P5；语音 P4；FLC 背景保存由逐帧重绘等价
// [RE 0x43D7BF / 0x43EE6E] releaseEventNpc：保释事件槽 NPC 上路（填犯人表 + loadWalkResources）
// 依据: releaseJailNpc/releaseHospitalNpc 反编译（a1>=4 段）:
//   byte_498E30 = g_currentPlayer（bailer）；byte_498E32 = 0（busy 自由游走）；
//   word_498E2C = word_48BAE0/48BAE2（監獄/醫院格）、word_498E2E = 0；g_miscTable80/word_498E2A =
//   cellEnt 坐标；byte_498E33 = 1（監獄）/2（醫院）；**起点格类型匹配（監獄格 type4 / 醫院格 type5）
//   恒真 → |=0x80 抓回闩锁**；jailFlags/hospitalFlags = 0；loadWalkResources。
// 见 498df0-event-slot-npc.md §2。
void releaseEventNpc(Application& app, int npc, bool fromJail) {
    GameState& st = app.gameState();
    if (npc < 4 || npc >= 8) {
        return;
    }
    const int i = npc - 4;
    NpcSlot80& slot = st.npcSlots[i];
    Player& pl = st.players[npc];
    const uint16_t cell = fromJail ? st.jailCellEntId : st.hospitalCellEntId;
    slot.bailer = static_cast<uint8_t>(st.currentPlayer);
    slot.busy = 0;   // 498E32=0 自由游走
    slot.cell = cell;
    slot.prevCell = 0;
    slot.timerA = 0;
    slot.timerB = 0;
    slot.timerC = 0;
    slot.timerD = 0;
    // 复用 players[npc] 承载位置（moveOneStep/onPhase/渲染管线）；kind=3 避开人类/AI 分支
    pl.cellEntId = cell;
    pl.prevCellEnt = 0;
    pl.alive = 1;
    pl.kind = 3;
    pl.stateFlags = 0;
    pl.byte54 = 0;
    pl.state37 = 0;
    pl.skipMove = 0;
    pl.fixedStep = 0;
    pl.travel = 0;
    if (cell > 0 && cell < st.cellEnts.size()) {
        const CellEnt& ce = st.cellEnts[cell];
        pl.spriteX = ce.x;
        pl.spriteY = ce.y;
        slot.pixelX = ce.x;
        slot.pixelY = ce.y;
        // [RE 0x43D7E0/0x43EEA0] status = 保释建筑（1=監獄/2=醫院），非按 NPC 固定归属
        slot.status = static_cast<uint8_t>(fromJail ? 1 : 2);
        // 起点格类型与 status 匹配（監獄格 type4 / 醫院格 type5）→ 置抓回闩锁 bit7（原版恒真）
        const uint8_t ct = static_cast<uint8_t>(ce.occMask & 0xFF);
        if (ct == (fromJail ? 4 : 5)) {
            slot.status = static_cast<uint8_t>(slot.status | 0x80);
        }
        st.cellEnts[cell].occMask |= static_cast<uint32_t>(4096u << i);  // NPC 占用位
    }
    if (fromJail) {
        st.jailFlags[npc] = 0;
    } else {
        st.hospitalFlags[npc] = 0;
    }
    loadWalkResources(st, npc);
    trace::logf("npc.release n=%d by=%d", npc, st.currentPlayer);
    RICH4_LOGI("releaseEventNpc: NPC %d released by %d, cell %u status=%u (RE 0x43D7BF/0x43EE6E)",
               npc, st.currentPlayer, cell, slot.status);
}

// [PORT] rebuildEventNpcFromSlots：从 npcSlots（g_miscTable80）重建 players[4..7] 运行时镜像。
//   用于读档：自由（busy==0）且 cell 有效者恢复位置/坐标/朝向 + 地图占用位 + 行走资源；
//   在押/未释放（busy!=0 或 cell==0）者清空镜像（渲染/轮转均以 busy/cell 跳过）。
void rebuildEventNpcFromSlots(GameState& st) {
    for (int i = 0; i < 4; ++i) {
        const NpcSlot80& slot = st.npcSlots[i];
        Player& pl = st.players[i + 4];
        const bool free = (slot.busy == 0 && slot.cell > 0 && slot.cell < st.cellEnts.size());
        pl.cellEntId = free ? slot.cell : 0;
        pl.prevCellEnt = free ? slot.prevCell : 0;
        pl.spriteX = free ? slot.pixelX : 0;
        pl.spriteY = free ? slot.pixelY : 0;
        pl.dir = free ? slot.dir : 0;
        pl.alive = free ? 1 : 0;
        pl.kind = free ? 3 : 0;
        if (free) {
            st.cellEnts[slot.cell].occMask |= static_cast<uint32_t>(4096u << i);
            loadWalkResources(st, i + 4);
        }
    }
    RICH4_LOGI("rebuildEventNpcFromSlots: busy=%d/%d/%d/%d cell=%u/%u/%u/%u", st.npcSlots[0].busy,
               st.npcSlots[1].busy, st.npcSlots[2].busy, st.npcSlots[3].busy, st.npcSlots[0].cell,
               st.npcSlots[1].cell, st.npcSlots[2].cell, st.npcSlots[3].cell);
}

// [RE 0x43D593] jailPlayer：入监（真人）/抓回坐牢（事件槽 NPC）
void jailPlayer(Application& app, int player, int days) {
    GameState& st = app.gameState();
    if (player < 0) {
        return;
    }
    if (player >= 4) {
        // [RE 0x43D593 a2>=4] NPC 抓回：清当前格占用（~(256<<player) = 4096<<i）+ busy=1（監獄）+
        //   status/timerA~D 全清 + jailFlags=1（可再保释）；**不动坐标/格**（停在抓回所在建筑格，
        //   渲染因 busy!=0 自动隐藏 0x40829D）；不清 bailer
        if (player < 8) {
            const int i = player - 4;
            NpcSlot80& slot = st.npcSlots[i];
            const uint16_t cell = slot.cell;
            if (cell > 0 && cell < st.cellEnts.size()) {
                st.cellEnts[cell].occMask &= ~static_cast<uint32_t>(4096u << i);
            }
            slot.busy = 1;
            slot.status = 0;
            slot.timerA = 0;
            slot.timerB = 0;
            slot.timerC = 0;
            slot.timerD = 0;
            st.jailFlags[player] = 1;
            RICH4_LOGI("jailPlayer: NPC %d 抓回坐牢 (RE 0x43D593)", player);
        }
        return;
    }
    Player& pl = st.players[player];
    const uint8_t cur = static_cast<uint8_t>((pl.stateFlags >> 16) & 0xFF);
    if (cur != 0) {
        const int total = ((cur & 0x7F) + days) & 0x7F; // [RE 0x43D6C5]
        pl.stateFlags = (pl.stateFlags & 0xFF00FFFFu) | (static_cast<uint32_t>(total) << 16);
        RICH4_LOGI("jailPlayer: p=%d days 累加 -> %d (RE 0x43D593)", player, total);
        return;
    }
    // [RE 0x40D761] 设置新状态前清旧状态（監獄/醫院标志 + stateFlags 全清；重写另清 cellTableIdx）
    st.jailFlags[player] = 0;
    st.hospitalFlags[player] = 0;
    pl.stateFlags = 0;
    // [RE 0x43D5F9] 入狱天数台词（sub_44F2C2，expr2；>6 天惨 / 4..6 随机 / 1..3 轻描淡写）
    playStatusDaysLine(app, player, days);
    pl.alive &= 0x0F; // [RE 0x43D601]
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask &= ~(256u << player); // [RE 0x43D61D]
    }
    pl.cellEntId = st.jailCellEntId; // [RE 0x43D627]
    pl.prevCellEnt = 0;              // [RE 0x43D630]
    pl.byte27 = 15;                  // [RE 0x43D637] g_playerRestoreDir = 15
    if (st.evtCells.size() > 2) {    // [RE 0x43D63E] evtCells[2] = 綠島（監獄）
        pl.spriteX = static_cast<uint16_t>(st.evtCells[2].x);
        pl.spriteY = static_cast<uint16_t>(st.evtCells[2].y);
    }
    pl.stateFlags = (pl.stateFlags & 0xFF00FFFFu) | (static_cast<uint32_t>(days) << 16); // BYTE2
    updatePlayerCarriedObjects(st, player);  // [RE 0x43D668] 挂身物件随玩家换位
    st.jailFlags[player] = 1;       // [RE 0x43D674]
    if (st.settings[1] != 0) {      // [RE 0x43D67B] byte_497159 动画过程开关
        // [RE 0x450F04] 帧 18（flags 0x120001）切换：押解警车到画面中间时全量重绘 →
        //   角色经棋子守卫（BYTE2 入狱≠0）消失 → 后续帧基于监狱场景
        playEventFlc(app, 538, 0, 40, 94, /*freeze=*/false, /*switchFrame=*/18);
    }
    insurancePayout(app, player, 2000 * days * st.moneyMul); // [RE 0x43D749]
    pl.byte66 = static_cast<uint8_t>(pl.byte66 + days);      // [RE 0x43D755] byte_496BAA 累计
    trace::logf("jail p=%d days=%d", player, days);
    RICH4_LOGI("jailPlayer: p=%d days=%d (RE 0x43D593)", player, days);
}

// [RE 0x43EC3F] hospitalizePlayer：住院（days 天；同 jailPlayer，用 BYTE3/醫院格/evtCells[1]/FLC 524）
void hospitalizePlayer(Application& app, int player, int days) {
    GameState& st = app.gameState();
    if (player < 0) {
        return;
    }
    if (player >= 4) {
        // [RE 0x43EC3F a2>=4] NPC 抓回住院：同 jailPlayer NPC 分支，busy=2 + hospitalFlags
        if (player < 8) {
            const int i = player - 4;
            NpcSlot80& slot = st.npcSlots[i];
            const uint16_t cell = slot.cell;
            if (cell > 0 && cell < st.cellEnts.size()) {
                st.cellEnts[cell].occMask &= ~static_cast<uint32_t>(4096u << i);
            }
            slot.busy = 2;
            slot.status = 0;
            slot.timerA = 0;
            slot.timerB = 0;
            slot.timerC = 0;
            slot.timerD = 0;
            st.hospitalFlags[player] = 1;
            RICH4_LOGI("hospitalizePlayer: NPC %d 抓回住院 (RE 0x43EC3F)", player);
        }
        return;
    }
    Player& pl = st.players[player];
    const uint8_t cur = static_cast<uint8_t>((pl.stateFlags >> 24) & 0xFF);
    if (cur != 0) {
        const int total = ((cur & 0x7F) + days) & 0x7F; // [RE 0x43ED74]
        pl.stateFlags = (pl.stateFlags & 0x00FFFFFFu) | (static_cast<uint32_t>(total) << 24);
        RICH4_LOGI("hospitalizePlayer: p=%d days 累加 -> %d (RE 0x43EC3F)", player, total);
        return;
    }
    st.jailFlags[player] = 0;
    st.hospitalFlags[player] = 0;
    pl.stateFlags = 0;
    // [RE 0x43ECA5] 住院天数台词（sub_44F2C2，expr2）
    playStatusDaysLine(app, player, days);
    pl.alive &= 0x0F; // [RE 0x43ECAD]
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask &= ~(256u << player); // [RE 0x43ECC9]
    }
    pl.cellEntId = st.hospitalCellEntId; // [RE 0x43ECD3]
    pl.prevCellEnt = 0;                  // [RE 0x43ECDC]
    pl.byte27 = 15;                      // [RE 0x43ECE3]
    if (st.evtCells.size() > 1) {        // [RE 0x43ECEA] evtCells[1] = 醫院
        pl.spriteX = static_cast<uint16_t>(st.evtCells[1].x);
        pl.spriteY = static_cast<uint16_t>(st.evtCells[1].y);
    }
    pl.stateFlags = (pl.stateFlags & 0x00FFFFFFu) | (static_cast<uint32_t>(days) << 24); // BYTE3
    updatePlayerCarriedObjects(st, player);  // [RE 0x43ED14] 挂身物件随玩家换位
    st.hospitalFlags[player] = 1;   // [RE 0x43ED20]
    if (st.settings[1] != 0) {      // [RE 0x43ED2E]
        // 原版流程：refreshGameUi(旧位置) → 传送 → 440x74 band 524 画在**旧位置地图**上 →
        //   [RE 0x450F04] 帧 30（flags 0x1E0001）切换：全量重绘 → 角色经 0x4086A1 守卫
        //   （BYTE3 住院≠0）消失（抬进医院）→ 后续帧基于医院场景
        playEventFlc(app, 524, 0, 210, 92, /*freeze=*/false, /*switchFrame=*/30);
    }
    insurancePayout(app, player, 2000 * days * st.moneyMul); // [RE 0x43EDF8]
    pl.byte66 = static_cast<uint8_t>(pl.byte66 + days);      // [RE 0x43EE04]
    trace::logf("hosp p=%d days=%d", player, days);
    RICH4_LOGI("hospitalizePlayer: p=%d days=%d (RE 0x43EC3F)", player, days);
}

// [RE 0x436A5A] checkLoanDue：贷款到期检查（回合开始，每玩家回合一次）
// 依据: 0x436A5A 反编译; v = sub_4521AA(当前日期, loanDate)（= 距到期剩余天数）
//   v==0 强执（showMessage 0x464B2C → sub_433BD8 扣款 → 清 loan/loanDate）；
//   v==1/2 提醒（0x464B43/0x464B5C）；v==3 催收演出 sub_43695E（柜员动画窗，1.4 银行 UI 接入）
// 差异: sub_43695E 演出与 sub_41906A 刷新暂以日志占位
static void checkLoanDue(Application& app, int p) {
    GameState& st = app.gameState();
    Player& pl = st.players[p];
    if (pl.loanDate == 0) {
        return;
    }
    const int32_t v = dateDiff(st.gameDate, pl.loanDate);
    switch (v) {
        case 0:
            showMessage(app, "貸款到期日\n\n強制執行！", 1500); // [RE 0x464B2C]
            payFromBank(app, p, pl.loan);
            pl.loan = 0;
            pl.loanDate = 0;
            RICH4_LOGI("loan due: player %d forced repayment done (RE 0x436A5A)", p);
            break;
        case 1:
            showMessage(app, "距貸款到期日\n\n還剩１天！", 1500); // [RE 0x464B43]
            break;
        case 2:
            showMessage(app, "距貸款到期日\n\n還剩２天！", 1500); // [RE 0x464B5C]
            break;
        case 3:
            // [RE 0x43695E] 催收演出（panel[23] + 柜员动画窗 sub_436034 runModal）
            bankDueDialog(app);
            break;
        default:
            break;
    }
}

// [RE 0x40C78C] turnToAdjacentCell：向后转（朝向反向 + 重选来路格）；轉向卡(6)/魔法屋惩罚 7 复用。
// 差异: 原版音效句柄 dword_4823F2 从未注册（静默）；NPC 分支（a1>=4 犯人表）P5。
void turnToAdjacentCell(GameState& st, int p) {
    Player& pl = st.players[p];
    pl.dir = static_cast<uint8_t>((pl.dir + 4) & 7);
    if (pl.cellEntId >= st.cellEnts.size()) {
        return;
    }
    const CellEnt& ce = st.cellEnts[pl.cellEntId];
    uint16_t cand[4];
    int n = 0;
    uint32_t mask = 0x40000000u;
    for (int i = 0; i < 4; ++i, mask >>= 1) {
        const uint16_t id = ce.exits[i];
        if (id != 0 && (mask & ce.occMask) == 0 && id != pl.prevCellEnt) {
            cand[n++] = id;
        }
    }
    pl.prevCellEnt = n != 0 ? cand[dbg::roll(dbg::SlotNpc, n)] : 0;
}

// [RE 0x41C84F] updatePlayerStates：回合开始状态倒计时（nextPlayer 切换玩家后调用）
// 依据: 0x41C84F 反编译; 0x41C868 refreshBankStockShares / 0x41C86D sub_436A5A 贷款到期（alive 检查前）；
//   BYTE0..BYTE3 编码 = 剩余天数（1..0x7F），bit7 = 结束标志：
//   n>0 → n-1；n==1 → (n-1)|0x80（BYTE1 出國为 (n-1)&0x3F==0 时置位）；bit7 → 结束处理：
//   BYTE0 住宿 → sub_40D6BE / BYTE1 出國 → sub_40D4E5（清位+恢复占用+loadWalkResources）/
//   BYTE2 監獄 → releaseJailNpc / BYTE3 醫院 → releaseHospitalNpc（清 g_jailFlags/g_hospitalFlags）；
//   fixedStep/skipMove/银行停业(+59)/融资(+60) 先清 bit7 再倒计时（0→0x80，下回清 0）；
//   byte54 冬眠 / state37 夢遊 在 stateFlags==0 时倒计时（0→0x80）；
//   保险期 +62：0x41CC4B 递减、0x41CAE3 到期清 0；
//   **a1>=4（事件槽 4..7）不 return**：timerA~D 递减（见函数内 NPC 分支）
// 差异: 原版结束分支未见状态字节清零（实测会永久卡在"住宿中"），重写在结束时清零对应 BYTE；
//       同盟天数 +61（P4）。附身神明寿命递减已接（attachEnd，0x41CC6F）
void updatePlayerStates(Application& app, int p) {
    GameState& st = app.gameState();
    if (p >= 4) {
        // [RE 0x41C84F a1>=4] 事件槽计时器递减：
        //   timerA~D 负值（0x80 到期标记）→ 清 0；timerA 清零时 byte_498EA0&=~0x40 +
        //   releaseWalkResources(a1, 当前动画组) + loadWalkResources（特殊动画组复位）；
        //   正值递减，1→0x80（下回合清 0）。恶人无坐牢/住院天数（updatePlayerStates 只走计时器，
        //   永久在押，唯一放出 = 保释 releaseEventNpc）。见 498df0-event-slot-npc.md §5-1b。
        if (p < 8) {
            const int i = p - 4;
            NpcSlot80& slot = st.npcSlots[i];
            if (slot.timerA < 0) {
                slot.timerA = 0;
                st.playerActionFlags[p] &= static_cast<uint8_t>(~0x40u);
                releaseWalkResources(st, p, st.playerMoveGroup[p]);
                loadWalkResources(st, p);
            }
            if (slot.timerB < 0) {
                slot.timerB = 0;
            }
            if (slot.timerC < 0) {
                slot.timerC = 0;
            }
            if (slot.timerD < 0) {
                slot.timerD = 0;
            }
            auto decayNpc = [](int8_t& v) {
                if (v == 0) {
                    return;
                }
                const int8_t old = v;
                v = static_cast<int8_t>(old - 1);
                if (old == 1) {
                    v = static_cast<int8_t>(0x80); // 到期标记（负值，下回合清 0）
                }
            };
            decayNpc(slot.timerA);
            decayNpc(slot.timerB);
            decayNpc(slot.timerC);
            decayNpc(slot.timerD);
        }
        return;
    }
    if (p < 0) {
        return;
    }
    // [RE 0x41C86D] 贷款到期检查（位于 alive/sceneRequest 检查之前）
    checkLoanDue(app, p);
    Player& pl = st.players[p];
    if (pl.alive == 0 || st.sceneRequest != 0) {
        return;
    }
    // [RE 0x41CA7B..0x41CAC5] 上回合到期置 bit7 的计数项清零（fixedStep/skipMove/银行停业/融资）
    if ((pl.fixedStep & 0x80) != 0) {
        pl.fixedStep = 0;
    }
    if ((pl.skipMove & 0x80) != 0) {
        pl.skipMove = 0;
    }
    if ((pl.bankRefuseDays & 0x80) != 0) {
        pl.bankRefuseDays = 0;
    }
    if ((pl.bankFinanceFlags & 0x80) != 0) {
        pl.bankFinanceFlags = 0;
    }
    auto decay = [](uint8_t v, uint8_t lowMask) -> uint8_t {
        if (v == 0) {
            return 0;
        }
        const uint8_t n = static_cast<uint8_t>(v - 1);
        return ((n & lowMask) == 0) ? static_cast<uint8_t>(n | 0x80) : n;
    };
    // BYTE0 住宿
    const uint8_t b0 = static_cast<uint8_t>(pl.stateFlags & 0xFF);
    if ((b0 & 0x80) != 0) {
        // [RE 0x40D6BE] 结束：alive|=0x10 → 该玩家回合自动"走出旅館"移动（0x40C05C 0x10 分支），
        //   半程清 stateFlags；本函数不清 BYTE0（原版清零在走出移动半程）
        endPlayerState(app, p);
    } else if (b0 != 0) {
        pl.stateFlags = (pl.stateFlags & 0xFFFFFF00u) | decay(b0, 0xFF);
    }
    // BYTE1 出國（低 6 位天数，bit6 = 乘坐飞机标记）
    const uint8_t b1 = static_cast<uint8_t>((pl.stateFlags >> 8) & 0xFF);
    if ((b1 & 0x80) != 0) {
        pl.stateFlags &= 0xFFFF00FFu;
        if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
            st.cellEnts[pl.cellEntId].occMask |= (256u << p); // [RE 0x40D4E5]
        }
    } else if (b1 != 0) {
        pl.stateFlags =
            (pl.stateFlags & 0xFFFF00FFu) | (static_cast<uint32_t>(decay(b1, 0x3F)) << 8);
    }
    // BYTE2 監獄
    const uint8_t b2 = static_cast<uint8_t>((pl.stateFlags >> 16) & 0xFF);
    if ((b2 & 0x80) != 0) {
        // [RE 0x43D7BF] releaseJailNpc：g_jailFlags 清 0 + sub_40D6BE（不清 BYTE2，走出移动半程清）
        st.jailFlags[p] = 0;
        endPlayerState(app, p);
    } else if (b2 != 0) {
        pl.stateFlags =
            (pl.stateFlags & 0xFF00FFFFu) | (static_cast<uint32_t>(decay(b2, 0xFF)) << 16);
    }
    // BYTE3 醫院
    const uint8_t b3 = static_cast<uint8_t>((pl.stateFlags >> 24) & 0xFF);
    if ((b3 & 0x80) != 0) {
        // [RE 0x43EE6E] releaseHospitalNpc 同构
        st.hospitalFlags[p] = 0;
        endPlayerState(app, p);
    } else if (b3 != 0) {
        pl.stateFlags =
            (pl.stateFlags & 0x00FFFFFFu) | (static_cast<uint32_t>(decay(b3, 0xFF)) << 24);
    }
    // byte54 冬眠 / state37 夢遊 / fixedStep（stateFlags 全 0 时才倒计时）
    if (pl.stateFlags == 0) {
        if (pl.byte54 != 0) {
            pl.byte54 = decay(pl.byte54, 0xFF);
        }
        if (pl.state37 != 0) {
            pl.state37 = decay(pl.state37, 0xFF);
        }
        if (pl.fixedStep != 0) {
            pl.fixedStep = decay(pl.fixedStep, 0x7F);
        }
    }
    // [RE 0x41CB6D..0x41CBB5] 无条件递减（stateFlags 非 0 也执行）
    if (pl.skipMove != 0) {
        pl.skipMove = decay(pl.skipMove, 0x7F);
    }
    if (pl.bankRefuseDays != 0) {
        pl.bankRefuseDays = decay(pl.bankRefuseDays, 0xFF);
    }
    if (pl.bankFinanceFlags != 0) {
        pl.bankFinanceFlags = decay(pl.bankFinanceFlags, 0x7F);
    }
    // [RE 0x41CBDC..0x41CC41] 同盟天数 +61：到期(bit7) → clearAllyPair；>0 → 每次双向
    //   addPlayerDebt(-20×M) 并递减（1 → 0x80，下回合解除；原版无条件执行）
    if ((pl.allyActive & 0x80) != 0) {
        clearAllyPair(st, p);
        pl.allyActive = 0;
    }
    if (pl.allyActive != 0) {
        const int ally = static_cast<int>(pl.ally) - 1;
        if (ally >= 0 && ally < 9 && st.players[ally].ally == static_cast<uint8_t>(p + 1)) {
            addPlayerDebt(st, p, ally, -20 * st.moneyMul);
            addPlayerDebt(st, ally, p, -20 * st.moneyMul);
        }
        const uint8_t n = static_cast<uint8_t>(pl.allyActive - 1);
        pl.allyActive = n != 0 ? n : 0x80;
    }
    // 保险期（+62）：0x41CC4B 递减（1 → 0x80），0x41CAE3 到期清 0（不受 stateFlags 限制）
    if (pl.insuranceDays != 0) {
        if ((pl.insuranceDays & 0x80) != 0) {
            pl.insuranceDays = 0;
        } else {
            pl.insuranceDays = decay(pl.insuranceDays, 0x7F);
        }
    }
    // [RE 0x41CC6F] 附身神明寿命递减（cellTable[cellTableIdx-1].life = +4），归零 → 飘走（轮替）
    // 依据: 0x41C84F 尾段 0x41CC6F..0x41CC9B；attachEnd 内部已 releaseCellTableSlot
    if (pl.cellTableIdx != 0) {
        uint8_t& life =
            st.cellTable[static_cast<size_t>(24 * (pl.cellTableIdx - 1)) + 4];
        if (life != 0) {
            --life;
            if (life == 0) {
                attachEnd(app, p);
            }
        }
    }
    // [RE 0x41CCA3..0x41CD8C] 临时载具计时（工程车 travel=31）：每回合 -4（4n+3 编码）；
    //   进入 0..3 区间后按暂存原载具（vehicleRestore +101）恢复——库存仍有该载具道具则
    //   扣回继续骑（骰子数 diceRestore），否则步行；都会 loadWalkResources 重载行走资源
    if ((pl.travel & 3) == 3) {
        const uint8_t oldType = static_cast<uint8_t>(pl.travel & 3);
        pl.travel = static_cast<uint8_t>(pl.travel - 4);
        if ((pl.travel & 0xFC) == 0) {
            const uint8_t orig = static_cast<uint8_t>(pl.vehicleRestore & oldType);
            bool restored = false;
            if (orig == 1 && st.itemStock[15 * p + 4] != 0) {
                --st.itemStock[15 * p + 4];
                pl.travel = 1;
                pl.diceCount = pl.diceRestore;
                restored = true;
            } else if (orig == 2 && st.itemStock[15 * p + 5] != 0) {
                --st.itemStock[15 * p + 5];
                pl.travel = 2;
                pl.diceCount = pl.diceRestore;
                restored = true;
            }
            if (!restored) {
                pl.travel = 0;      // [RE 0x41CD71] 步行
                pl.diceCount = 1;
            }
            loadWalkResources(st, p);  // [RE 0x41CD83]
            RICH4_LOGI("temp vehicle expired: p%d travel=%u dice=%u (RE 0x41C84F)", p, pl.travel,
                       pl.diceCount);
        }
    }
    // [RE 0x41CD8C] 研究所研发倒计时：仅当前玩家的 type4，归零产出道具
    for (Corp& cpr : st.corps) {
        if (cpr.type == 4 && cpr.researchLeft != 0 &&
            cpr.owner == static_cast<uint8_t>(p + 1)) {
            if (cpr.researchItem > cpr.sub) {
                cpr.researchLeft = 0;  // [RE 0x41CE2F] 研究中被拆到 sub<item → 作废
            } else {
                --cpr.researchLeft;
                if (cpr.researchLeft == 0) {
                    renderGameFrame(app);  // [RE 0x41CDCE] refreshGameUi(0,0,1)
                    char labMsg[64];
                    std::snprintf(labMsg, sizeof(labMsg), "%s開發成功！",
                                  kItemBagNames[8 + cpr.researchItem]);
                    showMessage(app, labMsg, 1500);                 // [RE 0x41CDFC]
                    givePlayerItem(st, p, cpr.researchItem + 8);    // [RE 0x41CE25] item id=+8
                    RICH4_LOGI("lab done: p%d researchItem=%u -> %s (RE 0x41C84F)", p,
                               cpr.researchItem, kItemBagNames[8 + cpr.researchItem]);
                }
            }
        }
    }
    RICH4_LOGI("player states: p=%d flags=%08X byte54=%u state37=%u ins=%u bankRefuse=%u bankFin=%u "
               "god=%u (RE 0x41C84F)",
               p, pl.stateFlags, pl.byte54, pl.state37, pl.insuranceDays, pl.bankRefuseDays,
               pl.bankFinanceFlags, pl.cellTableIdx);
}

// [RE 0x41CF67] advanceDay
// 依据: 0x41CF67 反编译; byte_46CB06 音乐淡出；sub_452117 日期推进；++g_dayCount；
//       checkVictory(0) 未结束则每日处理（股票/月度事件/住宅用地/商業用地到期）
// 差异: 音乐淡出（sub_454ACB/sub_454D91）、checkVictory（M3）、
//       sub_428475/sub_42BA97/sub_431712/sub_439BFA 待接入；
//       当前实现日期推进 + 物價指數 + 住宅用地/商業用地到期回收 + 小地图标记刷新
void advanceDay(Application& app) {
    GameState& st = app.gameState();
    const int newMonth = advanceDate(st);
    ++st.dayCount;
    // [RE 0x41CFB9] 胜利判定（时间上限/资产达标）：返回 1 = 游戏结束（sceneRequest 已置），
    //   跳过全部每日处理（原版 checkVictory 在 ++g_dayCount 之后、每日处理之前）
    if (checkVictory(app) == 1) {
        RICH4_LOGI("advance day: game over (checkVictory) scene=%d (RE 0x41CF67/0x41D89E)",
                   st.sceneRequest);
        return;
    }
    // [RE 0x41CFBF] 物價指數每日更新（只升不降；原版位于 checkVictory 之后）
    updateMoneyIndex(app);
    // [RE 0x428475] 交易挂单每日 age++（存活玩家每挂单 +1；advanceDay 0x41CFC4 调用）
    ageTradeOrders(st);

    // 住宅用地/商業用地到期回收（原版 estates+23/+48、corps+28/+52 与 dword_497160 比较）
    bool refreshed = false;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        Estate& es = st.estates[i];
        if ((es.flag & 0xF0) != 0) {
            es.flag = static_cast<uint8_t>(es.flag - 16);
            if ((es.flag & 0xF0) == 0) {
                es.flag = 0;
            }
        }
        if (es.expireDate == st.gameDate) {
            es.owner = 0;
            es.expireDate = 0;
            refreshed = true;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        Corp& cp = st.corps[i];
        if ((cp.flag & 0xF0) != 0) {
            cp.flag = static_cast<uint8_t>(cp.flag - 16);
            if ((cp.flag & 0xF0) == 0) {
                cp.flag = 0;
            }
        }
        if (cp.expireDate == st.gameDate) {
            cp.owner = 0;
            cp.expireDate = 0;
            refreshed = true;
        }
    }
    (void)refreshed;
    // [RE 0x41CF67] 休市倒计时（dword_4990DC）：bit7 未置且有值→--，==1 时置 bit7；bit7 已置→清 0
    if ((st.stockMarketClosed & 0x80) == 0) {
        if (st.stockMarketClosed != 0) {
            const int32_t was = st.stockMarketClosed;
            --st.stockMarketClosed;
            if (was == 1) {
                st.stockMarketClosed |= 0x80;
            }
        }
    } else {
        st.stockMarketClosed = 0;
    }
    // [RE 0x41CF67] 停牌/新闻每日倒计时（byte_496986 递减；byte_496987 高4位、低4位分别递减）
    for (int i = 0; i < 12; ++i) {
        if (st.stockHalted[i] != 0) {
            --st.stockHalted[i];
        }
        if (st.stockNews[i] != 0) {
            if ((st.stockNews[i] & 0xF0) != 0) {
                st.stockNews[i] = static_cast<uint8_t>(st.stockNews[i] - 16);
            }
            if ((st.stockNews[i] & 0x0F) != 0) {
                st.stockNews[i] = static_cast<uint8_t>(st.stockNews[i] - 1);
            }
        }
    }
    // [RE 0x41CF67] stockTick：每日股票行情（12 支随机涨跌 + 历史 + 大盘；休市则跳过）
    stockTick(app);
    // [RE 0x42915A] 每日交易量刷新（保留股份 → 交易量 10%~30%）
    refreshBankStockShares(app);
    // [RE 0x41D07B] 每日节日处理（sub_452444：节日插画 FLC + 节日音乐；见 452444-holiday-system.md）
    holidayDaily(app);
    // [RE 0x41CF67] 15 号：股东大会分红（sub_42BA97）+ 乐透开奖（sub_431712）——顺序：先分红后乐透
    if ((st.gameDate & 0xFF) == 15) {
        trace::logf("dayevt name=dividend");
        dividendMeeting(app);    // [RE 0x41D08F] 15 号分红（panel.mkf[76]）
        trace::logf("dayevt name=lottery");
        lotteryDrawMeeting(app); // [RE 0x41D094] 乐透开奖（依赖 case 9 投注）
    }
    // [RE 0x41CF67] 新月（跨月 1 号）：月初结息（sub_439BFA → sub_437E61）+ 事件格换位 + 月份计数
    if (newMonth) {
        trace::logf("dayevt name=settle");
    monthlySettle(app); // [RE 0x41D09E] 月初结息（panel.mkf[25] + 存款 ×1.1 + 排行演出）
        // [RE 0x41D0A5..0x41D0F1] 禮物(cellTable 类型13)/寶箱(14) 每月 1 号重定位：
        //   先记旧格 → deleteMapObject（槽12/13，≥12 不轮替）→ randomCellEnt(旧格) →
        //   createMapObject（原注释"卡片格/銀行格"为误——那是 cellType 格子属性，非 cellTable 物件）
        for (int slot = 13; slot <= 14; ++slot) {
            const size_t off = static_cast<size_t>(24 * (slot - 1));
            const int oldEnt = st.cellTable[off + 2] | (st.cellTable[off + 3] << 8);
            releaseCellTableSlot(app, slot);
            const int newEnt = randomCellEnt(st, oldEnt);
            if (newEnt > 0) {
                createMapObject(app, slot, newEnt, 0, 0);
            }
        }
        ++st.stockDivPeriod; // [RE 0x499084] 分红期数递增
    }
    // [RE 0x41CF67] 音乐切换计时器（byte_46CB06）：低4位递减到0 → 切下一首可选曲目
    if ((st.musicTimer & 0xF) != 0) {
        --st.musicTimer;
        if ((st.musicTimer & 0xF) == 0) {
            app.audio().playNextMusic();
            st.musicTimer = 0x33; // 重置（原版由事件 sub_452444 设 0x33/0x11）
        }
    }
    RICH4_LOGI("advance day: %d/%d/%d dayCount=%d (RE 0x41CF67)",
               static_cast<int>((st.gameDate >> 16) & 0xFFFF),
               static_cast<int>((st.gameDate >> 8) & 0xFF), static_cast<int>(st.gameDate & 0xFF),
               st.dayCount);
}

// [RE 0x448A7E] landingSpecialMove：移动结束特殊移动（原版 nextPlayer 0x418F78 与
//   updateGameState case 3 / 0x40DA4B 共用）。条件：临时载具计时中（travel&0x83==3）+
//   state37==0 + 未住宿/出國（stateFlags.BYTE0==0）+ 落在他人有建筑住宅/商業用地 →
//   ① 记账 30×M（房主欠工程车主，欠款矩阵）② 保存朝向 → 面向建筑 ③ FLC 526 工程车拆房
//   （第 3 帧降级 → demolishAtObjId 拆一级）④ 恢复朝向（landingEvent 照常收过路费）
static void landingSpecialMove(Application& app, int p) {
    GameState& st = app.gameState();
    Player& pl = st.players[p];
    bool hit = false;
    uint16_t objId = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t wx = 0;
    int32_t wy = 0;
    if (((pl.travel & 0x83) == 3) && pl.state37 == 0 && (pl.stateFlags & 0xFF) == 0 &&
        p < 4 && pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        const CellEnt& ce = st.cellEnts[pl.cellEntId];
        objId = ce.special;
        if (objId > 2000 && objId < 4000) {
            const int i = objId - 2000;
            if (i > 0 && i < static_cast<int>(st.estates.size())) {
                const Estate& es = st.estates[i];
                if (es.owner != 0 && es.owner - 1 != p && es.level != 0) {
                    addPlayerDebt(st, es.owner - 1, p, 30 * st.moneyMul);  // [RE 0x40DB35]
                    dx = es.x - ce.x;
                    dy = es.y - ce.y;
                    wx = es.x;
                    wy = es.y;
                    hit = true;
                }
            }
        } else if (objId > 4000 && objId < 6000) {
            const int i = objId - 4000;
            if (i > 0 && i < static_cast<int>(st.corps.size())) {
                const Corp& cp = st.corps[i];
                if (cp.owner != 0 && cp.owner - 1 != p && cp.sub != 0) {
                    addPlayerDebt(st, cp.owner - 1, p, 30 * st.moneyMul);
                    dx = cp.x - ce.x;
                    dy = cp.y - ce.y;
                    wx = cp.x;
                    wy = cp.y;
                    hit = true;
                }
            }
        }
    }
    if (hit) {
        pl.byte27 = pl.dir;                                         // [RE 0x40DB45]
        pl.dir = static_cast<uint8_t>(facingFromDelta(dx, dy));     // [RE 0x454FB4]
        int sx = 0;
        int sy = 0;
        if (!projectMapPoint(st, wx, wy, sx, sy)) {
            sx = wx;
            sy = wy;
        }
        app.audio().playEffectSlot(22);                             // [RE 0x40DC32 敲打音]
        // 先播工程车"打拳"动画（FLC 526；保帧期间保持未降级画面），播完再降级
        playEventFlc(app, 526, sx - 55, sy - 55, -1, false, 3);     // [RE 0x450CED flags 0xFF0001]
        demolishAtObjId(app, objId, 2);                             // [RE 0x40DC19]
        renderGameFrame(app);  // 降级结果立即刷新
        pl.dir = pl.byte27;                                         // [RE 0x40DCA8]
        RICH4_LOGI("bulldozer demolish: p%d obj %u (RE 0x40DA4B)", p, objId);
    }
}

// [RE 0x418EBD] nextPlayer
// 依据: 0x418EBD 反编译; 当前玩家 +1；越过 g_playerCount 进入事件槽 4；
//       4..7 检查 byte_498DF2 占用；8 → 回 0 并过天；跳过死亡玩家；
//       byte_46CAFF（跳过请求）→ sub_41906A(1) + alive[0]&=~4；
//       v1 时 advanceDay；sub_41C84F 状态效果倒计时；自动存档；末尾 byte_498EA0|=0x80
//       alive&0x30（载具/旅館走出）分支：重绘 → （alive&0x10）恢复朝向 + landAfterMove +
//       sub_448A7E → alive&=0x0F
// 差异: 自动存档（saveGameToSlot）待接入
void nextPlayer(Application& app) {
    GameState& st = app.gameState();
    st.manualView = false;
    int cur = st.currentPlayer;

    // [RE 0x418EBD] 玩家 8（機器娃娃）特殊分支：恢复原玩家（槽8 bailer = byte_498E70）+
    //   槽8 busy（byte_498E72）0→3 + 面板重绘（sub_415E70(1) 重写逐帧重绘等价）→ 该玩家重开回合
    if (cur == 8) {
        NpcSlot80& wslot = st.npcSlots[4];
        st.currentPlayer = wslot.bailer;
        if (wslot.busy == 0) {
            wslot.busy = 3;
        }
        st.playerActionFlags[st.currentPlayer] |= 0x80;
        RICH4_LOGI("worker end: back to player %d (RE 0x418EBD)", st.currentPlayer);
        return;
    }
    // [RE 0x418EFB] alive&0x30 分支（临时载具/旅館走出收尾）：无条件重绘；
    //   alive&0x10（走出）→ 恢复朝向（byte27）→ landAfterMove（落地神明效果 0x418F59）→
    //   landingSpecialMove（sub_448A7E 0x418F78，工程车拆房判定）→ 清 0x30
    if (cur < 4 && (st.players[cur].alive & 0x30) != 0) {
        renderGameFrame(app); // [RE 0x418F16 sub_41906A(1)]
        if ((st.players[cur].alive & 0x10) != 0) {
            const uint8_t v0 = st.players[cur].byte27 & 0xF;
            if (v0 != 15) {
                st.players[cur].dir = v0;
            }
            landAfterMove(app, cur, st.players[cur].cellEntId); // [RE 0x418F59]
            landingSpecialMove(app, cur);                       // [RE 0x418F78]
        }
        st.players[cur].alive &= 0x0F; // [RE 0x418F87]
        st.playerActionFlags[cur] |= 0x80;
        return;
    }

    bool dayPassed = false;
    int guard = 0;
    for (;;) {
        if (++guard > 64) {
            cur = 0;
            break;
        }
        int v2 = cur + 1;
        if (v2 == st.playerCount) {
            // [RE 0x418EBD] 玩家轮完 → 从事件槽 4 起检查（LABEL_19）
            v2 = 4;
            cur = 4;
        } else {
            cur = v2;
        }
        if (v2 >= 4 && v2 < 8) {
            // [RE 0x418EBD LABEL_19] 事件槽 4..7：busy==0（已保释上路）才进回合；
            //   busy!=0（在押）→ +1 继续；全部在押 → 走到 8 回绕过天。
            //   注意 busy 即旧文档误称的"498DF2 槽占用"（同一字节 0x498E32）
            if (st.npcSlots[cur - 4].busy == 0) {
                break; // LABEL_20 → cur>=4 → 选中
            }
            continue;
        }
        if (v2 == 8) {
            // 事件槽走完 → 回绕到 0 并过天
            cur = 0;
            dayPassed = true;
            if (st.players[0].alive == 0 && st.players[0].spriteX != 0) {
                continue; // 玩家 0 死亡且已放置 → 继续找下一活人
            }
            break;
        }
        // v2 < playerCount（0..playerCount-1）：LABEL_20
        if (st.players[cur].alive != 0 || st.players[cur].spriteX == 0) {
            break;
        }
        // 死亡且已放置 → 跳过
    }
    st.currentPlayer = cur;

    // byte_46CAFF 跳过请求
    if (st.gameSkipRequest) {
        st.gameSkipRequest = false;
        st.players[0].alive &= ~0x04;
    }
    if (dayPassed) {
        advanceDay(app);
    }
    updatePlayerStates(app, cur); // [RE 0x41C84F] 回合开始状态倒计时（住宿/出國/監獄/醫院等）
    // [RE 0x41904C] v1(过天) && byte_49715C(自动存档设置) → saveGameToSlot(0)（SAVE0=AUTO）
    if (dayPassed && st.settings[4] != 0) {
        saveGameToSlot(app.gameDir(), 0, st);
        RICH4_LOGI("auto save -> SAVE0.DAT (RE 0x41904C)");
    }
    st.playerActionFlags[cur] |= 0x80;
    RICH4_LOGI("next player: %d day=%d (RE 0x418EBD)", cur, dayPassed ? 1 : 0);
}

// [RE 0x40FAD6] cellFlyPending：46 槽 cellTable +6（g_cellFlyCount）任一非 0 = 弹飞动画未完
//   （原版返回 1=全 0 播完；case0 对 p==8 检查，未完则 wait 重置 1 继续等）
static bool cellFlyPending(const GameState& st) {
    for (int i = 0; i < 46; ++i) {
        if (st.cellTable[static_cast<size_t>(24 * i) + 6] != 0) {
            return true;
        }
    }
    return false;
}

// [RE 0x40D7C4] updateGameState
// 依据: 0x40D7C4 反编译; byte_498EA2 状态机:
//   0 = 收尾倒计时（byte_498EA5）→ sub_418E7F 重算（landingEvent 停留结算）/ 负数则 sub_418EBD
//   1 = 移动中: sub_40C05C 走一格，byte_48BB00 → onPlayerActionPhase，dword_48BAF8 递减
//   2 = 掷骰动画: 结束设 dword_48BAF8 = sub_419572() 并转状态 1
//   3 = 移动结束特殊移动（sub_448A7E 触发）: addPlayerDebt(owner-1, cur, 30*moneyMul) 仅记账
//       + 面向目标 + FLC 526；实际收租在 landingEvent 0x419A67
// 差异: sub_416E6D 小地图刷新（迁移为每 tick 全量重绘）、移动音效、case 3 的 FLC 视觉
void updateGameState(Application& app) {
    GameState& st = app.gameState();
    st.gameFrameTick = false; // byte_46CAFA = 0
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    // [RE 0x40829D/0x418C55] 玩家尚未放置（未跳伞入场）→ 先分配出生格 + 标记待入场：
    //   原版 beginPlayerTurn 内部处理 dword_475114（跳伞），落地才置 g_playerAlive
    //   （0x418D07）；重写必须在 beginPlayerTurn 之前触发，否则未入场 alive=0 会被
    //   checkPlayerAction 判为"无行动"而跳过该玩家回合
    if (p < 4 && st.pendingSpawnPlayer == 0 && st.players[p].spriteX == 0 &&
        st.players[p].spriteY == 0) {
        spawnPlayerAt(st, p);
    }
    // [RE 0x475114] 待入场玩家 → 播放跳伞动画（原版 sub_418C55 阻塞播放）
    if (st.pendingSpawnPlayer != 0) {
        updateParachute(app);
        return;
    }
    // [RE 0x401B9C] WinMain 内层循环：状态 0 且 byte_498EA0 高位 → sub_418C55
    if (st.playerActionState[p] == 0 && (st.playerActionFlags[p] & 0x80)) {
        st.playerActionFlags[p] &= 0x7F;
        beginPlayerTurn(app);
    }
    if (!st.gameStateActive) {
        return;
    }
    switch (st.playerActionState[p]) {
        case 0: {
            if (st.playerActionWait[p] != 0) {
                const int8_t v2 = st.playerActionWait[p];
                if ((v2 & 0x7F) != 0) {
                    st.playerActionWait[p] = static_cast<int8_t>(v2 - 1);
                }
                const int8_t v3 = st.playerActionWait[p];
                if ((v3 & 0x7F) == 0) {
                    if (v3 < 0) {
                        st.gameStateActive = false;
                        st.playerActionWait[p] = 0;
                        nextPlayer(app);
                    } else if (p == 8 && cellFlyPending(st)) {
                        // [RE 0x40D7C4 case0 p==8] 弹飞物件动画未播完（sub_40FAD6()==0）→
                        //   wait 重置 1 继续等，动画播完才落地结算/交还回合
                        st.playerActionWait[p] = 1;
                    } else {
                        calcPlayerWait(app); // 普通玩家 v0=false
                    }
                }
            }
            break;
        }
        case 1: {
            // [RE 0x40D932] 原版顺序 = 先处理上一步到达格 onPlayerActionPhase（byte_48BB00
            //   标志），**之后**再判 g_stepsRemaining==0 收尾——最后一步（stopped=true 的
            //   落地格：神明附身/惡犬/禮物/寶箱/地雷/炸彈均在停留判定内）不能漏。
            //   曾因先行 break 导致所有停留类场景触发失效（2026-09-25 修正）。
            if (st.stepActionPending) {
                st.stepActionPending = false;
                onPlayerActionPhase(app);
            }
            if (st.remainingSteps == 0) {
                // [RE 0x40D7C4 case1] 移动结束：停止载具/移动音效（原版 sub_4542E9）
                if (st.moveSoundIndex > 0) {
                    app.audio().stopEffectSlot(st.moveSoundIndex);
                }
                // [RE 0x40D7C4] 移动结束 → 状态 3（收租/停留动画）
                st.playerActionState[p] = 3;
                st.playerMoveFrame[p] = 0;
                st.showDice = false; // 移动结束隐藏骰子
                break;
            }
            if (st.remainingSteps && moveOneStep(app)) {
                st.stepActionPending = true;
                --st.remainingSteps;
            }
            break;
        }
        case 2: {
            // [RE 0x40D7C4 case2] 掷骰三阶段：0=角色扔骰动画 → 1=骰子滚动FLC → 2=点数停留
            const int group = st.playerMoveGroup[p];
            if (st.dicePhase == 0) {
                // 阶段0：角色扔骰动画（walkRes slot=2*state+group），帧数=每方向帧数
                //   原版 byte_498EA3 递增到 dword_498EC4[组]>>3；末帧=松手，需保持
                const int slot = 2 * 2 + group;
                int perDir = 4;
                if (slot >= 0 && slot < 13 && st.walkRes[p][slot].frameCount() > 0) {
                    perDir = st.walkRes[p][slot].frameCount() / 8;
                }
                if (perDir <= 0) {
                    perDir = 1;
                }
                if (st.playerMoveFrame[p] < perDir - 1) {
                    ++st.playerMoveFrame[p]; // 逐帧播扔骰动作（原版此阶段无音效）
                } else {
                    st.dicePhase = 1; // 停在末帧（松手），不重置回第0帧
                }
                break; // map_render 按 playerMoveFrame 画角色扔骰帧
            }
            if (st.dicePhase == 1) {
                if (!st.diceAnimOpened) {
                    st.diceAnimOpened = true;
                    st.diceLandPlayed = false;
                    // [RE 0x419572] 遥控骰子强制点数时按单骰选择 FLC（a2 非 0 → v2=1）
                    const int n = st.forcedDice != 0
                                      ? 1
                                      : std::max(1, std::min(3, static_cast<int>(st.players[p].diceCount)));
                    auto blob = st.panel.read(static_cast<size_t>(n + 3));
                    st.diceAnimActive = blob && st.diceAnim.open(std::move(*blob));
                    if (st.diceAnimActive) {
                        st.diceAnim.nextFrame();
                    } else {
                        st.remainingSteps = rollDice(app);
                        st.diceHoldStartMs = nowMs();
                        st.dicePhase = 2;
                    }
                }
                if (st.diceAnimActive) {
                    if (!st.diceAnim.nextFrame()) {
                        st.diceAnimActive = false;
                        // [RE 0x41962F] FLC 结束 → 第二次落地音效槽 2（dword_48235A）
                        app.audio().playEffectSlot(2);
                        st.remainingSteps = rollDice(app); // 滚动结束 → 最终点数
                        st.diceHoldStartMs = nowMs();
                        st.dicePhase = 2;
                    } else if (!st.diceLandPlayed && st.diceAnim.frameIndex() == 30) {
                        // [RE 0x451387] FLC 第30帧（落地瞬间）→ 第一次落地音效槽 2
                        //   （sub_450F04 用 dword_48C850=0x1E 触发帧 + dword_48C840=槽2）
                        app.audio().playEffectSlot(2);
                        st.diceLandPlayed = true;
                    }
                    break;
                }
            }
            // 阶段2：点数停留 500ms（sub_45285E），角色仍保持扔骰末帧
            st.showDice = true;
            if (nowMs() - st.diceHoldStartMs >= 500) {
                // [RE 0x40D7C4 case2] 骰子动画结束、转移动前播放：
                //   ① 载具/移动槽 = 特殊组?15 : (载具&3)+11（dword_4749D4）
                //   ② 音效槽 3（dword_482362，当前玩家<4 且 cellNo 非0）
                const int moveSfx = st.playerMoveGroup[p] ? 15 : (st.players[p].travel & 3) + 11;
                app.audio().playEffectSlot(moveSfx);
                if (p < 4 && st.players[p].cellNo != 0) {
                    app.audio().playEffectSlot(3);
                }
                st.showDice = false; // 移动开始，骰子消失（原版转状态1 后不再显示骰子）
                st.playerMoveFrame[p] = 0;
                st.dicePhase = 0;
                st.diceAnimOpened = false;
                st.playerActionState[p] = 1;
            }
            break;
        }
        case 3: {
            // [RE 0x40DA4B ← 0x448A7E] 移动结束特殊移动（逻辑见 landingSpecialMove；
            //   普通步行/機車/汽車 travel=0/1/2 不满足条件 → 直接转状态 0）
            landingSpecialMove(app, p);
            st.playerActionWait[p] = 5;
            st.playerActionState[p] = 0;
            break;
        }
        default:
            st.playerActionState[p] = 0;
            break;
    }
}

// [NEW] 进入游戏循环时初始化回合状态机
// 依据: 0x401B9C 进入游戏后首次循环（byte_46CB01=1, byte_46CAFB 由 sub_418C55 置 1）
void initTurnState(Application& app) {
    GameState& st = app.gameState();
    for (int i = 0; i < 9; ++i) {
        st.playerActionState[i] = 0;
        st.playerActionWait[i] = 0;
        st.playerMoveFrame[i] = 0;
        st.playerActionFlags[i] = 0;
        st.players[i].prevCellEnt = 0;
    }
    // currentPlayer 由调用方预设（新游戏=0 / 读档=存档值，对齐 0x402AC5 末 byte_498EA0[cur]）
    st.remainingSteps = 0;
    st.stepFrames = 0;
    st.stepActionPending = false;
    st.stepDwell = 0;
    st.gamePlayerControl = false;
    st.gameLoopActive = true;
    st.gamePanelReady = true;
    st.gameStateActive = true;
    // [RE 0x402F92] byte_498EA0[52*currentPlayer] |= 0x80（用运行时 currentPlayer，非硬编码 0）
    if (st.currentPlayer >= 0 && st.currentPlayer < 9) {
        st.playerActionFlags[st.currentPlayer] |= 0x80;
    }
    // [RE 0x475284] 前进面板初始位置（原版 .data 初值 180,120）
    st.advancePanelX = 180;
    st.advancePanelY = 120;
    st.advancePanelBlink = 0;
}

} // namespace rich4
