#include "game/app/card_effects.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/auction_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/card_lines.h"
#include "game/app/confirm_dialog.h"
#include "game/app/economy.h"
#include "game/app/item_lines.h"
#include "game/app/event_common.h"
#include "game/app/game_loop.h"
#include "game/app/game_panel.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/stock_market_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/target_select_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"

namespace rich4 {
namespace {

// [RE 0x4751F0/0x4521CB] 地权到期日 = 当前日期 + 期限偏移（年<<16 / 月<<8，月>12 +0xF400 进位）
uint32_t landExpireDate(const GameState& st) {
    if (st.cfgLandPerm < 1 || st.cfgLandPerm > 3) {
        return 0;
    }
    static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
    int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
    if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
        d += 0xF400;
    }
    return static_cast<uint32_t>(d);
}

// 目标选择：人类弹目标对话框；AI/托管读 aiCardSelect 预选（[RE 0x441BAA AI 分支 sub_41E6F2(0)]
//   = dword_48BE58）
// 注: 原 9bc150a 起人类分支误写成自递归（cardPickTarget 调自身）→ 栈溢出；2026-09-27 修
int cardPickTarget(Application& app, int mode) {
    GameState& st = app.gameState();
    if (st.players[st.currentPlayer].alive == 1) {
        return selectTargetDialog(app, mode);  // [RE 0x446AE8/0x445E4D]
    }
    return st.aiCardTarget;
}

// 选股：人类弹行情选股模态；AI 读预选（0-based → 返回 1-based，0=无）
int cardPickStock(Application& app, int mode) {
    GameState& st = app.gameState();
    if (st.players[st.currentPlayer].alive == 1) {
        return stockPickDialog(app, mode);
    }
    return st.aiCardTarget + 1;
}

// 当前玩家所站格 special(objId)（0 = 无/越界）
uint16_t standingObjId(const GameState& st) {
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return 0;
    }
    const Player& pl = st.players[cur];
    if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
        return 0;
    }
    return st.cellEnts[pl.cellEntId].special;
}

}  // namespace

// ===== id1 均富卡 [RE 0x4420D8] =====
int cardEffectEqualRich(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    cardBagRemove(st, cur, 1);  // [RE 0x4420E5 sub_441343(cur,1)]
    playCardLine(app, cur, 0);  // [RE 使用者台词 均富]
    int64_t sum = 0;
    int n = 0;
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (st.players[i].alive != 0) {
            sum += st.players[i].cash;
            ++n;
        }
    }
    if (n <= 0) {
        return 1;
    }
    const int32_t avg = static_cast<int32_t>(sum / n);  // [RE 0x4420FB v1/v2]
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (st.players[i].alive == 0) {
            continue;
        }
        if (st.players[i].cash > avg) {
            // [RE 0x442145 addPlayerDebt(i, cur, (cash-avg)/100)]：被削者债权、使用者欠
            addPlayerDebt(st, i, cur, (st.players[i].cash - avg) / 100);
        }
        st.players[i].cash = avg;  // [RE 0x44215C]
    }
    refreshPlayerPanelFor(app, cur);  // [RE 0x44216E sub_41D433(cur)]
    RICH4_LOGI("cardEqualRich: p%d alive=%d avg=%d (RE 0x4420D8)", cur, n, avg);
    return 1;
}

// ===== id2 均貧卡 [RE 0x4421B4] =====
int cardEffectEqualPoor(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x4421C9 sub_446AE8(0x0E0C0410)] 选玩家（flags 0x10 = 玩家位掩码）
    const int v = cardPickTarget(app, 0x0E0C0410);
    if (v == 0) {
        return 0;  // 取消 → 不消耗
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x4421ED sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 2);  // [RE 0x4421E6 sub_441343(cur,2)]
    playCardLine(app, cur, 1);  // [RE 使用者台词 均貧]
    playCardLine(app, t, kSpeechEqualPoorTarget);  // [RE 0x4422xx 目标台词]
    const int32_t avg = (st.players[t].cash + st.players[cur].cash) / 2;  // [RE 0x442200]
    if (st.players[t].cash > avg) {
        // [RE 0x44223F addPlayerDebt(t, cur, (cash[t]-avg)/100)]
        addPlayerDebt(st, t, cur, (st.players[t].cash - avg) / 100);
    }
    st.players[cur].cash = avg;  // [RE 0x442253]
    st.players[t].cash = avg;
    refreshPlayerPanelFor(app, cur);  // [RE 0x442258 sub_41D433]
    RICH4_LOGI("cardEqualPoor: p%d -> p%d avg=%d (RE 0x4421B4)", cur, t, avg);
    return 1;
}

