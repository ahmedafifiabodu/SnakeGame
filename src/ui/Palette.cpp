#include "Palette.h"

#include <algorithm>

namespace neoncoil::ui
{
    Color playerColourAt(int index)
    {
        const int clamped = std::clamp(index, 0, playerColourCount() - 1);
        return kPlayerColours[static_cast<std::size_t>(clamped)].colour;
    }

    const wchar_t* playerColourName(int index)
    {
        const int clamped = std::clamp(index, 0, playerColourCount() - 1);
        return kPlayerColours[static_cast<std::size_t>(clamped)].name;
    }

    int playerColourIndex(Color colour)
    {
        for (std::size_t i = 0; i < kPlayerColours.size(); ++i)
            if (kPlayerColours[i].colour == colour)
                return static_cast<int>(i);
        return 0;
    }
}
