#pragma once

#include "../core/Screen.h"
#include "../game/LevelGenerator.h"

namespace neoncoil::ui
{
    // The canvas is a fixed virtual resolution that the window letterboxes, so
    // every screen can be laid out against these constants and stays correct at
    // any window size or on any monitor.
    inline constexpr int kScreenWidth = 120;   // cells
    inline constexpr int kScreenHeight = 40;   // cells

    inline constexpr float kCanvasWidth = kScreenWidth * Screen::kCellWidth;    // 1920
    inline constexpr float kCanvasHeight = kScreenHeight * Screen::kCellHeight; // 960

    inline constexpr int kHudHeight = 3;       // cells
    inline constexpr int kFooterY = kScreenHeight - 2;

    // --- board, in virtual pixels -------------------------------------------
    // The board is NOT on the cell grid. Cells are 16x24 to suit text, which
    // would make every tile a stretched rectangle; giving the board its own
    // square-tile pixel space is what lets obstacles, food and the snake read
    // correctly, and what makes smooth movement and glow possible.
    inline constexpr float kTilePixels = 25.0f;
    inline constexpr float kBoardPixelWidth = kBoardWidth * kTilePixels;    // 1400
    inline constexpr float kBoardPixelHeight = kBoardHeight * kTilePixels;  // 800
    inline constexpr float kBoardPixelX = (kCanvasWidth - kBoardPixelWidth) * 0.5f;
    // Clears the HUD and leaves the caption row above it unobstructed.
    inline constexpr float kBoardPixelY = 4.0f * Screen::kCellHeight + 4.0f;

    inline constexpr float kBoardFrameThickness = 4.0f;

    // Cell row the board caption sits on, just above the frame.
    inline constexpr int kBoardCaptionRow = 3;

    static_assert(kBoardPixelY + kBoardPixelHeight < kFooterY * Screen::kCellHeight,
        "board overlaps the footer");
    static_assert(kBoardPixelX >= 0.0f, "board is wider than the canvas");
}
