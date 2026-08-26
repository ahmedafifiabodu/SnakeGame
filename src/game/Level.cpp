#include "Level.h"

#include <algorithm>

namespace neoncoil
{
    void Level::resize(int width, int height)
    {
        m_width = std::max(0, width);
        m_height = std::max(0, height);
        m_tiles.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), Tile::Empty);
        m_openTiles.clear();
        m_sentinels.clear();
    }

    bool Level::inBounds(Vec2 position) const
    {
        return position.x >= 0 && position.x < m_width && position.y >= 0 && position.y < m_height;
    }

    Tile Level::at(Vec2 position) const
    {
        if (!inBounds(position))
            return Tile::Wall;
        return m_tiles[static_cast<std::size_t>(position.y) * m_width + position.x];
    }

    void Level::set(Vec2 position, Tile tile)
    {
        if (!inBounds(position))
            return;
        m_tiles[static_cast<std::size_t>(position.y) * m_width + position.x] = tile;
    }

    bool Level::isBorder(Vec2 position) const
    {
        if (!inBounds(position))
            return true;
        return position.x == 0 || position.y == 0 || position.x == m_width - 1 || position.y == m_height - 1;
    }

    bool Level::destroyWall(Vec2 position)
    {
        if (!inBounds(position) || isBorder(position) || at(position) != Tile::Wall)
            return false;

        set(position, Tile::Empty);
        m_openTiles.push_back(position);
        return true;
    }

    void Level::rebuildOpenTiles()
    {
        m_openTiles.clear();
        m_openTiles.reserve(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) / 2);

        for (int y = 0; y < m_height; ++y)
            for (int x = 0; x < m_width; ++x)
                if (m_tiles[static_cast<std::size_t>(y) * m_width + x] == Tile::Empty)
                    m_openTiles.push_back({ x, y });
    }

    void Level::updateHazards(float deltaSeconds)
    {
        for (Sentinel& sentinel : m_sentinels)
        {
            sentinel.timer += deltaSeconds;
            if (sentinel.timer < sentinel.moveInterval)
                continue;

            sentinel.timer -= sentinel.moveInterval;

            const Vec2 next = sentinel.position + sentinel.step;
            if (isWall(next))
            {
                // Bounce. If both directions are blocked the sentinel simply
                // stays put, which is harmless.
                sentinel.step = sentinel.step * -1;
                const Vec2 back = sentinel.position + sentinel.step;
                if (!isWall(back))
                    sentinel.position = back;
            }
            else
            {
                sentinel.position = next;
            }
        }
    }

    bool Level::hazardAt(Vec2 position) const
    {
        return std::any_of(m_sentinels.begin(), m_sentinels.end(),
            [position](const Sentinel& sentinel) { return sentinel.position == position; });
    }
}
