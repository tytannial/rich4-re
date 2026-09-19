#!/usr/bin/env python3
"""《大富翁4》SMP 高彩位图格式解析与导出。

SMP 结构 (逆向自 sub_450069 / sub_450441)::

    u32 magic          'SMP\\0' (0x00504D53)
    u32 count          条目数
    u32 data_offset    数据区偏移 (= 12 + count*12)
    entry[count] x 12B:
        u16 width
        u16 height
        u16 x
        u16 y
        u32 size          像素字节数 (= width*height*2, 16bit 直接色)
    数据区 @data_offset: 各 entry 的 16bit 像素连续排列 (无调色板)

用法:
    python smp.py info <smp.bin>
    python smp.py extract <smp.bin> <outdir> [--rgb565]
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from spr import write_png


def _to_rgb(v: int, rgb565: bool) -> tuple[int, int, int]:
    if rgb565:
        return ((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31
    return ((v >> 10) & 0x1F) * 255 // 31, ((v >> 5) & 0x1F) * 255 // 31, (v & 0x1F) * 255 // 31


class SMP:
    def __init__(self, data: bytes):
        if data[:3] != b"SMP":
            raise ValueError("not an SMP resource")
        self.data = data
        self.count, self.data_offset = struct.unpack_from("<II", data, 4)
        self.entries = [struct.unpack_from("<4HI", data, 12 + i * 12) for i in range(self.count)]
        self.pixel_base = self.data_offset

    def rgba(self, index: int, rgb565: bool = False) -> tuple[int, int, bytes]:
        w, h, _, _, _ = self.entries[index]
        start = self.pixel_base + sum(e[4] for e in self.entries[:index])
        out = bytearray(w * h * 4)
        for i in range(w * h):
            off = start + i * 2
            if off + 2 > len(self.data):
                break
            v = struct.unpack_from("<H", self.data, off)[0]
            if v == 0:
                continue
            r, g, b = _to_rgb(v, rgb565)
            o = i * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = 255
        return w, h, bytes(out)


def _cmd_info(args) -> int:
    s = SMP(Path(args.file).read_bytes())
    print(f"file        : {Path(args.file).name}")
    print(f"size        : {len(s.data)}")
    print(f"count       : {s.count}")
    print(f"data_offset : {s.data_offset} (0x{s.data_offset:X})")
    for i, (w, h, x, y, sz) in enumerate(s.entries):
        print(f"  [{i:2}] {w:3}x{h:<3} at ({x},{y}) size={sz}")
    return 0


def _cmd_extract(args) -> int:
    s = SMP(Path(args.file).read_bytes())
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stem = Path(args.file).stem
    for i in range(s.count):
        w, h, rgba = s.rgba(i, args.rgb565)
        write_png(outdir / f"{stem}_{i:03d}.png", w, h, rgba)
    print(f"exported {s.count} frames to {outdir}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="SMP 解析/导出")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("file"); p.set_defaults(func=_cmd_info)
    p = sub.add_parser("extract")
    p.add_argument("file"); p.add_argument("outdir")
    p.add_argument("--rgb565", action="store_true")
    p.set_defaults(func=_cmd_extract)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
