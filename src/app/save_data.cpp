#include "game/app/save_data.h"

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

} // namespace rich4
