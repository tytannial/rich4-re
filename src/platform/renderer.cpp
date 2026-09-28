#include "game/platform/renderer.h"

#include "game/core/log.h"

namespace rich4 {

Renderer::~Renderer() { destroy(); }

bool Renderer::create(SDL_Window* window, int logicalWidth, int logicalHeight, bool vsync) {
    m_renderer = SDL_CreateRenderer(window, nullptr);
    if (!m_renderer) {
        RICH4_LOGE("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    m_logicalWidth = logicalWidth;
    m_logicalHeight = logicalHeight;
    setWidescreen(true);
    if (vsync) {
        SDL_SetRenderVSync(m_renderer, 1);
    }
    return true;
}

void Renderer::destroy() {
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
}

void Renderer::setWidescreen(bool enabled) {
    // LETTERBOX 保持 4:3 比例并留黑边；如需填满宽屏可换 SDL_LOGICAL_PRESENTATION_STRETCH。
    SDL_SetRenderLogicalPresentation(
        m_renderer, m_logicalWidth, m_logicalHeight,
        enabled ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED);
}

void Renderer::beginFrame() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
}

void Renderer::present() { SDL_RenderPresent(m_renderer); }

} // namespace rich4
