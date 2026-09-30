#include "microtest.h"

#include "game/core/big5_tables.h"
#include "game/core/encoding.h"

// [NEW] L0 单测：BIG5(cp950) → UTF-8 内嵌映射表（跨平台改造阶段3）
// 依据: 原版 Windows 经 MultiByteToWideChar(950) 转码；重写统一走内嵌表
//       （tools/gen_big5_tables.py 生成，docs/cross-platform.md）。
// 期望值由同一 Python cp950 codec 生成（表与期望同源）。

MT_TEST(big5_ascii_and_empty) {
    MT_CHECK(rich4::big5ToUtf8("RICH4") == "RICH4");
    MT_CHECK(rich4::big5ToUtf8("") == "");
    MT_CHECK(rich4::big5ToUtf8(nullptr) == "");
    MT_CHECK(rich4::big5ToUtf8("Game 123 #$%") == "Game 123 #$%");
}

MT_TEST(big5_common_phrases) {
    // 大富翁4: A46A B449 AFCE 34
    const char in1[] = {'\xA4', '\x6A', '\xB4', '\x49', '\xAF', '\xCE', '4', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in1) == u8"大富翁4");
    // 屬: C4DD
    const char in2[] = {'\xC4', '\xDD', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in2) == u8"屬");
    // ，: A141；。: A143
    const char in3[] = {'\xA1', '\x41', '\xA1', '\x43', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in3) == u8"，。");
    // 台積電: A578 BF6E B971
    const char in4[] = {'\xA5', '\x78', '\xBF', '\x6E', '\xB9', '\x71', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in4) == u8"台積電");
    // ＄(全角): A243; 平假名/符號區 A140 = U+3000 全角空格
    const char in5[] = {'\xA2', '\x43', '\xA1', '\x40', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in5) == u8"＄　");
}

MT_TEST(big5_length_semantics) {
    // 显式 length：嵌入 0 字节照转（与 MultiByteToWideChar 按长度转换一致）
    const char buf[4] = {'A', '\0', 'B', '\0'};
    const std::string out = rich4::big5ToUtf8(buf, 4);
    MT_EQ(static_cast<int>(out.size()), 4);
    MT_CHECK(out[0] == 'A' && out[1] == '\0' && out[2] == 'B' && out[3] == '\0');
    // 定长字段语义：先 A440(一) 后 0 填充
    const char field[6] = {'\xA4', '\x40', '\0', '\0', '\0', '\0'};
    const std::string s = rich4::big5ToUtf8(field, 6);
    MT_CHECK(s.compare(0, 3, u8"一") == 0);
    MT_EQ(static_cast<int>(s.size()), 7); // 3(一) + 4 个嵌入 NUL
}

MT_TEST(big5_invalid_sequences) {
    // 0x80 单独出现（非法 lead）→ U+FFFD
    const char in1[] = {'\x80', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in1) == u8"\uFFFD");
    // lead 在结尾被截断 → U+FFFD
    const char in2[] = {'\xA4', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in2) == u8"\uFFFD");
    // 有效 lead + 无效 tail（尾部邻接区 0x7F 不在 cp950 tail 范围）→ 1 个替换符 + 推进
    const char in3[] = {'\xA4', '\x7F', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in3) == u8"\uFFFD\u007F");
    // 混合：ASCII + 非法 + 汉字
    const char in4[] = {'A', '\x80', '\xA4', '\x40', '\0'};
    MT_CHECK(rich4::big5ToUtf8(in4) == u8"A\uFFFD一");
}

MT_TEST(big5_full_table_count) {
    int mapped = 0;
    for (int lead = 0x81; lead <= 0xFE; ++lead) {
        for (int tail = 0; tail <= 0xFF; ++tail) {
            if (rich4::kBig5ToUnicode[(lead - 0x81) * 256 + tail] != 0) {
                ++mapped;
            }
        }
    }
    MT_EQ(mapped, 13752); // Python cp950 codec 双字节可解码数
}

MT_TEST(big5_full_table_convert) {
    // 全表 13752 项逐项转换：非空 + UTF-8 长度与码点段一致
    int bad = 0;
    for (int lead = 0x81; lead <= 0xFE; ++lead) {
        for (int tail = 0x40; tail <= 0xFE; ++tail) {
            const uint16_t cp = rich4::kBig5ToUnicode[(lead - 0x81) * 256 + tail];
            if (cp == 0) {
                continue;
            }
            const char in[2] = {static_cast<char>(lead), static_cast<char>(tail)};
            const std::string out = rich4::big5ToUtf8(in, 2);
            if (out.empty()) {
                ++bad;
                continue;
            }
            const unsigned char b0 = static_cast<unsigned char>(out[0]);
            size_t expect = 1;
            if (b0 >= 0xF0u) {
                expect = 4;
            } else if (b0 >= 0xE0u) {
                expect = 3;
            } else if (b0 >= 0xC0u) {
                expect = 2;
            }
            size_t seg = 1;
            if (cp >= 0x10000u) {
                seg = 4;
            } else if (cp >= 0x800u) {
                seg = 3;
            } else if (cp >= 0x80u) {
                seg = 2;
            }
            if (out.size() != expect || expect != seg) {
                ++bad;
            }
        }
    }
    MT_EQ(bad, 0);
}
