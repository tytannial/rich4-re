#pragma once

#include <cstdint>

namespace rich4 {

class Application;
class Surface;
struct GameState;

// [RE 0x40829D] 等距地图渲染
// 依据: 0x40829D 反编译; 视口中心 = 当前玩家像素坐标（centerX/centerY = -1 时用上次值）,
//       遍历 kIsoDrawOrder 绘制地面等距地块（sub_4557A1）, 再按等距表定位
//       cellEnts 物件与玩家棋子; 首次进入时随机选择出生点 cellEnt
void renderMap(Application& app, int centerX, int centerY);

// [RE 0x40E033] createMapObject（定义见 src/app/map_render.cpp）
//   a4 != 0 且 type==15（死神）→ attachObject 完整附身（原版 0x40E0CB 分支）
int createMapObject(Application& app, int type, int cellEntId, uint8_t a3, uint8_t a4);

// [RE 0x40829D] spawnPlayerAt：为未放置玩家（spriteX/Y==0）分配出生格 + 标记待入场
//   （跳伞）；调用点：renderMap 视口计算 + updateGameState 回合开始（须在 beginPlayerTurn
//   之前，原版 beginPlayerTurn 0x418C55 内部处理 dword_475114 跳伞）——返回是否新分配
bool spawnPlayerAt(GameState& state, int p);

// [RE 0x40AA6C] randomCellEnt（定义见 src/app/map_render.cpp）
int randomCellEnt(const GameState& state, int nearCellEnt);

// [PORT 0x40829D] 世界像素坐标 → 当前视口屏幕坐标（renderMap 的 projectItem 同源公式：
//   以 state.viewX/viewY 为视口中心；越出 29x29 可视格返回 false）
// [NEW M4-D] 增 Surface 参数：视口中心 x 依赖地图区逻辑宽（宽屏地图区加宽）
bool projectMapPoint(const GameState& state, int px, int py, int& sx, int& sy,
                     const Surface& dst);

// [RE 0x407AD2] 地图物件/棋子资源初始化（loadMapData 末尾段；定义见 new_game.cpp loadMap）
// 每玩家棋子动画 = data.mkf[21*charIndex + 128 + 3*travel]
void initMapEntities(Application& app);

// [RE 0x40A4E1] 重建小地图工作副本（底图 + 住宅用地/商業用地/行業設施點玩家颜色标记）
//   原版在物件变化时反复调用（loadMapData/买地/advanceDay/玩家行动等），
//   重写为小地图每次绘制时重建，保证标记随物件变化更新
void buildMiniMapMarks(Application& app);

// [RE 0x451985] 收租/新闻联动高亮：
//   captureHighlightShapes = 从当前 mapHitRegions 提取 highlightEstates/highlightCorps 的
//   原版掩码形状快照（estate 帧 0/1、corp 帧 2/3，落点 = 锚点 y-40）到
//   GameState.highlightShapes；playHighlightBlink 启动时调用一次（原版 pickBuffer 语义：
//   播放期间不随地图数据重建，拆除后的地块仍高亮）
void captureHighlightShapes(Application& app);

//   drawEstateHighlight = 对快照区域做 RGB555 亮度闪烁（每帧调用；帧推进/停留由
//   playHighlightBlink 驱动）
void drawEstateHighlight(Application& app);

} // namespace rich4
