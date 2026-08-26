#pragma once

#include "../core/Vec2.h"

namespace neoncoil
{
    enum class Direction
    {
        Up,
        Down,
        Left,
        Right
    };

    constexpr Vec2 toDelta(Direction direction)
    {
        switch (direction)
        {
        case Direction::Up:    return { 0, -1 };
        case Direction::Down:  return { 0, 1 };
        case Direction::Left:  return { -1, 0 };
        case Direction::Right: return { 1, 0 };
        }
        return { 0, 0 };
    }

    constexpr bool isOpposite(Direction a, Direction b)
    {
        const Vec2 da = toDelta(a);
        const Vec2 db = toDelta(b);
        return da.x == -db.x && da.y == -db.y;
    }

    constexpr bool isHorizontal(Direction direction)
    {
        return direction == Direction::Left || direction == Direction::Right;
    }
}
