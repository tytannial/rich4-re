#pragma once

namespace rich4 {

class Application;

// [RE 0x41E345] aiDialog（託管AI 配置面板，定义见 src/app/ai_dialog.cpp）
// 槽列表只含人类玩家（alive&1）；再次点击选中玩家切换托管；確定写回 AI 配置
void aiDialog(Application& app);

} // namespace rich4
