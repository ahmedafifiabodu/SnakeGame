#include "Art.h"

#include "../core/BlockFont.h"
#include "../core/Glyphs.h"

namespace neoncoil::ui
{
    int bannerWidth(std::wstring_view text, int scaleX, int spacing)
    {
        if (text.empty())
            return 0;

        const int perCharacter = kFontWidth * scaleX;
        return static_cast<int>(text.size()) * perCharacter +
            (static_cast<int>(text.size()) - 1) * spacing;
    }

    void drawBanner(Screen& screen, int x, int y, std::wstring_view text,
        Color foreground, Color background, int scaleX, int scaleY, int spacing)
    {
        int cursorX = x;

        for (wchar_t character : text)
        {
            const font::Rows& rows = font::rowsFor(character);

            for (int row = 0; row < kFontHeight; ++row)
            {
                const char* line = rows[static_cast<std::size_t>(row)];

                for (int column = 0; column < kFontWidth; ++column)
                {
                    if (line[column] != '#')
                        continue;

                    for (int sy = 0; sy < scaleY; ++sy)
                        for (int sx = 0; sx < scaleX; ++sx)
                            screen.put(cursorX + column * scaleX + sx, y + row * scaleY + sy,
                                glyph::Block, foreground, background);
                }
            }

            cursorX += kFontWidth * scaleX + spacing;
        }
    }

    void drawBannerCentered(Screen& screen, int y, std::wstring_view text,
        Color foreground, Color background, int scaleX, int scaleY, int spacing)
    {
        const int width = bannerWidth(text, scaleX, spacing);
        drawBanner(screen, (screen.width() - width) / 2, y, text, foreground, background, scaleX, scaleY, spacing);
    }

    void drawSnakeFlourish(Screen& screen, int x, int y, int length, Color colour, Color background)
    {
        for (int i = 0; i < length; ++i)
        {
            const wchar_t body = (i % 4 < 2) ? glyph::Block : glyph::ShadeDark;
            screen.put(x + i, y, i == length - 1 ? glyph::Block : body, colour, background);
        }

        if (length > 2)
        {
            screen.put(x + length, y, glyph::TriRight, colour, background);
            screen.put(x - 1, y, glyph::Dot, Color::Slate, background);
        }
    }
}
