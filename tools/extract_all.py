#!/usr/bin/env python3
"""《大富翁4》资源一键解析/导出。

遍历游戏目录下所有 MKF，解压每个子资源，按魔数分类并导出:
    SPR  -> PNG 帧序列
    SMP  -> PNG 帧序列
    RIFF -> WAV
    GND  -> .gnd 原始数据
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


def classify(blob: bytes) -> str:
    if blob[:3] == b"SPR":
        return "SPR"
    if blob[:3] == b"SMP":
        return "SMP"
    if blob[:3] == b"GND":
        return "GND"
    if blob[:4] == b"RIFF":
        return "WAV"
    return "BIN"


def _gnd_note(blob: bytes) -> str:
    if len(blob) < 16:
        return ""
    gw, gh = struct.unpack_from("<HH", blob, 4)
    cnt = struct.unpack_from("<I", blob, 8)[0]
    return f"grid={gw}x{gh} count={cnt}"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="资源一键解析/导出")
    ap.add_argument("gamedir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest")
    ap.add_argument("--extract", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--rgb565", action="store_true")
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
                note = _gnd_note(blob) if kind == "GND" else ""
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
            elif kind == "WAV":
                (d / f"{e.index:04d}.wav").write_bytes(blob)
            else:
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
