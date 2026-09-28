#include "game/app/save_data.h"

#include "game/game_state.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

static_assert(sizeof(CellEnt) == 40, "CellEnt 必须匹配原版 40 字节");
static_assert(sizeof(Estate) == 52, "Estate 必须匹配原版 52 字节");
static_assert(sizeof(Corp) == 56, "Corp 必须匹配原版 56 字节");
static_assert(sizeof(SpecPt) == 52, "SpecPt 必须匹配原版 52 字节");
static_assert(sizeof(EvtCell) == 28, "EvtCell 必须匹配原版 28 字节");
static_assert(sizeof(TradeSlot) == 12, "TradeSlot 必须 12 字节");

namespace {

constexpr size_t kPlayerSize = 104;    // 玩家结构 104 字节（byte_496B79[104*i] 寻址）
constexpr size_t kRoleOffset = 19;     // 角色 ID 偏移（byte_48A19B = unk_48A188 + 19）
constexpr size_t kHeaderSize = 4 + 12; // version(4) + datetime(4) + map(2) + mode(2) + players(4)

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void writeU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void writeU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

// ---- 原版 g_playerMapBlocks 10008B 布局字节偏移（0x44808A dword 索引 × 4）----
constexpr size_t kBlkValid = 0;        // dword 0
constexpr size_t kBlkDate = 4;         // dword_48CB84
constexpr size_t kBlkPlayers = 8;      // idx2
constexpr size_t kBlkMisc80 = 424;     // idx106
constexpr size_t kBlkCellTable = 504;  // idx126
constexpr size_t kBlkCard = 1608;      // idx402
constexpr size_t kBlkItem = 1668;      // idx417
constexpr size_t kBlkProp = 1728;      // idx432
constexpr size_t kBlkGift = 1758;      // idx439 + 2 字节
constexpr size_t kBlkTurn = 1768;      // dword_48D268
constexpr size_t kBlkStockHist = 1772; // idx443
constexpr size_t kBlkShares = 8684;    // idx2171
constexpr size_t kBlkStocks = 9068;    // idx2267
constexpr size_t kBlkTrade = 9500;     // idx2375
constexpr size_t kBlkMoneyMul = 9836;  // dword_48F1EC
constexpr size_t kBlkDayCount = 9840;
constexpr size_t kBlkStockDivPeriod = 9844;  // dword_499084
constexpr size_t kBlkStockClosed = 9848;     // dword_4990DC
constexpr size_t kBlkStockTotal = 9852;      // dword_49907C
constexpr size_t kBlkMarketIndex = 9856;     // dword_499078
constexpr size_t kBlkGlobalDrift = 9860;     // dword_4990EC
constexpr size_t kBlkPublicFund = 9864;      // dword_48F208
constexpr size_t kBlkLottery = 9868;         // idx2467
constexpr size_t kBlkJail = 9904;            // idx2476
constexpr size_t kBlkHosp = 9912;            // idx2478
constexpr size_t kBlkNewsPos = 9920;         // dword_48F240
constexpr size_t kBlkFatePos = 9924;         // dword_48F244
constexpr size_t kBlkNewsOrder = 9928;       // idx2482
constexpr size_t kBlkFateOrder = 9964;       // idx2491

// 复制 mapDat 一段记录到 vector（[RE 0x407AD2] 各表 index 0 保留，拷贝 count+1 项）
template <typename T>
bool copyMapTable(std::vector<T>& out, const uint8_t* base, size_t size, uint32_t count,
                  uint32_t offset) {
    if (static_cast<size_t>(offset) + static_cast<size_t>(count + 1) * sizeof(T) > size) {
        return false;
    }
    out.resize(count + 1);
    std::memcpy(out.data(), base + offset, static_cast<size_t>(count + 1) * sizeof(T));
    return true;
}

// g_stocks 12×36B 记录（原版 dword0=名指针/1=specPt·halted·news/2=reserved·volume/3..8 float）
void buildStocksRecords(const GameState& st, uint8_t* out /*432*/) {
    for (int i = 0; i < 12; ++i) {
        uint8_t* r = out + 36 * i;
        const uint32_t spec = static_cast<uint32_t>(static_cast<int>(st.stocks[i][1])) & 0xFFFFu;
        const uint32_t halt = st.stockHalted[i] & 0xFFu;
        const uint32_t news = st.stockNews[i] & 0xFFu;
        const uint32_t res = static_cast<uint32_t>(st.stockReserved[i]) & 0xFFFFu;
        const uint32_t vol = static_cast<uint32_t>(st.stockVolume[i]) & 0xFFFFu;
        writeU32(r + 0, 0);  // 名指针：原版读档用 off_47F072 重建，写 0 安全
        writeU32(r + 4, spec | (halt << 16) | (news << 24));
        writeU32(r + 8, res | (vol << 16));
        for (int k = 3; k < 9; ++k) {
            std::memcpy(r + 4 * k, &st.stocks[i][k], 4);
        }
    }
}

void parseStocksRecords(GameState& st, const uint8_t* in /*432*/) {
    for (int i = 0; i < 12; ++i) {
        const uint8_t* r = in + 36 * i;
        const uint32_t d1 = readU32(r + 4);
        const uint32_t d2 = readU32(r + 8);
        st.stocks[i][0] = 0.0f;
        st.stocks[i][1] = static_cast<float>(static_cast<int>(d1 & 0xFFFFu));
        std::memcpy(&st.stocks[i][2], &d2, 4);
        st.stockHalted[i] = static_cast<uint8_t>((d1 >> 16) & 0xFFu);
        st.stockNews[i] = static_cast<uint8_t>((d1 >> 24) & 0xFFu);
        st.stockReserved[i] = static_cast<int32_t>(d2 & 0xFFFFu);
        st.stockVolume[i] = static_cast<int32_t>((d2 >> 16) & 0xFFFFu);
        for (int k = 3; k < 9; ++k) {
            std::memcpy(&st.stocks[i][k], r + 4 * k, 4);
        }
    }
}

// g_playerShares 4×12 记录（8B = {shares i32, avgCost f32}；flt_4971A4 随记录持久化）
void buildSharesRecords(const GameState& st, uint8_t* out /*384*/) {
    for (int p = 0; p < 4; ++p) {
        for (int s = 0; s < 12; ++s) {
            uint8_t* r = out + 8 * (p * 12 + s);
            writeU32(r, static_cast<uint32_t>(st.playerShares[p][s]));
            std::memcpy(r + 4, &st.playerAvgCost[p][s], 4);
        }
    }
}

void parseSharesRecords(GameState& st, const uint8_t* in /*384*/) {
    for (int p = 0; p < 4; ++p) {
        for (int s = 0; s < 12; ++s) {
            const uint8_t* r = in + 8 * (p * 12 + s);
            st.playerShares[p][s] = static_cast<int32_t>(readU32(r));
            std::memcpy(&st.playerAvgCost[p][s], r + 4, 4);
        }
    }
}

} // namespace

