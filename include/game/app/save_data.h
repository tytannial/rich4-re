#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

struct GameState;
struct Player;
struct TurnSnapshot;

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

// 原版 g_playerMapBlocks 每玩家快照字节数（0x44808A：2502 dword）
constexpr size_t kSnapshotBlockSize = 10008;

// [RE 0x403D74] 读取存档头（文件不存在/版本不符返回 false）
bool readSaveHeader(const std::string& path, SaveSlot& slot);

// [RE 0x402AC5] 读取完整存档文件（原始字节，供读档恢复）
bool readSaveFile(const std::string& path, std::vector<uint8_t>& out);

// [RE 0x402FD1] saveGameToSlot：把游戏内持久状态写入 SAVE%d.DAT
// 依据: 0x402FD1 反编译; 字段顺序与原版逐字节对齐（旧档 SAVE*.DAT 实测）:
//   version(38)/日期/地图/模式/玩家数/玩家(104×4)/人类数/事件槽80/格子表1104/卡片60/道具60/
//   股标30/礼物池8/回合数/股票史/持股(shares+均价交织)/股票表(含停牌·新闻·保留·交易量)/挂单336/
//   当前玩家/配置/…/通关进度/角色状态/公库/乐透/监狱/医院/新闻·命运序与位/旋转/
//   dwSize/mapDat blob/每玩家[ mapBlocks 10008 + mapDatCopy dwSize ]
bool saveGameToSlot(const std::string& gameDir, int slot, const struct GameState& state);

// [RE 0x402AC5] 解析 SAVE%d.DAT 原始字节 → GameState 全字段（与 saveGameToSlot 字段序对称单源）：
//   玩家/事件槽/格子表/卡片·道具·股票/挂单/标量/新闻·命运/mapDat→五表 vector/每玩家 mapBlocks+
//   mapDatCopies 快照。Player.name 由调用方按 charIndex 重建（原版 memcpy 后用指针表重定位）。
bool parseSaveBody(GameState& st, const std::vector<uint8_t>& raw);

// [RE 0x496B68] Player（字段化）↔ 原版 104 字节结构（按原版偏移手工序列化）
void serializePlayer(const Player& p, uint8_t out[104]);
// [RE 0x402AC5] 反向：104 字节 → Player（name 由 charIndex 上层重建，不在此恢复）
void deserializePlayer(Player& p, const uint8_t in[104]);

// [RE 0x498E80..] 把地图五表 vector 序列化为原版 mapDat blob（40B 头 + 五表，index0 占位）
//   依据: mapdat.md；读旧档由 new_game.cpp parseMapDat 反向解析
std::vector<uint8_t> buildMapDat(const GameState& st);

// [RE 0x407AD2] 解析 mapDat blob → 五表 vector（new_game.cpp parseMapDat 转调本函数，单源）
bool parseMapDatBlob(GameState& st, const uint8_t* data, size_t size);

// [RE 0x44808A] 把当前全状态写入回合快照 buffer（10008B g_playerMapBlocks 布局 + mapDat 副本）
void writeSnapshotBlock(const GameState& st, int p, TurnSnapshot& snap);

// [RE 0x448544] 反向：回合快照 buffer 回灌 GameState（時光機全状态回滚）
//   地块五表由 mapDatCopy 经 parseMapDat 重建；调用方负责后续 buildMiniMapMarks/loadWalkResources
bool readSnapshotBlock(GameState& st, const TurnSnapshot& snap);

} // namespace rich4
