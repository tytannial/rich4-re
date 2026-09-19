#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>

namespace rich4 {

// [PORT Win32:SetWindowsHookExA(WH_KEYBOARD)] 键盘输入
// 替换依据: 0x4015D6 安装全局键盘钩子 fn(0x401010)，读取 word_497168 键位表实现
//           快捷键（见 docs/formats/cfg.md）→ SDL_EVENT_KEY_DOWN 在事件循环中分发
// 对应 RICH4.CFG 的 28 个功能键位，索引语义见 docs/formats/cfg.md。
enum class GameKey : int {
    Up = 0,
    Right,
    Down,
    Left,
    MouseLeft,
    Cancel,
    Key6,
    Confirm,
    Key8,
    Key9,
    Back,
    Key11,
    Quick12,
    Quick13,
    Quick14,
    Quick15,
    Quick16,
    Quick17,
    Cycle18,
    Cycle19,
    Quick20,
    Quick21,
    Quick22,
    Quick23,
    Quick24,
    Key25,
    Key26,
    SaveExit,
    Count
};

class Input {
public:
    bool loadConfig(const std::string& cfgPath);
    void setDefaultBindings();

    void handleEvent(const SDL_Event& event);
    void newFrame();

    bool isDown(GameKey key) const;
    bool wasPressed(GameKey key) const;

private:
    std::array<uint16_t, static_cast<size_t>(GameKey::Count)> m_bindings{};
    std::array<bool, static_cast<size_t>(GameKey::Count)> m_down{};
    std::array<bool, static_cast<size_t>(GameKey::Count)> m_pressed{};
};

} // namespace rich4
