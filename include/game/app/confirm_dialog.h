#pragma once

namespace rich4 {

class Application;

// [RE 0x453A32] confirmDialog（data.mkf[440]，YES/NO 確認框）
// 依据: 0x453A32 → sub_450441(data, 440) + 居中 (x,y) + runModal(sub_45367E);
//       sub_45367E 鼠标 x 左/右半 → 帧 1/2 高亮; 左键抬起左半→1(YES)/右半→0(NO);
//       右键抬起或取消键 → 0
// [RE 0x440BA8] askDialog 形式：text 非空时先在 (cx, 140) 居中绘制文本
//   （setTextFont(16, 0xF0F0F0, 0x101010, 3, 1)，原版另有 g_tipFrame+72 背景框待补）
// 返回: true = YES（确定）, false = NO（取消）
bool confirmDialog(Application& app, const char* text = nullptr, int cx = 320, int cy = 200);

} // namespace rich4
