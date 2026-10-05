#pragma once

namespace rich4 {

class Surface;

// [NEW M4-D] UI 布局派生（第一批）：整幅放大模型（画布 = 逻辑×scale）下，
//   逻辑画布宽 = 设备宽/scale：native/4:3 = 640；宽屏画布（>4:3）= >640。
//   顶栏（40 逻辑高）与右栏（200 逻辑宽）保持原版尺寸，多出的宽度全部给地图区
//   （宽屏视野扩展）；右栏起点 = 地图区右缘。命中判定与绘制共用本表派生，
//   保证宽屏下点击坐标不错位。
inline constexpr int kTopBarH = 40; // 顶栏逻辑高（原版 0x417E26 / 工具条素材 440x40）
inline constexpr int kPanelW = 200; // 右栏逻辑宽（原版 440..640）

// 逻辑画布宽/高（= 设备宽/高 ÷ scale；scale=1 时等于设备尺寸）
int uiLogicalWidth(const Surface& surface);
int uiLogicalHeight(const Surface& surface);

// [NEW M4-D 实机] 临时锁定 native(640) 布局 + 世界绘制原点偏移：全屏剧场模态（fillBars）
//   进入时以原版 640 布局把游戏画面重绘到画布居中区（否则宽屏世界布局直接保留会被两侧
//   黑边裁掉右栏 → "右栏被切半"观感）。仅在 renderModalBackdrop 期间生效，其余时刻 identity。
bool layoutNativeLocked();
// 当前世界绘制原点偏移（= renderModalBackdrop 的 base；其余时刻 0）。
//   供内部会**绝对 setOrigin** 的绘制段（右栏 game_panel）叠加，与 blit 家族自动 +origin 一致。
int uiLayoutWorldOriginX();

// [NEW M4-D 实机] RAII：锁定/恢复 native 布局与世界原点（嵌套安全，计数式）
class LayoutNativeGuard {
public:
    explicit LayoutNativeGuard(int worldOriginX = 0);
    ~LayoutNativeGuard();
    LayoutNativeGuard(const LayoutNativeGuard&) = delete;
    LayoutNativeGuard& operator=(const LayoutNativeGuard&) = delete;
    static bool locked() { return s_depth > 0; }
    static int worldOriginX() { return s_worldOriginX; }

private:
    static int s_depth;
    static int s_worldOriginX;
    static int s_prevWorldOriginX;
};
// 地图区逻辑宽 = 逻辑画布宽 - 右栏（native 440；宽屏 >440）
int uiMapLogicalWidth(const Surface& surface);
// 右栏逻辑 x（= 地图区右缘）
inline int uiPanelLogicalX(const Surface& surface) { return uiMapLogicalWidth(surface); }
// 右栏绘制原点偏移（= 右栏右移量；右栏函数内坐标保持 440 基准，经 SurfaceOriginGuard 平移）
// [NEW M4-D 实机] 叠加世界原点：native 模态背景重绘（renderModalBackdrop）期间右栏
//   也要整体 +base（game_panel 用绝对 setOrigin，不叠加会覆盖外层原点 → 右栏贴左）
inline int uiPanelOffsetX(const Surface& surface) {
    return uiMapLogicalWidth(surface) - 440 + uiLayoutWorldOriginX();
}
// [NEW M4-D] 640 基准 UI（主菜单/选人/模态）水平居中偏移：native/free=0；wide(853)=106。
//   由 Application::dispatchModalAware 统一消费（事件坐标 -base、绘制原点 +base）
inline int uiModalBaseX(const Surface& surface) {
    const int w = (uiLogicalWidth(surface) - 640) / 2;
    return w > 0 ? w : 0;
}
// [NEW M4-D 实机] 屏幕锚点游戏 UI（前进面板/骰子动画，原版以 440 地图区中心 220 为基准）
//   在地图区加宽后的水平平移量：native 0；宽屏 = 视口中心 - 220（跟随角色所在视口中心）
inline int uiMapCenterShiftX(const Surface& surface) {
    return uiMapLogicalWidth(surface) / 2 - 220;
}

} // namespace rich4
