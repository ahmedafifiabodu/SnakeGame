#pragma once

#include <array>

namespace neoncoil::font
{
    // A 5x5 uppercase bitmap font. Every caption, every HUD label and every
    // glyph in the atlas comes from this one table.
    //
    // Uppercase-only is a style decision, not a limitation: the game is an
    // arcade cabinet pastiche, and it means the whole game ships with no font
    // file, no licensing question and no rasteriser dependency. Lowercase input
    // is folded to uppercase by rowsFor().
    inline constexpr int kWidth = 5;
    inline constexpr int kHeight = 5;

    using Rows = std::array<const char*, kHeight>;

    // Returns the 5x5 pattern for `character` ('#' = lit, anything else = off).
    // Unsupported characters return blank, never null.
    const Rows& rowsFor(wchar_t character);

    // True when the character has a real pattern (i.e. is not silently blank).
    bool isSupported(wchar_t character);
}
