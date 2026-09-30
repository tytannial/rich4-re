#!/usr/bin/env python3
"""从 rich4.exe 提取节日表，生成 kHoliday[8][24][12]（供 map_tables.cpp）。

表位置（DGROUP 段）:
    byte_47FF4A : g_holidayTable [组=4*mode+map][24][12]
      +0 flag(日历红字/有效, 0x80=占位哨兵) +1 type(0固定/1浮动/2第N星期X) +2 月 +3 日
      +4 weekday +5 效果flags(&1插画/&4音乐/&8发卡) +6/+7 插画 data.mkf idx
      +8/+9 插画FLC参数 +10/+11 节日音乐 scene idx
    dword_47639C : findHoliday type1 浮动表（u32，按 daysSince1998 索引，取低16=月<<8|日）

用法:
    python tools/gen_holiday_tables.py [exe]            # 校验 + 探测 + 打印生成片段
"""
from __future__ import annotations

import sys
from pathlib import Path

DGROUP_RAW = 0x61600
DGROUP_VA = 0x463000
HOLIDAY_VA, HOLIDAY_N = 0x47FF4A, 8 * 24 * 12
FLOAT_VA = 0x47639C


def va_to_raw(va: int) -> int:
    return DGROUP_RAW + (va - DGROUP_VA)