// ===== id3 購地卡 [RE 0x442325]（脚下格强制收购）=====
int cardEffectBuyLand(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    int seller = -1;
    int32_t price = 0;
    int32_t compensate = 0;
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        Estate& es = st.estates[i];
        if (es.owner == 0 || es.owner == cur + 1) {  // [RE 0x442380/0x442393]
            return 0;
        }
        price = static_cast<int32_t>((es.level * es.priceBase + es.priceAdd) * st.moneyMul);
        if (price > st.players[cur].cash) {  // [RE 0x4423B5]
            showMessage(app, "您的現金不足！", 1500);  // [RE 0x4425F6 byte_46530C]
            return 0;                                  // 不消耗
        }
        seller = es.owner - 1;
        // [RE 0x4423C4] M × priceAdd × (level+2)/5（flt_46531C=2.0 / flt_465320=5.0）
        compensate = (st.moneyMul * es.priceAdd) * (es.level + 2) / 5;
        addPlayerDebt(st, seller, cur, compensate);  // [RE 0x4423FB]
        es.owner = static_cast<uint8_t>(cur + 1);     // [RE 0x44243E]
        if (uint32_t e = landExpireDate(st)) {        // [RE 0x44244B]
            es.expireDate = e;
        }
        buildMiniMapMarks(app);                      // [RE 0x442443 rebuildMiniMap(0)]
    } else if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[i];
        if (cp.owner == 0 || cp.owner == cur + 1) {
            return 0;
        }
        price = static_cast<int32_t>((cp.sub * cp.feeTable[0] + cp.buildPrice) * st.moneyMul);
        if (price > st.players[cur].cash) {
            showMessage(app, "您的現金不足！", 1500);
            return 0;
        }
        seller = cp.owner - 1;
        compensate = (st.moneyMul * cp.buildPrice) * (cp.sub + 2) / 5;  // [RE 0x44253D]
        addPlayerDebt(st, seller, cur, compensate);                      // [RE 0x442574]
        cp.owner = static_cast<uint8_t>(cur + 1);                        // [RE 0x4425B7]
        if (uint32_t e = landExpireDate(st)) {                           // [RE 0x4425E9]
            cp.expireDate = e;
        }
        buildMiniMapMarks(app);
    } else {
        return 0;
    }
    transferMoney(app, cur, seller, price, 0);  // [RE 0x442473] 使用者付、原主收（入银行）
    playCardLine(app, cur, 2);                  // [RE 0x44242F 使用者台词 購地]
    playCardLine(app, seller, kSpeechBuyLandSeller);  // [RE 0x4424A7 原主台词]
    cardBagRemove(st, cur, 3);                  // [RE 0x442610 sub_441343(cur,3)]
    RICH4_LOGI("cardBuyLand: objId=%u seller=%d price=%d comp=%d (RE 0x442325)", objId, seller,
               price, compensate);
    return 1;
}

// ===== id6 轉向卡 [RE 0x442F4D] =====
int cardEffectTurn(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x442F61 sub_446AE8(0x0E0C0010)] 选玩家
    const int v = cardPickTarget(app, 0x0E0C0010);
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x442FCA sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 6);   // [RE 0x442F8A sub_441343(cur,6)]
    playCardLine(app, cur, 5);   // [RE 使用者台词 轉向]
    playCardLine(app, t, (t == cur) ? kSpeechTurnSelf : kSpeechTurnTarget);  // [RE 对方/自己]
    turnToAdjacentCell(st, t);   // [RE 0x443025 → 0x40C78C] 朝向反向 + 重选来路格
    RICH4_LOGI("cardTurn: p%d -> p%d dir=%u prev=%u (RE 0x442F4D)", cur, t, st.players[t].dir,
               st.players[t].prevCellEnt);
    return 1;
}

// ===== id8 拍賣卡 [RE 0x443225]（脚下地强制拍卖）=====
int cardEffectAuction(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        Estate& es = st.estates[i];
        if (es.owner != 0) {
            // [RE 0x442362] 补偿原主 M × priceAdd × (level+2)/5
            addPlayerDebt(st, es.owner - 1, cur,
                          (st.moneyMul * es.priceAdd) * (es.level + 2) / 5);
            playCardLine(app, es.owner - 1, kSpeechAuctionOwner);  // [RE 0x4423AE 原主台词]
        }
        if (!runAuction(app, cur, objId, true)) {  // [RE 0x4423F3] 流标
            es.owner = 0;                          // [RE 0x4423FA] 归公
            es.expireDate = 0;
            buildMiniMapMarks(app);
        }
    } else if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[i];
        if (cp.owner != 0) {
            addPlayerDebt(st, cp.owner - 1, cur,
                          (st.moneyMul * cp.buildPrice) * (cp.sub + 2) / 5);
            playCardLine(app, cp.owner - 1, kSpeechAuctionOwner);  // [RE 原主台词]
        }
        if (!runAuction(app, cur, objId, true)) {
            cp.owner = 0;
            cp.expireDate = 0;
            buildMiniMapMarks(app);
        }
    } else {
        return 0;  // [RE LABEL_21] 脚下非地块 → 不消耗
    }
    playCardLine(app, cur, 7);  // [RE 0x4423xx 使用者台词 拍賣]
    cardBagRemove(st, cur, 8);  // [RE 0x442340/0x442616 sub_441343(cur,8)]
    RICH4_LOGI("cardAuction: objId=%u seller=%d (RE 0x443225)", objId, cur);
    return 1;
}

// ===== id11 怪獸卡 [RE 0x443917]（选地彻底夷平）=====
int cardEffectMonster(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x44392C sub_446AE8(0x0E0C0506)] 选地块（estate|corp）
    const int v = cardPickTarget(app, 0x0E0C0506);
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 11);  // [RE 0x443949 sub_441343(cur,11)]
    playCardLine(app, cur, 10);  // [RE 使用者台词 怪獸]
    uint8_t owner = 0;
    if (v >= 4000) {
        const int i = v - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        owner = st.corps[i].owner;
        if (owner != 0) {  // [RE 0x44398A]
            addPlayerDebt(st, owner - 1, cur, st.moneyMul * 30 * st.corps[i].sub);
        }
    } else {
        const int i = v - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        owner = st.estates[i].owner;
        if (owner != 0) {  // [RE 0x4439B0]
            addPlayerDebt(st, owner - 1, cur, st.moneyMul * 30 * st.estates[i].level);
        }
    }
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);  // [RE 0x40AF12]
    focusView(app, x, y);            // [RE 0x41D476 refreshGameUi(x,y,0)]（视口对准目标）
    demolishAtObjId(app, v, 2);      // [RE 0x4439F5/0x443A81] mode2 = 拆建筑留地
    // [RE 0x443AAF] FLC 557 落点 (0,40)、flags 0x100001（BYTE2=0x10 → switchFrame=16）、音效 80
    // （原重写误把目标世界坐标当 FLC 落点且未传 switchFrame）
    playEventFlc(app, 557, 0, 40, 80, true, 16);
    eventAudioWait(app, 500);  // [RE 0x443AC5] sub_45285E(0x1F4) 停留 500ms（末帧保留）
    if (owner != 0) {
        playCardLine(app, owner - 1, kSpeechMonsterOwner);  // [RE 0x443AF3] 原主台词（FLC 后）
    }
    st.eventFlcActive = false;  // 释放 FLC 末帧
    st.manualView = false;      // [RE 0x41D546]
    renderGameFrame(app);
    RICH4_LOGI("cardMonster: objId=%d owner=%u (RE 0x443917)", v, owner);
    return 1;
}

