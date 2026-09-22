#pragma once

#include <cstdint>

namespace rich4 {

class Application;
struct GameState;

// [RE 0x417559] 地图物件右键提示（定义见 src/app/object_tip.cpp）
// 拾取用 sub_40A9D7（重写以渲染时记录的 mapHitRegions 代替）
void showObjectTip(Application& app, int x, int y);
void drawObjectTip(Application& app);

// [RE 0x40A9D7] 地图物件拾取（返回拾取缓冲 ID，0 = 无；定义见 src/app/object_tip.cpp）
// ID 空间: cellEnt 1..N / 2000+ 住宅用地 / 4000+ 商業用地 / 6000+ 行業設施點 / 8000+ 事件格 /
//          槽号<<8 cellTable 物件 / 0xF000|玩家
uint16_t pickMapObject(const GameState& state, int x, int y);

} // namespace rich4
