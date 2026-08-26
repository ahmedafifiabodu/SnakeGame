#include "Autoplay.h"

#include "../game/Food.h"
#include "../game/Level.h"
#include "../game/Snake.h"

#include <array>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace neoncoil::tools
{
    namespace
    {
        constexpr int kUnreachable = std::numeric_limits<int>::max();

        constexpr std::array<Direction, 4> kDirections = {
            Direction::Up, Direction::Down, Direction::Left, Direction::Right
        };

        // A tile the snake could stand on next tick. The tail is treated as
        // solid even though it vacates on the same step -- being one tile more
        // cautious than the rules require costs the demo nothing and keeps this
        // free of the growth bookkeeping the real collision path does.
        bool isPassable(const Level& level, const Snake& snake, Vec2 position)
        {
            return !level.isWall(position) && !snake.occupies(position);
        }

        // Sentinels move, so the tile in front of one is as lethal as the tile
        // it is on. Steering around the whole patrol line is what stops the
        // demo from walking into a hazard it technically had time to dodge.
        bool isThreatened(const Level& level, Vec2 position)
        {
            for (const Sentinel& sentinel : level.sentinels())
                if (sentinel.position == position
                    || sentinel.position + sentinel.step == position
                    || sentinel.position - sentinel.step == position)
                    return true;

            return false;
        }

        std::size_t indexOf(const Level& level, Vec2 position)
        {
            return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(level.width())
                + static_cast<std::size_t>(position.x);
        }

        // Breadth-first flood from `start`, which is assumed reachable. Returns
        // how many tiles it touched, and -- via `nearestFood` -- the step count
        // to the closest edible tile.
        int flood(const Level& level, const Snake& snake, const FoodField& food, Vec2 start, int& nearestFood)
        {
            const std::size_t tileCount = static_cast<std::size_t>(level.width()) * static_cast<std::size_t>(level.height());
            std::vector<bool> seen(tileCount, false);

            std::queue<std::pair<Vec2, int>> frontier;
            frontier.emplace(start, 0);
            seen[indexOf(level, start)] = true;

            nearestFood = kUnreachable;
            int reached = 0;

            while (!frontier.empty())
            {
                const auto [position, distance] = frontier.front();
                frontier.pop();
                ++reached;

                if (nearestFood == kUnreachable && food.indexAt(position).has_value())
                    nearestFood = distance;

                for (Direction direction : kDirections)
                {
                    const Vec2 next = position + toDelta(direction);
                    if (!level.inBounds(next) || seen[indexOf(level, next)])
                        continue;
                    if (!isPassable(level, snake, next))
                        continue;

                    seen[indexOf(level, next)] = true;
                    frontier.emplace(next, distance + 1);
                }
            }

            return reached;
        }
    }

    std::optional<Direction> Autoplay::chooseTurn(const Level& level, const Snake& snake, const FoodField& food) const
    {
        // A turn is already buffered for this step; queueing a second one here
        // would let the demo double-turn inside a single tick.
        if (snake.nextDirection() != snake.direction())
            return std::nullopt;

        struct Candidate
        {
            Direction direction{ Direction::Right };
            int space{ 0 };
            int foodDistance{ kUnreachable };
            bool threatened{ false };
            bool straight{ false };
        };

        std::vector<Candidate> candidates;

        for (Direction direction : kDirections)
        {
            if (isOpposite(direction, snake.direction()))
                continue;

            const Vec2 next = snake.head() + toDelta(direction);
            if (level.isWall(next) || snake.occupiesAfterStep(next))
                continue;

            Candidate candidate;
            candidate.direction = direction;
            candidate.threatened = isThreatened(level, next);
            candidate.straight = (direction == snake.direction());
            candidate.space = flood(level, snake, food, next, candidate.foodDistance);
            candidates.push_back(candidate);
        }

        if (candidates.empty())
            return std::nullopt;   // boxed in; hold the heading and take the hit

        // Enough open tiles ahead to lay the whole body down, plus slack. Below
        // that the snake is entering a pocket it will not get back out of.
        const int roomNeeded = snake.length() + snake.pendingGrowth() + 2;

        const Candidate* best = nullptr;
        for (const Candidate& candidate : candidates)
        {
            if (best == nullptr)
            {
                best = &candidate;
                continue;
            }

            // Ranked in strict order: survivable beats short, unthreatened
            // beats survivable, and a straight line breaks any remaining tie so
            // the demo does not jitter on equal-cost moves.
            const bool candidateSafe = candidate.space >= roomNeeded;
            const bool bestSafe = best->space >= roomNeeded;

            if (candidateSafe != bestSafe)
            {
                if (candidateSafe)
                    best = &candidate;
                continue;
            }

            if (candidate.threatened != best->threatened)
            {
                if (!candidate.threatened)
                    best = &candidate;
                continue;
            }

            if (!candidateSafe)
            {
                if (candidate.space > best->space)
                    best = &candidate;
                continue;
            }

            if (candidate.foodDistance != best->foodDistance)
            {
                if (candidate.foodDistance < best->foodDistance)
                    best = &candidate;
                continue;
            }

            if (!best->straight && candidate.straight)
                best = &candidate;
        }

        if (best->straight)
            return std::nullopt;

        return best->direction;
    }

    bool Autoplay::tickAbility(float deltaSeconds)
    {
        m_abilityTimer -= deltaSeconds;
        if (m_abilityTimer > 0.0f)
            return false;

        m_abilityTimer = kAbilityInterval;
        return true;
    }

    bool Autoplay::tickLevelClear(bool showing, float deltaSeconds)
    {
        if (!showing)
        {
            m_levelClearTimer = kLevelClearDwell;
            return false;
        }

        m_levelClearTimer -= deltaSeconds;
        return m_levelClearTimer <= 0.0f;
    }
}