// ===== id12 拆除卡 [RE 0x443B0F]（降一级 / 拆连锁 / 拆路面道具）=====
int cardEffectDemolish(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x443B28 sub_446AE8(0x0E0C0626)] 选目标：他人有建筑地块 / 路面道具（BYTE1=6
    //   case6：路障16/地雷17/炸彈18；原重写误记 0x0526=case5 导致路面道具过滤缺失）
    const int v = cardPickTarget(app, 0x0E0C0626);
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 12);  // [RE 0x443B47 sub_441343(cur,12)]
    playCardLine(app, cur, 11);  // [RE 使用者台词 拆除]
    // [RE 0x443D27] 路面道具：原版仅判 `(v8 & 0x8000) != 0`——编码 0x8000|槽<<8，
    //   槽 1 = 0x8100 > 2000。原重写误加 `v < 2000` 导致永远不匹配（拆不掉路面道具）
    if ((v & 0x8000) != 0) {
        const int slot = (v & 0x7F00) >> 8;  // [RE 0x443D36]
        releaseCellTableSlot(app, slot);      // [RE 0x443BC5 deleteMapObject]
        renderGameFrame(app);                 // [RE 0x443BD0 refreshGameUi(0,0,1)]
        RICH4_LOGI("cardDemolish: road item slot=%d (RE 0x443B0F)", slot);
        return 1;
    }
    uint8_t owner = 0;
    if (v > 2000 && v < 4000) {
        const int i = v - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        Estate& es = st.estates[i];
        owner = es.owner;
        if (es.level > 0) {
            --es.level;  // [RE 0x443CEA]
        }
        if (es.type != 0) {  // [RE 0x443CF3] 连锁店 → 拆平
            es.level = 0;
            es.type = 0;
        }
    } else if (v > 4000 && v < 6000) {
        const int i = v - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[i];
        owner = cp.owner;
        if (cp.sub > 0) {
            --cp.sub;  // [RE 0x443C40]
        }
        if (cp.sub == 0) {
            cp.type = 0;                // [RE 0x443C4B]
            forceHotelCheckout(st);     // [RE 0x443C5B]
        }
    } else {
        return 0;
    }
    if (owner != 0) {  // [RE 0x443D02/0x443D20] 有主 → 记债 30×M
        addPlayerDebt(st, owner - 1, cur, 30 * st.moneyMul);
        // 原主台词在 FLC 之后（[RE 0x443E28]），见下方
    }
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);
    focusView(app, x, y);  // [RE 0x41D476 refreshGameUi(x,y,0)]（视口对准目标）
    // [RE 0x443DDC] FLC 529 落点 (0,40)、flags 0x260001（BYTE2=0x26 → switchFrame=38）、音效 97
    // （原重写误把目标世界坐标当 FLC 落点且未传 switchFrame）
    playEventFlc(app, 529, 0, 40, 97, true, 38);
    eventAudioWait(app, 500);  // [RE 0x443DF2] sub_45285E(0x1F4) 停留 500ms（末帧保留）
    if (owner != 0) {
        playCardLine(app, owner - 1, kSpeechDemolishOwner);  // [RE 0x443E28] 原主台词
    }
    st.eventFlcActive = false;  // 释放 FLC 末帧
    st.manualView = false;      // [RE 0x41D546]
    renderGameFrame(app);
    RICH4_LOGI("cardDemolish: objId=%d owner=%u (RE 0x443B0F)", v, owner);
    return 1;
}

// ===== id13 搶奪卡 [RE 0x443E3D]（夺卡/道具）=====
int cardEffectSteal(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x443E58 sub_446AE8(0x0E0C0410)] 选玩家
    const int v = cardPickTarget(app, 0x0E0C0410);
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x443E6E sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    // [RE 0x443E9B selectCardOrItemDialog(t)]：重写约定 = 仅返回 id，转移在调用方（同命运生日）
    //   AI 分支 [RE sub_41E6F2(1) = dword_48BE5C]：aiCardSelect 已预选目标卡
    int sel;
    if (st.players[cur].alive == 1) {
        sel = selectCardOrItemFromPlayerDialog(app, t);
    } else {
        sel = st.aiCardTarget2;
    }
    if (sel == 0) {
        return v;  // 原版取消仍返回 v1（非 0=结束、不消耗抢夺卡）——照抄
    }
    int32_t price = 0;
    if ((sel & 0x8000) != 0) {
        const int itemId = sel & 0x7FFF;
        takePlayerItem(st, t, itemId);   // [RE 0x445AA2]
        givePlayerItem(st, cur, itemId); // [RE 0x445A4D]
        // [RE 0x443EA6 byte_47FDEF[8*v4]]：原版对道具编码越界读卡价（bug）→ 用道具价表修正
        price = (itemId >= 1 && itemId <= 13) ? kItemPrice[itemId - 1] : 0;
    } else {
        cardBagRemove(st, t, sel);        // [RE 0x441343]
        giveCardToBag(st, cur, sel);      // [RE 0x4412E4]
        price = (sel >= 0 && sel < 31) ? kCardPrices[sel] : 0;  // [RE byte_47FDEF[8*sel]]
    }
    addPlayerDebt(st, t, cur, price);  // [RE 0x443EA6] 被夺者债权、使用者欠
    playCardLine(app, cur, 12);        // [RE 使用者台词 搶奪]
    playCardLine(app, t, kSpeechStealTarget);  // [RE 0x443ECx 目标台词]
    cardBagRemove(st, cur, 13);        // [RE 0x443EB9 sub_441343(cur,13)]
    RICH4_LOGI("cardSteal: p%d -> p%d sel=%d price=%d (RE 0x443E3D)", cur, t, sel, price);
    return 1;
}

