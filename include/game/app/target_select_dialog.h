#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x446AE8 → 0x445E4D] 目标选择模态（道具/卡片选目标：sub_446AE8 = runModal 包装）
// mode 编码（WM_USER+1 lParam）：
//   低 16 位 = 筛选 flags（1=普通格/物件、2=住宅用地、4=商業用地、0x10=玩家、0x20=挂身物件、
//     0x80=地图滚动模式；0x40 特殊 → flags 强制 0xB7|0x80）
//   bit16-23 = 光标类型（cursorSelect(type,1,10) 命中时）；bit24-31 = 光标 idx-1
// 返回选中 objId（callEntity 编码：<2000 普通格、2000+ idx、0xF000|玩家、0xA100|物件槽）或 0 取消
// allowEmptyLand [USER 魔改]：额外接受**无主空地**（住宅 owner==0&&level==0 / 商业
//   owner==0&&sub==0；原版大菱形拾取覆盖 → flags=1 不选）。傳送機人物/神明/物品目的地用
//   （[HELP 94]「须移至空道路/空地」；调用方负责把 2000+/4000+ 对象 id 解析为格）
int selectTargetDialog(Application& app, int mode, bool allowEmptyLand = false);

// [RE 0x445E4D/0x40B066] 拾取命中 id（mapHitRegions 编码）→ **原版目标编码**
//   （玩家 0xF000|j → 0x8000|(1<<j)；物件 0xA100|槽+1 → 0x8000|(槽+1)<<8；其余原样）
// 用途：模态确认返回值 & AI 预选目标（dword_48BE58 语义；ai_card/ai_item 直接存
//   mapHitRegions 命中时须转换，否则效果函数按原版编码解析会取错槽/玩家）
int normalizeHitId(uint16_t v);

} // namespace rich4
