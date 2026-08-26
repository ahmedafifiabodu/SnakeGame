#pragma once

#include "Level.h"
#include "Snake.h"
#include "../core/Rng.h"
#include "../core/Vec2.h"

#include <optional>
#include <vector>

namespace neoncoil
{
    enum class FoodKind
    {
        Normal,
        Bonus
    };

    struct Food
    {
        Vec2 position{ 0, 0 };
        FoodKind kind{ FoodKind::Normal };
        float secondsRemaining{ 0.0f }; // Normal food never expires
    };

    // Owns what is currently edible. Spawning is the one place in the game that
    // can genuinely run out of room, so it reports failure instead of looping:
    // a board with no free tile is a win, not a hang.
    class FoodField
    {
    public:
        void clear();
        void update(float deltaSeconds);

        // Returns false only when the board has no free tile at all.
        bool spawn(FoodKind kind, const Level& level, const Snake& body, Rng& rng, float lifetimeSeconds = 0.0f);

        const std::vector<Food>& items() const { return m_items; }

        std::optional<std::size_t> indexAt(Vec2 position) const;
        void removeAt(std::size_t index);

        bool hasNormal() const;

    private:
        std::optional<Vec2> findFreeTile(const Level& level, const Snake& body, Rng& rng) const;
        bool isFree(Vec2 position, const Level& level, const Snake& body) const;

        std::vector<Food> m_items;
    };
}
