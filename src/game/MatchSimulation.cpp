#include "MatchSimulation.h"

#include <algorithm>
#include <cstddef>

namespace neoncoil
{
    namespace
    {
        // Ceiling on catch-up steps per frame, for the same reason PlayState has
        // one: a stalled host frame must not fire a dozen moves at once and kill
        // everybody through no fault of their own.
        constexpr int kMaxStepsPerFrame = 3;

        // How many candidate tiles a respawn tries before it settles for the
        // first merely-legal one. Enough to reliably land away from other
        // players on a 56x32 board without an exhaustive search.
        constexpr int kSpawnCandidates = 48;

        constexpr Direction kAllDirections[] = {
            Direction::Right, Direction::Left, Direction::Up, Direction::Down
        };
    }

    void MatchSimulation::start(const Level& arena, const MatchRules& rules, std::uint64_t seed,
        const std::vector<MatchPlayerInit>& players)
    {
        m_arena = arena;
        m_arena.rebuildOpenTiles();
        m_rules = rules;
        m_rng.reseed(seed);

        m_racers.clear();
        m_food.clear();
        m_openedWalls.clear();
        m_events.clear();

        m_occupancy.assign(static_cast<std::size_t>(m_arena.width()) * static_cast<std::size_t>(m_arena.height()),
            kInvalidSlot);

        for (const MatchPlayerInit& player : players)
            addPlayer(player);

        m_phase = MatchPhase::Countdown;
        m_phaseRemaining = m_rules.countdownSeconds;
        m_bonusTimer = m_rules.bonusFoodInterval;
        m_tick = 0;

        topUpFood();
    }

    void MatchSimulation::addPlayer(const MatchPlayerInit& player)
    {
        if (player.slot == kInvalidSlot || find(player.slot) != nullptr)
            return;

        Racer racer;
        racer.slot = player.slot;
        racer.name = player.name;
        racer.colourIndex = player.colourIndex;
        racer.typeIndex = player.typeIndex;
        racer.ability.configure(snakeTypeAt(player.typeIndex).ability);

        m_racers.push_back(std::move(racer));
        spawn(m_racers.back());
        rebuildOccupancy();
    }

    void MatchSimulation::removePlayer(PlayerSlot slot)
    {
        const auto it = std::find_if(m_racers.begin(), m_racers.end(),
            [slot](const Racer& racer) { return racer.slot == slot; });

        if (it == m_racers.end())
            return;

        m_events.push_back(it->name + L" left the match");
        m_racers.erase(it);
        rebuildOccupancy();
    }

    bool MatchSimulation::hasPlayer(PlayerSlot slot) const
    {
        return find(slot) != nullptr;
    }

    int MatchSimulation::livePlayerCount() const
    {
        return static_cast<int>(m_racers.size());
    }

    MatchSimulation::Racer* MatchSimulation::find(PlayerSlot slot)
    {
        for (Racer& racer : m_racers)
            if (racer.slot == slot)
                return &racer;
        return nullptr;
    }

    const MatchSimulation::Racer* MatchSimulation::find(PlayerSlot slot) const
    {
        for (const Racer& racer : m_racers)
            if (racer.slot == slot)
                return &racer;
        return nullptr;
    }

    void MatchSimulation::queueDirection(PlayerSlot slot, Direction direction)
    {
        if (m_phase == MatchPhase::Finished)
            return;

        // Turns are accepted during the countdown so the first move is not a
        // scramble, but the snakes do not move until Running.
        if (Racer* racer = find(slot); racer != nullptr && racer->alive)
            racer->body.queueDirection(direction);
    }

    void MatchSimulation::requestAbility(PlayerSlot slot)
    {
        if (m_phase != MatchPhase::Running)
            return;

        if (Racer* racer = find(slot); racer != nullptr && racer->alive)
            racer->abilityRequested = true;
    }

    // ------------------------------------------------------------- occupancy --

