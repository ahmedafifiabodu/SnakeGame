#pragma once

#include "Level.h"
#include "MatchRules.h"
#include "MatchSnapshot.h"
#include "Snake.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace neoncoil
{
    // The local player's own snake, run ahead of the host.
    //
    // Without this, a turn is only visible after it has travelled to the host
    // and the resulting snapshot has travelled back -- a full round trip, which
    // over a relay is most of a tenth of a second even when everything is close.
    // The snake still moves at the right speed the whole time, so the game is
    // not slow; it is *late*, which is worse, because the player's hands stop
    // agreeing with the screen.
    //
    // So the client keeps its own copy of one snake -- its own -- and steps it
    // on the same clock the host uses. A turn applies on the next frame. When a
    // snapshot arrives it is the truth: the body is replaced with the host's,
    // and every input the host had not yet seen is replayed on top. If the host
    // agreed with the prediction, that replay lands on exactly the same tiles
    // and nothing moves on screen. If it disagreed, the snake corrects, which is
    // the honest outcome -- the host owns the rules.
    //
    // Deliberately predicts movement and nothing else. Eating, kills, walls
    // opening and deaths all stay host-authoritative: guessing at those would
    // mean showing a player a score they did not get or a death that did not
    // happen, and taking it back looks far worse than a tenth of a second.
    class SnakePrediction
    {
    public:
        // Starts predicting from an authoritative snapshot. Call at match start
        // and after every respawn.
        void begin(const Level& arena, const MatchRules& rules,
            const SnakeSnapshot& authoritative, std::uint8_t typeIndex);

        void stop() { m_active = false; }
        bool active() const { return m_active; }

        // A turn the player just made, with the sequence number that went to the
        // host. Applied locally at once and remembered until the host confirms
        // it has been seen.
        void queueDirection(Direction direction, std::uint32_t sequence);

        // Advances the local snake on the host's tick clock.
        void update(float deltaSeconds);

        // Folds in the host's version. `lastInput` is the sequence of the last
        // input the host applied to this snake; everything after it is replayed.
        void reconcile(const SnakeSnapshot& authoritative, std::uint32_t lastInput);

        const std::deque<Vec2>& body() const { return m_snake.body(); }
        Direction direction() const { return m_snake.direction(); }

        // 0..1 through the current tick, for drawing the snake sliding between
        // tiles rather than teleporting between them.
        float stepFraction() const;

    private:
        struct PendingInput
        {
            std::uint32_t sequence{ 0 };
            Direction direction{ Direction::Right };
        };

        float tickSeconds() const;

        // True when the snake could take this step without the host having to
        // disagree. Prediction stops rather than guessing at a death.
        bool canStepInto(Vec2 tile) const;

        // Replaces the local body with the host's, wholesale.
        void adopt(const SnakeSnapshot& authoritative);

        // Whether the host's head is somewhere the prediction has recently been.
        // Being a step or two ahead is not a disagreement -- it is the point.
        bool agreesWith(const SnakeSnapshot& authoritative) const;

        Level m_arena;
        MatchRules m_rules{};
        Snake m_snake;

        std::deque<PendingInput> m_pending;
        std::uint8_t m_typeIndex{ 0 };

        float m_accumulator{ 0.0f };
        bool m_active{ false };

        // Set when a predicted step would have hit something. The snake holds
        // still until the host says what happened, because the alternative is
        // showing a crash that a shield or a phase may have prevented.
        bool m_blocked{ false };
    };
}
