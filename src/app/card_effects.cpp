#include "game/app/card_effects.h"

#include "game/app/auction_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/game_loop.h"
#include "game/app/game_panel.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
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
    const int v = selectTargetDialog(app, 0x0E0C0410);
    if (v == 0) {
        return 0;  // 取消 → 不消耗
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x4421ED sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 2);  // [RE 0x4421E6 sub_441343(cur,2)]
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
    const int v = selectTargetDialog(app, 0x0E0C0010);
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x442FCA sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    cardBagRemove(st, cur, 6);   // [RE 0x442F8A sub_441343(cur,6)]
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
        }
        if (!runAuction(app, cur, objId, true)) {
            cp.owner = 0;
            cp.expireDate = 0;
            buildMiniMapMarks(app);
        }
    } else {
        return 0;  // [RE LABEL_21] 脚下非地块 → 不消耗
    }
    cardBagRemove(st, cur, 8);  // [RE 0x442340/0x442616 sub_441343(cur,8)]
    RICH4_LOGI("cardAuction: objId=%u seller=%d (RE 0x443225)", objId, cur);
    return 1;
}

// ===== id11 怪獸卡 [RE 0x443917]（选地彻底夷平）=====
int cardEffectMonster(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x44392C sub_446AE8(0x0E0C0506)] 选地块（estate|corp）
    const int v = selectTargetDialog(app, 0x0E0C0506);
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 11);  // [RE 0x443949 sub_441343(cur,11)]
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
    focusView(app, x, y);            // [RE 0x41D476 refreshGameUi(x,y,0)]
    demolishAtObjId(app, v, 2);      // [RE 0x4439F5] mode2 = 拆建筑留地
    playEventFlc(app, 557, x, y, 80);  // [RE 0x443A1A] 怪獸破壞 FLC 557（音效 80）
    RICH4_LOGI("cardMonster: objId=%d owner=%u (RE 0x443917)", v, owner);
    return 1;
}

// ===== id12 拆除卡 [RE 0x443B0F]（降一级 / 拆连锁 / 拆路面道具）=====
int cardEffectDemolish(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x443B28 sub_446AE8(0x0E0C0526)] 选目标（地/路面道具）
    const int v = selectTargetDialog(app, 0x0E0C0526);
    if (v == 0) {
        return 0;
    }
    cardBagRemove(st, cur, 12);  // [RE 0x443B47 sub_441343(cur,12)]
    if ((v & 0x8000) != 0 && v < 2000) {  // 路面道具（0x8000|槽<<8）
        const int slot = (v & 0x7F00) >> 8;  // [RE 0x443B6F]
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
    }
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);
    focusView(app, x, y);
    playEventFlc(app, 529, x, y, 97);  // [RE 0x443D6C] 拆除 FLC 529（音效 97）
    RICH4_LOGI("cardDemolish: objId=%d owner=%u (RE 0x443B0F)", v, owner);
    return 1;
}

// ===== id13 搶奪卡 [RE 0x443E3D]（夺卡/道具）=====
int cardEffectSteal(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    // [RE 0x443E58 sub_446AE8(0x0E0C0410)] 选玩家
    const int v = selectTargetDialog(app, 0x0E0C0410);
    if (v == 0) {
        return 0;
    }
    const int t = bitScanPlayer(static_cast<uint32_t>(v));  // [RE 0x443E6E sub_40D293]
    if (t < 0 || t >= 4) {
        return 0;
    }
    // [RE 0x443E9B selectCardOrItemDialog(t)]：重写约定 = 仅返回 id，转移在调用方（同命运生日）
    const int sel = selectCardOrItemFromPlayerDialog(app, t);
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
    cardBagRemove(st, cur, 13);        // [RE 0x443EB9 sub_441343(cur,13)]
    RICH4_LOGI("cardSteal: p%d -> p%d sel=%d price=%d (RE 0x443E3D)", cur, t, sel, price);
    return 1;
}

}  // namespace rich4