// ===== 档B：整路段类（天使/惡魔/漲價/查封）=====

// 收集「与 objId 所指地块同路段（同名）」的 estate 索引到 out；返回目标自身索引（-1 无效）
int collectSameRouteEstates(const GameState& st, int objId, std::vector<int>& out) {
    const int ti = objId - 2000;
    if (ti <= 0 || ti >= static_cast<int>(st.estates.size())) {
        return -1;
    }
    for (int j = 1; j < static_cast<int>(st.estates.size()); ++j) {
        if (std::strcmp(st.estates[j].name, st.estates[ti].name) == 0) {
            out.push_back(j);
        }
    }
    return ti;
}

// ===== id9 天使卡 [RE 0x4434C0]（整路段加盖）=====
int cardEffectAngel(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0006);  // [RE 0x4434E1 sub_446AE8]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 9);  // [RE 0x4434FE sub_441343(cur,9)]
    playCardLine(app, cur, 8);  // [RE 0x443519 使用者台词 天使]
    bool capped = false;
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    if (v > 2000 && v < 4000) {
        std::vector<int> route;
        if (collectSameRouteEstates(st, v, route) < 0) {
            return 0;
        }
        for (int j : route) {
            Estate& es = st.estates[j];
            if (es.level >= 5) {  // [RE 0x443539]
                continue;
            }
            st.highlightEstates.push_back(j);  // [RE 0x44355A markPickBuffer]
            if (es.type != 0) {
                if (es.level == 0) {
                    es.level = 1;  // [RE 0x44357B] 连锁店 0→1
                }
            } else {
                ++es.level;                        // [RE 0x44358B]
                if (es.level == 5) {
                    capped = true;                 // [RE 0x4435A8]
                }
            }
        }
    } else if (v > 4000 && v < 6000) {
        const int ci = v - 4000;
        if (ci <= 0 || ci >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        st.highlightCorps.push_back(ci);  // [RE 0x4434FD markPickBuffer]
        if (angelUpgrade(app, static_cast<uint16_t>(v)) < 0) {
            capped = true;  // [RE 0x443614] bit7 = 封顶
        }
    } else {
        return 0;
    }
    st.highlightFrame = (st.highlightEstates.empty() && st.highlightCorps.empty()) ? -1 : 0;
    playHighlightBlink(app, false);  // [RE 0x4435B8/0x443624 highlightBlink]（修改前画面）
    // [RE 0x4436C6] refreshGameUi(0,0,1)：全量重绘 → 显示升级结果（原重写 FLC 与重绘顺序颠倒）
    renderGameFrame(app);
    if (capped) {
        playEventFlc(app, 523, 0, 40, 90, false, 0);  // [RE LABEL_27 playGodBuildFlc]
    }
    RICH4_LOGI("cardAngel: objId=%d capped=%d (RE 0x4434C0)", v, capped ? 1 : 0);
    return 1;
}

// ===== id10 惡魔卡 [RE 0x4436E0]（整路段夷平）=====
int cardEffectDevil(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0006);  // [RE 0x4436FF]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 10);  // [RE 0x44371C sub_441343(cur,10)]
    playCardLine(app, cur, 9);   // [RE 使用者台词 惡魔]
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    if (v > 2000 && v < 4000) {
        std::vector<int> route;
        if (collectSameRouteEstates(st, v, route) < 0) {
            return 0;
        }
        for (int j : route) {
            Estate& es = st.estates[j];
            if (es.owner != 0) {
                addPlayerDebt(st, es.owner - 1, cur,
                              st.moneyMul * 30 * es.level);  // [RE 0x44379A]
            }
            st.highlightEstates.push_back(j);  // [RE 0x4437C2 markPickBuffer]
            es.level = 0;                      // [RE 0x4437CB]
            es.type = 0;
        }
    } else if (v > 4000 && v < 6000) {
        const int ci = v - 4000;
        if (ci <= 0 || ci >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[ci];
        if (cp.owner != 0) {
            addPlayerDebt(st, cp.owner - 1, cur, st.moneyMul * 30 * cp.sub);
        }
        st.highlightCorps.push_back(ci);  // [RE 0x4438C4 markPickBuffer]
        cp.sub = 0;
        cp.type = 0;
        forceHotelCheckout(st);  // [RE 0x4438D5]
    } else {
        return 0;
    }
    st.highlightFrame = (st.highlightEstates.empty() && st.highlightCorps.empty()) ? -1 : 0;
    playHighlightBlink(app, false);  // [RE 0x4438E0/0x4438F6 highlightBlink]（修改前画面）
    renderGameFrame(app);            // [RE refreshGameUi]（显示拆平结果）
    RICH4_LOGI("cardDevil: objId=%d (RE 0x4436E0)", v);
    return 1;
}

// ===== id14 停留卡 [RE 0x443F80] =====
int cardEffectStay(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0010);  // [RE 0x443FA5]
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x443FBF sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 14);  // [RE 0x443FC9 sub_441343(cur,14)]
    playCardLine(app, cur, 13);  // [RE 使用者台词 停留]
    playCardLine(app, t, (t == cur) ? kSpeechStaySelf : kSpeechStayTarget);  // [RE 对方/自己]
    if (t == cur) {
        st.players[t].skipMove = 0x80;  // [RE 0x443FFF] 自己（下回合清）
    } else {
        st.players[t].skipMove = 1;     // [RE 0x44400x] 他人：下次前进停留
    }
    RICH4_LOGI("cardStay: p%d -> p%d skipMove=%u (RE 0x443F80)", cur, t, st.players[t].skipMove);
    return 1;
}

