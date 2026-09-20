#pragma once

namespace rich4 {

class Application;

// [RE 0x40A9BD] mapDialog（大地圖界面）
// 依据: 0x40A9BD → runModal(sub_40A801) + sub_415E70(1);
//       sub_40A801 WM_PAINT 绘制大地图（dword_48BADC 帧 1，400x400）到 (20,60)
//       + 玩家标记（g_pieceSprites 帧 6，11392/65536 缩放）; WM_RBUTTONUP → postModalExit(0)
void mapDialog(Application& app);

} // namespace rich4
