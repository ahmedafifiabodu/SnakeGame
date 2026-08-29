#pragma once

#include "../core/Colors.h"
#include "../core/Screen.h"
#include "../game/SnakeType.h"

#include <string>

namespace neoncoil::ui
{
    // "What does this snake actually do": a crawling preview in the player's
    // own colour, the tagline, the ability with its cooldown, and the stat bars.
    //
    // Lives here rather than inside MenuState because the lobby needs the same
    // answer. A player picking a snake before an online match was being shown a
    // name and nothing else, and had no way to find out what SHED or GOLD RUSH
    // meant without leaving the session. Two implementations of this panel would
    // eventually disagree about the same snake, which is worse than either.
    //
    // Draws downward from `y` and clips itself to `height`, so a caller with a
    // short panel gets the important rows rather than an overflow: the ability
    // is what a player needs before a match, so it is placed before the notes
    // and the stats rather than after them.
    void drawSnakeReport(Screen& screen, int x, int y, int width, int height,
        const SnakeType& type, Color bodyColour, float elapsed);

    // The animated strip on its own, for callers that want the preview without
    // the prose. `elapsed` drives the crawl.
    void drawSnakeStrip(Screen& screen, int x, int y, int width,
        const SnakeType& type, Color bodyColour, float elapsed);

    // Where a snake's portrait art lives. Shared because the lobby draws the
    // same five pictures the main menu does, and a second copy of this rule
    // would eventually point at a different folder.
    std::string portraitPath(const SnakeType& type);

    // The portrait, fitted into a cell-grid rectangle with a wash of the
    // snake's own colour behind it. Draws nothing when the art is missing, so a
    // build without the assets still runs.
    void drawSnakePortrait(Screen& screen, int cellX, int cellY, int cellW, int cellH,
        const SnakeType& type);
}
