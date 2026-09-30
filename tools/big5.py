#!/usr/bin/env python3
"""BIG5 (cp950) 编解码与字符串提取工具。

《大富翁4》游戏内码为 BIG5（繁体中文），本模块提供 cp950 <-> UTF-8 转换
以及从二进制数据中提取字符串的能力，供其它资源解析脚本复用。

用法:
    python big5.py decode <file>
    python big5.py strings <file> [--min 4] [--offset 0] [--length 0]
"""
from __future__ import annotations
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import argparse
import sys
from pathlib import Path

ENCODING = "cp950"


def decode(data: bytes) -> str:
    return data.decode(ENCODING, errors="replace")


def encode(text: str) -> bytes:
    return text.encode(ENCODING, errors="replace")


def _is_printable_byte(b: int) -> bool:
    return 0x20 <= b <= 0x7E


def _big5_char_at(data: bytes, i: int) -> tuple[str, int] | None:
    if i + 1 >= len(data):
        return None
    lead, trail = data[i], data[i + 1]
    if 0x81 <= lead <= 0xFE and (0x40 <= trail <= 0x7E or 0xA1 <= trail <= 0xFE):
        try:
            return data[i:i + 2].decode(ENCODING), 2
        except UnicodeDecodeError:
            return None
    return None


def extract_strings(data: bytes, min_len: int = 4):
    """提取 ASCII 或 BIG5 连续串，返回 (offset, text) 列表。"""
    out = []
    i, n = 0, len(data)
    while i < n:
        start = i
        chars: list[str] = []
        while i < n:
            b = data[i]
            if _is_printable_byte(b):
                chars.append(chr(b))
                i += 1
                continue
            pair = _big5_char_at(data, i)
            if pair:
                chars.append(pair[0])
                i += pair[1]
                continue
            break
        if len(chars) >= min_len:
            out.append((start, "".join(chars)))
        i = start + 1 if i == start else i
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="BIG5 (cp950) 工具")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_dec = sub.add_parser("decode", help="将文件按 BIG5 解码输出为 UTF-8")
    p_dec.add_argument("file")

    p_str = sub.add_parser("strings", help="提取 BIG5/ASCII 字符串")
    p_str.add_argument("file")
    p_str.add_argument("--min", type=int, default=4)
    p_str.add_argument("--offset", type=lambda s: int(s, 0), default=0)
    p_str.add_argument("--length", type=lambda s: int(s, 0), default=0)

    args = ap.parse_args(argv)

    if args.cmd == "decode":
        sys.stdout.buffer.write(decode(Path(args.file).read_bytes()).encode("utf-8"))
        return 0

    if args.cmd == "strings":
        data = Path(args.file).read_bytes()
        if args.offset or args.length:
            end = args.offset + args.length if args.length else len(data)
            data = data[args.offset:end]
        for off, s in extract_strings(data, args.min):
            print(f"0x{off + args.offset:06X}: {s}")
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
