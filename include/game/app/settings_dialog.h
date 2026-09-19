#pragma once

namespace rich4 {

class Application;

// [RE 0x411B53] settingsDialog（定义见 src/app/settings_dialog.cpp）
// 返回 0（取消）或 1（已保存）
int settingsDialog(Application& app, int page);

} // namespace rich4
