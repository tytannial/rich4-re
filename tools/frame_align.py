"""帧落点像素对齐工具。

背景：SMP/SPR 帧头 (w,h,x,y,size) 的 x/y 是锚点偏移（i16），原版元素 blit 的
落点 = 实参 - offset（0x455B3A/0x455C52/0x455E24）。重写 `blitElement*` 以参数为
左上角（不应用 offset），移植时若帧 offset 非 0 或原版实参可疑，用本工具做
像素级对齐求出真实落点（方法见 docs/reverse/frame-anchor.md）。

子命令：
  offsets  打印 SPR/SMP 资源的帧表与锚点偏移
  align    在基准帧上全范围搜索候选帧的最佳落点（像素差均值最小）

用法：
  python tools/frame_align.py offsets resources/MultiverseJourney/Panel.mkf --index 65
  python tools/frame_align.py offsets Panel_0065.bin
  python tools/frame_align.py align --base f4.png --base-at 104,110 --cand f8.png
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover
    print("需要 numpy 与 Pillow：pip install numpy pillow", file=sys.stderr)
    raise


def to_signed(v: int) -> int:
    return v - 65536 if v >= 32768 else v


def read_resource(path: Path, index: int | None) -> bytes:
    if path.suffix.lower() == ".mkf":
        if index is None:
            raise SystemExit("读取 .mkf 需要 --index")
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from mkf import MKF

        m = MKF(path)
        return m.payload(m.entries[index])
    data = path.read_bytes()
    if index is not None:
        return data[index:]
    return data


def cmd_offsets(args) -> int:
    blob = read_resource(Path(args.path), args.index)
    magic = blob[:3].decode("latin1")
    if magic not in ("SPR", "SMP"):
        print(f"不是 SPR/SMP 资源（magic={magic!r}）", file=sys.stderr)
        return 1
    count, data_offset = struct.unpack_from("<II", blob, 4)
    print(f"{args.path} [{magic}] frames={count} data_offset={data_offset}")
    nonzero = 0
    for i in range(count):
        w, h, x, y, size = struct.unpack_from("<4HI", blob, 12 + i * 12)
        sx, sy = to_signed(x), to_signed(y)
        mark = " *" if (sx or sy) else ""
        if sx or sy:
            nonzero += 1
        print(f"  [{i:3}] {w:3}x{h:<3} off=({sx:4},{sy:4}) size={size}{mark}")
    print(f"非零 offset 帧: {nonzero}/{count}（* 标记）")
    return 0


def cmd_align(args) -> int:
    base = Image.open(args.base).convert("RGBA")
    base_arr = np.array(base).astype(np.int32)
    cand = Image.open(args.cand).convert("RGBA")
    cand_arr = np.array(cand).astype(np.int32)
    ch, cw = cand_arr.shape[:2]
    bx, by = args.base_at
    alpha = cand_arr[:, :, 3] > 0
    if not alpha.any():
        alpha = np.ones((ch, cw), dtype=bool)  # 不透明资源：全矩形比较

    if args.range:
        x0, x1, y0, y1 = args.range
    else:
        x0, x1 = bx - 150, bx + 150
        y0, y1 = by - 150, by + 150
    bh, bw = base_arr.shape[:2]
    results = []
    for Y in range(y0, y1 + 1):
        for X in range(x0, x1 + 1):
            px0, py0 = X - bx, Y - by
            if px0 < 0 or py0 < 0 or px0 + cw > bw or py0 + ch > bh:
                continue
            reg = base_arr[py0:py0 + ch, px0:px0 + cw]
            err = np.abs(reg[:, :, :3] - cand_arr[:, :, :3]).sum(axis=2)[alpha].mean()
            results.append((err, X, Y))
    if not results:
        print("搜索范围内无有效位置", file=sys.stderr)
        return 1
    results.sort()
    print(f"{args.cand} 相对 {args.base}(基准点 {bx},{by}) 的最佳落点：")
    for err, X, Y in results[: args.top]:
        print(f"  ({X:4},{Y:4})  err={err:.1f}")
    return 0


def _pair(s: str) -> tuple[int, int]:
    a, b = s.split(",")
    return int(a), int(b)


def _quad(s: str) -> tuple[int, int, int, int]:
    v = [int(t) for t in s.split(",")]
    if len(v) != 4:
        raise argparse.ArgumentTypeError("需要 X0,X1,Y0,Y1 四个整数")
    return v[0], v[1], v[2], v[3]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="帧落点像素对齐工具")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("offsets", help="打印 SPR/SMP 帧表锚点偏移")
    p.add_argument("path")
    p.add_argument("--index", type=int, default=None, help="MKF 资源索引")
    p.set_defaults(func=cmd_offsets)

    p = sub.add_parser("align", help="全范围搜索最佳落点")
    p.add_argument("--base", required=True, help="基准帧 PNG（如护士全身帧 4）")
    p.add_argument("--base-at", required=True, type=_pair, metavar="X,Y", help="基准帧在屏幕上的落点")
    p.add_argument("--cand", required=True, help="待对齐帧 PNG（如嘴部帧 7/8）")
    p.add_argument("--range", type=_quad, metavar="X0,X1,Y0,Y1",
                   help="搜索范围（默认基准点 ±150）")
    p.add_argument("--top", type=int, default=5, help="输出前 N 个候选（默认 5）")
    p.set_defaults(func=cmd_align)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