// ===== id15 冬眠卡 [RE 0x4440EA]（全体对手冬眠 5 天 + 各记债 150×M）=====
int cardEffectHibernate(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    cardBagRemove(st, cur, 15);  // [RE 0x4440F1 sub_441343(cur,15)]
    playCardLine(app, cur, 14);  // [RE 使用者台词 冬眠]
    const int n = st.playerCount < 4 ? st.playerCount : 4;
    for (int i = 0; i < n; ++i) {
        Player& q = st.players[i];
        // [RE 0x44412B] 非自己 / 存活 / 已放置 / 无状态效果 才命中
        if (i == cur || q.alive == 0 || q.spriteX == 0 || q.stateFlags != 0) {
            continue;
        }
        addPlayerDebt(st, i, cur, 150 * st.moneyMul);  // [RE 0x44415F]
        q.state37 = 0;                                  // [RE 0x44416B] 清梦游
        q.byte54 = 5;                                   // [RE 0x444172]
        q.byte66 = static_cast<uint8_t>(q.byte66 + 5);  // [RE 0x44417A byte_496BAA]
    }
    // [RE 0x4440EA v0>=4 段] 事件槽 NPC（4..7）：`!byte_498DF2`（即 busy==0 自由游走；
    //   原版 DF2 与 498E32 同一字节）→ byte_498DF5(timerB)=0、byte_498DF4(timerA)=5
    //   （冬眠 5 回合，由 updatePlayerStates NPC 分支递减；在押 NPC 跳过）
    for (int i = 0; i < 4; ++i) {
        if (st.npcSlots[i].busy == 0) {
            st.npcSlots[i].timerB = 0;
            st.npcSlots[i].timerA = 5;
            RICH4_LOGI("cardHibernate: NPC %d timerA=5 (RE 0x4440EA)", i + 4);
        }
    }
    renderGameFrame(app);  // [RE refreshGameUi(0,0,1)]
    RICH4_LOGI("cardHibernate: p%d affected others (RE 0x4440EA)", cur);
    return 1;
}

// 清载具并暂存（梦游/冬眠共用；[RE 0x4442BC 段]）
void stripVehicle(GameState& st, int p) {
    Player& q = st.players[p];
    q.vehicleRestore = q.travel;    // [RE byte_496BCE]
    q.diceRestore = q.diceCount;    // [RE byte_496BCF]
    const uint8_t veh = static_cast<uint8_t>(q.travel & 3);
    if (veh != 0) {
        if (veh == 1) {
            ++st.itemStock[15 * p + 4];  // [RE byte_499160]
        }
        if (veh == 2) {
            ++st.itemStock[15 * p + 5];  // [RE byte_499161]
        }
        q.travel = 0;      // [RE g_playerVehicle=0]
        q.diceCount = 1;   // [RE byte_496B7A=1]
    }
    loadWalkResources(st, p);
}

// ===== id16 夢遊卡 [RE 0x4441DC] =====
int cardEffectSleepwalk(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0710);  // [RE 0x444201]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 16);  // [RE 0x44421E sub_441343(cur,16)]
    playCardLine(app, cur, 15);  // [RE 使用者台词 夢遊]
    const int orig = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x44423x]
    if (orig < 0 || orig >= 4) {
        return 0;
    }
    if (st.players[orig].byte54 != 0) {  // [RE 0x44423F] 已冬眠 → 不叠加
        return 1;
    }
    addPlayerDebt(st, orig, cur, 150 * st.moneyMul);  // [RE 0x44426C]
    // [RE 0x44427F/0x444293] 免罪(21) → 抵消；否则嫁祸(19) → 改目标（= resolvePenaltyTarget 链）
    const int r = resolvePenaltyTarget(app, orig);
    if (r == -1) {
        renderGameFrame(app);
        return 1;
    }
    const int t = r;
    // [RE 0x4443xx off_48089E] 受害者"睡觉"台词（价值表列 21）
    {
        const int ci = st.players[t].charIndex;
        if (ci >= 0 && ci < 12 && kCardVictimLines[ci][0] != '\0') {
            playLine(app, t, kCardVictimLines[ci]);
        }
    }
    st.players[t].state37 = static_cast<uint8_t>((t != cur) ? 5 : 4);  // [RE 0x4442F4]
    st.players[t].byte58 = static_cast<uint8_t>(st.players[t].byte58 + 5);  // [RE byte_496BAA? +58]
    stripVehicle(st, t);  // [RE 0x4442BC 段]
    // [RE 0x4443xx] 原目标未被转嫁且持復仇卡(18) → 復仇：拖使用者下水 5 天
    if (t == orig && cardBagHas(st, orig, 18)) {
        triggerRevengeCard(app, orig);
        st.players[cur].state37 = 5;
        stripVehicle(st, cur);
    }
    renderGameFrame(app);  // [RE sub_41D546]
    RICH4_LOGI("cardSleepwalk: p%d -> p%d state37=%u (RE 0x4441DC)", cur, t,
               st.players[t].state37);
    return 1;
}

// ===== id17 陷害卡 [RE 0x4444BF] =====
int cardEffectFrame(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0710);  // [RE 0x4444E4]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 17);  // [RE 0x444501 sub_441343(cur,17)]
    playCardLine(app, cur, 16);  // [RE 使用者台词 陷害]
    const int orig = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x44450x]
    if (orig < 0 || orig >= 4) {
        return 0;
    }
    addPlayerDebt(st, orig, cur, 150 * st.moneyMul);  // [RE 0x444539]
    // [RE 0x44454C/0x444560] 免罪(21) → 抵消；嫁祸(19) → 改目标
    const int r = resolvePenaltyTarget(app, orig);
    if (r == -1) {
        renderGameFrame(app);
        return 1;
    }
    const int t = r;
    if (t == cur) {
        jailPlayer(app, cur, 4);  // [RE 0x4445A7] 反弹自己 4 天
    } else {
        jailPlayer(app, t, 5);    // [RE 0x4445A0] 目标 5 天
    }
    playCardLine(app, t, kSpeechFrameTarget);  // [RE 0x4445Cx 目标台词]
    // [RE 0x4445Cx] 原目标未被转嫁且持復仇卡(18) → 使用者坐牢 5 天
    if (t == orig && cardBagHas(st, orig, 18)) {
        triggerRevengeCard(app, orig);
        jailPlayer(app, cur, 5);
    }
    renderGameFrame(app);  // [RE sub_41D546]
    RICH4_LOGI("cardFrame: p%d -> p%d (RE 0x4444BF)", cur, t);
    return 1;
}

