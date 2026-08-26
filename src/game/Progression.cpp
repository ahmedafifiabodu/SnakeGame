#include "Progression.h"

#include <algorithm>

namespace neoncoil
{
    LevelPlan planFor(int levelIndex)
    {
        const int index = std::max(1, levelIndex);

        LevelPlan plan;
        plan.index = index;
        // Food value is tied to the target so the number of pickups per level
        // stays near 10-12 forever. Without this the two curves diverge and
        // level 20 quietly becomes a 26-pickup grind.
        plan.targetScore = 100 + 60 * (index - 1);
        plan.foodValue = 10 + 5 * (index - 1);

        const float speedScale = std::min(1.35f, 1.0f + 0.03f * static_cast<float>(index - 1));
        plan.tickSeconds = 0.15f / speedScale;

        plan.bonusEveryNFoods = index >= 3 ? 3 : 4;
        plan.bonusLifetimeSeconds = std::max(5.0f, 9.0f - 0.3f * static_cast<float>(index - 1));

        return plan;
    }

    int levelCompletionBonus(int levelIndex, int scoreAtCompletion, int target)
    {
        const int overshoot = std::max(0, scoreAtCompletion - target);
        return 50 * std::max(1, levelIndex) + overshoot / 2;
    }
}