// [RE 0x402FD1] Player（字段化）→ 原版 104 字节结构
void serializePlayer(const Player& p, uint8_t out[kPlayerSize]) {
    std::memset(out, 0, kPlayerSize);
    out[4] = static_cast<uint8_t>(p.color & 0xFF);
    out[5] = static_cast<uint8_t>((p.color >> 8) & 0xFF);
    out[6] = static_cast<uint8_t>((p.color >> 16) & 0xFF);
    out[7] = static_cast<uint8_t>((p.color >> 24) & 0xFF);
    std::memcpy(out + 8, &p.spriteX, 2);
    std::memcpy(out + 10, &p.spriteY, 2);
    std::memcpy(out + 12, &p.cellEntId, 2);
    std::memcpy(out + 14, &p.prevCellEnt, 2);
    out[16] = p.dir;
    out[17] = p.travel;
    out[18] = p.diceCount;
    out[19] = p.charIndex;
    out[20] = p.byte20;
    out[21] = p.alive;
    out[22] = p.aiCardItem;
    out[23] = p.aiPersonality;
    out[24] = p.aiLoanPct;
    out[25] = p.aiCashPct;
    out[26] = p.aiStockPct;
    out[27] = p.byte27;
    std::memcpy(out + 28, &p.cash, 4);
    std::memcpy(out + 32, &p.bank, 4);
    std::memcpy(out + 36, &p.loan, 4);
    std::memcpy(out + 40, &p.bankAdvance, 4);
    std::memcpy(out + 44, &p.loanDate, 4);
    std::memcpy(out + 48, &p.points, 2);
    std::memcpy(out + 50, &p.stateFlags, 4);
    out[54] = p.byte54;
    out[55] = p.state37;
    out[56] = p.skipMove;
    out[57] = p.fixedStep;
    out[59] = p.bankRefuseDays;
    out[60] = p.bankFinanceFlags;
    out[61] = p.allyActive; // [RE 0x496BA5] 同盟生效天数
    out[62] = p.insuranceDays;
    out[63] = p.cellTableIdx; // [RE 0x496BA7] 附身神明物件
    out[64] = p.cellNo;       // [RE 0x496BA8] 挂身道具物件（路障/炸彈）
    out[65] = p.ally;
    out[66] = p.byte66;
    std::memcpy(out + 68, &p.luckA, 2); // [RE 0x496BAC] 神明三属性修正
    std::memcpy(out + 70, &p.luckB, 2);
    std::memcpy(out + 72, &p.luckC, 2);
    std::memcpy(out + 74, &p.stayCorpIdx, 2);
    std::memcpy(out + 92, &p.monthSettleA, 4);
    std::memcpy(out + 96, &p.monthSettleB, 4);
    out[100] = p.kind;
}

