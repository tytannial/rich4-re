#!/usr/bin/env python3
"""从 rich4.exe 提取游戏内(0x417E26)静态表，生成 src/app/map_tables.cpp。

表位置（DGROUP 段, VA 0x463000 → raw 0x61600）::

    word_46CCF0     0x46CCF0  8 方向 x 29 x 29 x {i16 x, i16 y}  等距投影表 (26912B)
    byte_473610     0x473610  绘制遍历序 {i8 row, i8 col}, 0x80 结束 (<=608B)
    byte_474910     0x474910  等距系数 i8[8][4]
    dword_474951    0x474951  棋子偏移 i32[8][2]（普通）
    dword_474991    0x474991  棋子偏移 i32[8][2]（cellTable==18）
    g_cellTypeInit  0x47ED3C  u8[46]
    off_47F072      0x47F072  股票初始表 i32[8][12][9] (8 = 4 地图 x 2 模式, 3456B)
    byte_47FDF6     0x47FDF6  道具初始库存 u8[30]（每 8 字节取首字节）
    byte_47FEE6     0x47FEE6  u8[8]（每 8 字节取首字节）
    unk_47ECEC      0x47ECEC  u32[10][2]
    word_475208     0x475208  日历背景资源索引 u16[8]
    byte_475218     0x475218  月份->背景帧组 u8[12]
    dword_4752AA    0x4752AA  小地图 y 位置 i32[4]（按 byte_49715D 布局）
    off_475274      0x475274  玩家信息页签文本指针 x4
    off_47511C      0x47511C  月份名指针 x12
    byte_415D1D     0x415D1D  金额前缀模板 u8[20]
    word_4749E2     0x4749E2  神明三属性修正表 i16[19] x3（0x4749E2/474A06/474A2A）
    g_charData      0x47E80C  角色模板 12 x 104B（提取 +4 颜色 u16 与 +8..+103 96B）

用法:
    python tools/gen_map_tables.py [exe] [out.cpp]
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

ISO_TABLE_VA, ISO_TABLE_LEN = 0x46CCF0, 26912
# [NEW M4-D] 等距投影表扩展：原表 29x29（半径 14），宽屏/放大模式需更大视野 →
#   扩为 65x65（半径 32）；表内 29x29 逐项保真，外圈用边界最后一步差分线性外推
#   （新区域无原版基准；步长外推即计划 §4.2 的"实测步长"法）。
ISO_HALF_SRC, ISO_HALF_DST = 14, 32
ISO_N_SRC = 2 * ISO_HALF_SRC + 1
ISO_N_DST = 2 * ISO_HALF_DST + 1
DRAW_ORDER_VA, DRAW_ORDER_DIRS, DRAW_ORDER_MAX = 0x473610, 8, 304
ISO_COEFF_VA, ISO_COEFF_LEN = 0x474910, 32
PIECE_OFF_VA = 0x474951
PIECE_OFF18_VA = 0x474991
CELL_TYPE_VA, CELL_TYPE_N = 0x47ED3C, 46
STOCK_VA, STOCK_LEN = 0x47F072, 3456
PROP_STOCK_VA, PROP_STOCK_N = 0x47FDF6, 30
MISC8_VA, MISC8_N = 0x47FEE6, 8
MISC80_VA, MISC80_N = 0x47ECEC, 10
CALENDAR_RES_VA, CALENDAR_RES_N = 0x475208, 8
MONTH_FRAME_VA, MONTH_FRAME_N = 0x475218, 12
MINIMAP_Y_VA, MINIMAP_Y_N = 0x4752AA, 4
TAB_TEXT_VA, TAB_TEXT_N = 0x475274, 4
MONTH_NAME_VA, MONTH_NAME_N = 0x47511C, 12
HOLIDAY_VA, HOLIDAY_GROUPS, HOLIDAY_N, HOLIDAY_STRIDE = 0x47FF4A, 8, 24, 12
MONEY_PREFIX_VA, MONEY_PREFIX_LEN = 0x415D1D, 20
OBJECT_NAME_VA, OBJECT_NAME_N = 0x47ED76, 19
BUILDING_NAME_VA, BUILDING_NAME_N = 0x475138, 16
LUCK_TABLE_VA, LUCK_TABLE_N = 0x4749E2, 18  # 三表间隔 0x24=36B=18×i16（type18 不索引，LABEL_11 仅 type<16）
CHAR_DATA_VA, CHAR_DATA_N, CHAR_DATA_SIZE = 0x47E80C, 12, 104

FLT_AI_ASSET_VA = 0x463190   # 100.0f
FLT_STOCK_TOTAL_VA = 0x463194  # 10.0f


class ExeReader:
    def __init__(self, data: bytes):
        self.data = data

    def raw(self, va: int) -> int:
        return DGROUP_RAW + (va - DGROUP_VA)

    def bytes_at(self, va: int, size: int) -> bytes:
        off = self.raw(va)
        return self.data[off:off + size]

    def u16(self, va: int, index: int = 0) -> int:
        return struct.unpack_from("<H", self.bytes_at(va + 2 * index, 2))[0]

    def u32(self, va: int, index: int = 0) -> int:
        return struct.unpack_from("<I", self.bytes_at(va + 4 * index, 4))[0]

    def i32(self, va: int, index: int = 0) -> int:
        return struct.unpack_from("<i", self.bytes_at(va + 4 * index, 4))[0]

    def f32(self, va: int) -> float:
        return struct.unpack_from("<f", self.bytes_at(va, 4))[0]

    def cstr_big5(self, va: int) -> str:
        off = self.raw(va)
        end = self.data.index(b"\x00", off)
        return self.data[off:end].decode("big5")


def fmt_i8(v: int) -> str:
    return str(v)


def main(argv: list[str]) -> int:
    exe_path = Path(argv[1]) if len(argv) > 1 else Path("resources/MultiverseJourney/rich4.exe")
    out_path = Path(argv[2]) if len(argv) > 2 else Path("src/app/map_tables.cpp")
    r = ExeReader(exe_path.read_bytes())

    # 等距投影表：i16[8][29][29][2]（exe 原表）→ 扩展为 [8][65][65][2]
    iso_raw = r.bytes_at(ISO_TABLE_VA, ISO_TABLE_LEN)
    iso_vals = list(struct.unpack_from(f"<{ISO_TABLE_LEN // 2}h", iso_raw, 0))
    iso_src = [[[[0, 0] for _ in range(ISO_N_SRC)] for _ in range(ISO_N_SRC)]
               for _ in range(8)]
    for d in range(8):
        base = d * ISO_N_SRC * ISO_N_SRC * 2
        for rr in range(ISO_N_SRC):
            for cc in range(ISO_N_SRC):
                i = base + (rr * ISO_N_SRC + cc) * 2
                iso_src[d][rr][cc] = [iso_vals[i], iso_vals[i + 1]]

    iso_dst = [[[[0, 0] for _ in range(ISO_N_DST)] for _ in range(ISO_N_DST)]
               for _ in range(8)]
    lo_src = ISO_HALF_DST - ISO_HALF_SRC
    hi_src = ISO_HALF_DST + ISO_HALF_SRC
    for d in range(8):
        T = iso_dst[d]
        for rr in range(ISO_N_SRC):
            for cc in range(ISO_N_SRC):
                T[lo_src + rr][lo_src + cc] = list(iso_src[d][rr][cc])
        # 列外推（对表内行）
        for rr in range(lo_src, hi_src + 1):
            dcol = (T[rr][hi_src][0] - T[rr][hi_src - 1][0],
                    T[rr][hi_src][1] - T[rr][hi_src - 1][1])
            for cc in range(hi_src + 1, ISO_N_DST):
                T[rr][cc] = [T[rr][cc - 1][0] + dcol[0], T[rr][cc - 1][1] + dcol[1]]
            dcol = (T[rr][lo_src][0] - T[rr][lo_src + 1][0],
                    T[rr][lo_src][1] - T[rr][lo_src + 1][1])
            for cc in range(lo_src - 1, -1, -1):
                T[rr][cc] = [T[rr][cc + 1][0] + dcol[0], T[rr][cc + 1][1] + dcol[1]]
        # 行外推（所有列）
        for cc in range(ISO_N_DST):
            drow = (T[hi_src][cc][0] - T[hi_src - 1][cc][0],
                    T[hi_src][cc][1] - T[hi_src - 1][cc][1])
            for rr in range(hi_src + 1, ISO_N_DST):
                T[rr][cc] = [T[rr - 1][cc][0] + drow[0], T[rr - 1][cc][1] + drow[1]]
            drow = (T[lo_src][cc][0] - T[lo_src + 1][cc][0],
                    T[lo_src][cc][1] - T[lo_src + 1][cc][1])
            for rr in range(lo_src - 1, -1, -1):
                T[rr][cc] = [T[rr + 1][cc][0] + drow[0], T[rr + 1][cc][1] + drow[1]]

    # 绘制遍历序：8 方向 x 304 项，扫描到 -128
    order_raw = r.bytes_at(DRAW_ORDER_VA, DRAW_ORDER_DIRS * DRAW_ORDER_MAX * 2)
    draw_orders = []
    for d in range(DRAW_ORDER_DIRS):
        order = []
        for i in range(DRAW_ORDER_MAX):
            row, col = struct.unpack_from("<bb", order_raw, d * DRAW_ORDER_MAX * 2 + i * 2)
            if row == -128:
                break
            order.append((row, col))
        draw_orders.append(order)
    draw_order_n = max(len(o) for o in draw_orders)

    coeff = list(struct.unpack_from("<32b", r.bytes_at(ISO_COEFF_VA, ISO_COEFF_LEN), 0))
    piece_off = [tuple(r.i32(PIECE_OFF_VA, 2 * i + k) for k in range(2)) for i in range(8)]
    piece_off18 = [tuple(r.i32(PIECE_OFF18_VA, 2 * i + k) for k in range(2)) for i in range(8)]
    cell_type = list(r.bytes_at(CELL_TYPE_VA, CELL_TYPE_N))

    stock_vals = list(struct.unpack_from(f"<{STOCK_LEN // 4}i", r.bytes_at(STOCK_VA, STOCK_LEN), 0))

    prop_raw = r.bytes_at(PROP_STOCK_VA, PROP_STOCK_N * 8)
    prop_stock = [prop_raw[8 * i] for i in range(PROP_STOCK_N)]
    misc8_raw = r.bytes_at(MISC8_VA, MISC8_N * 8)
    misc8 = [misc8_raw[8 * i] for i in range(MISC8_N)]
    # 道具表 byte_47FEE6 13×8B：+0 礼物池初值、+1 价格（点券，sub_445B3F 用）、+4 名称指针
    item_raw = r.bytes_at(MISC8_VA, 13 * 8)
    item_price = [item_raw[8 * i + 1] for i in range(13)]
    # idx12（核子飛彈）名称指针原版无效（0x01010001，非 DGROUP 地址）→ 置空（沿用既有修补）
    item_names = []
    for i in range(13):
        p = struct.unpack_from("<I", item_raw, 8 * i + 4)[0]
        if p < DGROUP_VA or p >= DGROUP_VA + 0x20000:
            item_names.append("")
        else:
            item_names.append(r.cstr_big5(p))

    misc80 = [tuple(r.u32(MISC80_VA, 2 * i + k) for k in range(2)) for i in range(MISC80_N)]
    calendar_res = [r.u16(CALENDAR_RES_VA, i) for i in range(CALENDAR_RES_N)]
    month_frame = list(r.bytes_at(MONTH_FRAME_VA, MONTH_FRAME_N))
    minimap_y = [r.i32(MINIMAP_Y_VA, i) for i in range(MINIMAP_Y_N)]

    tab_text = [r.cstr_big5(r.u32(TAB_TEXT_VA, i)) for i in range(TAB_TEXT_N)]
    month_name = [r.cstr_big5(r.u32(MONTH_NAME_VA, i)) for i in range(MONTH_NAME_N)]
    object_name = [r.cstr_big5(r.u32(OBJECT_NAME_VA, i)) for i in range(OBJECT_NAME_N)]
    building_name = [r.cstr_big5(r.u32(BUILDING_NAME_VA, i)) for i in range(BUILDING_NAME_N)]
    holiday_raw = r.bytes_at(HOLIDAY_VA, HOLIDAY_GROUPS * HOLIDAY_N * HOLIDAY_STRIDE)
    holiday = [[[holiday_raw[g * HOLIDAY_N * HOLIDAY_STRIDE + i * HOLIDAY_STRIDE + f]
                 for f in range(5)] for i in range(HOLIDAY_N)] for g in range(HOLIDAY_GROUPS)]
    money_prefix = list(r.bytes_at(MONEY_PREFIX_VA, MONEY_PREFIX_LEN))

    # 神明三属性修正表 word_4749E2/474A06/474A2A i16[18]（索引 = cellTable 类型；表间隔 36B）
    luck_raw = r.bytes_at(LUCK_TABLE_VA, 3 * 18 * 2)
    luck = [list(struct.unpack_from(f"<{LUCK_TABLE_N}h", luck_raw, t * LUCK_TABLE_N * 2))
            for t in range(3)]

    char_color = []
    char_tmpl = []
    for c in range(CHAR_DATA_N):
        t = r.bytes_at(CHAR_DATA_VA + CHAR_DATA_SIZE * c, CHAR_DATA_SIZE)
        char_color.append(struct.unpack_from("<I", t, 4)[0])
        char_tmpl.append(list(t[8:104]))

    ai_ratio = r.f32(FLT_AI_ASSET_VA)
    stock_ratio = r.f32(FLT_STOCK_TOTAL_VA)

    out = []
    w = out.append
    w("// 由 tools/gen_map_tables.py 从 rich4.exe 生成，勿手改。")
    w('#include "game/app/map_tables.h"')
    w("")
    w("namespace rich4 {")
    w("")
    w(f"// [RE 0x46CCF0] word_46CCF0 等距投影表：8 方向 x {ISO_N_DST}x{ISO_N_DST} 格 x "
      "{i16 x, i16 y}")
    w(f"//   表内 [-{ISO_HALF_SRC}..{ISO_HALF_SRC}]^2 为 exe 原表逐项保真；外圈为边界步长线性外推")
    w(f"//   （[NEW M4-D] 宽地图区/放大模式；native 29x29 窗口逐项与原表一致）")
    w(f"//   索引 = {4 * ISO_N_DST * ISO_N_DST}*dir + {4 * ISO_N_DST}*(row+{ISO_HALF_DST}) + "
      f"4*(col+{ISO_HALF_DST})（字节）")
    w(f"const int16_t kIsoProjection[8][{ISO_N_DST}][{ISO_N_DST}][2] = {{")
    for d in range(8):
        w("  {")
        for row in range(ISO_N_DST):
            cells = ", ".join("{%d,%d}" % (iso_dst[d][row][c][0], iso_dst[d][row][c][1])
                              for c in range(ISO_N_DST))
            w("    {" + cells + "},")
        w("  },")
    w("};")
    w("")
    w(f"// [RE 0x473610] byte_473610 绘制遍历序（8 方向 x {draw_order_n} 项，0x80 结束）")
    w(f"const int8_t kIsoDrawOrder[8][{draw_order_n}][2] = {{")
    for d in range(DRAW_ORDER_DIRS):
        w("  {")
        line = []
        for row, col in draw_orders[d]:
            line.append("{%d,%d}" % (row, col))
            if len(line) == 16:
                w("    " + ", ".join(line) + ",")
                line = []
        if line:
            w("    " + ", ".join(line) + ",")
        w("  },")
    w("};")
    w("")
    w("// [RE 0x474910] byte_474910 等距投影系数 i8[8][4]")
    w("const int8_t kIsoCoeff[8][4] = {")
    for d in range(8):
        w("    {%d, %d, %d, %d}," % tuple(coeff[4 * d:4 * d + 4]))
    w("};")
    w("")
    w("// [RE 0x474951] dword_474951 棋子绘制偏移 i32[8][2]（普通）")
    w("const int32_t kPieceOffset[8][2] = {")
    for x, y in piece_off:
        w(f"    {{{x}, {y}}},")
    w("};")
    w("")
    w("// [RE 0x474991] dword_474991 棋子绘制偏移 i32[8][2]（cellTable==18）")
    w("const int32_t kPieceOffset18[8][2] = {")
    for x, y in piece_off18:
        w(f"    {{{x}, {y}}},")
    w("};")
    w("")
    w("// [RE 0x47ED3C] g_cellTypeInit 格子类型初值 u8[46]")
    w("const uint8_t kCellTypeInit[46] = {" + ", ".join(str(v) for v in cell_type) + "};")
    w("")
    w("// [RE 0x4749E2/0x474A06/0x474A2A] 神明三属性修正表 i16[18]（索引 = cellTable 类型；")
    w("//   attachObject 0x40EAD7 +=、deleteMapObject 0x40E14D -=；语义见 map-object-refresh.md §3）")
    for name, comment in (("kLuckA", "排名评分 10×A (0x437D1A)"),
                          ("kLuckB", "AI 建设门槛 + 台词吉凶 (0x41FACC/0x44B896)"),
                          ("kLuckC", "台词吉凶 (0x44B896 a3)")):
        w(f"// {comment}")
        w(f"const int16_t {name}[{LUCK_TABLE_N}] = " + "{ " +
          ", ".join(str(v) for v in luck[("kLuckA", "kLuckB", "kLuckC").index(name)]) + " };")
    w("")
    w("// [RE 0x47F072] off_47F072 股票初始表 i32[8][12][9]（8 = 4 地图 x 2 模式）")
    w("const int32_t kStockInit[8][12][9] = {")
    for g in range(8):
        w("  {")
        for s in range(12):
            base = (g * 12 + s) * 9
            w("    {" + ", ".join(str(v) for v in stock_vals[base:base + 9]) + "},")
        w("  },")
    w("};")
    w("")
    w("// [RE 0x47FDF6] byte_47FDF6 道具初始库存 u8[30]（每 8 字节结构取首字节）")
    w("const uint8_t kPropStockInit[30] = {" + ", ".join(str(v) for v in prop_stock) + "};")
    w("")
    w("// [RE 0x47FEE6] byte_47FEE6 u8[8]（每 8 字节结构取首字节）")
    w("const uint8_t kMisc8Init[8] = {" + ", ".join(str(v) for v in misc8) + "};")
    w("")
    w("// [RE 0x47FEE7] byte_47FEE7 道具价格（13 种，道具结构 +1；0x445B3F 没收计价）")
    w("const uint8_t kItemPrice[13] = {" + ", ".join(str(v) for v in item_price) + "};")
    w("")
    w("// [RE 0x47FEEA] 道具名表（13 种，道具结构 +4 指针；0x445ADA 礼物显示）")
    w("const char* const kItemNames[13] = {" +
      ", ".join('"%s"' % s for s in item_names) + "};")
    w("")
    w("// [RE 0x47ECEC] unk_47ECEC u32[10][2]")
    w("const uint32_t kMiscTable80[10][2] = {")
    for a, b in misc80:
        w(f"    {{{a}, {b}}},")
    w("};")
    w("")
    w("// [RE 0x475208] word_475208 日历背景资源索引 u16[8]（4*mode+map）")
    w("const uint16_t kCalendarRes[8] = {" + ", ".join(str(v) for v in calendar_res) + "};")
    w("")
    w("// [RE 0x475218] byte_475218 月份->背景帧组 u8[12]")
    w("const uint8_t kMonthFrame[12] = {" + ", ".join(str(v) for v in month_frame) + "};")
    w("")
    w("// [RE 0x4752AA] dword_4752AA 小地图 y 位置 i32[4]（按 byte_49715D 布局索引）")
    w("const int32_t kMiniMapY[4] = {" + ", ".join(str(v) for v in minimap_y) + "};")
    w("")
    w("// [RE 0x475274] off_475274 玩家信息页签文本")
    w("const char* const kTabText[4] = {" + ", ".join('"%s"' % s for s in tab_text) + "};")
    w("")
    w("// [RE 0x47ED76] off_47ED76 物件/神明名（19 项，cellTable 类型 0..18）")
    w("const char* const kObjectNames[19] = {" + ", ".join('"%s"' % s for s in object_name) + "};")
    w("")
    w("// [RE 0x475138] off_475138 建筑/费用名（16 项：0-10 建筑等级、11-15 费用；"
      "off_475150 = +6 公司名、off_475164 = +11 费用名）")
    w("const char* const kBuildingNames[16] = {" + ", ".join('"%s"' % s for s in building_name) + "};")
    w("")
    w("// [RE 0x47511C] off_47511C 月份名")
    w("const char* const kMonthName[12] = {" + ", ".join('"%s"' % s for s in month_name) + "};")
    w("")
    w("// [RE 0x47FF4A] byte_47FF4A.. 节日表 [组=4*mode+map][24]{flag,type,month,day,weekday}")
    w("// 依据: 0x4521F0; type 0=固定月日 1=每年同月日 2=第N个星期X(用 weekday)")
    w("const uint8_t kHoliday[8][24][5] = {")
    for g in range(HOLIDAY_GROUPS):
        w(f"  {{ // base={g}")
        for i in range(HOLIDAY_N):
            w("    {" + ", ".join(str(v) for v in holiday[g][i]) + "},")
        w("  },")
    w("};")
    w("")
    w("// [RE 0x415D1D] byte_415D1D 金额前缀模板 u8[20]（'$' + 预留数字位）")
    w("const uint8_t kMoneyPrefix[20] = {" + ", ".join("0x%02X" % v for v in money_prefix) + "};")
    w("")
    w("// [RE 0x47E80C] g_charData 角色模板：+4 颜色 u16[12]")
    w("const uint32_t kCharColor[12] = {" + ", ".join(str(v) for v in char_color) + "};")
    w("")
    w("// [RE 0x47E80C] g_charData 角色模板：+8..+103（96 字节/角色）")
    w("const uint8_t kCharTemplate[12][96] = {")
    for c in range(12):
        vals = ", ".join("0x%02X" % v for v in char_tmpl[c])
        w(f"    {{{vals}}},")
    w("};")
    w("")
    w("// [RE 0x463190] flt_463190（AI 初始资金系数）")
    w(f"const float kAiAssetRatio = {ai_ratio!r}f;")
    w("")
    w("// [RE 0x463194] flt_463194（股票总市值系数）")
    w(f"const float kStockTotalRatio = {stock_ratio!r}f;")
    w("")
    w("} // namespace rich4")
    w("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"wrote {out_path}")
    print(f"iso={len(iso_vals)} drawOrder={draw_order_n}x{DRAW_ORDER_DIRS} stock={len(stock_vals)}")
    print("tabs:", tab_text)
    print("months:", month_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
