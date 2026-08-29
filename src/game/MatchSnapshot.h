#pragma once

#include "Direction.h"
#include "Food.h"
#include "../core/Vec2.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil
{
    inline constexpr int kMaxMatchPlayers = 4;

    // Seat in a session. Assigned by the host on join, reused when a seat is
    // vacated. Deliberately NOT a player identity: identity is the opaque id in
    // net::PlayerIdentity, which is what an account system will later own.
    using PlayerSlot = std::uint8_t;
    inline constexpr PlayerSlot kInvalidSlot = 0xFF;

    enum class MatchPhase : std::uint8_t
    {
        Countdown,
        Running,
        Finished
    };

    // One snake, as it appears on the wire. Bodies are sent whole rather than
    // as deltas: a 64-segment cap makes the worst case ~260 bytes per snake, and
    // absolute state means a client that misses anything still converges.
    struct SnakeSnapshot
    {
        PlayerSlot slot{ kInvalidSlot };
        bool alive{ false };
        float respawnRemaining{ 0.0f };
        Direction direction{ Direction::Right };
        std::vector<Vec2> body;              // head first
        int score{ 0 };
        int kills{ 0 };
        int deaths{ 0 };
        bool phasing{ false };
        bool shielded{ false };
        bool abilityActive{ false };
        float abilityCharge{ 0.0f };

        // Sequence of the last input the host applied to this snake. The owning
        // client uses it to throw away the inputs it no longer has to replay --
        // without it, a prediction cannot tell which of its turns the host has
        // already seen and would keep re-applying all of them.
        std::uint32_t lastInput{ 0 };
    };

    struct FoodSnapshot
    {
        Vec2 position{ 0, 0 };
        FoodKind kind{ FoodKind::Normal };
        float secondsRemaining{ 0.0f };
    };

    // Everything that changes during a match. The static board is sent once, in
    // MatchStart; only walls the game destroys at runtime are echoed here, and
    // they are cumulative so re-applying a snapshot is idempotent.
    struct MatchSnapshot
    {
        std::uint32_t tick{ 0 };
        MatchPhase phase{ MatchPhase::Countdown };
        float phaseRemaining{ 0.0f };
        std::vector<SnakeSnapshot> snakes;
        std::vector<FoodSnapshot> food;
        std::vector<Vec2> sentinels;
        std::vector<Vec2> openedWalls;

        const SnakeSnapshot* find(PlayerSlot slot) const;
    };

    struct MatchStanding
    {
        PlayerSlot slot{ kInvalidSlot };
        std::wstring name;
        std::uint8_t colourIndex{ 0 };
        std::uint8_t typeIndex{ 0 };
        int score{ 0 };
        int kills{ 0 };
        int deaths{ 0 };
    };

    // Ordered best first. `winner` is kInvalidSlot on an empty match or a draw.
    struct MatchResult
    {
        std::vector<MatchStanding> standings;
        PlayerSlot winner{ kInvalidSlot };
        bool draw{ false };
    };
}
