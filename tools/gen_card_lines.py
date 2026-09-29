#!/usr/bin/env python3
"""生成 src/app/card_lines.cpp：卡片台词表（0x48123A，12 角色 x 90 槽指针）
与受害台词表（0x48089E = 价值表 0x48084A 第 21 列，12 角色；梦游受害者台词）。

用法: python tools/gen_card_lines.py [--exe resources/MultiverseJourney/rich4.exe]
"""
import argparse
import struct
from pathlib import Path

DGROUP_VA, DGROUP_RAW = 0x463000, 0x61600
CARD_VA = 0x48123A        # 12 角色 x 90 槽（角色 stride 360 字节）
CHAR_N, SLOT_N = 12, 90
VICTIM_VA = 0x48089E      # 价值表 0x48084A + 21*4（列 21）


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="resources/MultiverseJourney/rich4.exe")
    ap.add_argument("--out", default="src/app/card_lines.cpp")
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

    rows = []
    for c in range(CHAR_N):
        row = [cstr(u32(CARD_VA + 4 * (SLOT_N * c + k))) for k in range(SLOT_N)]
        rows.append("    {" + ", ".join(cpp(s) for s in row) + "},")

    victims = []
    for c in range(CHAR_N):
        victims.append("    " + cpp(cstr(u32(VICTIM_VA + 4 * 27 * c))) + ",")

    text = (
        "// 由 tools/gen_card_lines.py 从 rich4.exe 生成，勿手改\n"
        "// [RE 0x48123A] 卡片台词表：12 角色 x 90 槽文本指针（角色 stride 360 字节）；\n"
        "//   槽 0..29 = 卡 id1..30 使用者台词；对方/受害者槽见 card_lines.h 常量；\n"
        "//   文本含 '#NNNN' 语音前缀 / '#NNNN@MM' 表情帧\n"
        "// [RE 0x48089E] 受害台词（价值表 0x48084A 第 21 列；梦游受害者）\n"
        '#include "game/app/card_lines.h"\n'
        "\n"
        '#include "game/app/item_lines.h"\n'
        '#include "game/application.h"\n'
        '#include "game/game_state.h"\n'
        "\n"
        "namespace rich4 {\n"
        "\n"
        "const char* const kCardSpeech[12][90] = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "\n"
        "const char* const kCardVictimLines[12] = {\n"
        + "\n".join(victims)
        + "\n};\n"
        "\n"
        "// [RE 0x44EF41] playCardLine：按槽播角色气泡台词（复用 playLine 的语音/表情/状态守卫）\n"
        "void playCardLine(Application& app, int player, int slot) {\n"
        "    if (player < 0 || player >= 9 || slot < 0 || slot >= 90) {\n"
        "        return;\n"
        "    }\n"
        "    const int ci = app.gameState().players[player].charIndex;\n"
        "    if (ci < 0 || ci >= 12) {\n"
        "        return;\n"
        "    }\n"
        "    const char* line = kCardSpeech[ci][slot];\n"
        "    if (line == nullptr || line[0] == '\\0') {\n"
        "        return;\n"
        "    }\n"
        "    playLine(app, player, line);\n"
        "}\n"
        "\n"
        "} // namespace rich4\n"
    )
    Path(args.out).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