// ===== id26 查稅卡 [RE 0x4451F0] =====
int cardEffectTaxAudit(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0410);  // [RE 0x445215]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 26);  // [RE 0x445232 sub_441343(cur,26)]
    playCardLine(app, cur, 25);  // [RE 使用者台词 查稅]
    int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x44524x]
    if (t < 0 || t >= 4) {
        return 0;
    }
    int32_t tax = static_cast<int32_t>(st.players[t].cash * 0.2);  // [RE dbl_4653D8=0.2]
    addPlayerDebt(st, t, cur, tax / 100);                          // [RE 0x44526A]
    // [RE 0x44527D] 免費卡(20) 抵用成功 → 不转账
    if (cardBagHas(st, t, 20) && applyFreeCard(app, t, tax) == 1) {
        renderGameFrame(app);
        return 1;
    }
    // [RE 0x4452A7] 嫁祸(19)（税额 > 2000 才可用）→ 改目标
    if (cardBagHas(st, t, 19) && tax > 2000) {
        const int r = passOnCardDialog(app, t);
        if (r != -1) {
            t = r;
        }
    }
    if (t != cur) {
        tax = static_cast<int32_t>(st.players[t].cash * 0.2);  // [RE 0x4452E0 重算]
        playCardLine(app, t, kSpeechTaxTarget);                 // [RE 0x4453xx 目标台词]
        transferMoney(app, t, cur, tax, 0);                     // [RE 0x4452F5]
        char text[192];
        std::snprintf(text, sizeof text, "%s被課稅%d元", playerNameNoSpace(st, t).c_str(), tax);
        showMessage(app, text, 1500);  // [RE 0x44531B unk_4653C0]
    }
    renderGameFrame(app);  // [RE sub_41D546]
    RICH4_LOGI("cardTaxAudit: p%d -> p%d tax=%d (RE 0x4451F0)", cur, t, tax);
    return 1;
}

// ===== id27 漲價卡 [RE 0x44542D] =====
int cardEffectPriceUp(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0006);  // [RE 0x445452]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 27);  // [RE 0x44546F sub_441343(cur,27)]
    playCardLine(app, cur, 26);  // [RE 使用者台词 漲價]
    if (v > 2000 && v < 4000) {
        std::vector<int> route;
        if (collectSameRouteEstates(st, v, route) < 0) {
            return 0;
        }
        for (int j : route) {
            st.estates[j].flag = 80;  // [RE 0x4454B2] 0x50：高半字节 5 = 5 天加倍
        }
    } else if (v > 4000 && v < 6000) {
        const int ci = v - 4000;
        if (ci <= 0 || ci >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        st.corps[ci].flag = 80;  // [RE 0x445505]
    } else {
        return 0;
    }
    renderGameFrame(app);  // [RE refreshGameUi(0,0,1)]
    RICH4_LOGI("cardPriceUp: objId=%d (RE 0x44542D)", v);
    return 1;
}

// ===== id28 查封卡 [RE 0x445593] =====
int cardEffectSeal(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0006);  // [RE 0x4455B8]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 28);  // [RE 0x4455D5 sub_441343(cur,28)]
    playCardLine(app, cur, 27);  // [RE 使用者台词 查封]
    if (v > 2000 && v < 4000) {
        std::vector<int> route;
        if (collectSameRouteEstates(st, v, route) < 0) {
            return 0;
        }
        for (int j : route) {
            st.estates[j].flag = 81;  // [RE 0x445612] 0x51：高半字节 5 天 + 低半字节查封标记
        }
    } else if (v > 4000 && v < 6000) {
        const int ci = v - 4000;
        if (ci <= 0 || ci >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[ci];
        cp.flag = 81;  // [RE 0x445656]
        if (cp.type == 4) {
            cp.researchLeft = 0;  // [RE 0x445666] 研究所查封 → 停研发
        }
    } else {
        return 0;
    }
    renderGameFrame(app);  // [RE refreshGameUi(0,0,1)]
    RICH4_LOGI("cardSeal: objId=%d (RE 0x445593)", v);
    return 1;
}

// ===== id29 同盟卡 [RE 0x445710] =====
int cardEffectAlly(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0410);  // [RE 0x445735]
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 29);  // [RE 0x445752 sub_441343(cur,29)]
    playCardLine(app, cur, 28);  // [RE 使用者台词 同盟]
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x44576x]
    if (t < 0 || t >= 4) {
        return 0;
    }
    // [RE 0x44579x/0x4457Cx] 先解双方旧盟
    if (st.players[cur].ally != 0) {
        clearAllyPair(st, cur);
    }
    if (st.players[t].ally != 0) {
        clearAllyPair(st, t);
    }
    st.players[cur].ally = static_cast<uint8_t>(t + 1);  // [RE 0x44581x]
    st.players[cur].allyActive = 7;
    st.players[t].ally = static_cast<uint8_t>(cur + 1);
    st.players[t].allyActive = 7;
    playCardLine(app, t, kSpeechAllyTarget);  // [RE 0x44583x 目标台词]
    refreshPlayerPanelFor(app, cur);  // [RE 0x445833 sub_41D433(cur)]
    renderGameFrame(app);             // [RE sub_41D546]
    RICH4_LOGI("cardAlly: p%d <-> p%d 7 days (RE 0x445710)", cur, t);
    return 1;
}