    void MatchSimulation::rebuildOccupancy()
    {
        const std::size_t tiles = static_cast<std::size_t>(m_arena.width()) *
            static_cast<std::size_t>(m_arena.height());

        if (m_occupancy.size() != tiles)
            m_occupancy.assign(tiles, kInvalidSlot);
        else
            std::fill(m_occupancy.begin(), m_occupancy.end(), kInvalidSlot);

        for (const Racer& racer : m_racers)
        {
            if (!racer.alive)
                continue;

            for (const Vec2& segment : racer.body.body())
            {
                if (!m_arena.inBounds(segment))
                    continue;
                const std::size_t index = static_cast<std::size_t>(segment.y) *
                    static_cast<std::size_t>(m_arena.width()) + static_cast<std::size_t>(segment.x);
                m_occupancy[index] = racer.slot;
            }
        }
    }

    PlayerSlot MatchSimulation::ownerAt(Vec2 tile) const
    {
        if (!m_arena.inBounds(tile))
            return kInvalidSlot;

        const std::size_t index = static_cast<std::size_t>(tile.y) *
            static_cast<std::size_t>(m_arena.width()) + static_cast<std::size_t>(tile.x);
        return index < m_occupancy.size() ? m_occupancy[index] : kInvalidSlot;
    }

    bool MatchSimulation::occupiedByAnyone(Vec2 tile) const
    {
        return ownerAt(tile) != kInvalidSlot;
    }

    // ---------------------------------------------------------------- spawning --

    bool MatchSimulation::chooseSpawn(const Racer& racer, Vec2& head, Direction& direction)
    {
        const std::vector<Vec2>& open = m_arena.openTiles();
        if (open.empty())
            return false;

        const int length = std::max(2, m_rules.startLength);

        Vec2 bestHead{ 0, 0 };
        Direction bestDirection = Direction::Right;
        int bestClearance = -1;

        Vec2 fallbackHead{ 0, 0 };
        Direction fallbackDirection = Direction::Right;
        bool haveFallback = false;

        for (int attempt = 0; attempt < kSpawnCandidates; ++attempt)
        {
            const Vec2 candidate = open[static_cast<std::size_t>(
                m_rng.range(0, static_cast<int>(open.size()) - 1))];

            for (Direction facing : kAllDirections)
            {
                const Vec2 forward = toDelta(facing);

                // The head, the whole body laid out behind it, and the tile
                // directly ahead all have to be clear -- otherwise a player
                // respawns already dead, which is the worst bug this mode could
                // ship with.
                bool viable = true;
                for (int i = -1; i < length && viable; ++i)
                {
                    const Vec2 tile = candidate - forward * i;
                    viable = m_arena.inBounds(tile) && !m_arena.isWall(tile) &&
                        !occupiedByAnyone(tile) && !m_arena.hazardAt(tile);
                }

                if (!viable)
                    continue;

                if (!haveFallback)
                {
                    fallbackHead = candidate;
                    fallbackDirection = facing;
                    haveFallback = true;
                }

                // Prefer the spot furthest from anybody else, so a respawn is
                // not an instant re-death and players spread across the board.
                int clearance = m_arena.width() + m_arena.height();
                for (const Racer& other : m_racers)
                {
                    if (!other.alive || other.slot == racer.slot || other.body.length() == 0)
                        continue;
                    clearance = std::min(clearance, manhattan(candidate, other.body.head()));
                }

                if (clearance > bestClearance)
                {
                    bestClearance = clearance;
                    bestHead = candidate;
                    bestDirection = facing;
                }
            }
        }

        if (bestClearance >= 0)
        {
            head = bestHead;
            direction = bestDirection;
            return true;
        }

        if (haveFallback)
        {
            head = fallbackHead;
            direction = fallbackDirection;
            return true;
        }

        return false;
    }

    void MatchSimulation::spawn(Racer& racer)
    {
        Vec2 head{ 0, 0 };
        Direction facing = Direction::Right;

        if (!chooseSpawn(racer, head, facing))
        {
            // A board with nowhere left to stand. Keep the player dead but keep
            // retrying rather than placing them inside a wall.
            racer.alive = false;
            racer.respawnTimer = std::max(0.5f, m_rules.respawnSeconds);
            return;
        }

        racer.body.reset(m_arena.size(), head, facing, std::max(2, m_rules.startLength));
        racer.ability.reset();
        racer.alive = true;
        racer.respawnTimer = 0.0f;
        racer.tickAccumulator = 0.0f;
        racer.abilityRequested = false;
    }

