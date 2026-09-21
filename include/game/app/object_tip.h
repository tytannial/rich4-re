#pragma once

namespace rich4 {

class Application;

// [RE 0x417559] 地图物件右键提示（定义见 src/app/object_tip.cpp）
// 拾取用 sub_40A9D7（重写以渲染时记录的 mapHitRegions 代替）
void showObjectTip(Application& app, int x, int y);
void drawObjectTip(Application& app);

} // namespace rich4
