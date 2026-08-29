#pragma once

#include "../core/Colors.h"
#include "../core/Screen.h"
#include "../game/SnakeType.h"

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
}
