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

// [NEW M4-D 实机] 全屏剧场模态（fillBars）进入时的一次性背景重绘：以原版 640 布局
//   （LayoutNativeGuard）把游戏画面重绘到画布居中区（世界原点 +base）。
//   用途: 宽屏世界布局直接保留会被两侧黑边裁掉右栏（"右栏被切半"观感）——
//   背景换成完整原版画面后再填黑两侧。由 Application::dispatchModalAware 调用。
void renderModalBackdrop(Application& app, int base);

// [RE 0x45144F] 仅叠加当前事件 FLC 帧（不重绘地图；供 playEventFlc preserveScene 模式：
//   住院 524/入狱 538 过渡动画期间保持动画开始前的场景表面，对齐原版 saveBackground）
void overlayEventFlcFrame(Application& app);

// [NEW] 阻塞演出谓词：事件 FLC（含 preserveScene 冻结）/ 跳伞入场 / 待入场串行闸。
// 依据: 原版这类窗口内 PeekMessage 只取消息不派发（0x45144F/0x4528B9/0x4544F6）→
//   WM_TIMER/WM_PAINT 被丢弃，任何增量绘制都不可能在此期间发生/保留。
// 用途: ① 演出期禁止"需要控制权的增量层"（前进面板 0x417191）绘制；
//       ② enablePlayerControl（0x4196F1）不得在演出未收尾时抢跑（GO 面板演出期可见根因）。
bool blockingPerf(Application& app);

} // namespace rich4
