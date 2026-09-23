#pragma once

#include <cstdint>

namespace rich4 {

// 日期工具（定义见 src/app/date_util.cpp）。日期编码 = (year<<16)|(month<<8)|day，
// 同 g_gameDate 0x497160 与存档日期；闰年规则 year%4==0（原版 byte_47638F 月表 + 特判）。

// [RE 0x451F8C] daysSince1998：1998-01-01 起的累计天数（闰年 366）
int32_t daysSince1998(uint32_t date);

// [RE 0x4521AA] dateDiff：a2 相对 a1 的天数差（daysSince1998(a2) − daysSince1998(a1)）
int32_t dateDiff(uint32_t a, uint32_t b);

// [RE 0x45218F] dateAddDays：日期 +days 天（经 0x45201F 天数→日期）
uint32_t dateAddDays(uint32_t date, int days);

} // namespace rich4
