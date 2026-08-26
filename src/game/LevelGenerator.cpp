#include "LevelGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace neoncoil
{
    namespace
    {
        struct Rect
        {
            int x{ 0 };
            int y{ 0 };
            int w{ 0 };
            int h{ 0 };

            bool contains(Vec2 p) const
            {
                return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
            }
        };

        struct Params
        {
            Archetype archetype{ Archetype::Open };
            float density{ 0.02f };
            int sentinelCount{ 0 };
            float sentinelInterval{ 0.45f };
        };

        constexpr Vec2 kNeighbours[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

        int interiorArea(const Level& level)
        {
            return std::max(0, (level.width() - 2) * (level.height() - 2));
        }

        // The guaranteed-clear starting area. Both the carver and the validator
        // call this so they can never disagree about where it is.
        Rect spawnPocket(int width, int height, int startLength)
        {
            const int centreX = width / 2;
            const int centreY = height / 2;
            const int left = std::max(1, centreX - startLength - 3);
            const int right = std::min(width - 2, centreX + 12);
            return Rect{ left, centreY - 1, right - left + 1, 3 };
        }

        void setInterior(Level& level, Vec2 position, Tile tile)
        {
            if (level.isBorder(position))
                return;
            level.set(position, tile);
        }

        void fillInterior(Level& level, int x, int y, int w, int h, Tile tile)
        {
            for (int row = 0; row < h; ++row)
                for (int col = 0; col < w; ++col)
                    setInterior(level, { x + col, y + row }, tile);
        }

        void drawBorder(Level& level)
        {
            for (int y = 0; y < level.height(); ++y)
                for (int x = 0; x < level.width(); ++x)
                    if (level.isBorder({ x, y }))
                        level.set({ x, y }, Tile::Wall);
        }

        int openNeighbourCount(const Level& level, Vec2 position)
        {
            int count = 0;
            for (const Vec2& offset : kNeighbours)
                if (!level.isWall(position + offset))
                    ++count;
            return count;
        }

        // ---- archetype carvers ---------------------------------------------
        // Each carver writes walls over the whole interior; mirrorHorizontally()
        // is applied afterwards, which is what makes layouts read as designed
        // rather than as noise.

        void carveOpen(Level& level, float density, Rng& rng)
        {
            const int budget = static_cast<int>(density * interiorArea(level));
            int placed = 0;
            int guard = 0;

            while (placed < budget && guard++ < 4000)
            {
                const int w = rng.range(2, 4);
                const int h = rng.range(1, 3);
                const int x = rng.range(2, level.width() - 3 - w);
                const int y = rng.range(2, level.height() - 3 - h);

                fillInterior(level, x, y, w, h, Tile::Wall);
                placed += w * h;
            }
        }

        void carvePillars(Level& level, float density, Rng& rng)
        {
            // Denser levels get tighter spacing rather than bigger pillars, so
            // the corridors between them stay navigable.
            const float t = std::clamp((density - 0.02f) / 0.14f, 0.0f, 1.0f);
            const int spacing = static_cast<int>(std::lround(9.0f - t * 4.0f));

            for (int y = 3; y < level.height() - 4; y += spacing)
            {
                for (int x = 3; x < level.width() - 4; x += spacing)
                {
                    if (!rng.chance(0.85f))
                        continue;

                    const int jitterX = rng.range(-1, 1);
                    const int jitterY = rng.range(-1, 1);
                    fillInterior(level, x + jitterX, y + jitterY, 2, 2, Tile::Wall);
                }
            }
        }

        void punchDoorway(Level& level, Vec2 start, Vec2 step, int length, Rng& rng, int doorWidth)
        {
            if (length <= doorWidth + 2)
                return;

            const int offset = rng.range(1, length - doorWidth - 1);
            for (int i = 0; i < doorWidth; ++i)
                setInterior(level, start + step * (offset + i), Tile::Empty);
        }

        void carveRooms(Level& level, float density, Rng& rng)
        {
            const int verticalDividers = density > 0.08f ? 3 : 2;
            const int horizontalDividers = density > 0.11f ? 3 : 2;
            const int doorWidth = density > 0.12f ? 3 : 4;

            for (int i = 1; i <= verticalDividers; ++i)
            {
                const int x = level.width() * i / (verticalDividers + 1);
                for (int y = 1; y < level.height() - 1; ++y)
                    setInterior(level, { x, y }, Tile::Wall);

                const int doors = rng.range(2, 3);
                for (int d = 0; d < doors; ++d)
                    punchDoorway(level, { x, 1 }, { 0, 1 }, level.height() - 2, rng, doorWidth);
            }

            for (int i = 1; i <= horizontalDividers; ++i)
            {
                const int y = level.height() * i / (horizontalDividers + 1);
                for (int x = 1; x < level.width() - 1; ++x)
                    setInterior(level, { x, y }, Tile::Wall);

                const int doors = rng.range(3, 4);
                for (int d = 0; d < doors; ++d)
                    punchDoorway(level, { 1, y }, { 1, 0 }, level.width() - 2, rng, doorWidth);
            }
        }

        void carveRings(Level& level, float density, Rng& rng)
        {
            const int step = density > 0.10f ? 4 : 6;

            for (int inset = 4; inset * 2 < std::min(level.width(), level.height()) - 4; inset += step)
            {
                const int left = inset;
                const int right = level.width() - 1 - inset;
                const int top = inset;
                const int bottom = level.height() - 1 - inset;

                for (int x = left; x <= right; ++x)
                {
                    setInterior(level, { x, top }, Tile::Wall);
                    setInterior(level, { x, bottom }, Tile::Wall);
                }
                for (int y = top; y <= bottom; ++y)
                {
                    setInterior(level, { left, y }, Tile::Wall);
                    setInterior(level, { right, y }, Tile::Wall);
                }

                // Gaps on every side keep each ring enterable from anywhere.
                const int gap = 4;
                punchDoorway(level, { left, top }, { 1, 0 }, right - left, rng, gap);
                punchDoorway(level, { left, bottom }, { 1, 0 }, right - left, rng, gap);
                punchDoorway(level, { left, top }, { 0, 1 }, bottom - top, rng, gap);
                punchDoorway(level, { right, top }, { 0, 1 }, bottom - top, rng, gap);
            }
        }

        void carveDiagonals(Level& level, float density, Rng& rng)
        {
            const int budget = static_cast<int>(density * interiorArea(level));
            int placed = 0;
            int guard = 0;

            while (placed < budget && guard++ < 500)
            {
                Vec2 cursor{ rng.range(3, level.width() - 4), rng.range(3, level.height() - 4) };
                const Vec2 step{ rng.chance(0.5f) ? 1 : -1, rng.chance(0.5f) ? 1 : -1 };
                const int length = rng.range(8, 18);

                for (int i = 0; i < length; ++i)
                {
                    setInterior(level, cursor, Tile::Wall);
                    setInterior(level, cursor + Vec2{ 1, 0 }, Tile::Wall);
                    placed += 2;
                    cursor += step;
                }
            }
        }

        void carveCorridors(Level& level, float density, Rng& rng)
        {
            // Long straight runs with gaps: produces corridor structure that
            // rewards route planning, without the 1-tile dead ends a true maze
            // algorithm would create.
            const int budget = static_cast<int>(density * interiorArea(level));
            int placed = 0;
            int guard = 0;

            while (placed < budget && guard++ < 800)
            {
                const bool horizontal = rng.chance(0.5f);
                const Vec2 step = horizontal ? Vec2{ 1, 0 } : Vec2{ 0, 1 };
                Vec2 cursor{ rng.range(2, level.width() - 3), rng.range(2, level.height() - 3) };
                const int length = rng.range(6, horizontal ? 20 : 12);

                for (int i = 0; i < length; ++i)
                {
                    setInterior(level, cursor, Tile::Wall);
                    ++placed;
                    cursor += step;
                }
            }
        }

        void carveCavern(Level& level, float density, Rng& rng)
        {
            const float fill = 0.26f + density * 0.9f;

            for (int y = 1; y < level.height() - 1; ++y)
                for (int x = 1; x < level.width() - 1; ++x)
                    level.set({ x, y }, rng.chance(fill) ? Tile::Wall : Tile::Empty);

            for (int pass = 0; pass < 4; ++pass)
            {
                Level next = level;
                for (int y = 1; y < level.height() - 1; ++y)
                {
                    for (int x = 1; x < level.width() - 1; ++x)
                    {
                        int walls = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                                if ((dx != 0 || dy != 0) && level.isWall({ x + dx, y + dy }))
                                    ++walls;

                        next.set({ x, y }, walls >= 5 ? Tile::Wall : Tile::Empty);
                    }
                }
                level = next;
            }
        }

        void carveArchetype(Level& level, Archetype archetype, float density, Rng& rng)
        {
            switch (archetype)
            {
            case Archetype::Open:      carveOpen(level, density, rng); break;
            case Archetype::Pillars:   carvePillars(level, density, rng); break;
            case Archetype::Rooms:     carveRooms(level, density, rng); break;
            case Archetype::Rings:     carveRings(level, density, rng); break;
            case Archetype::Diagonals: carveDiagonals(level, density, rng); break;
            case Archetype::Corridors: carveCorridors(level, density, rng); break;
            case Archetype::Cavern:    carveCavern(level, density, rng); break;
            case Archetype::Count:     break;
            }
        }

        void mirrorHorizontally(Level& level)
        {
            for (int y = 1; y < level.height() - 1; ++y)
                for (int x = 1; x < level.width() / 2; ++x)
                    level.set({ level.width() - 1 - x, y }, level.at({ x, y }));
        }

        void carveSpawnPocket(Level& level, int startLength)
        {
            const Rect pocket = spawnPocket(level.width(), level.height(), startLength);
            fillInterior(level, pocket.x, pocket.y, pocket.w, pocket.h, Tile::Empty);

            level.spawn = { level.width() / 2, level.height() / 2 };
            level.spawnDirection = Direction::Right;
        }

        // Knocks open a wall next to any tile that has only one way out. A
        // 1-tile dead end is an instant-death trap in a game where you cannot
        // stop moving, so this is a fairness pass, not a cosmetic one.
        void removeDeadEnds(Level& level, Rng& rng, int passes)
        {
            for (int pass = 0; pass < passes; ++pass)
            {
                bool changed = false;

                for (int y = 1; y < level.height() - 1; ++y)
                {
                    for (int x = 1; x < level.width() - 1; ++x)
                    {
                        const Vec2 position{ x, y };
                        if (level.isWall(position) || openNeighbourCount(level, position) != 1)
                            continue;

                        std::vector<Vec2> candidates;
                        for (const Vec2& offset : kNeighbours)
                        {
                            const Vec2 neighbour = position + offset;
                            if (level.isWall(neighbour) && !level.isBorder(neighbour))
                                candidates.push_back(neighbour);
                        }

                        if (candidates.empty())
                            continue;

                        level.set(rng.pick(candidates), Tile::Empty);
                        changed = true;
                    }
                }

                if (!changed)
                    break;
            }
        }

        // Flood fills from spawn and walls off everything it could not reach.
        // After this the "food spawned somewhere unreachable" failure mode
        // cannot exist, because unreachable open tiles no longer exist.
        int keepRegionContainingSpawn(Level& level)
        {
            const int width = level.width();
            const int height = level.height();
            std::vector<std::uint8_t> visited(static_cast<std::size_t>(width) * height, 0);

            std::vector<Vec2> stack;
            if (!level.isWall(level.spawn))
            {
                stack.push_back(level.spawn);
                visited[static_cast<std::size_t>(level.spawn.y) * width + level.spawn.x] = 1;
            }

            int reachable = 0;
            while (!stack.empty())
            {
                const Vec2 current = stack.back();
                stack.pop_back();
                ++reachable;

                for (const Vec2& offset : kNeighbours)
                {
                    const Vec2 next = current + offset;
                    if (!level.inBounds(next) || level.isWall(next))
                        continue;

                    std::uint8_t& mark = visited[static_cast<std::size_t>(next.y) * width + next.x];
                    if (mark != 0)
                        continue;

                    mark = 1;
                    stack.push_back(next);
                }
            }

            for (int y = 1; y < height - 1; ++y)
                for (int x = 1; x < width - 1; ++x)
                    if (!level.isWall({ x, y }) && visited[static_cast<std::size_t>(y) * width + x] == 0)
                        level.set({ x, y }, Tile::Wall);

            return reachable;
        }

        void placeSentinels(Level& level, int count, float interval, int startLength, Rng& rng)
        {
            const Rect pocket = spawnPocket(level.width(), level.height(), startLength);
            const std::vector<Vec2>& open = level.openTiles();
            if (open.empty())
                return;

            int placed = 0;
            int guard = 0;

            while (placed < count && guard++ < 600)
            {
                const Vec2 position = rng.pick(open);

                if (pocket.contains(position) || manhattan(position, level.spawn) < 14)
                    continue;
                if (level.hazardAt(position))
                    continue;

                // Needs room to actually patrol, otherwise it just vibrates.
                const bool horizontal = rng.chance(0.5f);
                const Vec2 step = horizontal ? Vec2{ 1, 0 } : Vec2{ 0, 1 };

                int run = 0;
                for (int i = 1; i <= 4; ++i)
                {
                    if (level.isWall(position + step * i))
                        break;
                    ++run;
                }
                for (int i = 1; i <= 4; ++i)
                {
                    if (level.isWall(position - step * i))
                        break;
                    ++run;
                }

                if (run < 5)
                    continue;

                Sentinel sentinel;
                sentinel.position = position;
                sentinel.step = rng.chance(0.5f) ? step : step * -1;
                sentinel.moveInterval = interval;
                sentinel.timer = rng.unit() * interval;
                level.addSentinel(sentinel);
                ++placed;
            }
        }

        Params paramsFor(int levelIndex, Rng& rng)
        {
            Params params;
            params.density = std::clamp(0.02f + 0.016f * static_cast<float>(levelIndex - 1), 0.02f, 0.16f);
            params.sentinelCount = std::clamp(levelIndex - 4, 0, 4);
            params.sentinelInterval = std::max(0.22f, 0.45f - 0.02f * static_cast<float>(levelIndex - 5));

            // Archetypes unlock with depth so early levels stay readable and
            // later ones surprise. Earlier entries stay in the pool.
            std::vector<Archetype> pool{ Archetype::Open, Archetype::Pillars };
            if (levelIndex >= 3)
            {
                pool.push_back(Archetype::Rooms);
                pool.push_back(Archetype::Rings);
            }
            if (levelIndex >= 5)
                pool.push_back(Archetype::Diagonals);
            if (levelIndex >= 7)
            {
                pool.push_back(Archetype::Corridors);
                pool.push_back(Archetype::Cavern);
            }

            params.archetype = rng.pick(pool);
            return params;
        }

        Level buildCandidate(int levelIndex, std::uint64_t seed, int startLength, const Params& params)
        {
            Rng rng(seed);

            Level level;
            level.resize(kBoardWidth, kBoardHeight);
            level.index = levelIndex;
            level.seed = seed;
            level.archetypeName = LevelGenerator::archetypeName(params.archetype);

            drawBorder(level);
            carveArchetype(level, params.archetype, params.density, rng);
            mirrorHorizontally(level);
            drawBorder(level);
            carveSpawnPocket(level, startLength);
            removeDeadEnds(level, rng, 2);
            keepRegionContainingSpawn(level);
            level.rebuildOpenTiles();
            placeSentinels(level, params.sentinelCount, params.sentinelInterval, startLength, rng);

            return level;
        }
    }

    std::uint64_t LevelGenerator::levelSeed(std::uint64_t runSeed, int levelIndex)
    {
        return Rng::mix(runSeed, static_cast<std::uint64_t>(levelIndex) * 0x1000193ull);
    }

    const wchar_t* LevelGenerator::archetypeName(Archetype archetype)
    {
        switch (archetype)
        {
        case Archetype::Open:      return L"Open Field";
        case Archetype::Pillars:   return L"Pillars";
        case Archetype::Rooms:     return L"Chambers";
        case Archetype::Rings:     return L"Rings";
        case Archetype::Diagonals: return L"Shards";
        case Archetype::Corridors: return L"Corridors";
        case Archetype::Cavern:    return L"Cavern";
        case Archetype::Count:     break;
        }
        return L"Unknown";
    }

    Level LevelGenerator::generate(int levelIndex, std::uint64_t runSeed, int startLength)
    {
        const std::uint64_t seed = levelSeed(runSeed, levelIndex);

        // Rejection sampling with a softening density. Six attempts is far more
        // than measured need (see --selftest), and the guaranteed-valid open
        // room below means generate() can never fail.
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            const std::uint64_t attemptSeed = Rng::mix(seed, static_cast<std::uint64_t>(attempt));

            Rng chooser(attemptSeed);
            Params params = paramsFor(levelIndex, chooser);
            params.density *= std::pow(0.7f, static_cast<float>(attempt));

            Level candidate = buildCandidate(levelIndex, attemptSeed, startLength, params);
            if (validate(candidate, startLength).valid)
                return candidate;
        }

        Params fallback;
        fallback.archetype = Archetype::Open;
        fallback.density = 0.0f;
        fallback.sentinelCount = 0;
        return buildCandidate(levelIndex, seed, startLength, fallback);
    }

    LevelReport LevelGenerator::validate(const Level& level, int startLength)
    {
        LevelReport report;

        if (level.width() < 8 || level.height() < 8)
        {
            report.failure = L"board too small";
            return report;
        }

        for (int x = 0; x < level.width(); ++x)
        {
            if (!level.isWall({ x, 0 }) || !level.isWall({ x, level.height() - 1 }))
            {
                report.failure = L"border is not sealed";
                return report;
            }
        }
        for (int y = 0; y < level.height(); ++y)
        {
            if (!level.isWall({ 0, y }) || !level.isWall({ level.width() - 1, y }))
            {
                report.failure = L"border is not sealed";
                return report;
            }
        }

        if (level.isWall(level.spawn))
        {
            report.failure = L"spawn tile is walled";
            return report;
        }

        const Vec2 back = toDelta(level.spawnDirection) * -1;
        for (int i = 1; i < startLength; ++i)
        {
            if (level.isWall(level.spawn + back * i))
            {
                report.failure = L"snake body would spawn inside a wall";
                return report;
            }
        }

        for (int i = 1; i <= 3; ++i)
        {
            if (level.isWall(level.spawn + toDelta(level.spawnDirection) * i))
            {
                report.failure = L"no room to move on spawn";
                return report;
            }
        }

        const Rect pocket = spawnPocket(level.width(), level.height(), startLength);
        for (const Sentinel& sentinel : level.sentinels())
        {
            if (pocket.contains(sentinel.position))
            {
                report.failure = L"sentinel starts inside the spawn pocket";
                return report;
            }
        }

        std::vector<std::uint8_t> visited(static_cast<std::size_t>(level.width()) * level.height(), 0);
        std::vector<Vec2> stack{ level.spawn };
        visited[static_cast<std::size_t>(level.spawn.y) * level.width() + level.spawn.x] = 1;

        while (!stack.empty())
        {
            const Vec2 current = stack.back();
            stack.pop_back();
            ++report.reachableTiles;

            for (const Vec2& offset : kNeighbours)
            {
                const Vec2 next = current + offset;
                if (!level.inBounds(next) || level.isWall(next))
                    continue;

                std::uint8_t& mark = visited[static_cast<std::size_t>(next.y) * level.width() + next.x];
                if (mark != 0)
                    continue;

                mark = 1;
                stack.push_back(next);
            }
        }

        for (int y = 0; y < level.height(); ++y)
            for (int x = 0; x < level.width(); ++x)
                if (!level.isWall({ x, y }))
                    ++report.openTiles;

        if (report.reachableTiles != report.openTiles)
        {
            report.failure = L"level contains unreachable open tiles";
            return report;
        }

        if (static_cast<int>(level.openTiles().size()) != report.openTiles)
        {
            report.failure = L"open tile cache is stale";
            return report;
        }

        const int minimum = static_cast<int>(kMinOpenFraction * static_cast<float>(interiorArea(level)));
        if (report.reachableTiles < minimum)
        {
            report.failure = L"playable area is too small";
            return report;
        }

        report.valid = true;
        return report;
    }
}