// [RE 0x402AC5] 反向：104 字节 → Player（name 由 charIndex 上层重建）
void deserializePlayer(Player& p, const uint8_t in[kPlayerSize]) {
    p.color = readU32(in + 4);
    std::memcpy(&p.spriteX, in + 8, 2);
    std::memcpy(&p.spriteY, in + 10, 2);
    std::memcpy(&p.cellEntId, in + 12, 2);
    std::memcpy(&p.prevCellEnt, in + 14, 2);
    p.dir = in[16];
    p.travel = in[17];
    p.diceCount = in[18];
    p.charIndex = in[19];
    p.byte20 = in[20];
    p.alive = in[21];
    p.aiCardItem = in[22];
    p.aiPersonality = in[23];
    p.aiLoanPct = in[24];
    p.aiCashPct = in[25];
    p.aiStockPct = in[26];
    p.byte27 = in[27];
    std::memcpy(&p.cash, in + 28, 4);
    std::memcpy(&p.bank, in + 32, 4);
    std::memcpy(&p.loan, in + 36, 4);
    std::memcpy(&p.bankAdvance, in + 40, 4);
    std::memcpy(&p.loanDate, in + 44, 4);
    std::memcpy(&p.points, in + 48, 2);
    std::memcpy(&p.stateFlags, in + 50, 4);
    p.byte54 = in[54];
    p.state37 = in[55];
    p.skipMove = in[56];
    p.fixedStep = in[57];
    p.bankRefuseDays = in[59];
    p.bankFinanceFlags = in[60];
    p.allyActive = in[61];
    p.insuranceDays = in[62];
    p.cellTableIdx = in[63];
    p.cellNo = in[64];
    p.ally = in[65];
    p.byte66 = in[66];
    std::memcpy(&p.luckA, in + 68, 2);
    std::memcpy(&p.luckB, in + 70, 2);
    std::memcpy(&p.luckC, in + 72, 2);
    std::memcpy(&p.stayCorpIdx, in + 74, 2);
    std::memcpy(&p.monthSettleA, in + 92, 4);
    std::memcpy(&p.monthSettleB, in + 96, 4);
    p.kind = in[100];
}

