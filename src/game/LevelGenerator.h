#pragma once

#include "Level.h"
#include "../core/Rng.h"

#include <cstdint>
#include <string>

namespace neoncoil
{
    // Board dimensions are fixed for the whole game. A stable board means the
    // console is sized once at start-up, the HUD layout never moves, and levels
    // differ by their content rather than by their shape.
    inline constexpr int kBoardWidth = 56;
    inline constexpr int kBoardHeight = 32;

    enum class Archetype
    {
        Open,
        Pillars,
        Rooms,
        Rings,
        Diagonals,
        Corridors,
        Cavern,
        Count
    };

    // Result of checking a generated level against the playability invariants.
    struct LevelReport
    {
        bool valid{ false };
        int openTiles{ 0 };
        int reachableTiles{ 0 };
        std::wstring failure;
    };

    // Produces levels that are guaranteed playable rather than merely random.
    //
    // The invariants, all enforced by construction or by rejection:
    //   1. The outer ring is solid wall.
    //   2. The spawn tile, the body tiles behind it and the tile ahead are open.
    //   3. Every open tile is reachable from spawn -- unreachable pockets are
    //      filled in, so food can never spawn somewhere the snake cannot go.
    //   4. The reachable area is at least kMinOpenFraction of the interior.
    //   5. No sentinel starts inside the spawn pocket.
    class LevelGenerator
    {
    public:
        // Deterministic: the same (runSeed, levelIndex, startLength) always
        // produces the same level, which is what makes a bad level reproducible.
        static Level generate(int levelIndex, std::uint64_t runSeed, int startLength);

        static std::uint64_t levelSeed(std::uint64_t runSeed, int levelIndex);

        static LevelReport validate(const Level& level, int startLength);

        static const wchar_t* archetypeName(Archetype archetype);

        // Fraction of the interior that must remain open and reachable.
        static constexpr float kMinOpenFraction = 0.45f;
    };
}
