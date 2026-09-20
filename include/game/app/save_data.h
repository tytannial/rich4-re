#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

struct GameState;

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

// [RE 0x402FD1] saveGameToSlot：把游戏内持久状态写入 SAVE%d.DAT
// 依据: 0x402FD1 反编译; 字段顺序 version(38)/日期/地图/模式/玩家数/玩家(104x4)/
//       人类数/道具表/格子表/卡片/股票/回合/地图数据/每玩家棋盘数据
// 差异: g_miscTable336/g_miscTable36A/dword_499084 等阶段 3 字段以 0 占位
bool saveGameToSlot(const std::string& gameDir, int slot, const struct GameState& state);

} // namespace rich4
