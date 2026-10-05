#include "game/app/ui_layout.h"

#include "game/render/surface.h"

namespace rich4 {

int LayoutNativeGuard::s_depth = 0;
int LayoutNativeGuard::s_worldOriginX = 0;
int LayoutNativeGuard::s_prevWorldOriginX = 0;

LayoutNativeGuard::LayoutNativeGuard(int worldOriginX) {
    if (s_depth == 0) {
        s_prevWorldOriginX = s_worldOriginX;
        s_worldOriginX = worldOriginX;
    }
    ++s_depth;
}

LayoutNativeGuard::~LayoutNativeGuard() {
    --s_depth;
    if (s_depth == 0) {
        s_worldOriginX = s_prevWorldOriginX;
    }
}

bool layoutNativeLocked() { return LayoutNativeGuard::locked(); }

int uiLayoutWorldOriginX() { return LayoutNativeGuard::worldOriginX(); }

int uiLogicalWidth(const Surface& surface) {
    if (layoutNativeLocked()) {
        return Surface::kWidth; // 640：原版布局（模态背景 native 重绘专用）
    }
    const float s = surface.scale();
    return s > 0.0f ? static_cast<int>(surface.width() / s + 0.5f) : surface.width();
}

int uiLogicalHeight(const Surface& surface) {
    const float s = surface.scale();
    return s > 0.0f ? static_cast<int>(surface.height() / s + 0.5f) : surface.height();
}

int uiMapLogicalWidth(const Surface& surface) {
    const int w = uiLogicalWidth(surface) - kPanelW;
    // 防御：画布异常小（< 右栏）时回退原版地图区宽
    return w > 0 ? w : 440;
}

} // namespace rich4
