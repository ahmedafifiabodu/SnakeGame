#pragma once

#include "Direction.h"
#include "../core/Vec2.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace neoncoil
{
    // The snake body and its movement rules. Deliberately knows nothing about
    // walls, food, scoring or rendering: PlayState asks where the head is going
    // next, resolves the consequences, then tells the snake to commit.
    class Snake
    {
    public:
        // Lays the body out behind `head`, opposite to `direction`, clamped to
        // the board. Safe to call repeatedly (level restarts, new levels).
        void reset(Vec2 boardSize, Vec2 head, Direction direction, int length);

        // Queues a turn. Reversing onto yourself and repeating the current
        // heading are both rejected here, so callers can pass raw input.
        // Two turns may be buffered, which is what makes fast double-taps
        // (e.g. right-then-up around a corner) feel responsive instead of eaten.
        void queueDirection(Direction direction);

        Direction direction() const { return m_direction; }
        Direction nextDirection() const;

        Vec2 head() const { return m_body.front(); }
        Vec2 tail() const { return m_body.back(); }
        Vec2 nextHead() const { return head() + toDelta(nextDirection()); }

        const std::deque<Vec2>& body() const { return m_body; }
        int length() const { return static_cast<int>(m_body.size()); }

        void grow(int segments) { m_pendingGrowth += segments; }
        int pendingGrowth() const { return m_pendingGrowth; }

        // Applies the queued turn and advances one tile, consuming one unit of
        // pending growth if any is outstanding.
        void commitStep();

        bool occupies(Vec2 position) const;

        // Self-collision test for the *next* step. The tail tile is excluded
        // when the snake is not growing, because that segment vacates on the
        // same tick -- without this, tight turns are unfairly lethal.
        bool occupiesAfterStep(Vec2 position) const;

        // Removes segments from the back until at most `keepLength` remain.
        // Returns the removed positions so the caller can spawn effects.
        std::vector<Vec2> shedTo(int keepLength);

    private:
        int occupancyIndex(Vec2 position) const;
        void addOccupancy(Vec2 position);
        void removeOccupancy(Vec2 position);

        std::deque<Vec2> m_body;
        std::deque<Direction> m_queuedTurns;
        Direction m_direction{ Direction::Right };
        int m_pendingGrowth{ 0 };

        // Occupancy counts rather than flags: while phasing, the body is allowed
        // to overlap itself, so a tile can legitimately hold several segments.
        Vec2 m_boardSize{ 0, 0 };
        std::vector<std::uint16_t> m_occupancy;
    };
}
