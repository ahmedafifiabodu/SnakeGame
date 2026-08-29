#include "SnakeCard.h"

#include "Draw.h"
#include "../core/Glyphs.h"

#include <algorithm>

namespace neoncoil::ui
{
    namespace
    {
        // Small stat readout: "SPEED  ####------".
        void statBar(Screen& screen, int x, int y, int width, const wchar_t* label,
            float fraction, Color colour)
        {
            const int barWidth = std::clamp(width - 9, 4, 14);
            screen.text(x, y, label, Color::Silver, Color::Black);
            progressBar(screen, x + 9, y, barWidth, fraction, colour, Color::Slate, Color::Black);
        }
    }

    void drawSnakeStrip(Screen& screen, int x, int y, int width,
        const SnakeType& type, Color bodyColour, float elapsed)
    {
        if (width <= 0)
            return;

        // The track is wider than the strip so the snake leaves and re-enters
        // rather than teleporting from one edge to the other.
        const int track = width + 12;
        const int offset = static_cast<int>(elapsed * 9.0f) % track;

        screen.fillRect(x, y, width, 1, glyph::Space, Color::Silver, Color::Navy);

        for (int i = 0; i < 10; ++i)
        {
            const int px = x + ((offset - i + track) % track) - 6;
            if (px < x || px >= x + width)
                continue;

            wchar_t body = type.bodyGlyph;
            if (type.altBodyGlyph != 0 && i % 2 == 0)
                body = type.altBodyGlyph;

            screen.put(px, y, i == 0 ? type.headGlyph : body,
                i == 0 ? Color::White : bodyColour, Color::Navy);
        }
    }

    void drawSnakeReport(Screen& screen, int x, int y, int width, int height,
        const SnakeType& type, Color bodyColour, float elapsed)
    {
        if (width <= 0 || height <= 0)
            return;

        const int bottom = y + height;
        int row = y;

        // Every write goes through this, so running out of panel truncates the
        // report rather than drawing over whatever is underneath it.
        const auto room = [&](int rows) { return row + rows <= bottom; };

        if (!room(1))
            return;

        drawSnakeStrip(screen, x, row, width, type, bodyColour, elapsed);
        ++row;

        // Wrapped rather than truncated. A tagline cut mid-word ("FAST,
        // FRAGILE, UNFORGIVI") looks like a rendering fault, and the panel is
        // not so short that losing the last word is the only option.
        for (const std::wstring& line : wrapText(type.tagline, width))
        {
            if (!room(1))
                break;
            screen.text(x, row++, line, Color::Silver, Color::Black);
        }
        ++row;

        // --- ability ----------------------------------------------------------
        //
        // Before the notes and before the stats: a player choosing a snake for a
        // match wants to know what the space bar does, and everything else is
        // detail they can read afterwards.
        if (room(1))
        {
            screen.put(x, row, glyph::Bolt, Color::Gold, Color::Black);
            screen.text(x + 2, row, truncateTo(type.ability.name, width - 2), Color::Gold, Color::Black);
            ++row;
        }

        for (const std::wstring& line : wrapText(type.ability.summary, width))
        {
            if (!room(1))
                break;
            screen.text(x, row, line, Color::Silver, Color::Black);
            ++row;
        }

        if (room(1))
        {
            // Cooldown and duration go on one line when they fit and two when
            // they do not, because "Duration 2" with the seconds cut off is
            // worse than no duration at all.
            const std::wstring cooldown = L"Cooldown " +
                std::to_wstring(static_cast<int>(type.ability.cooldownSeconds)) + L"s";

            std::wstring duration;
            if (type.ability.durationSeconds > 0.0f)
            {
                duration = L"Duration " +
                    std::to_wstring(static_cast<int>(type.ability.durationSeconds)) + L"s";
            }

            if (duration.empty())
            {
                screen.text(x, row++, cooldown, Color::Slate, Color::Black);
            }
            else if (static_cast<int>(cooldown.size() + duration.size()) + 3 <= width)
            {
                screen.text(x, row++, cooldown + L"   " + duration, Color::Slate, Color::Black);
            }
            else
            {
                screen.text(x, row++, cooldown, Color::Slate, Color::Black);
                if (room(1))
                    screen.text(x, row++, duration, Color::Slate, Color::Black);
            }

            ++row;
        }

        // --- stats ------------------------------------------------------------
        //
        // Checked a row at a time rather than all three at once: two bars are a
        // better use of two remaining rows than nothing, and the alternative
        // was a panel that skipped the stats and then found room for a note.
        bool statsComplete = true;

        const auto stat = [&](const wchar_t* label, float fraction)
        {
            if (!room(1))
            {
                statsComplete = false;
                return;
            }
            statBar(screen, x, row++, width, label, fraction, type.accent);
        };

        stat(L"SPEED", (type.speedMultiplier - 0.8f) / 0.6f);
        stat(L"GROWTH", static_cast<float>(type.growthPerFood) / 3.0f);
        stat(L"SCORING", (type.scoreMultiplier - 0.8f) / 0.6f);

        if (!statsComplete)
            return;

        ++row;

        // --- notes ------------------------------------------------------------
        //
        // Last, because they are flavour and detail. A short panel drops them
        // and loses nothing a player needs in the thirty seconds before a match.
        for (const std::wstring& note : type.notes)
        {
            if (!room(1))
                break;
            screen.put(x, row, glyph::Bullet, type.accent, Color::Black);
            screen.text(x + 2, row, truncateTo(note, width - 2), Color::Silver, Color::Black);
            ++row;
        }
    }
}
