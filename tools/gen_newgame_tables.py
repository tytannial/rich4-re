#!/usr/bin/env python3
"""从 rich4.exe 提取选人界面(0x404E44)静态表，生成 src/app/new_game_tables.cpp。

表位置（DGROUP 段, VA 0x463000 → raw 0x61600）::

    g_playerCountText 0x46CB88  3 指针 (BIG5 "二人/三人/四人")
    g_startMoney      0x46CB94  6 项 u32
    g_travelModeText  0x46CBAC  3 指针 (BIG5 "步行/機車/汽車")
    g_landPermText    0x46CBB8  6 指针 (BIG5 "無限/二年/...")
    g_gameTimeText    0x46CBD0  6 指针 (同文本)
    g_gameDays        0x46CBE8  6 项 u32
    g_winMoneyMul     0x46CC00  6 项 u32
    g_ctrlRects       0x46CC18  13 组 u16 x 4 (left,top,right,bottom)
    g_mapMarkY        0x46CC80  4 项 u16
    g_listRects       0x46CC88  6 组 u16 x 4
    g_listBgFrame     0x46CCB8  6 项 u16
    g_playerSlotX     0x46CB58  12 项 u32 (3 组 x 4)
    面板标签           0x463138  6 个 BIG5 串
    "無限"            0x463171
    g_charData        0x47E80C  12 项 x 104B ([0]=名字指针)
    通道缩放表 -16     0x485B68  32 字节
    通道缩放表 -20     0x485AE8  32 字节

用法:
    python tools/gen_newgame_tables.py [exe] [out.cpp]
"""
from __future__ import annotations
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import struct
import sys
from pathlib import Path

DGROUP_RAW = 0x61600
DGROUP_VA = 0x463000

CTRL_RECTS_VA, CTRL_RECTS_N = 0x46CC18, 13
LIST_RECTS_VA, LIST_RECTS_N = 0x46CC88, 6
MAP_MARK_Y_VA, MAP_MARK_Y_N = 0x46CC80, 4
LIST_BG_FRAME_VA, LIST_BG_FRAME_N = 0x46CCB8, 6
START_MONEY_VA, START_MONEY_N = 0x46CB94, 6
GAME_DAYS_VA, GAME_DAYS_N = 0x46CBE8, 6
WIN_MUL_VA, WIN_MUL_N = 0x46CC00, 6
PLAYER_SLOT_X_VA, PLAYER_SLOT_X_N = 0x46CB58, 12

PLAYER_COUNT_TEXT_VA, PLAYER_COUNT_N = 0x46CB88, 3
TRAVEL_TEXT_VA, TRAVEL_N = 0x46CBAC, 3
LAND_PERM_TEXT_VA, LAND_PERM_N = 0x46CBB8, 6
GAME_TIME_TEXT_VA, GAME_TIME_N = 0x46CBD0, 6

PANEL_LABEL_VAS = [0x463138, 0x463141, 0x46314A, 0x463153, 0x46315C, 0x463165]
INFINITE_TEXT_VA = 0x463171

CHAR_DATA_VA, CHAR_DATA_N, CHAR_DATA_SIZE = 0x47E80C, 12, 104

CHANNEL_HALF_VA, CHANNEL_THIRD_VA, CHANNEL_LEN = 0x485B68, 0x485AE8, 32


class ExeReader:
    def __init__(self, data: bytes):
        self.data = data

    def raw(self, va: int) -> int:
        return DGROUP_RAW + (va - DGROUP_VA)

    def bytes_at(self, va: int, size: int) -> bytes:
        off = self.raw(va)
        return self.data[off:off + size]

    def u32(self, va: int, index: int = 0) -> int:
        return struct.unpack_from("<I", self.bytes_at(va + 4 * index, 4))[0]

    def u16(self, va: int, index: int = 0) -> int:
        return struct.unpack_from("<H", self.bytes_at(va + 2 * index, 2))[0]

    def cstr_big5(self, va: int) -> str:
        off = self.raw(va)
        end = self.data.index(b"\x00", off)
        return self.data[off:end].decode("big5")


def u8_array(name: str, data: bytes) -> str:
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"const uint8_t {name}[{len(data)}] = {{{body}}};"


def u16_array(name: str, values: list[int]) -> str:
    body = ", ".join(str(v) for v in values)
    return f"const uint16_t {name}[{len(values)}] = {{{body}}};"


def u16_rects(name: str, values: list[tuple[int, int, int, int]]) -> str:
    lines = [f"const uint16_t {name}[{len(values)}][4] = {{"]
    for rect in values:
        lines.append("    {" + ", ".join(str(v) for v in rect) + "},")
    lines.append("};")
    return "\n".join(lines)


def u32_array(name: str, values: list[int]) -> str:
    body = ", ".join(str(v) for v in values)
    return f"const uint32_t {name}[{len(values)}] = {{{body}}};"


def u32_grid(name: str, rows: int, cols: int, values: list[int]) -> str:
    lines = [f"const uint32_t {name}[{rows}][{cols}] = {{"]
    for r in range(rows):
        row = values[r * cols:(r + 1) * cols]
        lines.append("    {" + ", ".join(str(v) for v in row) + "},")
    lines.append("};")
    return "\n".join(lines)


def str_array(name: str, values: list[str], count: int) -> str:
    body = ", ".join('"' + s + '"' for s in values)
    return f"const char* const {name}[{count}] = {{{body}}};"