def main() -> None:
    exe = Path(next((a for a in sys.argv[1:] if not a.startswith("--")), "resources/MultiverseJourney/rich4.exe"))
    data = exe.read_bytes()
    t = data[va_to_raw(HOLIDAY_VA): va_to_raw(HOLIDAY_VA) + HOLIDAY_N]
    assert len(t) == HOLIDAY_N, f"short read {len(t)}"

    # 校验：前 5 字段与现有 kHoliday[..][5] 一致（防偏移错）
    src = Path("src/app/map_tables.cpp").read_text(encoding="utf-8")
    import re
    existing = re.search(r"const uint8_t kHoliday\[8\]\[24\]\[5\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if existing:
        nums = re.findall(r"\{(\s*\d+,\s*\d+,\s*\d+,\s*\d+,\s*\d+)\s*\}", existing.group(1))
        print(f"[check] existing 5-byte rows parsed: {len(nums)} (expect 8*24=192)")
        mism = 0
        for idx, grp in enumerate(nums):
            a, b, c, d, e = [int(x.strip()) for x in grp.split(",")]
            r = idx * 12
            if (t[r], t[r + 1], t[r + 2], t[r + 3], t[r + 4]) != (a, b, c, d, e):
                mism += 1
                if mism <= 6:
                    print(f"  MISMATCH row {idx}: exist {a},{b},{c},{d},{e} vs exe "
                          f"{t[r]},{t[r+1]},{t[r+2]},{t[r+3]},{t[r+4]}")
        print(f"[check] mismatches={mism}")
    else:
        print("[check] kHoliday[..][5] not found in map_tables.cpp")

    # 生成 [8][24][12]
    lines = [
        "// [RE 0x47FF4A] g_holidayTable [组=4*mode+map][24][12]",
        "// 生成: tools/gen_holiday_tables.py（勿手改）",
        "// {flag,type,month,day,weekday, effectFlags, resFlcLo,resFlcHi, flcParamLo,flcParamHi, musicLo,musicHi}",
        "const uint8_t kHoliday[8][24][12] = {",
    ]
    for g in range(8):
        lines.append(f"  {{ // base={g}  (mode={g // 4} map={g % 4})")
        for i in range(24):
            r = (g * 24 + i) * 12
            row = t[r:r + 12]
            lines.append("    {" + ",".join(str(v) for v in row) + "},")
        lines.append("  },")
    lines.append("};")
    out = Path("build/kHoliday_gen.cpp.txt")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"[emit] wrote {out} ({len(lines)} lines)")

    # --write: 就地更新 map_tables.cpp 定义块 + map_tables.h extern 类型
    if "--write" in sys.argv:
        import re as _re
        block = "\n".join([
            "// [RE 0x47FF4A] g_holidayTable [组=4*mode+map][24][12]",
            "// 生成: tools/gen_holiday_tables.py --write（勿手改）",
            "// {flag,type,month,day,weekday, effectFlags, resFlcLo,resFlcHi, flcParamLo,flcParamHi, musicLo,musicHi}",
            "const uint8_t kHoliday[8][24][12] = {",
        ]) + "\n" + "\n".join(lines[4:-1]) + "\n};"
        mt = Path("src/app/map_tables.cpp")
        mt_src = mt.read_text(encoding="utf-8")
        pat = _re.compile(r"// \[RE 0x47FF4A\].*?\nconst uint8_t kHoliday\[8\]\[24\]\[5\] = \{.*?\n\};", _re.S)
        if pat.search(mt_src):
            mt.write_text(pat.sub(lambda _m: block, mt_src), encoding="utf-8", newline="\n")
        hdr = Path("include/game/app/map_tables.h")
        hs = hdr.read_text(encoding="utf-8")
        hs = hs.replace("extern const uint8_t kHoliday[8][24][5];",
                        "extern const uint8_t kHoliday[8][24][12];")
        hs = hs.replace("[组=4*mode+map][24]{flag,type,month,day,weekday}",
                        "[组=4*mode+map][24][12]{flag,type,month,day,weekday, effectFlags,resFlc,flcParam,music}")
        hdr.write_text(hs, encoding="utf-8", newline="\n")
        print("[write] updated map_tables.cpp + map_tables.h (kHoliday[..][12])")

        # 农历浮动节日表 dword_47639C → src/app/holiday_tables.cpp + extern
        flen = 8401
        fb = va_to_raw(FLOAT_VA)
        rows = [int.from_bytes(data[fb + i * 4: fb + i * 4 + 2], "little") for i in range(flen)]
        fl = [
            "// [RE 0x47639C] 农历浮动节日表（索引=daysSince1998，值=农历月<<8|日；findHoliday type1）",
            "// 生成: tools/gen_holiday_tables.py --write（勿手改）",
            '#include "game/app/map_tables.h"',
            "",
            "namespace rich4 {",
            "",
            "const uint16_t kFloatHoliday[] = {",
        ]
        for i in range(0, flen, 12):
            fl.append("    " + ",".join(str(x) for x in rows[i:i + 12]) + ",")
        fl += ["};", "", f"const int kFloatHolidayLen = {flen};", "", "}  // namespace rich4", ""]
        ht = Path("src/app/holiday_tables.cpp")
        ht.write_text("\n".join(fl), encoding="utf-8", newline="\n")
        hs2 = hdr.read_text(encoding="utf-8")
        if "kFloatHoliday" not in hs2:
            hs2 = hs2.replace(
                "extern const uint8_t kHoliday[8][24][12];",
                "extern const uint8_t kHoliday[8][24][12];\n"
                "// [RE 0x47639C] 农历浮动节日表（findHoliday type1；越界=不命中）\n"
                "extern const uint16_t kFloatHoliday[];\nextern const int kFloatHolidayLen;")
            hdr.write_text(hs2, encoding="utf-8", newline="\n")
        print(f"[write] {ht} kFloatHoliday[{flen}] + extern")

    # 探测浮动表 dword_47639C：dump days 0..40 的 u32（低16=月<<8|日）
    print("[probe] dword_47639C (u32[] low16 = month<<8|day, high16):")
    fr = va_to_raw(FLOAT_VA)
    for day in range(0, 44, 4):
        v = int.from_bytes(data[fr + day * 4: fr + day * 4 + 4], "little")
        lo = v & 0xFFFF
        print(f"  day{day:4d}: u32=0x{v:08x} low=0x{lo:04x} ({lo >> 8:2d}/{lo & 0xFF:2d}) hi=0x{v >> 16:04x}")


if __name__ == "__main__":
    main()