// [RE 0x407AD2 / mapdat.md] 五表 vector → 原版 mapDat blob
std::vector<uint8_t> buildMapDat(const GameState& st) {
    const uint32_t ceC = static_cast<uint32_t>(st.cellEnts.size() ? st.cellEnts.size() - 1 : 0);
    const uint32_t esC = static_cast<uint32_t>(st.estates.size() ? st.estates.size() - 1 : 0);
    const uint32_t coC = static_cast<uint32_t>(st.corps.size() ? st.corps.size() - 1 : 0);
    const uint32_t spC = static_cast<uint32_t>(st.specPts.size() ? st.specPts.size() - 1 : 0);
    const uint32_t evC = static_cast<uint32_t>(st.evtCells.size() ? st.evtCells.size() - 1 : 0);

    const uint32_t ceOff = 40;
    const uint32_t esOff = ceOff + (ceC + 1) * 40;
    const uint32_t coOff = esOff + (esC + 1) * 52;
    const uint32_t spOff = coOff + (coC + 1) * 56;
    const uint32_t evOff = spOff + (spC + 1) * 52;
    const size_t total = evOff + (evC + 1) * 28;

    std::vector<uint8_t> blob(total, 0);
    uint8_t* h = blob.data();
    writeU32(h + 0, ceC);
    writeU32(h + 4, ceOff);
    writeU32(h + 8, esC);
    writeU32(h + 12, esOff);
    writeU32(h + 16, coC);
    writeU32(h + 20, coOff);
    writeU32(h + 24, spC);
    writeU32(h + 28, spOff);
    writeU32(h + 32, evC);
    writeU32(h + 36, evOff);

    auto put = [&](uint32_t off, const void* src, size_t bytes) {
        if (bytes && off + bytes <= total) {
            std::memcpy(blob.data() + off, src, bytes);
        }
    };
    put(ceOff, st.cellEnts.data(), st.cellEnts.size() * sizeof(CellEnt));
    put(esOff, st.estates.data(), st.estates.size() * sizeof(Estate));
    put(coOff, st.corps.data(), st.corps.size() * sizeof(Corp));
    put(spOff, st.specPts.data(), st.specPts.size() * sizeof(SpecPt));
    put(evOff, st.evtCells.data(), st.evtCells.size() * sizeof(EvtCell));
    return blob;
}

// [RE 0x407AD2] mapDat blob → 五表 vector
bool parseMapDatBlob(GameState& st, const uint8_t* b, size_t size) {
    if (size < 40) {
        return false;
    }
    const uint32_t ceC = readU32(b + 0);
    const uint32_t ceO = readU32(b + 4);
    const uint32_t esC = readU32(b + 8);
    const uint32_t esO = readU32(b + 12);
    const uint32_t coC = readU32(b + 16);
    const uint32_t coO = readU32(b + 20);
    const uint32_t spC = readU32(b + 24);
    const uint32_t spO = readU32(b + 28);
    const uint32_t evC = readU32(b + 32);
    const uint32_t evO = readU32(b + 36);
    return copyMapTable(st.cellEnts, b, size, ceC, ceO) &&
           copyMapTable(st.estates, b, size, esC, esO) &&
           copyMapTable(st.corps, b, size, coC, coO) &&
           copyMapTable(st.specPts, b, size, spC, spO) &&
           copyMapTable(st.evtCells, b, size, evC, evO);
}

// [RE 0x44808A] 当前全状态 → 回合快照 buffer
void writeSnapshotBlock(const GameState& st, int p, TurnSnapshot& snap) {
    (void)p;
    uint8_t blk[kSnapshotBlockSize];
    std::memset(blk, 0, sizeof(blk));
    writeU32(blk + kBlkValid, 1);
    writeU32(blk + kBlkDate, st.gameDate);
    for (int k = 0; k < 4; ++k) {
        serializePlayer(st.players[k], blk + kBlkPlayers + kPlayerSize * k);
    }
    std::memcpy(blk + kBlkMisc80, st.npcSlots, sizeof(st.npcSlots));  // 80
    if (st.cellTable.size() == 46 * 24) {
        std::memcpy(blk + kBlkCellTable, st.cellTable.data(), 46 * 24);
    }
    std::memcpy(blk + kBlkCard, st.cardState60, 60);
    std::memcpy(blk + kBlkItem, st.itemStock, 60);
    std::memcpy(blk + kBlkProp, st.propStock, 30);
    std::memcpy(blk + kBlkGift, st.misc8A, 8);
    writeU32(blk + kBlkTurn, static_cast<uint32_t>(st.turnCounter));
    std::memcpy(blk + kBlkStockHist, st.stockHistory, 4 * 1728);
    buildSharesRecords(st, blk + kBlkShares);
    buildStocksRecords(st, blk + kBlkStocks);
    std::memcpy(blk + kBlkTrade, st.tradeSlots, 336);
    writeU32(blk + kBlkMoneyMul, static_cast<uint32_t>(st.moneyMul));
    writeU32(blk + kBlkDayCount, static_cast<uint32_t>(st.dayCount));
    writeU32(blk + kBlkStockDivPeriod, static_cast<uint32_t>(st.stockDivPeriod));
    writeU32(blk + kBlkStockClosed, static_cast<uint32_t>(st.stockMarketClosed));
    writeU32(blk + kBlkStockTotal, static_cast<uint32_t>(st.stockTotalValue));
    writeU32(blk + kBlkMarketIndex, static_cast<uint32_t>(st.marketIndex));
    std::memcpy(blk + kBlkGlobalDrift, &st.stockGlobalDrift, 4);
    writeU32(blk + kBlkPublicFund, static_cast<uint32_t>(st.publicFund));
    std::memcpy(blk + kBlkLottery, st.lotteryNumbers, 36);
    std::memcpy(blk + kBlkJail, st.jailFlags, 8);
    std::memcpy(blk + kBlkHosp, st.hospitalFlags, 8);
    writeU32(blk + kBlkNewsPos, static_cast<uint32_t>(st.newsPos));
    writeU32(blk + kBlkFatePos, static_cast<uint32_t>(st.fatePos));
    std::memcpy(blk + kBlkNewsOrder, st.newsOrder, 36);
    std::memcpy(blk + kBlkFateOrder, st.fateOrder, 37);

    snap.block.assign(blk, blk + kSnapshotBlockSize);
    snap.mapDatCopy = buildMapDat(st);
    snap.valid = true;
}

