#pragma once

namespace rich4 {

class Application;
struct Corp;

// [RE 0x44101D] labDevelopDialog：研究所（type4）研发道具入口。
// 依据: 0x44101D 反编译；触发在 landingEvent 收尾 loc_41B077 内（0x41B0B3..0x41B106）：
//   owner 自己 + state37==0 + type==4 + sub!=0 + (flag&0x0F)==0（升级询问/建设施后汇聚到此，
//   turn_system.cpp labDevelopAfterLanding）——即「先升級(加蓋)询问后研究」、首次建研究所即弹。
//   人类弹「請選擇欲開發道具」(0x4402D7)：底图 g_tipFrame 帧7 (400×89 五格) 不透明 +
//   标题框 帧5 (249×170) + panel[11] 帧 10..14 五图标（機器工人/時光機/傳送機/工程車/核子飛彈），
//   可选数 = corp.sub（等级，逐级解锁，index>=sub 图标区灰度）；悬停黄双框 + 按下下沉；
//   左键选 idx → researchItem=idx+1、researchLeft=5；右键/ESC 取消。
//   AI（alive!=1）不弹框，恒选最高解锁项 idx=sub-1（即 researchItem=sub）。
//   产出在 updatePlayerStates（0x41C84F 尾段）：倒计时归零 givePlayerItem(cur, researchItem+8)。
// 详见 docs/reverse/functions/44101d-lab-develop.md
void labDevelopDialog(Application& app, Corp& cp);

} // namespace rich4