// ===== id30 烏龜卡 [RE 0x4458DF] =====
int cardEffectTurtle(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int v = cardPickTarget(app, 0x0E0C0010);  // [RE 0x445904]
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x44591x]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 30);  // [RE 0x44592E sub_441343(cur,30)]
    playCardLine(app, cur, 29);  // [RE 使用者台词 烏龜]
    playCardLine(app, t, (t == cur) ? kSpeechTurtleSelf : kSpeechTurtleTarget);  // [RE 对方/自己]
    if (t == cur) {
        st.players[t].fixedStep = 2;  // [RE 0x44595x]
    } else {
        st.players[t].fixedStep = 3;  // [RE 0x44597x]
    }
    RICH4_LOGI("cardTurtle: p%d -> p%d fixedStep=%u (RE 0x4458DF)", cur, t,
               st.players[t].fixedStep);
    return 1;
}

// ===== id4 換地卡 [RE 0x442622]（交换两块同类地块的 owner）=====
int cardEffectSwapLand(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    int target = 0;
    if (objId > 2000 && objId < 4000) {
        target = cardPickTarget(app, 0x0E0C0202);  // [RE 0x442685] 选 estate
    } else if (objId > 4000 && objId < 6000) {
        target = cardPickTarget(app, 0x0E0C0204);  // [RE 0x4428CC] 选 corp
    } else {
        return 0;  // 脚下非地块 → 不消耗
    }
    if (target == 0) {
        return 0;  // [RE 0x44269F/0x4428E6] 取消
    }
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    if (objId > 2000 && objId < 4000) {
        const int a = objId - 2000;
        const int b = target - 2000;
        if (a <= 0 || a >= static_cast<int>(st.estates.size()) || b <= 0 ||
            b >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        st.highlightEstates.push_back(a);  // [RE 0x4426B7 markPickBuffer]
        st.highlightEstates.push_back(b);
        playCardLine(app, cur, 3);  // [RE 0x442777 使用者台词 換地]
        if (st.estates[b].owner != 0) {
            playCardLine(app, st.estates[b].owner - 1, kSpeechSwapLandOther);  // [RE 0x44287A]
        }
        const uint8_t o = st.estates[a].owner;  // [RE 0x4427BB/0x4427C1] owner 互换
        st.estates[a].owner = st.estates[b].owner;
        st.estates[b].owner = o;
        buildMiniMapMarks(app);  // [RE 0x4427C6 rebuildMiniMap(0)]
    } else {
        const int a = objId - 4000;
        const int b = target - 4000;
        if (a <= 0 || a >= static_cast<int>(st.corps.size()) || b <= 0 ||
            b >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        st.highlightCorps.push_back(a);  // [RE 0x4428FE markPickBuffer]
        st.highlightCorps.push_back(b);
        playCardLine(app, cur, 3);  // [RE 使用者台词 換地]
        if (st.corps[b].owner != 0) {
            playCardLine(app, st.corps[b].owner - 1, kSpeechSwapLandOther);  // [RE 对方台词]
        }
        const uint8_t o = st.corps[a].owner;  // [RE 0x442A09/0x442A0F] owner 互换
        st.corps[a].owner = st.corps[b].owner;
        st.corps[b].owner = o;
        buildMiniMapMarks(app);  // [RE 0x442A14 rebuildMiniMap(0)]
    }
    st.highlightFrame = 0;
    playHighlightBlink(app);  // [RE 0x4427CE/0x442A1C highlightBlink]
    renderGameFrame(app);     // [RE 0x4427D9/0x442A27 refreshGameUi(0,0,1)]
    cardBagRemove(st, cur, 4);  // [RE loc_442AE2 sub_441343(cur,4)]
    RICH4_LOGI("cardSwapLand: %u <-> %d (RE 0x442622)", objId, target);
    return 1;
}

// ===== id5 換屋卡 [RE 0x442B02]（交换两块地块的 level/sub + type：房子互换、地权不变）=====
int cardEffectSwapHouse(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    int target = 0;
    if (objId > 2000 && objId < 4000) {
        target = cardPickTarget(app, 0x0E0C0202);  // [RE 0x442B6B] 选 estate
    } else if (objId > 4000 && objId < 6000) {
        target = cardPickTarget(app, 0x0E0C0204);  // [RE 0x442D81] 选 corp
    } else {
        return 0;
    }
    if (target == 0) {
        return 0;  // [RE 0x442B85/0x442D9B] 取消
    }
    st.highlightEstates.clear();
    st.highlightCorps.clear();
    if (objId > 2000 && objId < 4000) {
        const int a = objId - 2000;
        const int b = target - 2000;
        if (a <= 0 || a >= static_cast<int>(st.estates.size()) || b <= 0 ||
            b >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        st.highlightEstates.push_back(a);  // [RE 0x442BA0 markPickBuffer]
        st.highlightEstates.push_back(b);  // [RE 0x442BBA]
        playCardLine(app, cur, 4);         // [RE 使用者台词 換屋]
        if (st.estates[b].owner != 0) {
            playCardLine(app, st.estates[b].owner - 1, kSpeechSwapHouseOther);  // [RE 对方台词]
        }
        // [RE 0x40B4F8] 房屋互换（level+24/type+26 交换；动画简化为直接交换）
        Estate& ea = st.estates[a];
        Estate& eb = st.estates[b];
        const uint8_t lv = ea.level;
        const uint8_t ty = ea.type;
        ea.level = eb.level;
        ea.type = eb.type;
        eb.level = lv;
        eb.type = ty;
    } else {
        const int a = objId - 4000;
        const int b = target - 4000;
        if (a <= 0 || a >= static_cast<int>(st.corps.size()) || b <= 0 ||
            b >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        st.highlightCorps.push_back(a);  // [RE 0x442DB7 markPickBuffer]
        st.highlightCorps.push_back(b);  // [RE 0x442DD1]
        playCardLine(app, cur, 4);       // [RE 使用者台词 換屋]
        if (st.corps[b].owner != 0) {
            playCardLine(app, st.corps[b].owner - 1, kSpeechSwapHouseOther);  // [RE 对方台词]
        }
        Corp& ca = st.corps[a];
        Corp& cb = st.corps[b];
        const uint8_t sb = ca.sub;
        const uint8_t ty = ca.type;
        ca.sub = cb.sub;
        ca.type = cb.type;
        cb.sub = sb;
        cb.type = ty;
    }
    st.highlightFrame = 0;
    playHighlightBlink(app);  // [RE 0x442C89/0x442EA4 highlightBlink]
    renderGameFrame(app);
    cardBagRemove(st, cur, 5);  // [RE loc_442F2D sub_441343(cur,5)]
    RICH4_LOGI("cardSwapHouse: %u <-> %d (RE 0x442B02)", objId, target);
    return 1;
}

// ===== id24 紅卡 [RE 0x444F25]（选股票涨停 3 天）=====
int cardEffectRedStock(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x444F50 cursorSelect(12,15,10) + stockPanelMain(1)] 选股（0=取消不消耗）
    const int v = cardPickStock(app, 1);
    if (v == 0) {
        return 0;
    }
    st.stockNews[v - 1] = 0x30;  // [RE byte_496987=32] 高 4 位 3 = 涨停 3 天
    stockNewsApply(app, v);      // [RE 0x429040]
    cardBagRemove(st, cur, 24);  // [RE 0x444F74 sub_441343(cur,24)]
    renderGameFrame(app);        // [RE sub_41906A(1)]
    RICH4_LOGI("cardRedStock: stock=%d halted=0x30 (RE 0x444F25)", v);
    return 1;
}

// ===== id25 黑卡 [RE 0x44503F]（选股票跌停 3 天 + 持股者损失记债）=====
int cardEffectBlackStock(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    float before[12];
    for (int i = 0; i < 12; ++i) {
        before[i] = st.stocks[i][5];  // [RE 选股前快照 12 支现价]
    }
    // [RE 0x4450xx cursorSelect + stockPanelMain(2)] 选股
    const int v = cardPickStock(app, 2);
    if (v == 0) {
        return 0;
    }
    st.stockNews[v - 1] = 0x03;  // [RE byte_496987=2] 低 4 位 3 = 跌停 3 天
    stockNewsApply(app, v);      // [RE 0x429040]
    // [RE v9 = 快照价 - 现价（跌为正）；持股者 j 欠使用者 股数×v9/200]
    const float diff = before[v - 1] - st.stocks[v - 1][5];
    for (int j = 0; j < st.playerCount && j < 4; ++j) {
        const int shares = st.playerShares[j][v - 1];  // [RE dword_497198]
        if (shares != 0) {
            addPlayerDebt(st, j, cur,
                          static_cast<int32_t>(shares * diff / 200.0f));  // [RE flt_4653BC=200]
        }
    }
    cardBagRemove(st, cur, 25);  // [RE sub_441343(cur,25)]
    renderGameFrame(app);
    RICH4_LOGI("cardBlackStock: stock=%d halted=0x03 diff=%.2f (RE 0x44503F)", v, diff);
    return 1;
}

// ===== 被动：id18 復仇卡 [RE 0x444691] =====
void triggerRevengeCard(Application& app, int victim) {
    GameState& st = app.gameState();
    if (victim < 0 || victim >= 4) {
        return;
    }
    renderGameFrame(app);  // [RE refreshGameUi(受害者XY,0)]
    char text[160];
    std::snprintf(text, sizeof text, "%s\n\n", playerNameNoSpace(st, victim).c_str());
    showCardGet(app, 18, text);   // [RE sub_441F73(18,...)]
    cardBagRemove(st, victim, 18);  // [RE sub_441343(victim,18)]
    playCardLine(app, victim, 17);  // [RE 0x44471F 受害者台词 復仇]
    playCardLine(app, st.currentPlayer, kSpeechRevengeUser);  // [RE 0x44474F 使用者（被反伤）]
    // 差异: 原版尾段与 applyExemptCard(0x444753) 共享指令（IDA 边界）；重写不尾调以免误耗免罪卡
    RICH4_LOGI("triggerRevengeCard: victim p%d (RE 0x444691)", victim);
}

// ===== 被动：id20 免費卡 [RE 0x444A60] =====
int applyFreeCard(Application& app, int target, int32_t fee) {
    GameState& st = app.gameState();
    if (target < 0 || target >= 4) {
        return 0;
    }
    renderGameFrame(app);  // [RE refreshGameUi(视口,0)]
    bool use = false;
    if (st.players[target].alive == 1) {
        char text[160];
        std::snprintf(text, sizeof text, "%s\n\n是否使用免費卡？",
                      playerNameNoSpace(st, target).c_str());  // [RE aS_42 0x465388]
        use = confirmDialog(app, text, 220, 320);  // [RE askDialog]
    } else {
        // [RE AI 分支] 阈值 (rand%3000+3000)×M；费用 ≤ 现金且阈值 ≥ 费用 → 不用
        const int32_t threshold = (std::rand() % 3000 + 3000) * st.moneyMul;
        use = !(fee <= st.players[target].cash && threshold >= fee);
    }
    if (!use) {
        return 0;
    }
    char text[160];
    std::snprintf(text, sizeof text, "%s\n\n免費卡生效！",
                  playerNameNoSpace(st, target).c_str());
    showCardGet(app, 20, text);    // [RE "使用%s" + sub_441F73(20,...)]
    cardBagRemove(st, target, 20);  // [RE sub_441343(target,20)]
    playCardLine(app, target, 19);  // [RE 0x444B0x 使用者台词 免費]
    RICH4_LOGI("applyFreeCard: p%d fee=%d used (RE 0x444A60)", target, fee);
    return 1;
}

}  // namespace rich4
