#include "game/platform/input.h"

#include "game/core/log.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {
constexpr size_t kKeyCount = Input::kKeyCount;
}

// [RE 0x47EDFA] 可绑定键表（78 项，每项 {u32 vk; const char* name;}，名称表见热键界面）
// 依据: 0x411122 WM_KEYDOWN 以 wParam(VK) 查表；0x401010 钩子以 wParam 匹配键位表
uint8_t sdlKeycodeToVk(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<uint8_t>('A' + (key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<uint8_t>('0' + (key - SDLK_0));
    }
    switch (key) {
        case SDLK_BACKSPACE:
            return 0x08;
        case SDLK_TAB:
            return 0x09;
        case SDLK_RETURN:
            return 0x0D;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return 0x11;
        case SDLK_ESCAPE:
            return 0x1B;
        case SDLK_SPACE:
            return 0x20;
        case SDLK_PAGEUP:
            return 0x21;
        case SDLK_PAGEDOWN:
            return 0x22;
        case SDLK_END:
            return 0x23;
        case SDLK_HOME:
            return 0x24;
        case SDLK_LEFT:
            return 0x25;
        case SDLK_UP:
            return 0x26;
        case SDLK_RIGHT:
            return 0x27;
        case SDLK_DOWN:
            return 0x28;
        case SDLK_INSERT:
            return 0x2D;
        case SDLK_ASTERISK:
            return 0x6A;
        case SDLK_KP_PLUS:
            return 0x6B;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            return 0x6D;
        case SDLK_SLASH:
        case SDLK_KP_DIVIDE:
            return 0x6F;
        case SDLK_F1:
            return 0x70;
        case SDLK_F2:
            return 0x71;
        case SDLK_F3:
            return 0x72;
        case SDLK_F4:
            return 0x73;
        case SDLK_F5:
            return 0x74;
        case SDLK_F6:
            return 0x75;
        case SDLK_F7:
            return 0x76;
        case SDLK_F8:
            return 0x77;
        case SDLK_F9:
            return 0x78;
        case SDLK_F10:
            return 0x79;
        case SDLK_F11:
            return 0x7A;
        case SDLK_F12:
            return 0x7B;
        case SDLK_SEMICOLON:
            return 0xBA;
        case SDLK_EQUALS:
            return 0xBB;
        case SDLK_COMMA:
            return 0xBC;
        case SDLK_PERIOD:
            return 0xBE;
        case SDLK_GRAVE:
            return 0xC0;
        case SDLK_LEFTBRACKET:
            return 0xDB;
        case SDLK_BACKSLASH:
            return 0xDC;
        case SDLK_RIGHTBRACKET:
            return 0xDD;
        case SDLK_APOSTROPHE:
            return 0xDE;
        default:
            return 0;
    }
}

void Input::setDefaultBindings() {
    // 默认键位来自 exe 中的 unk_47EDC2（见 docs/formats/cfg.md）。
    static const uint16_t kDefaults[kKeyCount] = {
        0x26, 0x27, 0x28, 0x25, 0x0D, 0x1B, 0x09, 0x09,
        0x59, 0x4E, 0x20, 0x44, 0x57, 0x58, 0x43, 0x45,
        0x46, 0x4D, 0xBC, 0xBE, 0x41, 0x56, 0x53, 0x4C,
        0x48, 0x21, 0x22, 0x1151,
    };
    std::memcpy(m_bindings.data(), kDefaults, sizeof(kDefaults));
}

void Input::setBindings(const uint16_t* values) {
    if (values) {
        std::memcpy(m_bindings.data(), values, kKeyCount * sizeof(uint16_t));
    }
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
    const uint8_t vk = sdlKeycodeToVk(event.key.key);
    if (vk == 0) {
        return;
    }
    // [RE 0x401010] 钩子累积 word_46CB07：CTRL → 0x1100；其他键 |= VK；
    // 任意键抬起（lParam bit31）→ 清零
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (vk == 0x11) {
            m_acc = 0x1100;
        } else {
            m_acc |= vk;
        }
    } else {
        m_acc = 0;
    }
    for (size_t i = 0; i < kKeyCount; ++i) {
        // [RE 0x401010] 索引 0-5（游標/確定/取消）钩子以单键 wParam 比较（CTRL 被忽略）；
        // 索引 6+ 以累积值比较（支持 CTRL 组合键）
        const uint16_t value = (i <= 5) ? vk : m_acc;
        const bool down = event.type == SDL_EVENT_KEY_DOWN && value == m_bindings[i];
        if (down && !m_down[i]) {
            m_pressed[i] = true;
        }
        m_down[i] = down;
    }
}

void Input::newFrame() { m_pressed.fill(false); }

bool Input::isDown(GameKey key) const { return m_down[static_cast<size_t>(key)]; }

bool Input::wasPressed(GameKey key) const { return m_pressed[static_cast<size_t>(key)]; }

} // namespace rich4
