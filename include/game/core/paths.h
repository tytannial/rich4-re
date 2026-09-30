#pragma once

#include <string>

namespace rich4 {

// [NEW] 跨平台资源路径解析（无原版对应）
// 依据: Windows 文件系统大小写不敏感，Linux 大小写敏感；原版资源名大小写混排
//       （help.mkf/jump.mkf/map.mkf 为小写，Data.mkf/Panel.mkf 等为大写），
//       代码若硬编码错误大小写，Linux 下加载失败（见 docs/cross-platform.md）
// 语义: 先按原样尝试；失败则遍历目录做 ASCII 大小写不敏感匹配；仍失败返回原拼接路径
std::string resolveResourcePath(const std::string& dir, const std::string& name);

// [NEW] 可写数据目录回退（无原版对应）
// 依据: 原版全部存档/配置写入游戏目录（便携语义）；Linux 标准安装（/usr/share/games）
//       目录只读 → 写失败。策略（用户决策）: 游戏目录优先，不可写时回退
//       SDL_GetPrefPath("rich4","rich4")（Linux: ~/.local/share/rich4/；Windows: %APPDATA%）
// 返回: 可写目录（首个可用者）；两者都不可写时返回 gameDir（调用方 fopen 失败即报错）
const std::string& writableDataDir(const std::string& gameDir);

// [NEW] 回退数据目录（SDL_GetPrefPath；SDL 不可用时返回空串）
const std::string& fallbackDataDir();

// [NEW] 数据文件（存档/配置）读写路径：
//   writableDataFile = writableDataDir + name（写入位置固定，进程内缓存）
//   readableDataFile = 先找写入目录，再找另一处（兼容旧档/原版档），均无则返回写入路径
std::string writableDataFile(const std::string& gameDir, const std::string& name);
std::string readableDataFile(const std::string& gameDir, const std::string& name);

} // namespace rich4
