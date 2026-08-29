#include "SnakePrediction.h"

#include "SnakeType.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        // How far ahead of the host the prediction may run before it stops.
        //
        // On a healthy connection the client leads by under one step -- at seven
        // steps a second, a hundred-millisecond round trip is a fraction of a
        // tick. If snapshots stop arriving, an unbounded prediction would march
        // the snake across the board alone and snap it back when the host
        // reappeared. Freezing after a few steps is better: a snake that stops
        // reads as a connection problem, one that teleports reads as a bug.
        constexpr int kMaxUnconfirmedSteps = 6;

        // How far the host's head may be from the prediction before the
        // prediction is considered wrong.
        //
        // Being one or two steps out is not a disagreement, it is the client
        // being ahead, which is the entire point. Snapping on that would undo
        // the prediction on every snapshot and produce exactly the stutter this
        // class exists to remove. Only a head the prediction never visited at
        // all is a real divergence -- a death, a shed, a wall opening, or a turn
        // the host refused.
        constexpr std::size_t kAgreementDepth = 3;
        constexpr int kAgreementLengthSlack = 3;
    }

    void SnakePrediction::begin(const Level& arena, const MatchRules& rules,
        const SnakeSnapshot& authoritative, std::uint8_t typeIndex)
    {
        if (authoritative.body.empty())
        {
            m_active = false;
            return;
        }

        m_arena = arena;
        m_rules = rules;
        m_typeIndex = typeIndex;

        adopt(authoritative);

        m_pending.clear();
        m_accumulator = 0.0f;
        m_active = true;
    }

    void SnakePrediction::adopt(const SnakeSnapshot& authoritative)
    {
        m_snake.reset(m_arena.size(), authoritative.body.front(), authoritative.direction,
            static_cast<int>(authoritative.body.size()));
        m_blocked = false;
    }

    float SnakePrediction::tickSeconds() const
    {
        // The host's formula, not an approximation of it. If these two disagree
        // the prediction drifts by a step every few seconds and the player sees
        // a correction they did nothing to earn.
        const float speed = snakeTypeAt(m_typeIndex).speedMultiplier;
        return m_rules.tickSeconds / std::max(0.1f, speed);
    }

    void SnakePrediction::queueDirection(Direction direction, std::uint32_t sequence)
    {
        if (!m_active)
            return;

        // Validated by the same rules the host uses, so an input the host will
        // reject is never held in the pending list pretending to matter.
        const Direction before = m_snake.nextDirection();
        m_snake.queueDirection(direction);
        if (m_snake.nextDirection() == before && direction != before)
            return;

        m_pending.push_back({ sequence, direction });

        // Bounded: the host confirms these within a round trip, so a list this
        // long means the connection has gone and the oldest entries are of no
        // use to anybody.
        while (m_pending.size() > 32)
            m_pending.pop_front();
    }

    bool SnakePrediction::canStepInto(Vec2 tile) const
    {
        if (!m_arena.inBounds(tile) || m_arena.isBorder(tile))
            return false;
        if (m_arena.isWall(tile))
            return false;
        if (m_arena.hazardAt(tile))
            return false;

        // Only self-collision is knowable locally. Another player's body is not:
        // the client's picture of it is a round trip old, so refusing to move
        // into it would stop the snake for a tail that has already gone.
        return !m_snake.occupiesAfterStep(tile);
    }

    void SnakePrediction::update(float deltaSeconds)
    {
        if (!m_active)
            return;

        const float tick = tickSeconds();
        m_accumulator += deltaSeconds;

        for (int taken = 0; taken < kMaxUnconfirmedSteps && m_accumulator >= tick; ++taken)
        {
            m_accumulator -= tick;

            if (!canStepInto(m_snake.nextHead()))
            {
                // Prediction stops rather than guessing at a death. A shield or
                // a phase may mean the host does not agree that this was fatal,
                // and showing a crash that is then taken back is far worse than
                // a snake that pauses for a fraction of a second.
                m_blocked = true;
                break;
            }

            m_snake.commitStep();
            m_blocked = false;
        }

        // Clamped so a stalled frame cannot bank a dozen steps and fire them all
        // the moment the game catches up.
        m_accumulator = std::clamp(m_accumulator, 0.0f, tick);
    }

    bool SnakePrediction::agreesWith(const SnakeSnapshot& authoritative) const
    {
        const std::deque<Vec2>& predicted = m_snake.body();
        if (predicted.empty() || authoritative.body.empty())
            return false;

        const int lengthDelta = std::abs(static_cast<int>(predicted.size()) -
            static_cast<int>(authoritative.body.size()));
        if (lengthDelta > kAgreementLengthSlack)
            return false;

        // The host's head should be somewhere the prediction has recently been:
        // that is what "the client is a step or two ahead" looks like.
        const Vec2 hostHead = authoritative.body.front();
        const std::size_t depth = std::min(kAgreementDepth, predicted.size());

        for (std::size_t i = 0; i < depth; ++i)
        {
            if (predicted[i] == hostHead)
                return true;
        }

        return false;
    }

    void SnakePrediction::reconcile(const SnakeSnapshot& authoritative, std::uint32_t lastInput)
    {
        if (!m_active)
            return;

        if (!authoritative.alive || authoritative.body.empty())
        {
            // Dead, or waiting to respawn. Nothing to predict; the host sends a
            // fresh body when there is, and begin() picks it up again.
            m_active = false;
            return;
        }

        // Everything the host has already applied is history.
        while (!m_pending.empty() && m_pending.front().sequence <= lastInput)
            m_pending.pop_front();

        if (agreesWith(authoritative) && !m_blocked)
            return;

        // The host disagreed, or the prediction had stopped against something.
        // Either way the host's body is the truth.
        adopt(authoritative);

        // Then the inputs it has not seen yet, in the order they were made, so a
        // turn made during the round trip is not lost by the correction that
        // arrives in the middle of it.
        for (const PendingInput& input : m_pending)
            m_snake.queueDirection(input.direction);
    }

    float SnakePrediction::stepFraction() const
    {
        const float tick = tickSeconds();
        if (tick <= 0.0f)
            return 0.0f;
        return std::clamp(m_accumulator / tick, 0.0f, 1.0f);
    }
}