    // ---------------------------------------------------------------- stepping --

    void MatchSimulation::update(float deltaSeconds)
    {
        if (m_phase == MatchPhase::Finished)
            return;

        if (m_phase == MatchPhase::Countdown)
        {
            m_phaseRemaining -= deltaSeconds;
            if (m_phaseRemaining > 0.0f)
                return;

            m_phase = MatchPhase::Running;
            m_phaseRemaining = m_rules.durationSeconds;
        }

        m_phaseRemaining -= deltaSeconds;
        m_arena.updateHazards(deltaSeconds);

        // Bodies moved last frame, so every "is this tile free" question below
        // -- food placement included -- needs a current grid to answer against.
        rebuildOccupancy();

        // --- food timers ------------------------------------------------------
        for (FoodSnapshot& food : m_food)
        {
            if (food.kind == FoodKind::Bonus)
                food.secondsRemaining -= deltaSeconds;
        }
        m_food.erase(std::remove_if(m_food.begin(), m_food.end(),
            [](const FoodSnapshot& food)
            {
                return food.kind == FoodKind::Bonus && food.secondsRemaining <= 0.0f;
            }),
            m_food.end());

        m_bonusTimer -= deltaSeconds;
        if (m_bonusTimer <= 0.0f)
        {
            m_bonusTimer = m_rules.bonusFoodInterval;
            placeFood(FoodKind::Bonus, m_rules.bonusLifetimeSeconds);
        }

        topUpFood();

        // --- abilities and respawns ------------------------------------------
        for (Racer& racer : m_racers)
        {
            racer.ability.update(deltaSeconds);

            if (!racer.alive)
            {
                racer.respawnTimer -= deltaSeconds;
                if (racer.respawnTimer <= 0.0f)
                {
                    rebuildOccupancy();
                    spawn(racer);
                }
                continue;
            }

            if (!racer.abilityRequested)
                continue;

            racer.abilityRequested = false;

            // Shed on a short snake burns the cooldown for nothing, exactly as
            // in the single-player rules.
            if (racer.ability.definition().kind == AbilityKind::Shed && racer.body.length() <= 6)
                continue;

            if (const std::optional<AbilityKind> fired = racer.ability.tryActivate(); fired.has_value())
            {
                if (*fired == AbilityKind::Shed)
                {
                    const int keep = std::max(4, racer.body.length() / 2);
                    const std::vector<Vec2> shed = racer.body.shedTo(keep);
                    racer.score += static_cast<int>(shed.size()) * 5;
                }
                else if (*fired == AbilityKind::GoldRush)
                {
                    placeFood(FoodKind::Bonus, m_rules.bonusLifetimeSeconds);
                }
            }
        }

        rebuildOccupancy();

        // --- movement ---------------------------------------------------------
        // Each snake keeps its own accumulator, so the speed differences between
        // snake types survive into multiplayer instead of being flattened.
        for (Racer& racer : m_racers)
        {
            if (!racer.alive)
                continue;

            const float speed = typeOf(racer).speedMultiplier * racer.ability.speedScale();
            const float tickSeconds = m_rules.tickSeconds / std::max(0.1f, speed);

            racer.tickAccumulator += deltaSeconds;

            for (int step = 0; step < kMaxStepsPerFrame && racer.tickAccumulator >= tickSeconds && racer.alive; ++step)
            {
                racer.tickAccumulator -= tickSeconds;
                stepRacer(racer);
                rebuildOccupancy();
            }

            if (racer.tickAccumulator > tickSeconds)
                racer.tickAccumulator = tickSeconds;
        }

        ++m_tick;

        // --- end conditions ---------------------------------------------------
        if (m_phaseRemaining <= 0.0f)
        {
            finish();
            return;
        }

        if (m_rules.scoreLimit > 0)
        {
            for (const Racer& racer : m_racers)
            {
                if (racer.score >= m_rules.scoreLimit)
                {
                    finish();
                    return;
                }
            }
        }
    }