// [RE 0x448544] 回合快照 buffer → 全状态回灌
bool readSnapshotBlock(GameState& st, const TurnSnapshot& snap) {
    if (!snap.valid || snap.block.size() < kSnapshotBlockSize) {
        return false;
    }
    const uint8_t* b = snap.block.data();
    st.gameDate = readU32(b + kBlkDate);
    for (int k = 0; k < 4; ++k) {
        deserializePlayer(st.players[k], b + kBlkPlayers + kPlayerSize * k);
    }
    std::memcpy(st.npcSlots, b + kBlkMisc80, sizeof(st.npcSlots));
    if (st.cellTable.size() == 46 * 24) {
        std::memcpy(st.cellTable.data(), b + kBlkCellTable, 46 * 24);
    }
    std::memcpy(st.cardState60, b + kBlkCard, 60);
    std::memcpy(st.itemStock, b + kBlkItem, 60);
    std::memcpy(st.propStock, b + kBlkProp, 30);
    std::memcpy(st.misc8A, b + kBlkGift, 8);
    st.turnCounter = static_cast<int32_t>(readU32(b + kBlkTurn));
    std::memcpy(st.stockHistory, b + kBlkStockHist, 4 * 1728);
    parseSharesRecords(st, b + kBlkShares);
    parseStocksRecords(st, b + kBlkStocks);
    std::memcpy(st.tradeSlots, b + kBlkTrade, 336);
    st.moneyMul = static_cast<int32_t>(readU32(b + kBlkMoneyMul));
    st.dayCount = static_cast<int32_t>(readU32(b + kBlkDayCount));
    st.stockDivPeriod = static_cast<int32_t>(readU32(b + kBlkStockDivPeriod));
    st.stockMarketClosed = static_cast<int32_t>(readU32(b + kBlkStockClosed));
    st.stockTotalValue = static_cast<int32_t>(readU32(b + kBlkStockTotal));
    st.marketIndex = static_cast<int32_t>(readU32(b + kBlkMarketIndex));
    std::memcpy(&st.stockGlobalDrift, b + kBlkGlobalDrift, 4);
    st.publicFund = static_cast<int32_t>(readU32(b + kBlkPublicFund));
    std::memcpy(st.lotteryNumbers, b + kBlkLottery, 36);
    std::memcpy(st.jailFlags, b + kBlkJail, 8);
    std::memcpy(st.hospitalFlags, b + kBlkHosp, 8);
    st.newsPos = static_cast<int32_t>(readU32(b + kBlkNewsPos));
    st.fatePos = static_cast<int32_t>(readU32(b + kBlkFatePos));
    std::memcpy(st.newsOrder, b + kBlkNewsOrder, 36);
    std::memcpy(st.fateOrder, b + kBlkFateOrder, 37);
    if (!snap.mapDatCopy.empty()) {
        parseMapDatBlob(st, snap.mapDatCopy.data(), snap.mapDatCopy.size());
    }
    return true;
}

