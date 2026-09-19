#!/usr/bin/env python3
"""《大富翁4》MKF 压缩资源解码器 (LZHUF 变体)。

算法 = 自适应哈夫曼(LZHUF) + LZSS。逆向自 rich4.exe:
    sub_455040  主解压循环
    sub_4551BB  哈夫曼解码 (从根 0x500 遍历 son 树)
    sub_455109  更新入口 (根频率达 0x8000 时先频率减半重建)
    sub_45511B  自适应哈夫曼树更新
    sub_4550CC  频率减半重建

运行时工作区 (init 数据来自 exe DGROUP unk_483630, 复制到 word_4847BC)::

    base 0x4847BC : freq[642]  (int16, 末项 0xFFFF 哨兵)
    base 0x484CC0 : son[641]   (int16, 值均为字节偏移; >=0x502 为叶子)
    base 0x4851C2 : prnt[641]  (int16)
    base 0x4856C4 : sym2node[321]

位流为 LSB-first。哈夫曼符号:
    0..255   字面量
    256..320 匹配, length = symbol - 253 (3..67)
匹配 offset 为可变长前缀码 (表 byte_483530=码长, byte_483430=高6位) + 6 位低位,
共 12 位, 0xFFF 为流结束标记。
"""
from __future__ import annotations

from array import array
from pathlib import Path

DGROUP_RAW = 0x61600
DGROUP_VA = 0x463000

INIT_VA = 0x483630
INIT_LEN = 0x118C          # 4492

SON_W = 0x0504 >> 1        # word_484CC0, word index base
PRNT_W = 0x0A06 >> 1       # word_4851C2
SYM_W = 0x0F08 >> 1        # word_4856C4
ROOT_OFF = 0x0500
LEAF_THRESHOLD = 0x0502
MAX_FREQ = 0x8000
SYM_COUNT = 321

LEN_TABLE_VA = 0x483530
HI_TABLE_VA = 0x483430


def _va_to_raw(va: int) -> int:
    return DGROUP_RAW + (va - DGROUP_VA)


class LZHUF:
    def __init__(self, exe_path: str | Path):
        exe = Path(exe_path).read_bytes()
        raw = _va_to_raw(INIT_VA)
        self.init = array("H")
        self.init.frombytes(exe[raw:raw + INIT_LEN])
        self.len_table = exe[_va_to_raw(LEN_TABLE_VA):_va_to_raw(LEN_TABLE_VA) + 256]
        self.hi_table = exe[_va_to_raw(HI_TABLE_VA):_va_to_raw(HI_TABLE_VA) + 256]

    def _update_tree(self, w: array, a1: int) -> int:
        S, P, Y = SON_W, PRNT_W, SYM_W
        a1 = w[Y + (a1 >> 1)]
        result = 0
        for _ in range(4096):
            idx = a1 >> 1
            f = (w[idx] + 1) & 0xFFFF
            w[idx] = f
            result = f
            if f > w[idx + 1]:
                v2 = a1 + 2
                v3 = 642
                v4 = f - 1
                while True:
                    if v3 == 0:
                        break
                    eq = w[v2 >> 1] == v4
                    v2 += 2
                    v3 -= 1
                    if not eq:
                        break
                v6 = v2 - 4
                v7 = w[v6 >> 1]
                w[v6 >> 1] = v4 + 1
                w[idx] = v7
                v9 = w[S + idx]
                v3v = w[S + (v6 >> 1)]
                w[P + (v3v >> 1)] = a1
                if v3v < LEAF_THRESHOLD:
                    w[P + (v3v >> 1) + 1] = a1
                v10 = v9
                result = v3v
                w[P + (v10 >> 1)] = v6
                if v10 < LEAF_THRESHOLD:
                    w[P + (v10 >> 1) + 1] = v6
                w[S + idx] = result
                w[S + (v6 >> 1)] = v10
                a1 = v6
            a1 = w[P + (a1 >> 1)]
            if a1 == 0:
                break
        else:
            raise ValueError("update_tree loop exceeded")
        return result

    def _reconst(self, w: array) -> None:
        for v1 in range(SYM_COUNT):
            v2 = w[SYM_W + v1]
            if w[v2 >> 1] & 1:
                self._update_tree(w, v1 * 2)
        for i in range(641):
            w[i] >>= 1

    def decompress(self, src: bytes, out_size: int) -> bytes:
        w = array("H", self.init)
        S, P = SON_W, PRNT_W
        len_table = self.len_table
        hi_table = self.hi_table
        bitpos = 0
        nbits = len(src) << 3
        out = bytearray()
        ap = out.append
        limit = out_size + 8192
        while len(out) < out_size:
            if bitpos > nbits + 128 or len(out) > limit:
                raise ValueError(f"decompress runaway out={len(out)} bits={bitpos}")
            v2 = ROOT_OFF
            for _ in range(64):
                node = w[S + (v2 >> 1)]
                if node >= LEAF_THRESHOLD:
                    break
                bit = (src[bitpos >> 3] >> (bitpos & 7)) & 1
                bitpos += 1
                v2 = node + (2 if bit else 0)
            else:
                raise ValueError("huffman depth exceeded")
            ebx = node - LEAF_THRESHOLD
            if w[0x280] != MAX_FREQ:
                self._update_tree(w, ebx)
            else:
                self._reconst(w)
                self._update_tree(w, ebx)
            sym = ebx >> 1
            if sym < 256:
                ap(sym)
                continue
            byteoff = bitpos >> 3
            shift = bitpos & 7
            chunk = src[byteoff:byteoff + 5]
            if len(chunk) < 5:
                chunk += b"\x00" * (5 - len(chunk))
            bits = (int.from_bytes(chunk, "little") >> shift) & 0xFFFFFFFF
            b = bits & 0xFF
            ln = len_table[b]
            offset = ((hi_table[b] << 6) | ((bits >> ln) & 0x3F)) & 0xFFF
            bitpos += ln + 6
            if offset == 0xFFF:
                break
            length = sym - 253
            start = len(out) - 1 - offset
            for k in range(length):
                ap(out[start + k])
        return bytes(out)


def _main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="LZHUF 解压测试")
    ap.add_argument("exe")
    ap.add_argument("compressed")
    ap.add_argument("out_size", type=lambda s: int(s, 0))
    ap.add_argument("out")
    args = ap.parse_args()

    dec = LZHUF(args.exe)
    blob = Path(args.compressed).read_bytes()
    data = dec.decompress(blob, args.out_size)
    Path(args.out).write_bytes(data)
    print(f"in={len(blob)} out={len(data)} (expect {args.out_size})")
    print("head:", data[:32].hex(" "))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
