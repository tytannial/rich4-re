"""[NEW] 生成 src/gen/big5_tables.cpp —— cp950(950) 双字节 → Unicode 映射表。

依据: 游戏内码 BIG5/cp950（docs/formats/text.md）；原版 Windows 版经
      MultiByteToWideChar(950) 转换，非 Windows 平台无此 API → 内嵌映射表统一两平台
      （docs/cross-platform.md）。表内容 = Python codec cp950（源自 Windows CP950 表）。

用法:
  python tools/gen_big5_tables.py            # 生成到 src/gen/big5_tables.cpp
  python tools/gen_big5_tables.py --check    # 只统计不写文件
"""
from __future__ import annotations

import argparse
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "core" / "big5_tables.cpp"

LEAD_BASE = 0x81
LEAD_COUNT = 0xFE - 0x81 + 1  # 126


def build_table() -> tuple[list[int], int]:
    table = [0] * (LEAD_COUNT * 256)
    n = 0
    for lead in range(0x81, 0xFF):
        for tail in range(0x00, 0x100):
            try:
                s = bytes([lead, tail]).decode("cp950")
            except UnicodeDecodeError:
                continue
            table[(lead - LEAD_BASE) * 256 + tail] = ord(s)
            n += 1
    return table, n


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    table, n = build_table()
    print(f"cp950 双字节映射数: {n} / 表项 {len(table)}")

    if args.check:
        return

    lines = [
        "// 由 tools/gen_big5_tables.py 从 Python cp950 codec 生成，勿手改。",
        "// 依据: 原版经 MultiByteToWideChar(950) 转码；内嵌表使 Windows/Linux 行为一致",
        "",
        '#include "game/core/big5_tables.h"',
        "",
        "namespace rich4 {",
        "",
        f"// lead 0x{LEAD_BASE:02X}..0xFE, tail 0x00..0xFF；索引 = (lead-0x{LEAD_BASE:02X})*256 + tail",
        "const uint16_t kBig5ToUnicode[kBig5TableLeadCount * 256] = {",
    ]
    for i in range(0, len(table), 16):
        chunk = table[i : i + 16]
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace rich4")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"written: {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
