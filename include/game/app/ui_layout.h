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
// 地图区逻辑宽 = 逻辑画布宽 - 右栏（native 440；宽屏 >440）
int uiMapLogicalWidth(const Surface& surface);
// 右栏逻辑 x（= 地图区右缘）
inline int uiPanelLogicalX(const Surface& surface) { return uiMapLogicalWidth(surface); }
// 右栏绘制原点偏移（= 右栏右移量；右栏函数内坐标保持 440 基准，经 SurfaceOriginGuard 平移）
inline int uiPanelOffsetX(const Surface& surface) { return uiMapLogicalWidth(surface) - 440; }

} // namespace rich4
