#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rich4 {

class Application;

// [NEW] 统一调试命令层 + headless 脚本执行器（docs/testing.md §3；无原版对应）。
// 命令注册表为单一事实源：交互热键（后续迁移）、--exec 内联、--script 脚本共用。
// 脚本步进挂在 Application::renderFrame 每帧 tick（阻塞模态内部可重入推进，
// 支持 land 弹框 → click 应答 → wait trace 断言 的交错时序）。
namespace debug {

// ---- 初始化/驱动 ----
// 注册内置命令 + 读入脚本（--script 文件 / --exec 行序列）；main 在 init 后调用
void initFromCommandLine(Application& app);
void addScriptFile(const std::string& path); // --script
void addExecLines(const std::string& lines); // --exec "a; b; c"
bool hasWork();                       // 有脚本队列
void tick(Application& app);          // 每帧调用（renderFrame 尾部）
int failures();                       // 断言失败计数（main 退出码）
std::string reportSummary();          // 控制台/JSON 摘要

// ---- 执行入口 ----
// 执行单行命令（"a; b" 分号并列）；返回 false=命令错误（计入 failures 由脚本层负责）
bool execLine(Application& app, const std::string& line, std::string& err);

// ---- 目标选择器（替代鼠标指向；debug_keys pointedObject 读取）----
void setSelectedObject(uint16_t id);
uint16_t selectedObject();

// 控件/命令点击的直投入口（脚本 click/rclick/move；key 合成）
void synthesizeClick(Application& app, int x, int y, bool right, bool downOnly = false);
void synthesizeKey(Application& app, const std::string& spec);

// ---- named region 控件层（docs/testing.md §3.4；渐进登记，产品侧一次性 registerRegion） ----
void registerRegion(const char* name, int x, int y, int w, int h);

} // namespace debug
} // namespace rich4
