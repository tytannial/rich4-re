#pragma once

namespace rich4 {

class Application;

// [RE 0x417559] 地图物件右键提示（名称/地主/等级；框 = data.mkf[517] 帧 0）
// 依据: 0x417559 反编译; 拾取用 sub_40A9D7（重写以渲染时记录的 mapHitRegions 代替）;
//       文本分支：cellEnt 名称 / estate 地主+等级 / corp 公司+等级 / specPt 名称 /
//       evtCell 名称 / cellTable 神明名 / 玩家名
void showObjectTip(Application& app, int x, int y);
void drawObjectTip(Application& app);

} // namespace rich4
