#pragma once

#include "../game/Direction.h"

#include <optional>

namespace neoncoil
{
    class FoodField;
    class Level;
    class Snake;
}

namespace neoncoil::tools
{
    // Steers the snake during a headless capture (`--demo`).
    //
    // This is a capture tool, not a game feature: nothing in the shipped game
    // consults it. It exists so `--screenshot` and `--capture` produce a real
    // run -- a grown snake, a scoring HUD, a level worth looking at -- instead
    // of a length-four snake sliding straight into the nearest wall, which is
    // all an unattended PlayState does.
    //
    // The policy is deliberately simple and readable rather than optimal: head
    // for the nearest food, but never into a pocket too small to escape from.
    class Autoplay
    {
    public:
        // The turn to press this tick, or nothing when holding the current
        // heading is already the best move. Safe to call every frame: it
        // returns nothing while a turn is still buffered, so at most one turn
        // is queued per step.
        std::optional<Direction> chooseTurn(const Level& level, const Snake& snake, const FoodField& food) const;

        // Advances the ability timer and reports the single frame on which the
        // ability should fire. Cooldowns are enforced by the game, so a press
        // that arrives early simply does nothing.
        bool tickAbility(float deltaSeconds);

        // Call every frame with whether the level-clear panel is up. Reports
        // when to confirm it: not immediately, but after a beat, the way a
        // player reading the panel would -- which is also what gives a capture
        // a window wide enough to land on.
        bool tickLevelClear(bool showing, float deltaSeconds);

        // Seconds between ability presses. Loose enough that the game's own
        // cooldowns, not this, are what pace them.
        static constexpr float kAbilityInterval = 6.0f;

        // How long the level-clear panel is left up before it is confirmed.
        static constexpr float kLevelClearDwell = 1.5f;

    private:
        float m_abilityTimer{ kAbilityInterval };
        float m_levelClearTimer{ kLevelClearDwell };
    };
}
