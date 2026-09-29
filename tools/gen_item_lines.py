#!/usr/bin/env python3
"""生成 src/app/item_lines.cpp：道具台词表（0x480D5A，12 角色 x 26 列指针，取列 0..12 = 道具 id1..13）、
被踩中台词表（同表列 14/15/16 = 路障/地雷/定時炸彈）与价值台词表（0x48084A，12 角色 x 27 列指针，
取列 0..2 = 高/中/低价）。

用法: python tools/gen_item_lines.py [--exe resources/MultiverseJourney/rich4.exe]
"""
import argparse
import struct
from pathlib import Path

DGROUP_VA, DGROUP_RAW = 0x463000, 0x61600
LINES_VA = 0x480D5A
VALUE_VA = 0x48084A
CHAR_N, COL_N = 12, 13
TOUCH_COLS = (14, 15, 16)  # 路障/地雷/定時炸彈被踩中（0x480D92/96/9A）
# 金额/状态台词（off_48084A 家族 27 列）：6/7/8 收款（高/中/低）· 9/10/11 付款 ·
#   15 满级 · 16/17 连锁吐槽（必播/1/3 概率）· 18 欠债 · 12/13/14 意外之财（高/中/低）·
#   19/20/21 状态提示（坐牢/住院/冬眠）· 22 神明附身哭 · 23 神明飘走 ·
#   3/4/5 倒霉档（sub_44F2C2 住宿/入狱/住院天数 + 新闻业主随机 3/4）·
#   24 胜利大笑 · 25 淘汰认输 · 26 失败鼓励
MONEY_COLS = (6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 12, 13, 14, 19, 20, 21, 22, 23,
              3, 4, 5, 24, 25, 26)
VALUE_STRIDE, VALUE_COL_N = 27, 3


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="resources/MultiverseJourney/rich4.exe")
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

    touches = []
    for c in range(CHAR_N):
        row = [cstr(u32(LINES_VA + 4 * (26 * c + k))) for k in TOUCH_COLS]
        touches.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    money = []
    for c in range(CHAR_N):
        row = [cstr(u32(VALUE_VA + 4 * (VALUE_STRIDE * c + k))) for k in MONEY_COLS]
        money.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    values = []
    for c in range(CHAR_N):
        row = [cstr(u32(VALUE_VA + 4 * (VALUE_STRIDE * c + k))) for k in range(VALUE_COL_N)]
        values.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    text = (
        "// 由 tools/gen_item_lines.py 从 rich4.exe 生成，勿手改\n"
        "// [RE 0x480D5A] off_480D5A 角色台词表：12 角色 x 26 列文本指针；\n"
        "//   道具效果列 0..12（= 道具 id1..13）；被踩中列 14/15/16 = 路障/地雷/定時炸彈；\n"
        "//   含 '#NNNN' 语音前缀 / '#NNNN@MM' 表情\n"
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
        "// [RE 0x480D92/96/9A] 被踩中台词（列14 路障 / 列15 地雷 / 列16 定時炸彈）；\n"
        "//   onPlayerActionPhase case16/17/18 → sub_44EF41(踩中者, 1/1/2, ...)\n"
        "const char* const kItemTouchLines[12][3] = {\n"
        + "\n".join(touches)
        + "\n};\n"
        "\n"
        "const char* const kValueLines[12][3] = {\n"
        + "\n".join(values)
        + "\n};\n"
        "\n"
        "// [RE 0x480862/86A/86E/876/87A/882/886/88A/88E/892] 金额/状态台词（off_48084A 家族列）：\n"
        "//   0/1/2 = 收款高/中/低（sub_44F354）；3/4/5 = 付款高/中/低（sub_44F42D）；\n"
        "//   6 = 住宅满级（0x4199F1）；7 = 欠债者面对最大债主（sub_44F4ED）；\n"
        "//   8/9 = 同路段≥3 连锁吐槽（sub_44F627 必播列16 / 1/3 概率列17）；\n"
        "//   10/11/12 = 意外之财高/中/低（sub_44F567，列12/13/14；大財神免付时播）；\n"
        "//   13/14/15 = 状态提示坐牢/住院/冬眠（checkPlayerActionStatus 0x40C912）；\n"
        "//   16 = 神明附身哭（attachObject 穷神/衰神/死神）；17 = 神明飘走（attachEnd）；\n"
        "//   18/19/20 = 倒霉档列3/4/5（sub_44F2C2 住宿/入狱/住院 + 新闻业主 3/4 随机，expr2）；\n"
        "//   21 = 终局胜利大笑（列24 expr3）；22 = 淘汰认输（列25 expr2）；\n"
        "//   23 = 失败鼓励（列26 expr3，defeatFlow）\n"
        "const char* const kMoneyLines[12][24] = {\n"
        + "\n".join(money)
        + "\n};\n"
        "\n"
        "} // namespace rich4\n"
    )
    Path(args.out).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
