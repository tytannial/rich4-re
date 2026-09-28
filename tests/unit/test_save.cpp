#include "microtest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "game/app/save_data.h"
#include "game/game_state.h"

// [NEW] L0 单测：存档往返（[RE 0x402FD1/0x402AC5]，与 --save-selftest 同源逻辑）
MT_TEST(save_roundtrip_stable) {
    const std::string src = mt::g_gameDir + "/SAVE0.DAT";
    std::vector<uint8_t> raw;
    if (!rich4::readSaveFile(src, raw)) {
        std::printf("  SKIP (no %s)\n", src.c_str());
        return;
    }
    rich4::GameState a;
    MT_CHECK(rich4::parseSaveBody(a, raw));
    MT_EQ(a.playerCount, 4);
    MT_CHECK(a.cellEnts.size() > 10);
    MT_CHECK(a.estates.size() > 10);
    // save∘parse∘save 字节稳定
    const std::string dir = std::string(".");
    MT_CHECK(rich4::saveGameToSlot(dir, 900, a));
    std::vector<uint8_t> r900;
    rich4::GameState b;
    MT_CHECK(rich4::readSaveFile("./SAVE900.DAT", r900));
    MT_CHECK(rich4::parseSaveBody(b, r900));
    MT_CHECK(rich4::saveGameToSlot(dir, 901, b));
    std::vector<uint8_t> r901;
    rich4::readSaveFile("./SAVE901.DAT", r901);
    MT_CHECK(r900 == r901);
    MT_EQ(r900.size(), raw.size());
    std::remove("./SAVE900.DAT");
    std::remove("./SAVE901.DAT");
}

MT_TEST(save_header) {
    const std::string src = mt::g_gameDir + "/SAVE0.DAT";
    rich4::SaveSlot slot;
    if (!rich4::readSaveHeader(src, slot)) {
        std::printf("  SKIP (no %s)\n", src.c_str());
        return;
    }
    MT_CHECK(slot.valid);
    MT_EQ(slot.mapIndex, 0);
}
