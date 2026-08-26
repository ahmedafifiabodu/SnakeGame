#pragma once

#include <functional>

namespace neoncoil
{
    // Integer grid position / offset. Replaces COORD, whose SHORT members
    // silently truncate and make arithmetic easy to get wrong.
    struct Vec2
    {
        int x{ 0 };
        int y{ 0 };

        constexpr Vec2() = default;
        constexpr Vec2(int inX, int inY) : x(inX), y(inY) {}

        constexpr Vec2 operator+(const Vec2& rhs) const { return { x + rhs.x, y + rhs.y }; }
        constexpr Vec2 operator-(const Vec2& rhs) const { return { x - rhs.x, y - rhs.y }; }
        constexpr Vec2 operator*(int scalar) const { return { x * scalar, y * scalar }; }

        Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }

        constexpr bool operator==(const Vec2& rhs) const { return x == rhs.x && y == rhs.y; }
        constexpr bool operator!=(const Vec2& rhs) const { return !(*this == rhs); }
    };

    constexpr int manhattan(const Vec2& a, const Vec2& b)
    {
        const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
        const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
        return dx + dy;
    }
}

template <>
struct std::hash<neoncoil::Vec2>
{
    std::size_t operator()(const neoncoil::Vec2& v) const noexcept
    {
        return static_cast<std::size_t>(v.x) * 73856093u ^ static_cast<std::size_t>(v.y) * 19349663u;
    }
};
