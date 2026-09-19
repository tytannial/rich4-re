#include "game/resource/lzhuf.h"

#include <array>
#include <cstring>

namespace rich4 {

// 由 tools/gen_lzhuf_tables.py 从 rich4.exe 生成，勿手改。
extern const uint8_t kLzhufInit[4492];
extern const uint8_t kLzhufLenTable[256];
extern const uint8_t kLzhufHiTable[256];

namespace {

constexpr uint16_t kSonW = 0x0504 >> 1;   // word_484CC0
constexpr uint16_t kPrntW = 0x0A06 >> 1;  // word_4851C2
constexpr uint16_t kSymW = 0x0F08 >> 1;   // word_4856C4
constexpr uint16_t kRootOff = 0x0500;
constexpr uint16_t kLeafThreshold = 0x0502;
constexpr uint16_t kMaxFreq = 0x8000;
constexpr int kSymCount = 321;

struct Decoder {
    std::array<uint16_t, 4492 / 2> work{};
    const uint8_t* src = nullptr;
    size_t bitpos = 0;

    int bit() {
        const size_t p = bitpos++;
        return (src[p >> 3] >> (p & 7)) & 1;
    }

    int updateTree(size_t a1) {
        a1 = work[kSymW + (a1 >> 1)];
        int result = 0;
        for (int guard = 0; guard < 4096; ++guard) {
            const size_t idx = a1 >> 1;
            const uint16_t f = static_cast<uint16_t>(work[idx] + 1);
            work[idx] = f;
            result = f;
            if (f > work[idx + 1]) {
                size_t v2 = a1 + 2;
                int v3 = 642;
                const uint16_t v4 = static_cast<uint16_t>(f - 1);
                while (true) {
                    if (v3 == 0) {
                        break;
                    }
                    const bool eq = work[v2 >> 1] == v4;
                    v2 += 2;
                    --v3;
                    if (!eq) {
                        break;
                    }
                }
                const size_t v6 = v2 - 4;
                const uint16_t v7 = work[v6 >> 1];
                work[v6 >> 1] = static_cast<uint16_t>(v4 + 1);
                work[idx] = v7;

                const uint16_t v9 = work[kSonW + idx];
                const uint16_t v3v = work[kSonW + (v6 >> 1)];
                work[kPrntW + (v3v >> 1)] = static_cast<uint16_t>(a1);
                if (v3v < kLeafThreshold) {
                    work[kPrntW + (v3v >> 1) + 1] = static_cast<uint16_t>(a1);
                }
                const uint16_t v10 = v9;
                result = v3v;
                work[kPrntW + (v10 >> 1)] = static_cast<uint16_t>(v6);
                if (v10 < kLeafThreshold) {
                    work[kPrntW + (v10 >> 1) + 1] = static_cast<uint16_t>(v6);
                }
                work[kSonW + idx] = static_cast<uint16_t>(result);
                work[kSonW + (v6 >> 1)] = v10;
                a1 = v6;
            }
            a1 = work[kPrntW + (a1 >> 1)];
            if (a1 == 0) {
                break;
            }
        }
        return result;
    }

    void reconst() {
        for (int v1 = 0; v1 < kSymCount; ++v1) {
            const uint16_t v2 = work[kSymW + v1];
            if (work[v2 >> 1] & 1) {
                updateTree(static_cast<size_t>(v1) * 2);
            }
        }
        for (int i = 0; i < 641; ++i) {
            work[i] = static_cast<uint16_t>(work[i] >> 1);
        }
    }

    int decodeSymbol() {
        int v2 = kRootOff;
        uint16_t node = 0;
        for (int depth = 0; depth < 64; ++depth) {
            node = work[kSonW + (v2 >> 1)];
            if (node >= kLeafThreshold) {
                break;
            }
            v2 = node + (bit() ? 2 : 0);
        }
        const int ebx = node - kLeafThreshold;
        if (work[0x280] != kMaxFreq) {
            updateTree(static_cast<size_t>(ebx));
        } else {
            reconst();
            updateTree(static_cast<size_t>(ebx));
        }
        return ebx >> 1;
    }
};

} // namespace

std::vector<uint8_t> lzhufDecompress(const uint8_t* src, size_t srcSize, size_t outSize) {
    Decoder dec;
    dec.src = src;
    std::memcpy(dec.work.data(), kLzhufInit, sizeof(kLzhufInit));

    std::vector<uint8_t> out;
    out.reserve(outSize);
    const size_t srcBits = srcSize * 8;

    while (out.size() < outSize) {
        if (dec.bitpos > srcBits + 128 || out.size() > outSize + 8192) {
            break;
        }
        const int sym = dec.decodeSymbol();
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        const size_t pos = dec.bitpos;
        const size_t byteOff = pos >> 3;
        uint32_t bits = 0;
        for (int k = 0; k < 5 && byteOff + k < srcSize; ++k) {
            bits |= static_cast<uint32_t>(src[byteOff + k]) << (8 * k);
        }
        bits >>= (pos & 7);
        const uint8_t b = static_cast<uint8_t>(bits & 0xFF);
        const uint8_t ln = kLzhufLenTable[b];
        const uint32_t offset = ((static_cast<uint32_t>(kLzhufHiTable[b]) << 6) |
                                 ((bits >> ln) & 0x3F)) & 0xFFF;
        dec.bitpos += ln + 6;
        if (offset == 0xFFF) {
            break;
        }
        const size_t length = static_cast<size_t>(sym - 253);
        const size_t start = out.size() - 1 - offset;
        for (size_t k = 0; k < length; ++k) {
            out.push_back(out[start + k]);
        }
    }
    return out;
}

} // namespace rich4