bool readSaveHeader(const std::string& path, SaveSlot& slot) {
    // [RE 0x403D74] 依据: fopen("SAVE%d.DAT") → 读 version(4B, 须为 38) →
    // datetime(4B)/map(2B)/mode(2B)/players(4B) → 玩家数据 104x4（取偏移 19 角色 ID）
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    uint8_t buf[kHeaderSize];
    if (std::fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        std::fclose(fp);
        return false;
    }
    if (readU32(buf) != kSaveVersion) {
        std::fclose(fp);
        return false;
    }
    slot.datetime = readU32(buf + 4);
    slot.mapIndex = readU16(buf + 8);
    slot.gameMode = readU16(buf + 10);
    slot.playerCount = readU32(buf + 12);

    uint8_t players[kPlayerSize * 4];
    if (std::fread(players, 1, sizeof(players), fp) != sizeof(players)) {
        std::fclose(fp);
        return false;
    }
    std::fclose(fp);
    for (size_t i = 0; i < 4; ++i) {
        slot.roles[i] = players[kPlayerSize * i + kRoleOffset];
    }
    slot.valid = true;
    return true;
}

bool readSaveFile(const std::string& path, std::vector<uint8_t>& out) {
    // [RE 0x402AC5] 依据: 0x402AC5 以 fseek(4) 跳版本号后连续 fread 全部状态
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(fp);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    if (got != out.size() || readU32(out.data()) != kSaveVersion) {
        out.clear();
        return false;
    }
    return true;
}

