#pragma once

namespace rich4 {

class Application;

// [RE 0x401981] enterGameLoop
// 依据: 0x401981 压入 sub_417E26（游戏内事件处理器）并 PostMessage(0x401)；
//       a1 != 0 时调 sub_454D91(0)
// 迁移: 模态栈 + 消息泵 → runModal（EventStack）
void enterGameLoop(Application& app);

// [RE 0x417E26] renderGameFrame：游戏内整帧绘制（地图 + 高亮 + 物件提示 + 面板）
// 迁移: 原 static；供阻塞动画（收租联动闪烁 0x451985）等需要主动重绘的流程调用
void renderGameFrame(Application& app);

} // namespace rich4
