#pragma once

#include "Ability.h"
#include "Level.h"
#include "MatchRules.h"
#include "MatchSnapshot.h"
#include "Snake.h"
#include "SnakeType.h"
#include "../core/Rng.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil
{
    struct MatchPlayerInit
    {
        PlayerSlot slot{ kInvalidSlot };
        std::wstring name;
        std::uint8_t colourIndex{ 0 };
        std::uint8_t typeIndex{ 0 };
    };

    // The authoritative rules of an online match. Runs on the host and nowhere
    // else: clients render the MatchSnapshot this produces, so there is exactly
    // one copy of the rules in the build and no possibility of the two drifting.
    //
    // Deliberately headless -- no Screen, no Effects, no Input, no state stack.
    // That is what lets the same class be lifted into a dedicated server binary
    // later without dragging the renderer along with it.
    //
    // Reuses Snake, Level, AbilityRuntime and SnakeType unchanged. Food is owned
    // here rather than by FoodField because the multiplayer placement rule
    // ("free of every snake, not just one") differs from the single-player one,
    // and PlayState is not worth destabilising for it.
    class MatchSimulation
    {
    public:
        void start(const Level& arena, const MatchRules& rules, std::uint64_t seed,
            const std::vector<MatchPlayerInit>& players);

        // Mid-match roster changes. Leaving is graceful: the snake is removed on
        // the next tick and the match carries on with whoever is left.
        void addPlayer(const MatchPlayerInit& player);
        void removePlayer(PlayerSlot slot);
        bool hasPlayer(PlayerSlot slot) const;
        int livePlayerCount() const;

        // Intents, not commands. The simulation decides whether a turn is legal
        // and when it takes effect, exactly as PlayState does for one snake.
        // `sequence` is the client's own input counter, echoed back in the
        // snapshot so that client can tell which of its turns have landed.
        void queueDirection(PlayerSlot slot, Direction direction, std::uint32_t sequence = 0);
        void requestAbility(PlayerSlot slot);

        void update(float deltaSeconds);

        MatchPhase phase() const { return m_phase; }
        bool finished() const { return m_phase == MatchPhase::Finished; }
        std::uint32_t tick() const { return m_tick; }

        // Whether the last update() actually moved anybody. The board only
        // changes when a snake steps, so this is what tells the host session
        // there is something new worth putting on the wire -- rather than it
        // guessing on a timer and being, on average, half a snapshot late.
        bool steppedLastUpdate() const { return m_stepped; }

        const Level& arena() const { return m_arena; }
        const MatchRules& rules() const { return m_rules; }

        void buildSnapshot(MatchSnapshot& out) const;
        MatchResult result() const;

        // Human-readable feed ("VIPER was cut down by MIDAS"). Drained by the
        // host session, which forwards it to its own UI; clients derive their
        // own from the snapshot rather than being sent strings.
        std::vector<std::wstring> drainEvents();

    private:
        struct Racer
        {
            PlayerSlot slot{ kInvalidSlot };
            std::wstring name;
            std::uint8_t colourIndex{ 0 };
            std::uint8_t typeIndex{ 0 };

            Snake body;
            AbilityRuntime ability;

            bool alive{ false };
            float respawnTimer{ 0.0f };
            float tickAccumulator{ 0.0f };
            bool abilityRequested{ false };
            std::uint32_t lastInput{ 0 };

            int score{ 0 };
            int kills{ 0 };
            int deaths{ 0 };
        };

        Racer* find(PlayerSlot slot);
        const Racer* find(PlayerSlot slot) const;

        const SnakeType& typeOf(const Racer& racer) const { return snakeTypeAt(racer.typeIndex); }

        void rebuildOccupancy();
        bool occupiedByAnyone(Vec2 tile) const;
        PlayerSlot ownerAt(Vec2 tile) const;

        void spawn(Racer& racer);
        // Not const: picking a spawn draws from the match Rng, which is what
        // keeps respawn placement reproducible from the match seed.
        bool chooseSpawn(const Racer& racer, Vec2& head, Direction& direction);

        // Everything a spawn candidate is judged on. Free means in bounds, not
        // wall, not hazard and nobody standing on it; the runway is how far a
        // snake placed here could travel before it had to turn, and the escapes
        // are the sideways exits from that runway.
        bool spawnTileIsFree(Vec2 tile) const;
        int runwayAhead(Vec2 head, Direction facing) const;
        int escapesAhead(Vec2 head, Direction facing) const;

        void stepRacer(Racer& racer);
        void kill(Racer& racer, PlayerSlot culprit, const std::wstring& cause);
        void eat(Racer& racer, std::size_t foodIndex);

        void topUpFood();
        bool placeFood(FoodKind kind, float lifetimeSeconds);
        bool tileIsFree(Vec2 tile) const;

        void finish();

        Level m_arena;
        MatchRules m_rules{};
        Rng m_rng{ 0 };

        std::vector<Racer> m_racers;
        std::vector<FoodSnapshot> m_food;
        std::vector<Vec2> m_openedWalls;
        std::vector<std::wstring> m_events;

        // One entry per tile: the slot of whoever occupies it, or kInvalidSlot.
        // Rebuilt once per update rather than searched per collision test, which
        // turns every "is anything here" question into an array read.
        std::vector<PlayerSlot> m_occupancy;

        MatchPhase m_phase{ MatchPhase::Countdown };
        float m_phaseRemaining{ 0.0f };
        float m_bonusTimer{ 0.0f };
        std::uint32_t m_tick{ 0 };
        bool m_stepped{ false };
    };
}