    void MatchSimulation::stepRacer(Racer& racer)
    {
        const Vec2 next = racer.body.nextHead();

        const bool phasing = racer.ability.canPhaseWalls();
        const bool blockedByBorder = m_arena.isBorder(next);
        const bool blockedByWall = !blockedByBorder && m_arena.isWall(next) && !phasing;
        const bool blockedBySelf = racer.body.occupiesAfterStep(next) && !racer.ability.canPhaseSelf();
        const bool hitHazard = m_arena.hazardAt(next);

        // Whoever owns that tile, if it is not us. Phasing lets a snake pass
        // through walls, never through another player: making players
        // intangible would remove the only reason to watch each other.
        const PlayerSlot owner = ownerAt(next);
        const bool blockedByRival = owner != kInvalidSlot && owner != racer.slot;

        if (blockedByBorder || blockedByWall || blockedBySelf || hitHazard || blockedByRival)
        {
            if (racer.ability.consumeShield() && !blockedByBorder && !blockedByRival)
            {
                if (blockedByWall && m_arena.destroyWall(next))
                {
                    m_openedWalls.push_back(next);
                    m_arena.rebuildOpenTiles();
                }

                if (hitHazard)
                    m_arena.clearSentinels();
            }
            else
            {
                if (blockedByRival)
                    kill(racer, owner, L"was cut down by");
                else if (hitHazard)
                    kill(racer, kInvalidSlot, L"was caught by a sentinel");
                else if (blockedBySelf)
                    kill(racer, kInvalidSlot, L"bit their own tail");
                else
                    kill(racer, kInvalidSlot, L"hit the wall");
                return;
            }
        }

        racer.body.commitStep();

        // Phase expiring inside geometry is fatal, as it is in single player --
        // it is what stops Phase from being a free pass across the board.
        if (!racer.ability.canPhaseWalls() && m_arena.isWall(racer.body.head()))
        {
            kill(racer, kInvalidSlot, L"solidified inside a wall");
            return;
        }

        // The length cap keeps one runaway player from owning the arena and
        // keeps the worst-case snapshot bounded.
        if (racer.body.length() > m_rules.maxLength)
            racer.body.shedTo(m_rules.maxLength);

        for (std::size_t i = 0; i < m_food.size(); ++i)
        {
            if (m_food[i].position == racer.body.head())
            {
                eat(racer, i);
                break;
            }
        }
    }

    void MatchSimulation::eat(Racer& racer, std::size_t foodIndex)
    {
        const FoodSnapshot food = m_food[foodIndex];
        m_food.erase(m_food.begin() + static_cast<std::ptrdiff_t>(foodIndex));

        const SnakeType& type = typeOf(racer);
        const int base = food.kind == FoodKind::Bonus ? m_rules.bonusFoodValue : m_rules.foodValue;
        const float raw = static_cast<float>(base) * type.scoreMultiplier * racer.ability.scoreMultiplier();

        racer.score += static_cast<int>(raw + 0.5f);
        racer.body.grow(type.growthPerFood + (food.kind == FoodKind::Bonus ? 1 : 0));

        if (racer.ability.spawnsBonusOnEat())
            placeFood(FoodKind::Bonus, m_rules.bonusLifetimeSeconds);
    }

    void MatchSimulation::kill(Racer& racer, PlayerSlot culprit, const std::wstring& cause)
    {
        racer.alive = false;
        racer.respawnTimer = m_rules.respawnSeconds;
        ++racer.deaths;
        racer.score = std::max(0, racer.score - m_rules.deathPenalty);

        if (Racer* killer = culprit == kInvalidSlot ? nullptr : find(culprit); killer != nullptr)
        {
            ++killer->kills;
            killer->score += m_rules.killBonus;
            m_events.push_back(racer.name + L" " + cause + L" " + killer->name);
        }
        else
        {
            m_events.push_back(racer.name + L" " + cause);
        }
    }

    // -------------------------------------------------------------------- food --

    bool MatchSimulation::tileIsFree(Vec2 tile) const
    {
        if (!m_arena.inBounds(tile) || m_arena.isWall(tile) || m_arena.hazardAt(tile))
            return false;
        if (occupiedByAnyone(tile))
            return false;

        for (const FoodSnapshot& food : m_food)
            if (food.position == tile)
                return false;

        return true;
    }

