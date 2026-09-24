#pragma once

namespace rich4 {

class Application;

// [RE 0x446AE8 → 0x445E4D] 目标选择模态（道具/卡片选目标：sub_446AE8 = runModal 包装）
// mode 编码（WM_USER+1 lParam）：
//   低 16 位 = 筛选 flags（1=普通格/物件、2=住宅用地、4=商業用地、0x10=玩家、0x20=挂身物件、
//     0x80=地图滚动模式；0x40 特殊 → flags 强制 0xB7|0x80）
//   bit16-23 = 光标类型（cursorSelect(type,1,10) 命中时）；bit24-31 = 光标 idx-1
// 返回选中 objId（callEntity 编码：<2000 普通格、2000+ idx、0xF000|玩家、0xA100|物件槽）或 0 取消
int selectTargetDialog(Application& app, int mode);

} // namespace rich4
