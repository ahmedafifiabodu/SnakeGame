#include "Hit.h"

#include "../core/Input.h"

namespace neoncoil::ui
{
    void HitMap::add(int id, int cellX, int cellY, int width, int height)
    {
        if (width <= 0 || height <= 0 || id == kNone)
            return;

        m_regions.push_back(Region{ id, cellX, cellY, width, height });
    }

    int HitMap::at(Vec2 cell) const
    {
        for (auto it = m_regions.rbegin(); it != m_regions.rend(); ++it)
        {
            if (cell.x >= it->x && cell.x < it->x + it->w &&
                cell.y >= it->y && cell.y < it->y + it->h)
            {
                return it->id;
            }
        }

        return kNone;
    }

    int HitMap::hovered(const Input& input) const
    {
        if (!input.mouseInside())
            return kNone;
        return at(input.mouseCell());
    }

    int HitMap::clicked(const Input& input) const
    {
        if (!input.mouseClicked())
            return kNone;
        return at(input.mouseCell());
    }
}
