#pragma once

namespace rich4 {

class Application;
struct Corp;

// [RE 0x44101D] labDevelopDialog：研究所（type4）研发道具入口。
// 依据: 0x44101D 反编译；owner 停留自己研究所（sub!=0 且 flag&0x0F==0，0x41B106/0x41B0FC）调用。
//   人类弹「請選擇欲開發道具」(0x4402D7)：panel[11] 帧 10..14 五图标（機器工人/時光機/傳送機/
//   工程車/核子飛彈），可选数 = corp.sub（等级，逐级解锁，index>=sub 变暗不可点）；
//   左键选 idx → researchItem=idx+1、researchLeft=5；右键/ESC 取消。
//   AI（alive!=1）不弹框，恒选最高解锁项 idx=sub-1（即 researchItem=sub）。
//   产出在 updatePlayerStates（0x41C84F 尾段）：倒计时归零 givePlayerItem(cur, researchItem+8)。
// 详见 docs/reverse/functions/44101d-lab-develop.md
void labDevelopDialog(Application& app, Corp& cp);

} // namespace rich4