    bool MatchSimulation::placeFood(FoodKind kind, float lifetimeSeconds)
    {
        const std::vector<Vec2>& open = m_arena.openTiles();
        if (open.empty())
            return false;

        // Random probing, then a linear sweep as the fallback. The sweep is what
        // makes this correct on a nearly full board instead of a hang -- the same
        // reasoning FoodField uses for single player.
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const Vec2 candidate = open[static_cast<std::size_t>(
                m_rng.range(0, static_cast<int>(open.size()) - 1))];

            if (!tileIsFree(candidate))
                continue;

            m_food.push_back(FoodSnapshot{ candidate, kind, lifetimeSeconds });
            return true;
        }

        for (const Vec2& candidate : open)
        {
            if (!tileIsFree(candidate))
                continue;

            m_food.push_back(FoodSnapshot{ candidate, kind, lifetimeSeconds });
            return true;
        }

        return false;
    }

    void MatchSimulation::topUpFood()
    {
        int normal = 0;
        for (const FoodSnapshot& food : m_food)
            if (food.kind == FoodKind::Normal)
                ++normal;

        for (int i = normal; i < m_rules.normalFoodCount; ++i)
        {
            if (!placeFood(FoodKind::Normal, 0.0f))
                return;   // no room left; try again next frame
        }
    }

    // ------------------------------------------------------------------ results --

    void MatchSimulation::finish()
    {
        if (m_phase == MatchPhase::Finished)
            return;

        m_phase = MatchPhase::Finished;
        m_phaseRemaining = 0.0f;
        m_events.push_back(L"Match over");
    }

    MatchResult MatchSimulation::result() const
    {
        MatchResult result;
        result.standings.reserve(m_racers.size());

        for (const Racer& racer : m_racers)
        {
            MatchStanding standing;
            standing.slot = racer.slot;
            standing.name = racer.name;
            standing.colourIndex = racer.colourIndex;
            standing.typeIndex = racer.typeIndex;
            standing.score = racer.score;
            standing.kills = racer.kills;
            standing.deaths = racer.deaths;
            result.standings.push_back(std::move(standing));
        }

        std::sort(result.standings.begin(), result.standings.end(),
            [](const MatchStanding& a, const MatchStanding& b)
            {
                if (a.score != b.score) return a.score > b.score;
                if (a.kills != b.kills) return a.kills > b.kills;
                if (a.deaths != b.deaths) return a.deaths < b.deaths;
                return a.slot < b.slot;
            });

        if (!result.standings.empty())
        {
            const bool tied = result.standings.size() > 1 &&
                result.standings[0].score == result.standings[1].score &&
                result.standings[0].kills == result.standings[1].kills;

            result.draw = tied;
            result.winner = tied ? kInvalidSlot : result.standings.front().slot;
        }

        return result;
    }

    void MatchSimulation::buildSnapshot(MatchSnapshot& out) const
    {
        out.tick = m_tick;
        out.phase = m_phase;
        out.phaseRemaining = std::max(0.0f, m_phaseRemaining);

        out.snakes.clear();
        out.snakes.reserve(m_racers.size());

        for (const Racer& racer : m_racers)
        {
            SnakeSnapshot snake;
            snake.slot = racer.slot;
            snake.alive = racer.alive;
            snake.respawnRemaining = racer.respawnTimer;
            snake.direction = racer.body.direction();
            snake.score = racer.score;
            snake.kills = racer.kills;
            snake.deaths = racer.deaths;
            snake.phasing = racer.ability.canPhaseWalls();
            snake.shielded = racer.ability.hasShield();
            snake.abilityActive = racer.ability.isActive();
            snake.abilityCharge = racer.ability.chargeFraction();

            if (racer.alive)
            {
                snake.body.assign(racer.body.body().begin(), racer.body.body().end());
            }

            out.snakes.push_back(std::move(snake));
        }

        out.food = m_food;

        out.sentinels.clear();
        out.sentinels.reserve(m_arena.sentinels().size());
        for (const Sentinel& sentinel : m_arena.sentinels())
            out.sentinels.push_back(sentinel.position);

        out.openedWalls = m_openedWalls;
    }

    std::vector<std::wstring> MatchSimulation::drainEvents()
    {
        std::vector<std::wstring> drained;
        drained.swap(m_events);
        return drained;
    }
}
