#!/usr/bin/env python3
"""《大富翁4》MKF 资源包解析器。

MKF 容器格式（已通过 help.mkf / Data.mkf 验证）::

    offset 0 : u32  index_offset   # 索引表在文件中的绝对偏移
    ...      : 资源区（各资源头 + 数据）
    index_offset : u32[N]          # 索引表, N = (filesize - index_offset) // 4
                   index[0] = 4    # 资源区起始（首个资源头偏移）
                   index[i]        # 第 i-1 个资源头的绝对偏移
                   index[N-1]      # 最后一个资源的结束偏移

资源头（16 字节，紧接其数据）::

    u32 decompressed_size
    u32 compressed_size
    u32 data_offset        # 解压后数据内需做像素格式转换的起始偏移 (v10)
    u32 data_length        # 需转换的长度 (v11)

    若 compressed_size != decompressed_size 则数据为压缩存储（LZSS+LZHUF）。

子文件数据魔数：'SPR\\0'（精灵）、'SMP\\0'（声音），其余为原始/文本数据。

用法:
    python mkf.py info   <file.mkf>
    python mkf.py list   <file.mkf>
    python mkf.py extract <file.mkf> <outdir> [--index N] [--raw]
"""
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

HEADER_SIZE = 16


@dataclass
class Entry:
    index: int
    offset: int
    decompressed_size: int
    compressed_size: int
    data_offset: int
    data_length: int
    data_file_offset: int

    @property
    def compressed(self) -> bool:
        return self.compressed_size != self.decompressed_size


class MKF:
    def __init__(self, path: str | Path, exe_path: str | Path | None = None):
        self.path = Path(path)
        self.exe_path = Path(exe_path) if exe_path else self.path.parent / "rich4.exe"
        self._decoder = None
        self.data = self.path.read_bytes()
        self.index_offset = struct.unpack_from("<I", self.data, 0)[0]
        if self.index_offset <= 0 or self.index_offset >= len(self.data):
            raise ValueError(f"无效的 MKF 索引偏移: 0x{self.index_offset:X}")
        n = (len(self.data) - self.index_offset) // 4
        self.index = list(struct.unpack_from(f"<{n}I", self.data, self.index_offset))
        self.entries = self._build_entries()

    def _build_entries(self) -> list[Entry]:
        # 索引表每一项都是资源头偏移（最后一项也是有效资源）。
        # 依据: Data.mkf 索引 602 项, index[601] 指向 614400 字节资源
        #       （原版 0x401543 sub_450441(handle, 601) 读取）；7 个 MKF 全部验证。
        entries = []
        for i in range(len(self.index)):
            off = self.index[i]
            if off + HEADER_SIZE > self.index_offset:
                break
            dec, comp, doff, dlen = struct.unpack_from("<4I", self.data, off)
            if off + HEADER_SIZE + comp > self.index_offset:
                break
            entries.append(Entry(i, off, dec, comp, doff, dlen, off + HEADER_SIZE))
        return entries

    def raw(self, e: Entry) -> bytes:
        return self.data[e.data_file_offset:e.data_file_offset + e.compressed_size]

    def payload(self, e: Entry) -> bytes:
        if not e.compressed:
            return self.raw(e)
        return self.decoder().decompress(self.raw(e), e.decompressed_size)

    def decoder(self):
        if self._decoder is None:
            import lzhuf
            self._decoder = lzhuf.LZHUF(self.exe_path)
        return self._decoder

    def magic(self, e: Entry) -> str:
        return self.data[e.data_file_offset:e.data_file_offset + 3].decode("latin1")


def _cmd_info(args) -> int:
    m = MKF(args.file)
    compressed = sum(1 for e in m.entries if e.compressed)
    print(f"file        : {m.path.name}")
    print(f"size        : {len(m.data)} (0x{len(m.data):X})")
    print(f"index_offset: 0x{m.index_offset:X}")
    print(f"index count : {len(m.index)}  -> entries {len(m.entries)}")
    print(f"compressed  : {compressed} / {len(m.entries)}")
    return 0


def _cmd_list(args) -> int:
    m = MKF(args.file)
    print(f"{'idx':>4} {'off':>8} {'dec':>9} {'comp':>9} {'C':>1} {'doff':>6} {'dlen':>9} magic")
    for e in m.entries:
        print(f"{e.index:>4} 0x{e.offset:06X} {e.decompressed_size:>9} {e.compressed_size:>9} "
              f"{'C' if e.compressed else '.':>1} {e.data_offset:>6} {e.data_length:>9} {m.magic(e)!r}")
    return 0


def _cmd_stats(args) -> int:
    from collections import Counter
    m = MKF(args.file)
    magics: Counter = Counter()
    for e in m.entries:
        raw = m.data[e.data_file_offset:e.data_file_offset + 4]
        magics[raw.hex(" ")] += 1
    print(f"file        : {m.path.name}")
    print(f"entries     : {len(m.entries)}")
    print("magic counts:")
    for mg, cnt in magics.most_common():
        ascii_repr = bytes.fromhex(mg.replace(" ", "")).decode("latin1")
        printable = "".join(c if 0x20 <= ord(c) < 0x7F else "." for c in ascii_repr)
        print(f"  {mg:<12} {printable!r:<10} x{cnt}")
    return 0


def _cmd_extract(args) -> int:
    m = MKF(args.file)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    targets = m.entries if args.index is None else [m.entries[args.index]]
    for e in targets:
        try:
            blob = m.raw(e) if args.raw else m.payload(e)
        except NotImplementedError as exc:
            print(f"[skip] {exc}", file=sys.stderr)
            continue
        suffix = ".raw" if args.raw else ".bin"
        name = f"{m.path.stem}_{e.index:04d}{suffix}"
        (outdir / name).write_bytes(blob)
        print(f"写出 {name} ({len(blob)} 字节)")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="MKF 资源包解析器")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info"); p.add_argument("file"); p.set_defaults(func=_cmd_info)
    p = sub.add_parser("list"); p.add_argument("file"); p.set_defaults(func=_cmd_list)
    p = sub.add_parser("stats"); p.add_argument("file"); p.set_defaults(func=_cmd_stats)
    p = sub.add_parser("extract")
    p.add_argument("file"); p.add_argument("outdir")
    p.add_argument("--index", type=int, default=None)
    p.add_argument("--raw", action="store_true")
    p.set_defaults(func=_cmd_extract)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
