#pragma once

#include "../core/Colors.h"
#include "../core/Screen.h"
#include "../core/Vec2.h"

#include <string>
#include <string_view>
#include <vector>

namespace neoncoil::ui
{
    // Where the board is being drawn this frame, in virtual pixels. Passed
    // around instead of read from Layout directly so screen shake is just an
    // offset on the origin rather than a special case in every draw call.
    struct BoardView
    {
        float originX{ 0.0f };
        float originY{ 0.0f };
        float tileSize{ 25.0f };

        float left(int tileX) const { return originX + static_cast<float>(tileX) * tileSize; }
        float top(int tileY) const { return originY + static_cast<float>(tileY) * tileSize; }
        float centreX(float tileX) const { return originX + (tileX + 0.5f) * tileSize; }
        float centreY(float tileY) const { return originY + (tileY + 0.5f) * tileSize; }
    };

    // Solid square tile, optionally inset so neighbouring tiles read as
    // separate blocks rather than one mass.
    void boardTile(Screen& screen, const BoardView& view, Vec2 tile, Color colour, float inset = 0.0f);

    // Glyph from the atlas, scaled to `fraction` of the tile and centred.
    void boardGlyph(Screen& screen, const BoardView& view, Vec2 tile, wchar_t glyph,
        Color colour, float fraction = 1.0f);

    // Thin meter drawn in pixels but positioned on the cell grid, so callers
    // keep using cell coordinates while the bar itself stays a sensible height
    // instead of filling a whole 24px row.
    void progressBar(Screen& screen, int cellX, int cellY, int widthInCells, float fraction,
        Color filled, Color empty, Color background);

    // "[KEY] Label" pairs used along the footer. Returns the width drawn.
    int keyHint(Screen& screen, int x, int y, std::wstring_view key, std::wstring_view label, Color background);

    std::wstring toWide(const std::string& text);
    std::wstring padTo(std::wstring text, int width);
    std::wstring truncateTo(std::wstring text, int width);

    // Greedy word wrap. Words longer than `width` are hard-split.
    std::vector<std::wstring> wrapText(std::wstring_view text, int width);
}
