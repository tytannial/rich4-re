#include "game/app/debug_keys.h"

#include <cstring>
#include <string>

#include "game/app/bank_stay_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/dividend.h"
#include "game/app/facility_dialog.h"
#include "game/app/game_loop.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/month_settle.h"
#include "game/app/object_tip.h"
#include "game/app/stock_market_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/encoding.h"
#include "game/core/log.h"

namespace rich4 {

namespace {

int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// [NEW] 鼠标指向的地图物件 ID（复用渲染期拾取记录）
uint16_t pointedObject(Application& app) {
    int x = 0;
    int y = 0;
    app.mouseLogicalPos(x, y);
    return pickMapObject(app.gameState(), x, y);
}

std::string nameAt(const uint8_t* p, int maxLen) {
    char buf[24] = {};
    int n = 0;
    while (n < maxLen && n < 23 && p[n]) {
        buf[n] = static_cast<char>(p[n]);
        ++n;
    }
    return big5ToUtf8(buf, n);
}

} // namespace

// [NEW] 调试热键（--debug 启用；测试辅助，无原版依据）
bool handleDebugKey(Application& app, int code, bool shift) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (shift) {
        // ===== Ctrl+Shift+数字：商業用地设施一键布置（owner=当前玩家、sub=1）=====
        if (code >= 1 && code <= 5) {
            const uint16_t id = pointedObject(app);
            if (id >= 4000 && id < 6000) {
                const int i = id - 4000;
                if (i > 0 && i < static_cast<int>(st.corps.size())) {
                    Corp& cp = st.corps[i];
                    cp.owner = static_cast<uint8_t>(cur + 1);
                    cp.type = static_cast<uint8_t>(code - 1); // 1..5 → type 0..4
                    cp.sub = 1;
                    buildMiniMapMarks(app);
                    RICH4_LOGI("debug: corp[%d] -> %s owner=%d sub=1 (Ctrl+Shift+%d)", i,
                               kBuildingNames[6 + code - 1], cur + 1, code);
                }
            } else {
                RICH4_LOGW("debug: Ctrl+Shift+%d no corp under cursor (id=%u)", code, id);
            }
            return true;
        }
        // ===== Ctrl+Shift+字母：难测机制快捷构造 =====
        switch (code) {
            case 'g': { // 瞬移到指向地块并立即触发落地结算（landingEvent）
                const uint16_t id = pointedObject(app);
                if (id == 0) {
                    RICH4_LOGW("debug: Ctrl+Shift+G no object under cursor");
                    return true;
                }
                uint16_t cellId = 0;
                if (id < 2000) {
                    cellId = id; // 直接指向 cellEnt（道路格）
                } else {
                    for (size_t i = 1; i < st.cellEnts.size(); ++i) {
                        if (st.cellEnts[i].special == id) {
                            cellId = static_cast<uint16_t>(i);
                            break;
                        }
                    }
                }
                if (cellId == 0 || cellId >= st.cellEnts.size()) {
                    RICH4_LOGW("debug: Ctrl+Shift+G no cellEnt for id=%u", id);
                    return true;
                }
                Player& pl = st.players[cur];
                if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
                    st.cellEnts[pl.cellEntId].occMask &= ~(256u << cur);
                }
                pl.cellEntId = cellId;
                pl.spriteX = static_cast<uint16_t>(st.cellEnts[cellId].x);
                pl.spriteY = static_cast<uint16_t>(st.cellEnts[cellId].y);
                st.cellEnts[cellId].occMask |= (256u << cur);
                buildMiniMapMarks(app);
                RICH4_LOGI("debug: Ctrl+Shift+G teleport player %d -> cellEnt %u (id=%u)", cur,
                           cellId, id);
                // 立即结算（买地/升级/收租/转盘/事件；内部可能阻塞弹窗）
                const int wait = landingEvent(app, cellId);
                RICH4_LOGI("debug: Ctrl+Shift+G landingEvent -> wait=%d", wait);
                renderGameFrame(app);
                return true;
            }
            case 'v': { // 切换载具（步行→機車→汽車），骰子数同步（travel+1）
                Player& pl = st.players[cur];
                pl.travel = static_cast<uint8_t>((pl.travel + 1) % 3);
                pl.diceCount = static_cast<uint8_t>(pl.travel + 1); // [RE 0x407236]
                // 清行走资源组标志 → 下次移动 loadWalkResources 重载（travel 已变）
                st.playerActionFlags[cur] &= 0xF0;
                RICH4_LOGI("debug: Ctrl+Shift+V player %d travel=%u dice=%u", cur, pl.travel,
                           pl.diceCount);
                return true;
            }
            case 't': { // 循环携带神明（cellTableIdx = 槽号；类型=槽号 1..15）
                static const uint8_t kGodSlots[8] = {0, 1, 2, 5, 6, 7, 8, 15};
                Player& pl = st.players[cur];
                int idx = 0;
                for (int i = 0; i < 8; ++i) {
                    if (kGodSlots[i] == pl.cellTableIdx) {
                        idx = i;
                        break;
                    }
                }
                pl.cellTableIdx = kGodSlots[(idx + 1) % 8];
                RICH4_LOGI("debug: Ctrl+Shift+T player %d god=%u (%s)", cur, pl.cellTableIdx,
                           pl.cellTableIdx ? kObjectNames[pl.cellTableIdx] : "无");
                return true;
            }
            case 's': { // 循环玩家状态（无→住宿→出國→監獄→醫院→冬眠→夢遊）
                static int s_state = 0;
                s_state = (s_state + 1) % 7;
                Player& pl = st.players[cur];
                pl.stateFlags = 0;
                pl.byte54 = 0;
                pl.state37 = 0;
                switch (s_state) {
                    case 1: pl.stateFlags |= 3u; break;        // BYTE0 住宿
                    case 2: pl.stateFlags |= 3u << 8; break;   // BYTE1 出國
                    case 3: pl.stateFlags |= 3u << 16; break;  // BYTE2 監獄
                    case 4: pl.stateFlags |= 3u << 24; break;  // BYTE3 醫院
                    case 5: pl.byte54 = 3; break;              // 冬眠
                    case 6: pl.state37 = 3; break;             // 夢遊
                    default: break;                            // 0 = 无状态
                }
                RICH4_LOGI("debug: Ctrl+Shift+S player %d state=%d flags=%08X byte54=%u state37=%u",
                           cur, s_state, pl.stateFlags, pl.byte54, pl.state37);
                return true;
            }
            case 'b': // 银行存款 +100000
                st.players[cur].bank += 100000;
                RICH4_LOGI("debug: Ctrl+Shift+B player %d bank=%d", cur, st.players[cur].bank);
                return true;
            case 'u': { // 银行停留 UI（调试：无需走到银行格；配合 Ctrl+1 改 owner 测自家模式）
                int bankIdx = -1;
                for (size_t i = 1; i < st.specPts.size(); ++i) {
                    if (st.specPts[i].costType == 7) {
                        bankIdx = static_cast<int>(i);
                        break;
                    }
                }
                int cellId = -1;
                if (bankIdx > 0) { // specPt → cellEnts 下标（special == 6000+n）
                    const uint16_t want = static_cast<uint16_t>(6000 + bankIdx);
                    for (size_t c = 0; c < st.cellEnts.size(); ++c) {
                        if (st.cellEnts[c].special == want) {
                            cellId = static_cast<int>(c);
                            break;
                        }
                    }
                }
                if (cellId >= 0) {
                    RICH4_LOGI("debug: Ctrl+Shift+U open bank stay UI specPt=%d cellEnt=%d owner=%d "
                               "cur=%d",
                               bankIdx, cellId, st.specPts[bankIdx].owner, cur);
                    bankStayDialog(app, cellId);
                } else {
                    RICH4_LOGW("debug: Ctrl+Shift+U no bank cellEnt (specPt %d)", bankIdx);
                }
                return true;
            }
            case 'j': // 当前玩家入狱 3 天（jailPlayer 0x43D593：移格/标志/FLC 538/保险理赔）
                if (st.eventFlcActive) {
                    return true; // FLC 播放中防重入（键盘重复会叠加播放）
                }
                jailPlayer(app, cur, 3);
                // [RE 0x40DEFE] 对齐原版"工具条功能（含卡片）结束"的回合推进：有状态 →
                //   byte_498EA0 |= 0x80 触发 beginPlayerTurn（状态分支提示后自动跳过回合），
                //   不恢复玩家控制。等待操作阶段（wait=0）必须走此路径，否则前进面板因状态隐藏而悬空
                if (st.playerActionState[cur] == 0 && st.playerActionWait[cur] == 0) {
                    disablePlayerControl(app);
                    st.playerActionFlags[cur] |= 0x80;
                    st.gameStateActive = true;
                }
                return true;
            case 'l': // 当前玩家住院 3 天（hospitalizePlayer 0x43EC3F：FLC 524）
                if (st.eventFlcActive) {
                    return true;
                }
                hospitalizePlayer(app, cur, 3);
                if (st.playerActionState[cur] == 0 && st.playerActionWait[cur] == 0) {
                    disablePlayerControl(app);
                    st.playerActionFlags[cur] |= 0x80;
                    st.gameStateActive = true;
                }
                return true;
            case 'm': // 打开股市面板（测试 0x42B58F）
                if (st.gameLoopActive) {
                    stockMarketDialog(app);
                }
                return true;
            case 'k': { // 买入指向 specPt 的股票 100 股（测试控股/经营権转移）
                const uint16_t id = pointedObject(app);
                if (id > 6000 && id < 8000) {
                    const int si = id - 6000;
                    if (si > 0 && si < static_cast<int>(st.specPts.size())) {
                        const int stock = st.specPts[si].stockNo;
                        if (stock >= 0 && stock < 12) {
                            const int changed = buyStock(app, cur, stock, 100, false);
                            RICH4_LOGI("debug: Ctrl+Shift+K buy 100 of stock %d (specPt %d) -> "
                                       "player %d shares=%d owner=%u control=%d",
                                       stock, si, cur, st.playerShares[cur][stock],
                                       st.specPts[si].owner, changed);
                        }
                    }
                } else {
                    RICH4_LOGW("debug: Ctrl+Shift+K no specPt under cursor (id=%u)", id);
                }
                return true;
            }
            case 'd': // 过一天（advanceDay：日期/到期/每日事件）
                advanceDay(app);
                RICH4_LOGI("debug: Ctrl+Shift+D advanceDay dayCount=%d", st.dayCount);
                return true;
            case 'n': // 月初结息（monthlySettle 0x439BFA/0x437E61：发息 + 排行演出）
                if (st.gameLoopActive) {
                    monthlySettle(app);
                }
                return true;
            case 'e': // 15 号股东大会分红（dividendMeeting 0x42BA97/0x42B3EB）
                if (st.gameLoopActive) {
                    dividendMeeting(app);
                }
                return true;
            case 'p': // 当前玩家点券 +1000（测试監獄/醫院 NPC 保释 300 点券）
                st.players[cur].points = static_cast<uint16_t>(st.players[cur].points + 1000);
                RICH4_LOGI("debug: Ctrl+Shift+P player %d points -> %u", cur,
                           st.players[cur].points);
                return true;
            case 'c': { // 清空玩家全部状态 + 携带神明
                Player& pl = st.players[cur];
                pl.stateFlags = 0;
                pl.byte54 = 0;
                pl.state37 = 0;
                pl.cellTableIdx = 0;
                RICH4_LOGI("debug: Ctrl+Shift+C player %d states cleared", cur);
                return true;
            }
            case 'o': { // [NEW] 当前格生成 cellTable 物件（类型 1..18 循环；测附身/挂身/渲染）
                static int sDebugObjType = 1;
                const uint16_t cell = st.players[cur].cellEntId;
                const int obj = createMapObject(st, sDebugObjType, cell, 0, 0);
                RICH4_LOGI("debug: Ctrl+Shift+O createMapObject type %d -> obj %d cell %u "
                           "(cellTableIdx=%u cellNo=%u)",
                           sDebugObjType, obj, cell, st.players[cur].cellTableIdx,
                           st.players[cur].cellNo);
                sDebugObjType = sDebugObjType % 18 + 1;
                return true;
            }
            case 'x': { // [NEW] 附身/挂身物件寿命→1（测飘走轮替 / 炸弹爆炸）
                Player& pl = st.players[cur];
                for (int idx : {pl.cellTableIdx, pl.cellNo}) {
                    if (idx != 0) {
                        uint8_t& life = st.cellTable[static_cast<size_t>(24 * (idx - 1)) + 4];
                        if (life > 1) {
                            life = 1;
                        }
                        RICH4_LOGI("debug: Ctrl+Shift+X obj %d life -> %u", idx, life);
                    }
                }
                return true;
            }
            case 'i': { // [NEW] 使用请神符（0x444E1A：视野最近神明飞行附身；先塞卡走完整路径）
                giveCardToBag(st, cur, 23);
                const bool ok = useInviteGodCard(app);
                RICH4_LOGI("debug: Ctrl+Shift+I useInviteGodCard -> %d", ok ? 1 : 0);
                return true;
            }
            case 'h': { // [NEW] 使用送神符（0x444C45：清挂身炸弹/送走负面神明；先塞卡）
                giveCardToBag(st, cur, 22);
                const bool ok = useBanishGodCard(app);
                RICH4_LOGI("debug: Ctrl+Shift+H useBanishGodCard -> %d", ok ? 1 : 0);
                return true;
            }
            case 'y': { // [NEW] dump cellTable（活动槽 + 玩家附身/挂件/三属性 + 池）
                for (int slot = 0; slot < 46; ++slot) {
                    const uint8_t* e = &st.cellTable[24 * slot];
                    if (e[2] == 0 && e[3] == 0 && e[5] == 0 && e[6] == 0) {
                        continue;
                    }
                    const uint16_t ent = static_cast<uint16_t>(e[2] | (e[3] << 8));
                    RICH4_LOGI("  cellTable[%d] type %u(%s) ent %u dir %u life %u owner %u fly %u",
                               slot, e[0], e[0] < 19 ? kObjectNames[e[0]] : "?", ent, e[1], e[4],
                               e[5], e[6]);
                }
                for (int i = 0; i < st.playerCount && i < 4; ++i) {
                    const Player& pl = st.players[i];
                    RICH4_LOGI("  player %d attached %u cellNo %u luckA/B/C %d/%d/%d", i,
                               pl.cellTableIdx, pl.cellNo, pl.luckA, pl.luckB, pl.luckC);
                }
                RICH4_LOGI("  giftPool %u,%u,%u,%u,.. trapStock %u,%u,%u", st.misc8A[0],
                           st.misc8A[1], st.misc8A[2], st.misc8A[3], st.trapStock[0],
                           st.trapStock[1], st.trapStock[2]);
                RICH4_LOGI("debug: Ctrl+Shift+Y cellTable dump done");
                return true;
            }
            default:
                return false;
        }
    }
    switch (code) {
        case 1: { // 指向地块 owner = 当前回合玩家
            const uint16_t id = pointedObject(app);
            if (id >= 2000 && id < 4000) {
                const int i = id - 2000;
                if (i > 0 && i < static_cast<int>(st.estates.size())) {
                    st.estates[i].owner = static_cast<uint8_t>(cur + 1);
                    buildMiniMapMarks(app);
                    RICH4_LOGI("debug: estate[%d] owner -> %d", i, cur + 1);
                }
            } else if (id >= 4000 && id < 6000) {
                const int i = id - 4000;
                if (i > 0 && i < static_cast<int>(st.corps.size())) {
                    st.corps[i].owner = static_cast<uint8_t>(cur + 1);
                    buildMiniMapMarks(app);
                    RICH4_LOGI("debug: corp[%d] owner -> %d", i, cur + 1);
                }
            } else if (id >= 6000 && id < 8000) {
                const int i = id - 6000;
                if (i > 0 && i < static_cast<int>(st.specPts.size())) {
                    st.specPts[i].owner = static_cast<uint8_t>(cur + 1);
                    buildMiniMapMarks(app);
                    RICH4_LOGI("debug: specPt[%d] owner -> %d", i, cur + 1);
                }
            } else {
                RICH4_LOGW("debug: Ctrl+1 no estate/corp/specPt under cursor (id=%u)", id);
            }
            return true;
        }
        case 2:
        case 3: { // 等级 ±1（地产 level / 公司 sub，范围 0..5）
            const uint16_t id = pointedObject(app);
            const int delta = (code == 2) ? 1 : -1;
            if (id >= 2000 && id < 4000) {
                const int i = id - 2000;
                if (i > 0 && i < static_cast<int>(st.estates.size())) {
                    Estate& es = st.estates[i];
                    es.level = static_cast<uint8_t>(clampInt(es.level + delta, 0, 5));
                    RICH4_LOGI("debug: estate[%d] level -> %d", i, es.level);
                }
            } else if (id >= 4000 && id < 6000) {
                const int i = id - 4000;
                if (i > 0 && i < static_cast<int>(st.corps.size())) {
                    Corp& cp = st.corps[i];
                    cp.sub = static_cast<uint8_t>(clampInt(cp.sub + delta, 0, 5));
                    RICH4_LOGI("debug: corp[%d] sub -> %d", i, cp.sub);
                }
            } else {
                RICH4_LOGW("debug: Ctrl+%d no estate/corp under cursor (id=%u)", code, id);
            }
            return true;
        }
        case 4: { // 改建卡效果（0x44309B）：走 cardRebuildEffect（当前玩家所站格），与卡片栏 case8 同路径
            const int r = cardRebuildEffect(app);
            RICH4_LOGI("debug: Ctrl+4 cardRebuildEffect -> %d (RE 0x44309B)", r);
            return true;
        }
        case 5: { // 全图住宅用地归当前玩家
            for (size_t i = 1; i < st.estates.size(); ++i) {
                st.estates[i].owner = static_cast<uint8_t>(cur + 1);
            }
            for (size_t i = 1; i < st.corps.size(); ++i) {
                st.corps[i].owner = static_cast<uint8_t>(cur + 1);
            }
            for (size_t i = 1; i < st.specPts.size(); ++i) {
                st.specPts[i].owner = static_cast<uint8_t>(cur + 1);
            }
            buildMiniMapMarks(app);
            RICH4_LOGI("debug: all estates/corps/specPts owner -> %d", cur + 1);
            return true;
        }
        case 6: { // 全图有主住宅用地 level = 1
            int n = 0;
            for (size_t i = 1; i < st.estates.size(); ++i) {
                if (st.estates[i].owner != 0) {
                    st.estates[i].level = 1;
                    ++n;
                }
            }
            RICH4_LOGI("debug: %d owned estates level -> 1", n);
            return true;
        }
        case 7: // 当前玩家现金 +100000
            if (cur >= 0 && cur < 9) {
                st.players[cur].cash += 100000;
                RICH4_LOGI("debug: player %d cash -> %d", cur, st.players[cur].cash);
            }
            return true;
        case 8: { // 打印指向地块详情
            const uint16_t id = pointedObject(app);
            if (id >= 2000 && id < 4000) {
                const int i = id - 2000;
                if (i > 0 && i < static_cast<int>(st.estates.size())) {
                    const Estate& es = st.estates[i];
                    const std::string nm = nameAt(reinterpret_cast<const uint8_t*>(es.name), 19);
                    RICH4_LOGI("debug: estate[%d] id=%u name=%s type=%u owner=%u level=%u flag=%u "
                               "dir=%u priceAdd=%u priceBase=%u fees=[%u,%u,%u,%u,%u,%u] "
                               "price=%d expire=%u",
                               i, id, nm.c_str(), es.type, es.owner, es.level, es.flag, es.dir,
                               es.priceAdd, es.priceBase, es.fees[0], es.fees[1], es.fees[2],
                               es.fees[3], es.fees[4], es.fees[5], es.price, es.expireDate);
                }
            } else if (id >= 4000 && id < 6000) {
                const int i = id - 4000;
                if (i > 0 && i < static_cast<int>(st.corps.size())) {
                    const Corp& cp = st.corps[i];
                    RICH4_LOGI("debug: corp[%d] id=%u type=%u owner=%u sub=%u flag=%u "
                               "buildPrice=%u fees=[%u,%u,%u,%u,%u,%u] lastFee=%d research=%u/%u "
                               "expire=%u",
                               i, id, cp.type, cp.owner, cp.sub, cp.flag, cp.buildPrice,
                               cp.feeTable[0], cp.feeTable[1], cp.feeTable[2], cp.feeTable[3],
                               cp.feeTable[4], cp.feeTable[5], cp.lastFee, cp.researchItem,
                               cp.researchLeft, cp.expireDate);
                }
            } else if (id >= 6000 && id < 8000) {
                const int i = id - 6000;
                if (i > 0 && i < static_cast<int>(st.specPts.size())) {
                    const SpecPt& sp = st.specPts[i];
                    const std::string nm = nameAt(sp.pad4, 20);
                    RICH4_LOGI("debug: specPt[%d] id=%u name=%s owner=%u stockNo=%u costType=%u "
                               "sprite=%u fund=%d feeBase=%u capital=%d sharesLeft=%d",
                               i, id, nm.c_str(), sp.owner, sp.stockNo, sp.costType, sp.sprite,
                               sp.fund, sp.feeBase, sp.capital, sp.sharesLeft);
                }
            } else if (id >= 8000 && id < 10000) {
                const int i = id - 8000;
                if (i > 0 && i < static_cast<int>(st.evtCells.size())) {
                    const EvtCell& ev = st.evtCells[i];
                    const std::string nm = nameAt(ev.pad4, 20);
                    RICH4_LOGI("debug: evtCell[%d] id=%u name=%s sprite=%u", i, id, nm.c_str(),
                               ev.sprite);
                }
            } else if (id >= 1 && id < 2000) {
                if (id < st.cellEnts.size()) {
                    const CellEnt& ce = st.cellEnts[id];
                    RICH4_LOGI("debug: cellEnt[%u] type=%u objId=%u sprite=%u occMask=0x%08X", id,
                               ce.occMask & 0xFF, ce.special, ce.sprite, ce.occMask);
                }
            } else {
                RICH4_LOGW("debug: Ctrl+8 no object under cursor (id=%u)", id);
            }
            return true;
        }
        case 9: { // 指向地块的联动组归当前玩家（住宅=同街同名；连锁店=全部连锁店）
            // 联动组定义对齐原版收租（0x419A67 高亮循环）：
            //   住宅用地 = 同 owner + 同名 + type==0；连锁店 = 同 owner + type!=0（跨街道）
            const uint16_t id = pointedObject(app);
            if (id >= 2000 && id < 4000) {
                const int i = id - 2000;
                if (i > 0 && i < static_cast<int>(st.estates.size())) {
                    char street[19] = {};
                    std::memcpy(street, st.estates[i].name, sizeof(street));
                    int n = 0;
                    if (st.estates[i].type == 0) {
                        for (size_t j = 1; j < st.estates.size(); ++j) {
                            Estate& e = st.estates[j];
                            if (e.type == 0 &&
                                std::strncmp(e.name, street, sizeof(street)) == 0) {
                                e.owner = static_cast<uint8_t>(cur + 1);
                                ++n;
                            }
                        }
                        RICH4_LOGI("debug: Ctrl+9 street %s -> %d estates owner=%d", street, n,
                                   cur + 1);
                    } else {
                        for (size_t j = 1; j < st.estates.size(); ++j) {
                            if (st.estates[j].type != 0) {
                                st.estates[j].owner = static_cast<uint8_t>(cur + 1);
                                ++n;
                            }
                        }
                        RICH4_LOGI("debug: Ctrl+9 all %d chain stores owner=%d", n, cur + 1);
                    }
                    buildMiniMapMarks(app);
                }
            } else {
                RICH4_LOGW("debug: Ctrl+9 no estate under cursor (id=%u)", id);
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace rich4
