#pragma once

namespace neoncoil
{
    // Everything that defines "how hard is level N" lives here, so tuning the
    // curve is a one-file change instead of a hunt through gameplay code.
    struct LevelPlan
    {
        int index{ 1 };
        int targetScore{ 100 };
        int foodValue{ 10 };
        float tickSeconds{ 0.15f };  // base seconds per move, before snake modifiers
        int bonusEveryNFoods{ 4 };
        float bonusLifetimeSeconds{ 8.0f };
    };

    // Targets grow linearly and food value grows with them, which keeps every
    // level at roughly 10-13 pickups instead of degenerating into a grind.
    // Speed is deliberately the weakest lever: it is capped at +35%, and the
    // real difficulty comes from the generator's obstacle density and hazards.
    LevelPlan planFor(int levelIndex);

    // Points for finishing a level, scaled by how far past the target you went.
    int levelCompletionBonus(int levelIndex, int scoreAtCompletion, int target);
}
