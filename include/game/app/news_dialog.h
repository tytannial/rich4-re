#pragma once

namespace rich4 {

class Application;

// [RE 0x44B6DF] newsEvent：新聞格落地事件（landingEvent case 2）
// 依据: 0x44B6DF 主循环（g_newsOrder 抽取 → newsEventCheck 判定 → panel.mkf[66] 画布 +
//       data.mkf[441+idx] 插画 + 标题 + g_newsFuncs[idx](0) → 2400ms → (1)）；
//       判定 0x448BE2、效果表 0x475E24 共 36 条；
//       详见 docs/reverse/functions/44b6df-news-events.md。
// 差异: 语音 sub_44EF41 / 浮动数字 sub_44F354 → TODO(P4)；idx 7 拍卖 sub_43BDE5 → TODO(拍卖)。
void newsEvent(Application& app);

// [NEW] 调试：强制触发新闻。idx < 0 = 按 g_newsOrder 抽取下一条可触发事件。
void newsDebugFire(Application& app, int idx);

} // namespace rich4