bool saveGameToSlot(const std::string& gameDir, int slot, const GameState& state) {
    // [RE 0x402FD1] 依据: 0x402FD1 fwrite 字段序（旧档 SAVE*.DAT 实测逐字节对齐）
    char name[32];
    std::snprintf(name, sizeof(name), "SAVE%d.DAT", slot);
    const std::string path = gameDir + "/" + name;
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        return false;
    }
    auto w32 = [fp](uint32_t v) { std::fwrite(&v, 4, 1, fp); };
    auto w16 = [fp](uint16_t v) { std::fwrite(&v, 2, 1, fp); };
    auto wbuf = [fp](const void* p, size_t n) { std::fwrite(p, 1, n, fp); };

    const std::vector<uint8_t> mapDat = buildMapDat(state);
    const uint32_t dwSize = static_cast<uint32_t>(mapDat.size());

    uint8_t stocksRec[432];
    uint8_t sharesRec[384];
    buildStocksRecords(state, stocksRec);
    buildSharesRecords(state, sharesRec);

    w32(kSaveVersion);
    w32(state.gameDate);
    w16(static_cast<uint16_t>(state.mapIndex));
    w16(static_cast<uint16_t>(state.gameMode));
    w32(static_cast<uint32_t>(state.playerCount));
    for (int i = 0; i < 4; ++i) {
        uint8_t buf[kPlayerSize];
        if (i < state.playerCount) {
            serializePlayer(state.players[i], buf);
        } else {
            std::memset(buf, 0, sizeof(buf));
        }
        wbuf(buf, sizeof(buf));
    }
    w32(static_cast<uint32_t>(state.humanCount));
    wbuf(state.npcSlots, 80);                      // g_miscTable80 16x5（事件槽 4..8）
    wbuf(state.cellTable.data(), 24 * 46);         // g_cellTable
    wbuf(state.cardState60, 60);                   // g_cardState60
    wbuf(state.itemStock, 60);                     // g_itemStock 道具库存
    wbuf(state.propStock, 30);                     // g_propStock
    wbuf(state.misc8A, 8);                         // g_giftPool 礼物卡池（trapStock 原版不落盘）
    w32(static_cast<uint32_t>(state.turnCounter));
    wbuf(state.stockHistory, 4 * 1728);            // g_stockHistory
    wbuf(sharesRec, 384);                          // g_playerShares（shares+均价交织）
    wbuf(stocksRec, 432);                          // g_stocks（含停牌/新闻/保留/交易量）
    wbuf(state.tradeSlots, 336);                   // g_miscTable336 交易挂单（4 玩家×7 槽）
    w32(static_cast<uint32_t>(state.currentPlayer));
    w32(static_cast<uint32_t>(state.cfgTravel));
    w32(static_cast<uint32_t>(state.cfgLandPerm));
    w32(static_cast<uint32_t>(state.gameDaysLimit));
    w32(static_cast<uint32_t>(state.winMoney));
    w32(static_cast<uint32_t>(state.startMoneyVal));
    w32(static_cast<uint32_t>(state.moneyMul));
    w32(static_cast<uint32_t>(state.dayCount));
    w32(static_cast<uint32_t>(state.stockDivPeriod));   // dword_499084
    w32(static_cast<uint32_t>(state.stockMarketClosed)); // dword_4990DC
    w32(static_cast<uint32_t>(state.stockTotalValue));   // dword_49907C
    w32(static_cast<uint32_t>(state.marketIndex));       // dword_499078
    wbuf(&state.stockGlobalDrift, 4);                    // dword_4990EC
    wbuf(state.clearedMaps, 4);                          // g_clearedMaps 通关进度
    for (int i = 0; i < 12; ++i) {                       // g_charState 上轮通关遗留 AI
        const uint8_t v = state.newGameConfig.aiUsed[i] ? 2 : 0;
        wbuf(&v, 1);
    }
    w32(static_cast<uint32_t>(state.publicFund));        // dword_499080 公库/乐透奖金池
    wbuf(state.lotteryNumbers, 36);                     // g_lotteryNumbers
    wbuf(state.jailFlags, 8);                           // g_jailFlags
    wbuf(state.hospitalFlags, 8);                       // g_hospitalFlags
    w32(static_cast<uint32_t>(state.newsPos));           // dword_4990E0
    w32(static_cast<uint32_t>(state.fatePos));           // dword_4990B4
    wbuf(state.newsOrder, 36);                          // g_newsOrder
    wbuf(state.fateOrder, 37);                          // g_fateOrder
    w32(static_cast<uint32_t>(state.mapRotation));      // dword_499088
    w32(dwSize);                                        // dwSize
    wbuf(mapDat.data(), mapDat.size());                 // g_mapDat（地块五表 + 占用位）
    // 每玩家回合快照（g_playerMapBlocks 10008 + g_playerMapDatCopies dwSize）；缺失写全 0
    std::vector<uint8_t> zeros(kSnapshotBlockSize, 0);
    std::vector<uint8_t> emptyDat(dwSize, 0);
    for (int i = 0; i < state.playerCount && i < 4; ++i) {
        if (state.snapshots[i].block.size() == kSnapshotBlockSize) {
            wbuf(state.snapshots[i].block.data(), kSnapshotBlockSize);
        } else {
            wbuf(zeros.data(), kSnapshotBlockSize);
        }
        if (state.snapshots[i].mapDatCopy.size() == dwSize) {
            wbuf(state.snapshots[i].mapDatCopy.data(), dwSize);
        } else {
            wbuf(emptyDat.data(), dwSize);
        }
    }
    std::fclose(fp);
    return true;
}

