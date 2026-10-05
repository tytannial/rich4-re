#include "game/app/ui_layout.h"

#include "game/render/surface.h"

namespace rich4 {

int uiLogicalWidth(const Surface& surface) {
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
