#pragma once

namespace rich4 {

class Application;

// [RE 0x440AAC] selectFacilityDialog(flag)：设施选择对话框（商業用地建设/改建卡变更）
// 依据: 0x440AAC → g_tipFrame 帧 4 于 (43,279)（5 个 68x68 图标 @ x=50+68i, y=286）
//       + 帧 5 于 (220,140) + 标题「請選擇設施類別」(220,122) + runModal(sub_43FAE4)；
//       悬停 = 三层边框（word_46CAEC=0x280）+ 设施名 (220,154)；
//       左键抬起 postModalExit(i)、右键 postModalExit(-1)
// 返回 0..4（公園/旅館/購物中心/加油站/研究所），取消返回 -1
int selectFacilityDialog(Application& app, int flag = 0);

} // namespace rich4
