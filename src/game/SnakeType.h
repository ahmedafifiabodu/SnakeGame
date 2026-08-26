#pragma once

#include "../core/Colors.h"

#include <string>
#include <vector>

namespace neoncoil
{
    enum class AbilityKind
    {
        Dash,
        IronScales,
        Phase,
        GoldRush,
        Shed
    };

    struct AbilityDef
    {
        AbilityKind kind{ AbilityKind::Dash };
        std::wstring name;
        std::wstring summary;          // one line, shown in the menu and HUD
        float cooldownSeconds{ 10.0f };
        float durationSeconds{ 0.0f }; // 0 means instant or charge-based
    };

    // Everything that makes one snake play differently from another lives in
    // this struct. Adding a snake means adding a row to kSnakeTypes and, if it
    // brings a new ability, one case in AbilityRuntime -- nothing else in the
    // game needs to change.
    struct SnakeType
    {
        std::wstring name;
        std::wstring tagline;
        std::vector<std::wstring> notes; // short bullet lines for the menu panel

        // Appearance. Each glyph is drawn across both cells of a board tile.
        wchar_t headGlyph{ 0 };
        wchar_t bodyGlyph{ 0 };
        wchar_t altBodyGlyph{ 0 }; // non-zero enables alternating banding
        Color accent{ Color::Green };

        // Gameplay modifiers.
        float speedMultiplier{ 1.0f };
        int startLength{ 4 };
        int growthPerFood{ 1 };
        float scoreMultiplier{ 1.0f };

        AbilityDef ability;
    };

    const std::vector<SnakeType>& snakeTypes();
    int snakeTypeCount();

    // Clamps rather than throwing: menu indices come from user input.
    const SnakeType& snakeTypeAt(int index);
}
