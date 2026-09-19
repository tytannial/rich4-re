#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/platform/audio.h"
#include "game/platform/input.h"
#include "game/platform/renderer.h"
#include "game/platform/window.h"
#include "game/resource/spr.h"

namespace rich4 {

// 引擎骨架：初始化平台层，加载一份测试资源并渲染，验证数据链路。
class Application {
public:
    bool init(const std::string& gameDir);
    void run();
    void shutdown();

private:
    bool loadTestSprite();

    std::string m_gameDir;
    Window m_window;
    Renderer m_renderer;
    Input m_input;
    Audio m_audio;

    bool m_running = false;
    std::vector<uint8_t> m_sprData;
    Spr m_spr;
    SDL_Texture* m_texture = nullptr;
    float m_texW = 0.0f;
    float m_texH = 0.0f;
};

} // namespace rich4
