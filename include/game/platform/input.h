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

// [RE 0x401010] SDL 键码 → Windows 虚拟键码（原版键盘钩子 wParam 比较用；
// 仅覆盖 byte_47EDFA 表中可绑定的 78 键，见 hotkey 界面名称表）
uint8_t sdlKeycodeToVk(SDL_Keycode key);

class Input {
public:
    static constexpr size_t kKeyCount = static_cast<size_t>(GameKey::Count);

    bool loadConfig(const std::string& cfgPath);
    void setDefaultBindings();

    void handleEvent(const SDL_Event& event);
    void newFrame();

    bool isDown(GameKey key) const;
    bool wasPressed(GameKey key) const;

    // 热键设置界面读写键位表（对应 word_497168，28 x u16）
    const uint16_t* bindings() const { return m_bindings.data(); }
    void setBindings(const uint16_t* values);

private:
    std::array<uint16_t, kKeyCount> m_bindings{};
    std::array<bool, kKeyCount> m_down{};
    std::array<bool, kKeyCount> m_pressed{};
    // [RE 0x401010] word_46CB07 累积值（CTRL 按下置 0x1100，其他键 |= VK，抬起清零）
    uint16_t m_acc = 0;
};

} // namespace rich4
