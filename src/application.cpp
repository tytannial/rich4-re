#include "game/application.h"

#include "game/core/log.h"
#include "game/resource/mkf.h"

#include <cstring>

namespace rich4 {

bool Application::init(const std::string& gameDir) {
    m_gameDir = gameDir;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        RICH4_LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    if (!m_window.create("Rich4 Recomp", 960, 720)) {
        return false;
    }
    if (!m_renderer.create(m_window.handle(), 640, 480)) {
        return false;
    }
    m_input.loadConfig(gameDir + "/RICH4.CFG");
    m_audio.open();

    loadTestSprite();
    m_running = true;
    return true;
}

bool Application::loadTestSprite() {
    MkfArchive data;
    if (!data.load(m_gameDir + "/Data.mkf")) {
        RICH4_LOGW("Data.mkf unavailable; running without test sprite");
        return false;
    }
    for (size_t i = 0; i < data.count(); ++i) {
        auto blob = data.read(i);
        if (!blob || blob->size() < 12) {
            continue;
        }
        if (std::memcmp(blob->data(), "SPR", 3) != 0) {
            continue;
        }
        m_sprData = std::move(*blob);
        if (!m_spr.parse(m_sprData) || m_spr.frameCount() == 0) {
            continue;
        }
        const SprFrame& f = m_spr.frame(0);
        const std::vector<uint8_t> rgba = m_spr.frameRgba(0);
        m_texture = SDL_CreateTexture(m_renderer.handle(), SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, f.width, f.height);
        if (!m_texture) {
            RICH4_LOGE("SDL_CreateTexture failed: %s", SDL_GetError());
            return false;
        }
        SDL_UpdateTexture(m_texture, nullptr, rgba.data(), f.width * 4);
        m_texW = static_cast<float>(f.width);
        m_texH = static_cast<float>(f.height);
        RICH4_LOGI("Loaded test SPR: Data.mkf[%zu] %ux%u", i, f.width, f.height);
        return true;
    }
    RICH4_LOGW("No SPR resource found in Data.mkf");
    return false;
}

void Application::run() {
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                m_running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                m_running = false;
            }
            m_input.handleEvent(event);
        }

        m_renderer.beginFrame();
        if (m_texture) {
            const float cx = 320.0f;
            const float cy = 240.0f;
            SDL_FRect dst{cx - m_texW * 0.5f, cy - m_texH * 0.5f, m_texW, m_texH};
            SDL_RenderTexture(m_renderer.handle(), m_texture, nullptr, &dst);
        }
        m_renderer.present();
        m_input.newFrame();
    }
}

void Application::shutdown() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_audio.close();
    m_renderer.destroy();
    m_window.destroy();
    SDL_Quit();
}

} // namespace rich4
