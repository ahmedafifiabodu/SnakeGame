#pragma once

#include "../core/Colors.h"

#include <array>

namespace neoncoil::ui
{
    // The eight snake colours the player can pick from. This lives here rather
    // than inside MenuState because the multiplayer lobby has to render other
    // players' choices, and the wire protocol carries the *index* rather than an
    // RGBA triple -- a byte instead of four, and it stays stable if the palette
    // is ever retuned.
    struct ColourOption
    {
        Color colour;
        const wchar_t* name;
    };

    inline constexpr std::array<ColourOption, 8> kPlayerColours = { {
        { Color::Green,   L"EMERALD" },
        { Color::Aqua,    L"MINT" },
        { Color::Cyan,    L"AZURE" },
        { Color::Blue,    L"COBALT" },
        { Color::Magenta, L"ORCHID" },
        { Color::Coral,   L"CORAL" },
        { Color::Gold,    L"GOLD" },
        { Color::White,   L"BONE" },
    } };

    inline constexpr int playerColourCount() { return static_cast<int>(kPlayerColours.size()); }

    // Clamps rather than throwing: indices arrive from the menu and from the
    // network, and neither is trusted to be in range.
    Color playerColourAt(int index);
    const wchar_t* playerColourName(int index);
    int playerColourIndex(Color colour);
}
