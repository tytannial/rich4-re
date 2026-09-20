#pragma once

namespace rich4 {

class Application;

// [RE 0x41E345] aiDialog（託管AI / 玩家資訊界面）
// 依据: 0x41E345 → panel.mkf[77] 界面 + 存活玩家列表（头像 + 名字）;
//       runModal(sub_41DDA9) 处理悬停/选择玩家与 13 个控件
// 差异: AI 個性/資金運用比例/使用卡片道具等设置项（阶段 3）暂以文本展示
void aiDialog(Application& app);

} // namespace rich4