bool parseSaveBody(GameState& st, const std::vector<uint8_t>& raw) {
    // [RE 0x402AC5] 依据: 0x402AC5 fseek(4) 后连续 fread 字段序（与 saveGameToSlot 对称）
    const uint8_t* d = raw.data();
    const size_t n = raw.size();
    if (n < 16 || readU32(d) != kSaveVersion) {
        return false;
    }
    size_t o = 4;
    bool ok = true;
    auto need = [&](size_t len) {
        if (o + len > n) {
            ok = false;
            return false;
        }
        return true;
    };
    auto u32 = [&]() -> uint32_t {
        if (!need(4)) {
            return 0;
        }
        uint32_t v = readU32(d + o);
        o += 4;
        return v;
    };
    auto u16 = [&]() -> uint16_t {
        if (!need(2)) {
            return 0;
        }
        uint16_t v = readU16(d + o);
        o += 2;
        return v;
    };
    auto bytes = [&](void* p, size_t len) {
        if (need(len)) {
            std::memcpy(p, d + o, len);
        }
        o += len;
    };

    st.gameDate = u32();
    st.mapIndex = u16();
    st.gameMode = u16();
    const int pc = static_cast<int>(u32());
    st.playerCount = pc < 1 ? 1 : (pc > 4 ? 4 : pc);
    for (int i = 0; i < 4; ++i) {
        uint8_t pb[kPlayerSize];
        bytes(pb, kPlayerSize);
        if (ok && i < st.playerCount) {
            deserializePlayer(st.players[i], pb);
        }
    }
    st.humanCount = static_cast<int>(u32());
    bytes(st.npcSlots, 80);  // g_miscTable80
    if (st.cellTable.size() != 46 * 24) {
        st.cellTable.assign(46 * 24, 0);
    }
    bytes(st.cellTable.data(), 46 * 24);
    bytes(st.cardState60, 60);
    bytes(st.itemStock, 60);
    bytes(st.propStock, 30);
    bytes(st.misc8A, 8);  // g_giftPool（trapStock 原版不落盘）
    st.turnCounter = static_cast<int32_t>(u32());
    bytes(st.stockHistory, 4 * 1728);
    {
        uint8_t shares[384];
        bytes(shares, 384);
        if (ok) {
            parseSharesRecords(st, shares);
        }
    }
    {
        uint8_t stocks[432];
        bytes(stocks, 432);
        if (ok) {
            parseStocksRecords(st, stocks);
        }
    }
    std::memset(st.tradeSlots, 0, 8 * 7 * sizeof(TradeSlot));
    bytes(st.tradeSlots, 336);  // g_miscTable336（前 28 记录 = 4 玩家×7 槽）
    {
        const int cp = static_cast<int>(u32());
        st.currentPlayer = cp < 0 ? 0 : (cp > 8 ? 8 : cp);
    }
    st.cfgTravel = static_cast<int32_t>(u32());
    st.cfgLandPerm = static_cast<int32_t>(u32());
    st.gameDaysLimit = static_cast<int32_t>(u32());
    st.winMoney = static_cast<int32_t>(u32());
    st.startMoneyVal = static_cast<int32_t>(u32());
    st.moneyMul = static_cast<int32_t>(u32());
    st.dayCount = static_cast<int32_t>(u32());
    st.stockDivPeriod = static_cast<int32_t>(u32());      // dword_499084
    st.stockMarketClosed = static_cast<int32_t>(u32());   // dword_4990DC
    st.stockTotalValue = static_cast<int32_t>(u32());     // dword_49907C
    st.marketIndex = static_cast<int32_t>(u32());         // dword_499078
    bytes(&st.stockGlobalDrift, 4);                       // dword_4990EC
    bytes(st.clearedMaps, 4);                             // g_clearedMaps
    for (int i = 0; i < 12; ++i) {  // g_charState 12 单字节 → aiUsed（非 0 = 上轮通关遗留 AI）
        uint8_t v = 0;
        bytes(&v, 1);
        st.newGameConfig.aiUsed[i] = (v != 0);
    }
    st.publicFund = static_cast<int32_t>(u32());
    bytes(st.lotteryNumbers, 36);
    bytes(st.jailFlags, 8);
    bytes(st.hospitalFlags, 8);
    st.newsPos = static_cast<int32_t>(u32());
    st.fatePos = static_cast<int32_t>(u32());
    bytes(st.newsOrder, 36);
    bytes(st.fateOrder, 37);
    st.mapRotation = static_cast<int>(u32());
    const uint32_t dwSize = u32();
    if (!ok || dwSize > n) {
        return false;
    }
    if (!need(dwSize)) {
        return false;
    }
    if (!parseMapDatBlob(st, d + o, dwSize)) {
        return false;
    }
    o += dwSize;
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (!need(kSnapshotBlockSize + dwSize)) {
            return false;
        }
        st.snapshots[i].block.assign(d + o, d + o + kSnapshotBlockSize);
        o += kSnapshotBlockSize;
        st.snapshots[i].mapDatCopy.assign(d + o, d + o + dwSize);
        o += dwSize;
        st.snapshots[i].valid = readU32(st.snapshots[i].block.data()) != 0;
    }
    return ok;
}

} // namespace rich4