def main(argv: list[str]) -> int:
    exe_path = Path(argv[1]) if len(argv) > 1 else Path("resources/MultiverseJourney/rich4.exe")
    out_path = Path(argv[2]) if len(argv) > 2 else Path("src/app/new_game_tables.cpp")

    reader = ExeReader(exe_path.read_bytes())

    ctrl_rects = [tuple(reader.u16(CTRL_RECTS_VA, 4 * i + k) for k in range(4))
                  for i in range(CTRL_RECTS_N)]
    list_rects = [tuple(reader.u16(LIST_RECTS_VA, 4 * i + k) for k in range(4))
                  for i in range(LIST_RECTS_N)]
    map_mark_y = [reader.u16(MAP_MARK_Y_VA, i) for i in range(MAP_MARK_Y_N)]
    list_bg = [reader.u16(LIST_BG_FRAME_VA, i) for i in range(LIST_BG_FRAME_N)]
    start_money = [reader.u32(START_MONEY_VA, i) for i in range(START_MONEY_N)]
    game_days = [reader.u32(GAME_DAYS_VA, i) for i in range(GAME_DAYS_N)]
    win_mul = [reader.u32(WIN_MUL_VA, i) for i in range(WIN_MUL_N)]
    slot_x = [reader.u32(PLAYER_SLOT_X_VA, i) for i in range(PLAYER_SLOT_X_N)]

    player_count_text = [reader.cstr_big5(reader.u32(PLAYER_COUNT_TEXT_VA, i))
                         for i in range(PLAYER_COUNT_N)]
    travel_text = [reader.cstr_big5(reader.u32(TRAVEL_TEXT_VA, i)) for i in range(TRAVEL_N)]
    land_perm_text = [reader.cstr_big5(reader.u32(LAND_PERM_TEXT_VA, i))
                      for i in range(LAND_PERM_N)]
    game_time_text = [reader.cstr_big5(reader.u32(GAME_TIME_TEXT_VA, i))
                      for i in range(GAME_TIME_N)]
    labels = [reader.cstr_big5(va) for va in PANEL_LABEL_VAS]
    infinite_text = reader.cstr_big5(INFINITE_TEXT_VA)

    char_names = []
    for i in range(CHAR_DATA_N):
        name_ptr = reader.u32(CHAR_DATA_VA + CHAR_DATA_SIZE * i)
        char_names.append(reader.cstr_big5(name_ptr))

    half = reader.bytes_at(CHANNEL_HALF_VA, CHANNEL_LEN)
    third = reader.bytes_at(CHANNEL_THIRD_VA, CHANNEL_LEN)

    parts = [
        "// 由 tools/gen_newgame_tables.py 从 rich4.exe 生成，勿手改。",
        '#include "game/app/new_game_tables.h"',
        "",
        "namespace rich4 {",
        "",
        "// [RE 0x46CB88] g_playerCountText（选项：玩家数）",
        str_array("kPlayerCountText", player_count_text, PLAYER_COUNT_N),
        "",
        "// [RE 0x46CBAC] g_travelModeText（选项：行进方式）",
        str_array("kTravelModeText", travel_text, TRAVEL_N),
        "",
        "// [RE 0x46CBB8] g_landPermText（选项：土地权限）",
        str_array("kLandPermText", land_perm_text, LAND_PERM_N),
        "",
        "// [RE 0x46CBD0] g_gameTimeText（选项：游戏时间）",
        str_array("kGameTimeText", game_time_text, GAME_TIME_N),
        "",
        "// [RE 0x463138..0x463165] 面板 6 行标签",
        str_array("kPanelLabels", labels, len(labels)),
        "",
        f'// [RE 0x463171] unk_463171 = "{infinite_text}"（胜利条件 0 倍率显示）',
        f'const char* const kInfiniteText = "{infinite_text}";',
        "",
        "// [RE 0x47E80C] g_charData 每项 +0 名字指针（12 角色）",
        str_array("kCharNames", char_names, CHAR_DATA_N),
        "",
        "// [RE 0x46CB94] g_startMoney",
        u32_array("kStartMoney", start_money),
        "",
        "// [RE 0x46CBE8] g_gameDays（0=无限）",
        u32_array("kGameDays", game_days),
        "",
        "// [RE 0x46CC00] g_winMoneyMul（胜利资金 = 起始资金 x 倍率，0=无限）",
        u32_array("kWinMoneyMul", win_mul),
        "",
        "// [RE 0x46CB58] g_playerSlotX[玩家数索引][槽]（底部头像 x，y 固定 440）",
        u32_grid("kPlayerSlotX", 3, 4, slot_x),
        "",
        "// [RE 0x46CC18] g_ctrlRects[13]（left,top,right,bottom）",
        u16_rects("kCtrlRects", ctrl_rects),
        "",
        "// [RE 0x46CC88] g_listRects[6]",
        u16_rects("kListRects", list_rects),
        "",
        "// [RE 0x46CC80] g_mapMarkY[4]",
        u16_array("kMapMarkY", map_mark_y),
        "",
        "// [RE 0x46CCB8] g_listBgFrame[6]",
        u16_array("kListBgFrame", list_bg),
        "",
        "// [RE 0x485B68] 通道缩放表 -16（亮度减半，32 字节）",
        u8_array("kChannelHalf", half),
        "",
        "// [RE 0x485AE8] 通道缩放表 -20（亮度 1/3，32 字节）",
        u8_array("kChannelThird", third),
        "",
        "} // namespace rich4",
        "",
    ]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out_path}")
    for name in ("kPlayerCountText", "kTravelModeText", "kCharNames", "kPanelLabels"):
        pass
    print("角色名:", " / ".join(char_names))
    print("标签  :", " / ".join(labels))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
