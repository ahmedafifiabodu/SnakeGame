#pragma once

namespace neoncoil
{
    // Tuning for an online match. Kept separate from LevelPlan because the
    // multiplayer mode is a timed arena deathmatch rather than a level ladder:
    // there is no target score to clear and no progression between boards.
    //
    // Every field is host-authoritative and travels to clients inside
    // MatchStart, so the host can retune a match without clients being on the
    // same build of the config file.
    struct MatchRules
    {
        float countdownSeconds{ 3.0f };
        float durationSeconds{ 180.0f };
        int   scoreLimit{ 0 };            // > 0 ends the match early
        float respawnSeconds{ 3.0f };
        int   startLength{ 4 };
        int   foodValue{ 10 };
        int   bonusFoodValue{ 30 };
        int   killBonus{ 50 };
        int   deathPenalty{ 25 };
        int   normalFoodCount{ 3 };
        float bonusFoodInterval{ 12.0f };
        float bonusLifetimeSeconds{ 8.0f };
        float tickSeconds{ 0.14f };       // base seconds per move, before snake modifiers
        int   arenaLevelIndex{ 4 };       // which generator archetype band the board comes from
        int   maxLength{ 64 };            // caps snapshot size, and stops one runaway snake owning the board
    };
}
