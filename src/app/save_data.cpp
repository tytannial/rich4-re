#include "game/app/save_data.h"

#include "game/game_state.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

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

// [RE 0x402FD1] Player（字段化）→ 原版 104 字节结构
void serializePlayer(uint8_t out[kPlayerSize], const Player& p) {
    std::memset(out, 0, kPlayerSize);
    out[4] = static_cast<uint8_t>(p.color & 0xFF);
    out[5] = static_cast<uint8_t>(p.color >> 8);
    std::memcpy(out + 8, &p.spriteX, 2);
    std::memcpy(out + 10, &p.spriteY, 2);
    std::memcpy(out + 12, &p.cellEntId, 2);
    std::memcpy(out + 14, &p.facing, 2);
    out[16] = p.dir;
    out[17] = p.travel;
    out[18] = p.pad18;
    out[19] = p.charIndex;
    out[20] = p.pad20;
    out[21] = p.alive;
    out[25] = p.aiAssetPct;
    std::memcpy(out + 28, &p.cash, 4);
    std::memcpy(out + 32, &p.bank, 4);
    std::memcpy(out + 36, &p.loan, 4);
    std::memcpy(out + 44, &p.loanDate, 4);
    std::memcpy(out + 48, &p.points, 2);
    std::memcpy(out + 50, &p.dword50, 4);
    out[54] = p.byte54;
    out[55] = p.state37;
    out[62] = p.cardCount;
    out[63] = p.cellTableIdx;
    out[65] = p.vehicle;
    out[100] = p.kind;
}

} // namespace

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
    // [RE 0x402FD1] 依据: 0x402FD1 反编译的 fwrite 字段顺序
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
    const uint8_t zeros[1728] = {};

    w32(kSaveVersion);
    w32(state.gameDate);
    w16(static_cast<uint16_t>(state.mapIndex));
    w16(static_cast<uint16_t>(state.gameMode));
    w32(static_cast<uint32_t>(state.playerCount));
    for (int i = 0; i < 4; ++i) {
        uint8_t buf[kPlayerSize];
        if (i < state.playerCount) {
            serializePlayer(buf, state.players[i]);
        } else {
            std::memset(buf, 0, sizeof(buf));
        }
        wbuf(buf, sizeof(buf));
    }
    w32(static_cast<uint32_t>(state.humanCount));
    wbuf(state.miscTable80, 80);                       // g_miscTable80 16x5
    wbuf(state.cellTable.data(), 24 * 46);             // g_cellTable
    wbuf(state.cardState60, 60);                       // g_cardState60
    wbuf(state.cardPool60, 60);                        // byte_49915C
    wbuf(state.propStock, 30);                         // g_propStock
    wbuf(state.misc8A, 8);                             // byte_497320
    w32(static_cast<uint32_t>(state.turnCounter));
    wbuf(state.stockHistory, 4 * 1728);                // g_stockHistory
    wbuf(state.playerShares, 8 * 48);                  // g_playerShares
    wbuf(state.stocks, 36 * 12);                       // g_stocks
    wbuf(zeros, 12 * 28);                              // g_miscTable336（阶段 3）
    w32(static_cast<uint32_t>(state.currentPlayer));
    w32(static_cast<uint32_t>(state.cfgTravel));
    w32(static_cast<uint32_t>(state.cfgLandPerm));
    w32(static_cast<uint32_t>(state.gameDaysLimit));
    w32(static_cast<uint32_t>(state.winMoney));
    w32(static_cast<uint32_t>(state.startMoneyVal));
    w32(static_cast<uint32_t>(state.moneyMul));
    w32(static_cast<uint32_t>(state.dayCount));
    w32(0); // dword_499084
    w32(0); // dword_4990DC
    w32(0); // dword_49907C
    w32(0); // dword_499078
    w32(0); // dword_4990EC
    for (int i = 0; i < 12; ++i) {                     // g_charState
        const uint8_t v = state.newGameConfig.aiUsed[i] ? 2 : 0;
        wbuf(&v, 1);
    }
    w32(0); // dword_499080
    wbuf(zeros, 36);                                   // g_miscTable36A
    wbuf(state.misc8A, 8);                             // g_misc8A
    wbuf(state.misc8B, 8);                             // g_misc8B
    w32(static_cast<uint32_t>(state.cardShufflePos));  // dword_4990E0
    w32(static_cast<uint32_t>(state.misc37Pos));       // dword_4990B4
    wbuf(state.cardShuffle, 36);                       // g_miscTable36B
    wbuf(state.misc37, 37);                            // g_miscTable37
    w32(static_cast<uint32_t>(state.mapRotation));     // dword_499088
    w32(0);                                            // dwSize（阶段 3 地图数据）
    // 每玩家棋盘数据（阶段 3；原版 10008B + dwSize）
    std::vector<uint8_t> block(10008, 0);
    for (int i = 0; i < state.playerCount; ++i) {
        wbuf(block.data(), block.size());
    }
    std::fclose(fp);
    return true;
}

} // namespace rich4
