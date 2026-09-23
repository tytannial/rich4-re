#include "game/app/date_util.h"

namespace rich4 {

namespace {

// [RE 0x47638F] byte_47638F 月份天数表（1-based；2 月闰年特判）
constexpr uint8_t kMonthDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int monthDays(int year, int month) {
    if (month == 2 && (year % 4) == 0) {
        return 29;
    }
    return kMonthDays[month];
}

} // namespace

// [RE 0x451F8C] daysSince1998
// 依据: 0x451F8C 反编译; 年累加（闰年 year%4==0 → 366）+ 月累加 byte_47638F + day-1
int32_t daysSince1998(uint32_t date) {
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    int32_t total = 0;
    for (int y = 1998; y < year; ++y) {
        total += (y % 4) ? 365 : 366;
    }
    for (int m = 1; m < month; ++m) {
        total += monthDays(year, m);
    }
    return total + day - 1;
}

// [RE 0x4521AA] dateDiff
// 依据: 0x4521AA 反编译; daysSince1998(a2) - daysSince1998(a1)
//   （例：dateDiff(今天, loanDate) = 贷款到期剩余天数）
int32_t dateDiff(uint32_t a, uint32_t b) {
    return daysSince1998(b) - daysSince1998(a);
}

// [RE 0x45218F] dateAddDays
// 依据: 0x45218F 反编译（daysSince1998 → +days → sub_45201F 天数→日期）
uint32_t dateAddDays(uint32_t date, int days) {
    int32_t remain = daysSince1998(date) + days;
    int year = 1998;
    for (;;) {
        const int yd = (year % 4) ? 365 : 366;
        if (remain < yd) {
            break;
        }
        remain -= yd;
        ++year;
    }
    int month = 1;
    for (;;) {
        const int md = monthDays(year, month);
        if (remain < md) {
            break;
        }
        remain -= md;
        ++month;
    }
    const int day = remain + 1;
    return (static_cast<uint32_t>(year) << 16) | (static_cast<uint32_t>(month) << 8) |
           static_cast<uint32_t>(day);
}

} // namespace rich4
