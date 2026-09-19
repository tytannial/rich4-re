#!/usr/bin/env python3
"""《大富翁4》SPR 精灵格式解析与导出。

SPR 结构 (逆向自 sub_450069 / sub_450441)::

    u32 magic          'SPR\\0' (0x00525053)
    u32 count          条目数
    u32 data_offset    数据区偏移 (= 12 + count*12)
    entry[count] x 12B:
        u16 width
        u16 height
        u16 x             屏幕放置坐标
        u16 y
        u32 size          像素数据字节数 (= width*height, 8bit 索引)
    数据区 @data_offset:
        512B  调色板 (256 x u16, 默认 RGB555)
        ...   各 entry 的 8bit 索引像素, 按条目顺序连续排列

像素索引 0 视为透明。用法:
    python spr.py info <spr.bin>
    python spr.py extract <spr.bin> <outdir> [--rgb565]
"""
from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

PALETTE_SIZE = 512


def write_png(path: str | Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def palette_to_rgb(palette: tuple, rgb565: bool) -> list[tuple[int, int, int]]:
    out = []
    for v in palette:
        if rgb565:
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            out.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
        else:
            r = (v >> 10) & 0x1F
            g = (v >> 5) & 0x1F
            b = v & 0x1F
            out.append((r * 255 // 31, g * 255 // 31, b * 255 // 31))
    return out


class SPR:
    def __init__(self, data: bytes):
        if data[:3] != b"SPR":
            raise ValueError("not an SPR resource")
        self.data = data
        self.count, self.data_offset = struct.unpack_from("<II", data, 4)
        self.entries = [struct.unpack_from("<4HI", data, 12 + i * 12) for i in range(self.count)]
        self.palette = struct.unpack_from("<256H", data, self.data_offset)
        self.pixel_base = self.data_offset + PALETTE_SIZE

    def pixels(self, index: int) -> bytes:
        start = self.pixel_base + sum(e[4] for e in self.entries[:index])
        return self.data[start:start + self.entries[index][4]]

    def rgba(self, index: int, rgb565: bool = False) -> tuple[int, int, int, bytes]:
        w, h, _, _, _ = self.entries[index]
        rgb = palette_to_rgb(self.palette, rgb565)
        pix = self.pixels(index)
        out = bytearray(w * h * 4)
        for i in range(w * h):
            idx = pix[i] if i < len(pix) else 0
            if idx == 0:
                continue
            r, g, b = rgb[idx]
            o = i * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = 255
        return w, h, bytes(out)


def _cmd_info(args) -> int:
    s = SPR(Path(args.file).read_bytes())
    print(f"file        : {Path(args.file).name}")
    print(f"size        : {len(s.data)}")
    print(f"count       : {s.count}")
    print(f"data_offset : {s.data_offset} (0x{s.data_offset:X})")
    for i, (w, h, x, y, sz) in enumerate(s.entries):
        print(f"  [{i:2}] {w:3}x{h:<3} at ({x},{y}) size={sz}")
    return 0


def _cmd_extract(args) -> int:
    s = SPR(Path(args.file).read_bytes())
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stem = Path(args.file).stem
    for i in range(s.count):
        w, h, _, _, _ = s.entries[i]
        _, _, rgba = s.rgba(i, args.rgb565)
        write_png(outdir / f"{stem}_{i:03d}.png", w, h, rgba)
    print(f"exported {s.count} frames to {outdir}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="SPR 解析/导出")
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
