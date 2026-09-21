#!/usr/bin/env python3
"""《大富翁4》资源一键解析/导出。

遍历游戏目录下所有 MKF，解压每个子资源，按魔数/结构分类并导出:
    SPR  -> PNG 帧序列
    SMP  -> PNG 帧序列
    FLC  -> PNG 帧序列 (0xAF12 帧动画, 见 docs/formats/flc.md)
    RAW  -> PNG (无头 16bit RGB555 位图, 尺寸按已知族, 见 docs/formats/raw_bitmap.md)
    RIFF -> WAV
    GND  -> .gnd 原始数据
    TEXT -> BIG5 文本 (help.mkf)
    其它 -> .bin

并可选生成资源索引 manifest.csv (mkf, index, type, size, note)。

用法:
    python extract_all.py <gamedir> --out <outdir> [--extract] [--manifest f.csv] [--only Data]
"""
from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path

from mkf import MKF
from spr import SPR, write_png
from smp import SMP
from flc import FLC

# 无头 16bit 位图族的字节数 -> (W, H)，见 docs/formats/raw_bitmap.md
RAW_DIMS = {80000: (200, 200), 194776: (388, 251), 614400: (640, 480)}


def classify(blob: bytes) -> str:
    if blob[:3] == b"SPR":
        return "SPR"
    if blob[:3] == b"SMP":
        return "SMP"
    if blob[:3] == b"GND":
        return "GND"
    if blob[:4] == b"RIFF":
        return "WAV"
    if len(blob) >= 6 and struct.unpack_from("<H", blob, 4)[0] == 0xAF12:
        return "FLC"
    return "BIN"


def refine_bin(blob: bytes, mkf_name: str) -> tuple[str, str]:
    if len(blob) == 0:
        return "EMPTY", ""
    if mkf_name.lower().startswith("help"):
        return "TEXT", ""
    if blob[:8] == b"\x00" * 8:
        return "PAD", "blank"
    if len(blob) in RAW_DIMS:
        w, h = RAW_DIMS[len(blob)]
        return "RAW", f"{w}x{h}"
    return "DATA", ""


def _gnd_note(blob: bytes) -> str:
    if len(blob) < 16:
        return ""
    gw, gh = struct.unpack_from("<HH", blob, 4)
    cnt = struct.unpack_from("<I", blob, 8)[0]
    return f"grid={gw}x{gh} count={cnt}"


def rgb555_to_rgba(blob: bytes, w: int, h: int) -> bytes:
    out = bytearray(w * h * 4)
    for i in range(w * h):
        v = struct.unpack_from("<H", blob, i * 2)[0]
        r = ((v >> 10) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x1F) * 255 // 31
        b = (v & 0x1F) * 255 // 31
        o = i * 4
        out[o] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = 255
    return bytes(out)


def _write_frame_png(d: Path, stem: str, i: int, w: int, h: int, rgb: list) -> None:
    rgba = bytearray(w * h * 4)
    for j in range(w * h):
        r, g, b = rgb[j]
        o = j * 4
        rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = b; rgba[o + 3] = 255
    write_png(d / f"{stem}_{i:03d}.png", w, h, bytes(rgba))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="资源一键解析/导出")
    ap.add_argument("gamedir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest")
    ap.add_argument("--extract", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--rgb565", action="store_true")
    ap.add_argument("--flc-frames", type=int, default=8, help="每个 FLC 导出帧数 (0=全部)")
    args = ap.parse_args(argv)

    gamedir = Path(args.gamedir)
    out = Path(args.out)
    rows: list[list] = []

    for mkf_path in sorted(gamedir.glob("*.mkf")):
        if args.only and args.only.lower() not in mkf_path.stem.lower():
            continue
        m = MKF(mkf_path)
        for e in m.entries:
            try:
                blob = m.payload(e)
                kind = classify(blob)
                note = ""
                if kind == "GND":
                    note = _gnd_note(blob)
                elif kind == "FLC":
                    f = FLC(blob)
                    note = f"{f.n_frames}f {f.width}x{f.height}"
                elif kind == "BIN":
                    kind, note = refine_bin(blob, mkf_path.name)
            except Exception as ex:  # noqa: BLE001
                rows.append([mkf_path.name, e.index, "ERR", e.decompressed_size, str(ex)])
                continue
            rows.append([mkf_path.name, e.index, kind, len(blob), note])

            if not args.extract:
                continue
            d = out / mkf_path.stem
            d.mkdir(parents=True, exist_ok=True)
            if kind == "SPR":
                s = SPR(blob)
                for i in range(s.count):
                    w, h, rgba = s.rgba(i, args.rgb565)
                    write_png(d / f"{e.index:04d}_{i:03d}.png", w, h, rgba)
            elif kind == "SMP":
                s = SMP(blob)
                for i in range(s.count):
                    w, h, rgba = s.rgba(i, args.rgb565)
                    write_png(d / f"{e.index:04d}_{i:03d}.png", w, h, rgba)
            elif kind == "FLC":
                s = FLC(blob)
                frames = s.decode()
                cnt = len(frames) if args.flc_frames <= 0 else min(args.flc_frames, len(frames))
                for i in range(cnt):
                    _write_frame_png(d, f"{e.index:04d}", i, s.width, s.height, frames[i])
            elif kind == "RAW":
                w, h = RAW_DIMS[len(blob)]
                write_png(d / f"{e.index:04d}.png", w, h, rgb555_to_rgba(blob, w, h))
            elif kind == "WAV":
                (d / f"{e.index:04d}.wav").write_bytes(blob)
            elif kind == "TEXT":
                (d / f"{e.index:04d}.txt").write_bytes(blob)
            elif kind in ("GND", "DATA", "PAD", "EMPTY"):
                (d / f"{e.index:04d}.bin").write_bytes(blob)
        print(f"done {mkf_path.name}")

    if args.manifest:
        with open(args.manifest, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["mkf", "index", "type", "size", "note"])
            w.writerows(rows)
        print(f"manifest -> {args.manifest} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
