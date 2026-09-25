#!/usr/bin/env python3
"""生成 src/app/item_lines.cpp：道具台词表（0x480D5A，12 角色 x 26 列指针，取列 0..12 = 道具 id1..13）
与价值台词表（0x48084A，12 角色 x 27 列指针，取列 0..2 = 高/中/低价）。

用法: python tools/gen_item_lines.py [--exe resources/game/rich4.exe]
"""
import argparse
import struct
from pathlib import Path

DGROUP_VA, DGROUP_RAW = 0x463000, 0x61600
LINES_VA = 0x480D5A
VALUE_VA = 0x48084A
CHAR_N, COL_N = 12, 13
VALUE_STRIDE, VALUE_COL_N = 27, 3


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="resources/game/rich4.exe")
    ap.add_argument("--out", default="src/app/item_lines.cpp")
    args = ap.parse_args()

    exe = Path(args.exe).read_bytes()

    def raw(va: int) -> int:
        return DGROUP_RAW + (va - DGROUP_VA)

    def u32(va: int) -> int:
        return struct.unpack_from("<I", exe, raw(va))[0]

    def cstr(va: int) -> str:
        if va == 0:
            return ""
        off = raw(va)
        end = exe.index(b"\x00", off)
        return exe[off:end].decode("cp950", errors="replace")

    def cpp(s: str) -> str:
        out = []
        for ch in s:
            if ch == "\\":
                out.append("\\\\")
            elif ch == '"':
                out.append('\\"')
            elif ch == "\n":
                out.append("\\n")
            else:
                out.append(ch)
        return '"' + "".join(out) + '"'

    lines = []
    for c in range(CHAR_N):
        row = [cstr(u32(LINES_VA + 4 * (26 * c + k))) for k in range(COL_N)]
        lines.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    values = []
    for c in range(CHAR_N):
        row = [cstr(u32(VALUE_VA + 4 * (VALUE_STRIDE * c + k))) for k in range(VALUE_COL_N)]
        values.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    text = (
        "// 由 tools/gen_item_lines.py 从 rich4.exe 生成，勿手改\n"
        "// [RE 0x480D5A] off_480D5A 角色台词表：12 角色 x 26 列文本指针；\n"
        "//   道具效果只取列 0..12（= 道具 id1..13）；含 '#NNNN' 语音前缀 / '#NNNN@MM' 表情\n"
        "// [RE 0x48084A] off_48084A 角色价值台词表：12 角色 x 27 列，取列 0..2\n"
        "//   （sub_44F230：>100 → 0；51..100 → 0/1 随机；<=50 → 2）\n"
        '#include "game/app/item_lines.h"\n'
        "\n"
        "namespace rich4 {\n"
        "\n"
        "const char* const kItemLines[12][13] = {\n"
        + "\n".join(lines)
        + "\n};\n"
        "\n"
        "const char* const kValueLines[12][3] = {\n"
        + "\n".join(values)
        + "\n};\n"
        "\n"
        "} // namespace rich4\n"
    )
    Path(args.out).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
