#pragma once

namespace rich4 {

class Application;

// [RE 0x43D304] 監獄格停留（landingEvent case 4）：保释面板
//   （人类 0x43CAAB/0x43C8FB 头像格选择 + AI 0x43D3DF 按个性自动保释）
void jailBailDialog(Application& app);

// [RE 0x43E9A4] 醫院格停留（landingEvent case 5）：办理出院面板
//   （人类 0x43DA27/0x43D88F 多阶段消息机 + 护士微动画 + AI 0x43EAA5 按个性自动办理）
void hospitalVisitDialog(Application& app);

} // namespace rich4
