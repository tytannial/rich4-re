#include "game/platform/input.h"

#include "game/core/log.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {
constexpr size_t kKeyCount = static_cast<size_t>(GameKey::Count);
}

void Input::setDefaultBindings() {
    // 默认键位来自 exe 中的 unk_47EDC2（见 docs/formats/cfg.md）。
    static const uint16_t kDefaults[kKeyCount] = {
        0x26, 0x27, 0x28, 0x25, 0x0D, 0x1B, 0x09, 0x09,
        0x59, 0x4E, 0x20, 0x44, 0x57, 0x58, 0x43, 0x45,
        0x46, 0x4D, 0xBC, 0xBE, 0x41, 0x56, 0x53, 0x4C,
        0x48, 0x21, 0x22, 0x11,
    };
    std::memcpy(m_bindings.data(), kDefaults, sizeof(kDefaults));
}

bool Input::loadConfig(const std::string& cfgPath) {
    setDefaultBindings();
    std::FILE* fp = std::fopen(cfgPath.c_str(), "rb");
    if (!fp) {
        RICH4_LOGW("RICH4.CFG not found (%s), using default key bindings", cfgPath.c_str());
        return false;
    }
    uint8_t settings[16];
    size_t n = std::fread(settings, 1, sizeof(settings), fp);
    n += std::fread(m_bindings.data(), 1, kKeyCount * sizeof(uint16_t), fp);
    std::fclose(fp);
    if (n < sizeof(settings) + kKeyCount * sizeof(uint16_t)) {
        RICH4_LOGW("RICH4.CFG too short, using default key bindings");
        setDefaultBindings();
        return false;
    }
    return true;
}

void Input::handleEvent(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
        return;
    }
    const uint16_t key = static_cast<uint16_t>(event.key.key);
    const bool down = event.type == SDL_EVENT_KEY_DOWN;
    for (size_t i = 0; i < kKeyCount; ++i) {
        if (m_bindings[i] == key) {
            if (down && !m_down[i]) {
                m_pressed[i] = true;
            }
            m_down[i] = down;
        }
    }
}

void Input::newFrame() { m_pressed.fill(false); }

bool Input::isDown(GameKey key) const { return m_down[static_cast<size_t>(key)]; }

bool Input::wasPressed(GameKey key) const { return m_pressed[static_cast<size_t>(key)]; }

} // namespace rich4
