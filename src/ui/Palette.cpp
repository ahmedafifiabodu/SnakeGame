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

    Color pingColour(int milliseconds)
    {
        if (milliseconds < 0)
            return Color::Slate;
        if (milliseconds < 60)
            return Color::Lime;      // under half a step: unnoticeable
        if (milliseconds < 120)
            return Color::Gold;      // about one step
        if (milliseconds < 220)
            return Color::Amber;
        return Color::Red;           // nearly two steps behind the host
    }

    std::wstring pingText(int milliseconds)
    {
        if (milliseconds < 0)
            return L"--";
        return std::to_wstring(milliseconds) + L" ms";
    }
}
