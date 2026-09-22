#include "game/app/debug_keys.h"

#include <cstring>
#include <string>

#include "game/app/facility_dialog.h"
#include "game/app/map_render.h"
#include "game/app/object_tip.h"
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
bool handleDebugKey(Application& app, int digit) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    switch (digit) {
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
            const int delta = (digit == 2) ? 1 : -1;
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
                RICH4_LOGW("debug: Ctrl+%d no estate/corp under cursor (id=%u)", digit, id);
            }
            return true;
        }
        case 4: { // 改建卡效果（0x44309B）：住宅↔连锁店 / 商業用地变更设施类型
            const uint16_t id = pointedObject(app);
            if (id >= 2000 && id < 4000) {
                // estate：level(+26) != 0 才可改建；type ^= 1；变连锁店且 level>1 → level=1
                const int i = id - 2000;
                if (i > 0 && i < static_cast<int>(st.estates.size())) {
                    Estate& es = st.estates[i];
                    if (es.level == 0) {
                        RICH4_LOGW("debug: Ctrl+4 estate[%d] level==0 (need building, RE 0x44309B)",
                                   i);
                        return true;
                    }
                    const bool wasChain = (es.type == 1);
                    es.type ^= 1;
                    if (!wasChain && es.level > 1) {
                        es.level = 1;
                    }
                    buildMiniMapMarks(app);
                    RICH4_LOGI("debug: estate[%d] %s type=%u level=%u (RE 0x44309B)", i,
                               wasChain ? "chain->house" : "house->chain", es.type, es.level);
                }
            } else if (id >= 4000 && id < 6000) {
                // corp：sub != 0 时 selectFacilityDialog(1) 变更设施类型；
                //   公園/加油站（type 0/3）时 sub 降为 1（原版 0x4431FC）
                const int i = id - 4000;
                if (i > 0 && i < static_cast<int>(st.corps.size())) {
                    Corp& cp = st.corps[i];
                    if (cp.sub == 0) {
                        RICH4_LOGW("debug: Ctrl+4 corp[%d] sub==0 (need facility, RE 0x443174)", i);
                        return true;
                    }
                    const int r = selectFacilityDialog(app, 1); // [RE 0x4431C4]
                    if (r >= 0) {
                        cp.type = static_cast<uint8_t>(r);
                        if ((cp.type == 0 || cp.type == 3) && cp.sub > 1) {
                            cp.sub = 1;
                        }
                        buildMiniMapMarks(app);
                        RICH4_LOGI("debug: corp[%d] type -> %u sub %u (RE 0x44309B)", i, cp.type,
                                   cp.sub);
                    }
                }
            } else {
                RICH4_LOGW("debug: Ctrl+4 no estate/corp under cursor (id=%u)", id);
            }
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
                    RICH4_LOGI("debug: specPt[%d] id=%u name=%s owner=%u index=%u sprite=%u "
                               "fund=%d price=%d",
                               i, id, nm.c_str(), sp.owner, sp.index, sp.sprite, sp.fund, sp.price);
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
                RICH4_LOGI("debug: cellEnt[%u]", id);
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
