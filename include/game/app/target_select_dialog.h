#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x446AE8 → 0x445E4D] 目标选择模态（道具/卡片选目标：sub_446AE8 = runModal 包装）
// mode 编码（WM_USER+1 lParam，按**字节**定义，2026-10-05 IDA 复核订正）：
//   byte0  = 筛选 flags（1=普通格/物件、2=住宅用地、4=商業用地、0x10=玩家、0x20=挂身物件；
//            0x40 宏 → flags = (原 flags & 0x80) | 0x37 且清 BYTE1——**单独 0x40 不能滚屏**；
//            0x80 = 地图滚动模式，**仅 3 个调用点带**：飛彈 0x300C0 / 核子飛彈 0x400C0 /
//            房地產公司 0x2090086）
//   byte1  = BYTE1 高字节过滤（case 1..8；非 0 时取代低 flags 判定，原版 v14 清 0）
//   byte2  = cursorSelect index（命中时光标类型；0x445E4D 内 cursorSelect(x,1,10)）
//   byte3  = frameCount-1（命中光标动画帧数，0x445EC1 dword_48C58C=(mode>>24)+1）
// 无半径/距离/同路段射程——人类选目标唯一空间判据 = 目标锚点落在当前 440×440 拾取缓冲内
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
