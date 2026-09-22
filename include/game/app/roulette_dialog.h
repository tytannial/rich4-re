#pragma once

namespace rich4 {

class Application;

// [RE 0x44090E] roulettePrompt(theme, arg)：转盘 UI（4 主题：0=出國天數/1=旅館天數/2=購物倍數/3=投保天數）
// 依据: 0x44090E → panel.mkf[(theme&3)+68]（14 帧：帧 0 底盘/帧 1 中心/帧 2..13 十二格）
//       + g_tipFrame 帧 5 @(220,140) + 文本 off_475CF8[theme]（%s = arg）+ 帧 0 @(265,230)；
//       动画 sub_43F7C6/sub_43F127：起始格 rand%12 → 指针逐格 +1（12 格循环，40ms/帧）→
//       滚动 40 帧 → 画中心 → 减速步进（间隔 1..5，每 3 步 +1）→ 落在有效格（表值≠0xFF）
//       且间隔达 5 → 停留 40 帧 → 返回 byte_475D0C[12*theme+终格]
// 音效: Effect.mkf[52]（dword_475D4C）
// 返回表值（出國天數/旅館天數/購物倍數/投保天數）
int roulettePrompt(Application& app, int theme, const char* arg);

} // namespace rich4
