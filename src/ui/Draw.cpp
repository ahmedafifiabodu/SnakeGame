#include "Draw.h"

#include "../core/Glyphs.h"

#include <algorithm>

namespace neoncoil::ui
{
    void boardTile(Screen& screen, const BoardView& view, Vec2 tile, Color colour, float inset)
    {
        const float size = view.tileSize - inset * 2.0f;
        if (size <= 0.0f)
            return;

        screen.rect(view.left(tile.x) + inset, view.top(tile.y) + inset, size, size, colour);
    }

    void boardGlyph(Screen& screen, const BoardView& view, Vec2 tile, wchar_t glyph,
        Color colour, float fraction)
    {
        const float size = view.tileSize * fraction;
        const float offset = (view.tileSize - size) * 0.5f;

        screen.drawGlyph(view.left(tile.x) + offset, view.top(tile.y) + offset, size, size, glyph, colour);
    }

    void progressBar(Screen& screen, int cellX, int cellY, int widthInCells, float fraction,
        Color filled, Color empty, Color background)
    {
        (void)background;

        if (widthInCells <= 0)
            return;

        constexpr float kBarHeight = 8.0f;

        const float x = static_cast<float>(cellX * Screen::kCellWidth);
        const float width = static_cast<float>(widthInCells * Screen::kCellWidth);
        const float y = static_cast<float>(cellY * Screen::kCellHeight)
            + (static_cast<float>(Screen::kCellHeight) - kBarHeight) * 0.5f;

        screen.overlayRect(x, y, width, kBarHeight, empty);

        const float lit = std::clamp(fraction, 0.0f, 1.0f) * width;
        if (lit <= 0.0f)
            return;

        screen.overlayRect(x, y, lit, kBarHeight, filled);
        screen.overlayGlowRect(x, y, lit, kBarHeight, filled, 5.0f, 0.55f);
    }

    int keyHint(Screen& screen, int x, int y, std::wstring_view key, std::wstring_view label, Color background)
    {
        int cursor = x;

        screen.put(cursor++, y, L'[', Color::Slate, background);
        screen.text(cursor, y, key, Color::Gold, background);
        cursor += static_cast<int>(key.size());
        screen.put(cursor++, y, L']', Color::Slate, background);
        screen.put(cursor++, y, L' ', Color::Silver, background);
        screen.text(cursor, y, label, Color::Silver, background);
        cursor += static_cast<int>(label.size());

        return cursor - x;
    }

    std::wstring toWide(const std::string& text)
    {
        // Player names and CLI arguments are restricted to printable ASCII, so a
        // direct widening is correct here and avoids a locale dependency.
        std::wstring result;
        result.reserve(text.size());
        for (unsigned char c : text)
            result.push_back(static_cast<wchar_t>(c));
        return result;
    }

    std::wstring padTo(std::wstring text, int width)
    {
        if (static_cast<int>(text.size()) < width)
            text.append(static_cast<std::size_t>(width) - text.size(), L' ');
        return text;
    }

    std::wstring truncateTo(std::wstring text, int width)
    {
        if (width <= 0)
            return {};
        if (static_cast<int>(text.size()) > width)
            text.resize(static_cast<std::size_t>(width));
        return text;
    }

    std::vector<std::wstring> wrapText(std::wstring_view text, int width)
    {
        std::vector<std::wstring> lines;
        if (width <= 0)
            return lines;

        std::wstring current;
        std::size_t cursor = 0;

        while (true)
        {
            const std::size_t space = text.find(L' ', cursor);
            const std::size_t count = space == std::wstring_view::npos ? std::wstring_view::npos : space - cursor;
            std::wstring word{ text.substr(cursor, count) };

            while (static_cast<int>(word.size()) > width)
            {
                if (!current.empty())
                {
                    lines.push_back(current);
                    current.clear();
                }
                lines.push_back(word.substr(0, static_cast<std::size_t>(width)));
                word.erase(0, static_cast<std::size_t>(width));
            }

            if (current.empty())
                current = word;
            else if (static_cast<int>(current.size() + 1 + word.size()) <= width)
                current += L' ' + word;
            else
            {
                lines.push_back(current);
                current = word;
            }

            if (space == std::wstring_view::npos)
                break;
            cursor = space + 1;
        }

        if (!current.empty())
            lines.push_back(current);

        return lines;
    }
}
