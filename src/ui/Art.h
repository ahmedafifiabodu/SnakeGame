#pragma once

#include "../core/Colors.h"
#include "../core/Screen.h"

#include <string_view>

namespace neoncoil::ui
{
    // A 5x5 block font. Every large caption in the game (SNAKE, PAUSED, GAME
    // OVER, LEVEL CLEAR, the level number) is drawn from this one source rather
    // than from hand-typed ASCII art, so captions stay consistent and adding a
    // new one costs nothing.
    inline constexpr int kFontHeight = 5;
    inline constexpr int kFontWidth = 5;

    // Width in console cells that drawBanner would occupy.
    int bannerWidth(std::wstring_view text, int scaleX = 2, int spacing = 1);

    void drawBanner(Screen& screen, int x, int y, std::wstring_view text,
        Color foreground, Color background = Color::Black,
        int scaleX = 2, int scaleY = 1, int spacing = 1);

    void drawBannerCentered(Screen& screen, int y, std::wstring_view text,
        Color foreground, Color background = Color::Black,
        int scaleX = 2, int scaleY = 1, int spacing = 1);

    // Decorative snake drawn along a row, used on the menu and result screens.
    void drawSnakeFlourish(Screen& screen, int x, int y, int length, Color colour, Color background);
}
