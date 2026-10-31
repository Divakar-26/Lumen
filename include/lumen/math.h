#ifndef LUMEN_MATH_H
#define LUMEN_MATH_H

#include <cmath>

namespace Lumen {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_value, float y_value) noexcept : x(x_value), y(y_value) {}

    constexpr Vec2 operator+(Vec2 rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(Vec2 rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(float scalar) const noexcept { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(float scalar) const noexcept { return {x / scalar, y / scalar}; }
    constexpr Vec2& operator+=(Vec2 rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(Vec2 rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_value, float y_value, float z_value) noexcept
        : x(x_value), y(y_value), z(z_value) {}

    constexpr Vec3 operator+(Vec3 rhs) const noexcept { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(Vec3 rhs) const noexcept { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator*(float scalar) const noexcept { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(float scalar) const noexcept { return {x / scalar, y / scalar, z / scalar}; }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float x_value, float y_value, float z_value, float w_value) noexcept
        : x(x_value), y(y_value), z(z_value), w(w_value) {}

    constexpr Vec4 operator+(Vec4 rhs) const noexcept { return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w}; }
    constexpr Vec4 operator-(Vec4 rhs) const noexcept { return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w}; }
    constexpr Vec4 operator*(float scalar) const noexcept { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
};

struct Insets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

struct Rect {
    Vec2 min{};
    Vec2 max{};

    constexpr Rect() noexcept = default;
    constexpr Rect(Vec2 min_value, Vec2 max_value) noexcept : min(min_value), max(max_value) {}
    constexpr Rect(float x, float y, float width, float height) noexcept
        : min{x, y}, max{x + width, y + height} {}

    constexpr float Width() const noexcept { return max.x - min.x; }
    constexpr float Height() const noexcept { return max.y - min.y; }
    constexpr Vec2 Size() const noexcept { return {Width(), Height()}; }
    constexpr Vec2 Center() const noexcept { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    constexpr bool IsEmpty() const noexcept { return max.x <= min.x || max.y <= min.y; }
    constexpr bool Contains(Vec2 point) const noexcept {
        return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
};

struct Transform {
    Vec2 translation{};
    Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;
    Vec2 anchor{};
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() noexcept = default;
    constexpr Color(float red, float green, float blue, float alpha = 1.0f) noexcept
        : r(red), g(green), b(blue), a(alpha) {}
};

inline bool IsFinite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline bool IsFinite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool IsFinite(const Rect& value) noexcept {
    return IsFinite(value.min) && IsFinite(value.max);
}

inline float Clamp(float value, float low, float high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

inline float Lerp(float from, float to, float t) noexcept {
    return from + (to - from) * t;
}

inline Vec2 Lerp(Vec2 from, Vec2 to, float t) noexcept {
    return {Lerp(from.x, to.x, t), Lerp(from.y, to.y, t)};
}

inline Vec3 Lerp(Vec3 from, Vec3 to, float t) noexcept {
    return {Lerp(from.x, to.x, t), Lerp(from.y, to.y, t), Lerp(from.z, to.z, t)};
}

inline Vec4 Lerp(Vec4 from, Vec4 to, float t) noexcept {
    return {Lerp(from.x, to.x, t), Lerp(from.y, to.y, t), Lerp(from.z, to.z, t), Lerp(from.w, to.w, t)};
}

} // namespace Lumen

#endif
