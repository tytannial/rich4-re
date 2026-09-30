#include "game/render/fli.h"

#include <cstring>
#include <utility>

#include "game/core/log.h"

namespace rich4 {

namespace {

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

// RGB888 → RGB555（与原版 dword_48A08C 表面一致）
uint16_t rgbTo555(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

} // namespace

bool FliDecoder::open(std::vector<uint8_t> data) {
    if (data.size() < 128) {
        return false;
    }
    m_data = std::move(data);
    const uint8_t* h = m_data.data();
    if (readU16(h + 4) != 0xAF12) {
        RICH4_LOGW("FliDecoder: bad magic 0x%04X (RE 0x450CED)", readU16(h + 4));
        return false;
    }
    m_frameCount = readU16(h + 6);
    m_width = readU16(h + 8);
    m_height = readU16(h + 10);
    m_speed = static_cast<int>(readU32(h + 16));
    if (m_speed <= 0) {
        m_speed = 100;
    }
    if (m_width <= 0 || m_height <= 0 || m_frameCount <= 0) {
        return false;
    }
    m_pixels.assign(static_cast<size_t>(m_width) * m_height, 0);
    m_palette.assign(256 * 3, 0);
    m_frame = 0;
    m_nextFrameOffset = 128; // 头 128 字节
    m_valid = true;
    RICH4_LOGI("FliDecoder: %dx%d frames=%d speed=%dms (RE 0x450CED)", m_width, m_height,
               m_frameCount, m_speed);
    return true;
}

void FliDecoder::rewind() {
    m_frame = 0;
    m_nextFrameOffset = 128;
}

uint16_t FliDecoder::paletteColor(uint8_t idx) const {
    // 调色板为 8bit RGB 直接值（实测 0x04 chunk 字节范围 0-255，非 FLIC 标准 6bit）
    return rgbTo555(m_palette[static_cast<size_t>(idx) * 3],
                    m_palette[static_cast<size_t>(idx) * 3 + 1],
                    m_palette[static_cast<size_t>(idx) * 3 + 2]);
}

void FliDecoder::setPixel(int x, int y, uint8_t idx) {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
        return;
    }
    m_pixels[static_cast<size_t>(y) * m_width + x] = paletteColor(idx);
}

bool FliDecoder::decodeChunk(const uint8_t* p, int size) {
    // 子 chunk: size(4)@+0, type(2)@+4, payload@+6
    const uint16_t type = readU16(p + 4);
    const uint8_t* d = p + 6;
    const uint8_t* end = p + size;
    const int dataSize = size - 6;
    if (dataSize <= 0) {
        return true;
    }
    switch (type) {
        // [RE 0x450555] 0x0004 调色板 delta: u16 nPairs; 每对 u8 skip,u8 cnt(0->256), cnt*3 字节 8bit RGB
        case 0x0004: {
            const int packets = readU16(d);
            const uint8_t* q = d + 2;
            int idx = 0;
            for (int i = 0; i < packets; ++i) {
                if (q + 1 > end) {
                    break;
                }
                idx += *q++;
                int count = *q++;
                if (count == 0) {
                    count = 256;
                }
                for (int j = 0; j < count && q + 3 <= end; ++j) {
                    if (idx < 256) {
                        m_palette[static_cast<size_t>(idx) * 3] = q[0];
                        m_palette[static_cast<size_t>(idx) * 3 + 1] = q[1];
                        m_palette[static_cast<size_t>(idx) * 3 + 2] = q[2];
                    }
                    q += 3;
                    ++idx;
                }
            }
            break;
        }
        // [RE 0x45059A] 0x0007 全帧 BRUN: u16 nRows; 行首 u16 高2位=0xC000 为跳行(仅移目标行);
        //   否则为包数; 每包 u8 skip,u8 cmd: cmd>=0x80 run(256-cmd 个 dword=交替双色),
        //   else literal(2*cmd 像素)
        case 0x0007: {
            const int nRows = readU16(d);
            const uint8_t* q = d + 2;
            int r = 0;
            for (int row = 0; row < nRows; ++row) {
                int npackets = 0;
                for (;;) {
                    if (q + 2 > end) {
                        return true;
                    }
                    const uint16_t v = readU16(q);
                    q += 2;
                    if ((v & 0xC000) != 0xC000) {
                        npackets = v;
                        break;
                    }
                    r += 0x4000 - (v & 0x3FFF); // 跳过目标行
                }
                int x = 0;
                for (int k = 0; k < npackets; ++k) {
                    if (q + 2 > end) {
                        break;
                    }
                    const int skip = q[0];
                    const int cmd = q[1];
                    q += 2;
                    x += skip;
                    if (cmd >= 0x80) {
                        if (q + 2 > end) {
                            break;
                        }
                        const uint8_t i0 = q[0];
                        const uint8_t i1 = q[1];
                        q += 2;
                        for (int j = 0; j < 256 - cmd; ++j) {
                            setPixel(x++, r, i0);
                            setPixel(x++, r, i1);
                        }
                    } else {
                        for (int j = 0; j < 2 * cmd; ++j) {
                            if (q >= end) {
                                break;
                            }
                            setPixel(x++, r, *q++);
                        }
                    }
                }
                ++r;
            }
            break;
        }
        // [RE 0x450894] 0x000C 局部 BRUN: u16 firstRow,u16 nRows; 每行 u8 包数;
        //   每包 u8 skip,u8 cmd: cmd>=0x80 run(256-cmd 单色像素), else literal(cmd 像素)
        case 0x000C: {
            const int firstRow = readU16(d);
            const int nRows = readU16(d + 2);
            const uint8_t* q = d + 4;
            for (int rr = 0; rr < nRows; ++rr) {
                const int r = firstRow + rr;
                if (q + 1 > end) {
                    break;
                }
                const int npackets = *q++;
                int x = 0;
                for (int k = 0; k < npackets; ++k) {
                    if (q + 2 > end) {
                        break;
                    }
                    const int skip = q[0];
                    const int cmd = q[1];
                    q += 2;
                    x += skip;
                    if (cmd >= 0x80) {
                        if (q >= end) {
                            break;
                        }
                        const uint8_t idx = *q++;
                        for (int j = 0; j < 256 - cmd; ++j) {
                            setPixel(x++, r, idx);
                        }
                    } else {
                        for (int j = 0; j < cmd; ++j) {
                            if (q >= end) {
                                break;
                            }
                            setPixel(x++, r, *q++);
                        }
                    }
                }
            }
            break;
        }
        // [RE 0x450A9D] 0x000F 全帧 RLE: 每行 1 前导字节; cmd<=0x80 run(cmd,1索引),
        //   else literal(256-cmd)
        case 0x000F: {
            const uint8_t* q = d;
            for (int r = 0; r < m_height; ++r) {
                ++q; // 每行前导字节
                int x = 0;
                while (x < m_width && q < end) {
                    const int cmd = *q++;
                    if (cmd <= 0x80) {
                        if (q >= end) {
                            break;
                        }
                        const uint8_t idx = *q++;
                        for (int j = 0; j < cmd; ++j) {
                            setPixel(x++, r, idx);
                        }
                    } else {
                        for (int j = 0; j < 256 - cmd; ++j) {
                            if (q >= end) {
                                break;
                            }
                            setPixel(x++, r, *q++);
                        }
                    }
                }
            }
            break;
        }
        // [RE 0x450C35] 0x0010 全帧字面: width*height 字节 8bit 索引
        case 0x0010: {
            const uint8_t* q = d;
            for (int r = 0; r < m_height; ++r) {
                for (int x = 0; x < m_width; ++x) {
                    if (q >= end) {
                        return true;
                    }
                    setPixel(x, r, *q++);
                }
            }
            break;
        }
        default:
            break; // 原版忽略未知 chunk（如 0x0012）
    }
    return true;
}

bool FliDecoder::nextFrame() {
    // 用声明帧数 m_frameCount 封顶：部分 FLC 数据尾部含超出 frameCount 的 chunk
    //   （回绕/占位，解码即得第 0 帧内容），若只靠数据末尾判断会多播一帧，
    //   导致骰子滚动末尾闪出第 0 帧（0004_000）。原版 sub_450F04 按帧数停。
    if (!m_valid || m_frame >= m_frameCount) {
        return false;
    }
    const uint8_t* base = m_data.data();
    const size_t total = m_data.size();
    size_t off = static_cast<size_t>(m_nextFrameOffset);
    // [RE 0x450F04] do-while: 跳过非 0xF1FA 帧（首帧常为 0xF100 占位）
    for (;;) {
        if (off + 16 > total) {
            rewind();
            return false;
        }
        const uint32_t fs = readU32(base + off);
        if (readU16(base + off + 4) == 0xF1FA && fs >= 16) {
            break;
        }
        if (fs == 0) {
            rewind();
            return false;
        }
        off += fs;
    }
    const uint32_t frameSize = readU32(base + off);
    const int chunks = readU16(base + off + 6);
    size_t p = off + 16;
    const size_t frameEnd = off + frameSize;
    for (int i = 0; i < chunks && p + 6 <= frameEnd && p + 6 <= total; ++i) {
        const uint32_t chunkSize = readU32(base + p);
        if (chunkSize < 6 || p + chunkSize > total) {
            break;
        }
        decodeChunk(base + p, static_cast<int>(chunkSize));
        p += chunkSize;
    }
    m_nextFrameOffset = static_cast<int>(frameEnd);
    ++m_frame;
    return true;
}

} // namespace rich4
