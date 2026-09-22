#pragma once

namespace rich4 {

class Application;

// [NEW] 调试热键（命令行 --debug 启用；测试辅助，无原版对应）
//   1=指向地块归当前玩家 2/3=等级±1 4=连锁店(预留) 5=全图住宅用地归当前玩家
//   6=全图有主住宅用地 level=1 7=当前玩家现金+100000 8=打印指向地块详情
//   返回 true 表示已处理（调用方需重绘）
bool handleDebugKey(Application& app, int digit);

} // namespace rich4
