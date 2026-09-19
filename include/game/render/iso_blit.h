#pragma once

#include <cstdint>

namespace rich4 {

class Surface;

// [RE 0x4557A1] 等距地块四边形纹理映射
// 依据: 0x4557A1 反编译; 4 顶点四边形 + 32x32 8bit 纹理逐行扫描填充,
//       纹理索引 = vInt*32 + uInt（原版 0x4557A1 内 (u>>16)&0x1F | (v>>11)&0x3E0）,
//       像素经 g_gndPalette 取 RGB555; 0 不透明覆盖
// 重写: 原版 0x45596A 边光栅化（16.16 定点 span 缓冲）等价实现为
//       标准扫描线 + u/v 线性插值（等距地块为平行四边形，仿射映射等价）
// quad 顶点顺序: (col,row)→(col+1,row)→(col+1,row+1)→(col,row+1)
// 纹理坐标对应: (0,0)→(32,0)→(32,32)→(0,32)
void isoBlitQuad(Surface& dst, const uint8_t* tile, const int16_t quad[4][2],
                 const uint16_t* palette, bool useClipRect);

} // namespace rich4
