#pragma once

#include "../core/Vec2.h"

#include <vector>

namespace neoncoil
{
    class Input;
}

namespace neoncoil::ui
{
    // Clickable regions, in cell coordinates.
    //
    // Immediate mode: a screen registers its regions while it draws, and reads
    // them on the next frame's update. That one-frame lag is invisible, and it
    // means a control is clickable exactly when it is visible -- there is no
    // second table of rectangles to keep in step with the layout.
    class HitMap
    {
    public:
        static constexpr int kNone = -1;

        void clear() { m_regions.clear(); }
        void add(int id, int cellX, int cellY, int width, int height);

        // Later regions win, matching draw order: a button drawn on top of a
        // panel is what a click on both of them hits.
        int at(Vec2 cell) const;

        // Convenience for the common pair of questions a screen asks.
        int hovered(const Input& input) const;
        int clicked(const Input& input) const;

    private:
        struct Region
        {
            int id{ kNone };
            int x{ 0 };
            int y{ 0 };
            int w{ 0 };
            int h{ 0 };
        };

        std::vector<Region> m_regions;
    };
}
