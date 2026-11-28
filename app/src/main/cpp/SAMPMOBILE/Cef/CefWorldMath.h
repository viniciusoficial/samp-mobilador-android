#pragma once

/**
 * World2D 叠加层的纯几何计算（无 JNI、无游戏符号、无 android 依赖）。
 *
 * 单独成头文件的理由和 CefLogic.java 一样：这是"决定页面画在哪"的公式，
 * 一旦写错，页面就会飘到屏幕外或者尺寸不对，而设备端 logcat 在这台 ROM 上
 * 不可靠，看不出是公式错还是投影错。公式是 constexpr，所以下面那组
 * static_assert 会在**每次编译时**执行——等于给这条公式挂了一个永久回归。
 *
 * 语义对照 PC src/client/core/browser/manager.cpp:1569 附近：
 *
 *   if (!CSprite::CalcScreenCoors(pos, &screen, &w, &h, true, true)) continue;
 *   if (screen.z <= 0.0f) continue;                       // 相机背后
 *   ImVec2 drawPos(screen.x - size.x * pivot.x,
 *                  screen.y - size.y * pivot.y);
 */
namespace cefworld
{

struct Placement
{
    float x = 0.0f;          // 页面左上角（屏幕像素）
    float y = 0.0f;
    bool onScreen = false;   // false = 这一帧不该显示
};

constexpr float Abs(float v) noexcept
{
    return v < 0.0f ? -v : v;
}

/**
 * 投影结果 -> 页面左上角。
 *
 * @param screenX/Y/Z CSprite::CalcScreenCoors 的输出
 * @param width/height 浏览器视口像素尺寸
 * @param pivotX/Y 锚点（PC 默认 0.5 / 1.0，即底部居中）
 * @param projected 投影函数是否返回真
 */
constexpr Placement Place2D(float screenX, float screenY, float screenZ,
                            float width, float height,
                            float pivotX, float pivotY,
                            bool projected) noexcept
{
    // 相机背后的点 screen.z <= 0，PC 也是直接跳过（否则会镜像画到脸上）。
    if (!projected || !(screenZ > 0.0f))
        return Placement{ 0.0f, 0.0f, false };

    return Placement{
        screenX - width * pivotX,
        screenY - height * pivotY,
        true
    };
}

/** 位置变化 ≥ eps 才值得过一次 JNI（PC 每帧重画，Android 每帧过 JNI 太贵）。 */
constexpr bool MovedBy(float a, float b, float eps = 0.5f) noexcept
{
    return !(Abs(a - b) < eps);
}

// ---------------------------------------------------------------------------
// 编译期回归。数值来自真机实测的一次 World2D：
//   world=(2090.6,1164.6,11.6) -> screen=(1149.0,239.3)
//   size=512x256, pivot=(0.5,1.0) -> pos=(893,-17)   （893 = 1149 - 256，
//   -17 = 239 - 256；页面底部正好贴在角色头顶的投影点上）
// ---------------------------------------------------------------------------

// 底部居中锚点：x 减半宽，y 减全高。
static_assert(Place2D(1000.0f, 500.0f, 10.0f, 512.0f, 256.0f, 0.5f, 1.0f, true).x == 744.0f);
static_assert(Place2D(1000.0f, 500.0f, 10.0f, 512.0f, 256.0f, 0.5f, 1.0f, true).y == 244.0f);
static_assert(Place2D(1000.0f, 500.0f, 10.0f, 512.0f, 256.0f, 0.5f, 1.0f, true).onScreen);

// 真机那一帧。
static_assert(Place2D(1149.0f, 239.0f, 11.6f, 512.0f, 256.0f, 0.5f, 1.0f, true).x == 893.0f);
static_assert(Place2D(1149.0f, 239.0f, 11.6f, 512.0f, 256.0f, 0.5f, 1.0f, true).y == -17.0f);

// 左上角锚点：不动。
static_assert(Place2D(300.0f, 200.0f, 5.0f, 128.0f, 64.0f, 0.0f, 0.0f, true).x == 300.0f);
static_assert(Place2D(300.0f, 200.0f, 5.0f, 128.0f, 64.0f, 0.0f, 0.0f, true).y == 200.0f);

// 中心锚点。
static_assert(Place2D(300.0f, 200.0f, 5.0f, 128.0f, 64.0f, 0.5f, 0.5f, true).x == 236.0f);
static_assert(Place2D(300.0f, 200.0f, 5.0f, 128.0f, 64.0f, 0.5f, 0.5f, true).y == 168.0f);

// 相机背后（z <= 0）与投影失败都必须隐藏，且坐标归零（避免上一帧的位置被复用）。
static_assert(!Place2D(100.0f, 100.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0.5f, true).onScreen);
static_assert(!Place2D(100.0f, 100.0f, -3.0f, 64.0f, 64.0f, 0.5f, 0.5f, true).onScreen);
static_assert(!Place2D(100.0f, 100.0f, 10.0f, 64.0f, 64.0f, 0.5f, 0.5f, false).onScreen);
static_assert(Place2D(100.0f, 100.0f, -3.0f, 64.0f, 64.0f, 0.5f, 0.5f, true).x == 0.0f);
static_assert(Place2D(100.0f, 100.0f, 10.0f, 64.0f, 64.0f, 0.5f, 0.5f, false).y == 0.0f);

// 亚像素抖动门限：< 0.5px 不回调，>= 0.5px 回调；两个方向都要生效。
static_assert(!MovedBy(0.0f, 0.4f));
static_assert(MovedBy(0.0f, 0.5f));
static_assert(!MovedBy(100.0f, 100.4f));
static_assert(MovedBy(100.0f, 99.5f));
static_assert(!MovedBy(-0.4f, 0.0f));
static_assert(MovedBy(-0.5f, 0.0f));

} // namespace cefworld
