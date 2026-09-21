#!/usr/bin/env python3
"""《大富翁4》FLC 帧动画解析与导出。

FLC 结构 (逆向自 sub_450CED 头解析 / sub_450F04 帧与 chunk 解码 /
sub_450555·sub_45059A·sub_450894·sub_450A9D·sub_450C35 各 chunk 处理器)::

    文件头 128B:
        u32 size
        u16 magic (=0xAF12)      仅支持 FLC，不认 0xAF11/FLI
        u16 n_frames
        u16 width
        u16 height
        u16 depth (=8)
        u16 flags
        u32 speed                帧间隔(ms)
    帧数据 @128，逐帧 [u32 frame_size] 串联:
        帧头: u32 size / u16 magic(0xF1FA; 首帧可能 0xF100 占位被跳过) /
              u16 n_chunks / u16 max_extra / u16 rsv / u32 next
        子 chunk: u32 size(含头) / u16 type，payload=size-6
            0x0004 调色板 delta (sub_450555): u16 n_pairs; 每对 u8 skip,u8 cnt(0->256),
                   cnt*3 字节 8bit RGB; 索引累加
            0x0007 全帧 BRUN (sub_45059A): u16 n_rows; 行首 u16 高2位=0xC000 为跳行;
                   否则包数; 每包 u8 skip,u8 cmd: cmd>=0x80 run(256-cmd 个 dword=交替双色),
                   else literal(2*cmd 像素)
            0x000C 局部 BRUN (sub_450894): u16 first_row,u16 n_rows; 每行 u8 包数;
                   每包 u8 skip,u8 cmd: cmd>=0x80 run(256-cmd 单色像素), else literal(cmd 像素)
            0x000F 全帧 RLE (sub_450A9D): 每行 1 前导字节; cmd<=0x80 run(cmd,1索引),
                   else literal(256-cmd)
            0x0010 全帧字面 (sub_450C35): width*height 字节 8bit 索引
    像素为 8bit 索引，经 8bit RGB 调色板展开。调色板为 **8bit 直接值**(0-255)，
    非 FLIC 标准 6bit，勿做 *4/<<2 扩展。

用法:
    python flc.py info   <flc.bin>
    python flc.py extract <flc.bin> <outdir> [--frames N] [--all]
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from spr import write_png

FLC_MAGIC = 0xAF12
FRAME_MAGIC = 0xF1FA
FRAME_MAGIC_FL = 0xF100


class FLC:
    def __init__(self, data: bytes):
        if len(data) < 128 or struct.unpack_from("<H", data, 4)[0] != FLC_MAGIC:
            raise ValueError("not an FLC resource")
        self.data = data
        self.n_frames, self.width, self.height = struct.unpack_from("<HHH", data, 6)
        self.depth, self.flags, self.speed = struct.unpack_from("<HHI", data, 12)

    def _frame_offsets(self) -> list[int]:
        offs = []
        pos = 128
        n = 0
        while pos + 16 <= len(self.data) and n < self.n_frames:
            csz, fmag = struct.unpack_from("<IH", self.data, pos)
            if csz <= 0:
                break
            if fmag == FRAME_MAGIC:
                offs.append(pos)
            pos += csz
            n += 1
        return offs

    def decode(self) -> list[list[tuple[int, int, int]]]:
        """返回每帧的 RGB 像素列表 (width*height)。逐帧累积。"""
        w, h = self.width, self.height
        pal: list[tuple[int, int, int]] = [(0, 0, 0)] * 256
        buf = [(0, 0, 0)] * (w * h)
        frames: list[list[tuple[int, int, int]]] = []
        for pos in self._frame_offsets():
            csz, _, nchunks = struct.unpack_from("<IHH", self.data, pos)
            cpos = pos + 16
            for _ in range(nchunks):
                if cpos + 6 > pos + csz or cpos + 6 >= len(self.data):
                    break
                sub_sz, st = struct.unpack_from("<IH", self.data, cpos)
                if sub_sz <= 6:
                    break
                self._chunk(st, cpos + 6, min(cpos + sub_sz, len(self.data)), pal, buf, w, h)
                cpos += sub_sz
            frames.append(list(buf))
        return frames

    def _chunk(self, st, p, end, pal, buf, w, h):
        d = self.data
        if st == 0x0004:
            npairs = struct.unpack_from("<H", d, p)[0]; p += 2
            idx = 0
            for _ in range(npairs):
                skip = d[p]; cnt = d[p + 1]; p += 2
                if cnt == 0:
                    cnt = 256
                idx += skip
                for _c in range(cnt):
                    pal[idx & 255] = (d[p], d[p + 1], d[p + 2]); p += 3; idx += 1
        elif st == 0x0007:
            nrows = struct.unpack_from("<H", d, p)[0]; p += 2
            r = 0
            for _row in range(nrows):
                while True:
                    v19 = struct.unpack_from("<h", d, p)[0]; p += 2
                    if (v19 & 0xC000) != 0xC000:
                        break
                    r += 0x4000 - (v19 & 0x3FFF)
                x = 0
                for _k in range(v19):
                    skip = d[p]; cmd = d[p + 1]; p += 2
                    x += skip
                    if cmd >= 0x80:
                        cnt = 256 - cmd
                        c0 = pal[d[p]]; c1 = pal[d[p + 1]]; p += 2
                        for _j in range(cnt):
                            for cc in (c0, c1):
                                if x < w and r < h:
                                    buf[r * w + x] = cc
                                x += 1
                    else:
                        for _j in range(2 * cmd):
                            if x < w and r < h:
                                buf[r * w + x] = pal[d[p]]
                            p += 1; x += 1
                r += 1
        elif st == 0x000C:
            firstrow, nrows = struct.unpack_from("<HH", d, p); p += 4
            for rr in range(nrows):
                r = firstrow + rr
                npack = d[p]; p += 1
                x = 0
                for _k in range(npack):
                    skip = d[p]; cmd = d[p + 1]; p += 2
                    x += skip
                    if cmd >= 0x80:
                        cnt = 256 - cmd; val = pal[d[p]]; p += 1
                        for _j in range(cnt):
                            if x < w and 0 <= r < h:
                                buf[r * w + x] = val
                            x += 1
                    else:
                        for _j in range(cmd):
                            if x < w and 0 <= r < h:
                                buf[r * w + x] = pal[d[p]]
                            p += 1; x += 1
        elif st == 0x000F:
            for row in range(h):
                p += 1
                x = 0
                while x < w and p < end:
                    cmd = d[p]; p += 1
                    if cmd <= 0x80:
                        val = pal[d[p]]; p += 1
                        for _n in range(cmd):
                            if x < w:
                                buf[row * w + x] = val
                            x += 1
                    else:
                        for _n in range(256 - cmd):
                            if x < w:
                                buf[row * w + x] = pal[d[p]]
                            p += 1; x += 1
        elif st == 0x0010:
            for row in range(h):
                for x in range(w):
                    buf[row * w + x] = pal[d[p]]; p += 1


def _cmd_info(args) -> int:
    f = FLC(Path(args.file).read_bytes())
    offs = f._frame_offsets()
    print(f"file       : {Path(args.file).name}")
    print(f"size       : {len(f.data)}")
    print(f"frames     : {f.n_frames} (解码帧 {len(offs)})")
    print(f"dimension  : {f.width}x{f.height} depth={f.depth}")
    print(f"speed      : {f.speed} ms")
    return 0


def _cmd_extract(args) -> int:
    f = FLC(Path(args.file).read_bytes())
    frames = f.decode()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stem = Path(args.file).stem
    w, h = f.width, f.height
    n = len(frames)
    idxs = range(n) if args.all else range(min(args.frames, n))
    for i in idxs:
        rgba = bytearray(w * h * 4)
        px = frames[i]
        for j in range(w * h):
            r, g, b = px[j]
            o = j * 4
            rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = b; rgba[o + 3] = 255
        write_png(outdir / f"{stem}_{i:03d}.png", w, h, bytes(rgba))
    print(f"exported {len(list(idxs)) if args.all else min(args.frames, n)}/{n} frames -> {outdir}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="FLC 帧动画解析/导出")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("file"); p.set_defaults(func=_cmd_info)
    p = sub.add_parser("extract")
    p.add_argument("file"); p.add_argument("outdir")
    p.add_argument("--frames", type=int, default=8)
    p.add_argument("--all", action="store_true")
    p.set_defaults(func=_cmd_extract)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
