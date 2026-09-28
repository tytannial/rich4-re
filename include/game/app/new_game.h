#pragma once

namespace rich4 {

class Application;

// 新游戏/读档后进入游戏的初始化链路（对应 WinMain case 0/1/4 分支）。
// 0x406DE7 新游戏初始化 → 0x401543 加载画面 → 0x407AD2 地图数据
// → 0x4190CF 面板资源 → 0x4291D6 股票 → 0x415872 开场 → 0x401981 游戏循环
// 返回 false 表示初始化失败（保持主菜单）。
bool startGame(Application& app, bool mode1);

// [RE 0x402AC5] loadGameFromSlot
// 依据: 0x402AC5 反编译; showLoadingScreen + fseek(4) 读取全部存档状态 +
//       loadMapData + 每玩家棋盘数据（10008B + 地图数据）; 由读档对话框返回后调用
bool loadGameFromSlot(Application& app, int slot);

// [RE 0x417D65 case 3] 游戏内读档：重载存档数据/地图/回合机但**不重入游戏内循环**
//   （主菜单读档经 loadGameFromSlot→enterGameLoop；游戏内 case3 复用本函数后由现有
//   gameEventHandler 模态继续渲染新局）。loadGameFromSlot = 本函数 + enterGameLoop。
bool reloadGameFromSlotData(Application& app, int slot);

} // namespace rich4
