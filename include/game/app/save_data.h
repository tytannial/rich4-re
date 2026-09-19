#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

// 存档槽头信息（对应 0x403D74 读取的 dword_48A340/33C/330/32C 与玩家角色表）。
// 依据: 0x403D74 中 fread 顺序 datetime(4B) / map(2B) / mode(2B) / players(4B) /
//       player[4] x 104B; 角色 ID 位于玩家结构偏移 19（byte_48A19B[104*i]）
struct SaveSlot {
    bool valid = false;
    uint32_t datetime = 0; // 高字 = 年份, 次字节 = 月, 低字节 = 日
    uint16_t mapIndex = 0;
    uint16_t gameMode = 0;
    uint32_t playerCount = 0;
    uint8_t roles[4] = {0, 0, 0, 0};
};

// 存档版本号（SAVE%d.DAT 偏移 0，固定 38）
constexpr uint32_t kSaveVersion = 38;

// [RE 0x403D74] 读取存档头（文件不存在/版本不符返回 false）
bool readSaveHeader(const std::string& path, SaveSlot& slot);

// [RE 0x402AC5] 读取完整存档文件（原始字节，供读档恢复）
bool readSaveFile(const std::string& path, std::vector<uint8_t>& out);

} // namespace rich4
