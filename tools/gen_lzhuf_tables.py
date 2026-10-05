#!/usr/bin/env python3
"""从 rich4.exe 提取 LZHUF 静态表，生成 src/gen/lzhuf_tables.cpp。

表位置（DGROUP 段）:
    unk_483630 : 4492 字节自适应树初始工作区 (freq/son/prnt/sym2node)
    byte_483530: 256 字节匹配偏移码长表
    byte_483430: 256 字节匹配偏移高 6 位表

用法:
    python tools/gen_lzhuf_tables.py [exe] [out.cpp]
"""
from __future__ import annotations

import sys
from pathlib import Path

DGROUP_RAW = 0x61600
DGROUP_VA = 0x463000

INIT_VA, INIT_LEN = 0x483630, 0x118C
LEN_VA, LEN_LEN = 0x483530, 256
HI_VA, HI_LEN = 0x483430, 256


def va_to_raw(va: int) -> int:
    return DGROUP_RAW + (va - DGROUP_VA)


def _format(data: bytes, name: str) -> str:
    lines = [f"extern const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    lines.append("};")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    exe_path = Path(argv[1]) if len(argv) > 1 else Path("resources/MultiverseJourney/rich4.exe")
    out_path = Path(argv[2]) if len(argv) > 2 else Path("src/gen/lzhuf_tables.cpp")

    exe = exe_path.read_bytes()
    init = exe[va_to_raw(INIT_VA):va_to_raw(INIT_VA) + INIT_LEN]
    len_t = exe[va_to_raw(LEN_VA):va_to_raw(LEN_VA) + LEN_LEN]
    hi_t = exe[va_to_raw(HI_VA):va_to_raw(HI_VA) + HI_LEN]

    parts = [
        "// 由 tools/gen_lzhuf_tables.py 从 rich4.exe 生成，勿手改。",
        "#include <cstdint>",
        "",
        "namespace rich4 {",
        "",
        _format(init, "kLzhufInit"),
        "",
        _format(len_t, "kLzhufLenTable"),
        "",
        _format(hi_t, "kLzhufHiTable"),
        "",
        "} // namespace rich4",
        "",
    ]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
