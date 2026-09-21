#pragma once

namespace rich4 {

class Application;

// [RE 0x40829D] 等距地图渲染
// 依据: 0x40829D 反编译; 视口中心 = 当前玩家像素坐标（centerX/centerY = -1 时用上次值）,
//       遍历 kIsoDrawOrder 绘制地面等距地块（sub_4557A1）, 再按等距表定位
//       cellEnts 物件与玩家棋子; 首次进入时随机选择出生点 cellEnt
void renderMap(Application& app, int centerX, int centerY);

// [RE 0x407AD2] 地图物件/棋子资源初始化（loadMapData 末尾段；定义见 new_game.cpp loadMap）
// 每玩家棋子动画 = data.mkf[21*charIndex + 128 + 3*travel]
void initMapEntities(Application& app);

// [RE 0x40A4E1] 重建小地图工作副本（底图 + 地产/公司/特殊点玩家颜色标记）
//   原版在物件变化时反复调用（loadMapData/买地/advanceDay/玩家行动等），
//   重写为小地图每次绘制时重建，保证标记随物件变化更新
void buildMiniMapMarks(Application& app);

} // namespace rich4
