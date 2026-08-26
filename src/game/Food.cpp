#include "Food.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        // Enough rejection samples that a full scan is only reached when the
        // board really is nearly full.
        constexpr int kSampleAttempts = 250;
    }

    void FoodField::clear()
    {
        m_items.clear();
    }

    void FoodField::update(float deltaSeconds)
    {
        for (Food& food : m_items)
            if (food.kind == FoodKind::Bonus)
                food.secondsRemaining -= deltaSeconds;

        std::erase_if(m_items, [](const Food& food)
        {
            return food.kind == FoodKind::Bonus && food.secondsRemaining <= 0.0f;
        });
    }

    bool FoodField::isFree(Vec2 position, const Level& level, const Snake& body) const
    {
        if (level.isWall(position) || body.occupies(position) || level.hazardAt(position))
            return false;

        return std::none_of(m_items.begin(), m_items.end(),
            [position](const Food& food) { return food.position == position; });
    }

    std::optional<Vec2> FoodField::findFreeTile(const Level& level, const Snake& body, Rng& rng) const
    {
        const std::vector<Vec2>& open = level.openTiles();
        if (open.empty())
            return std::nullopt;

        for (int attempt = 0; attempt < kSampleAttempts; ++attempt)
        {
            const Vec2 candidate = rng.pick(open);
            if (isFree(candidate, level, body))
                return candidate;
        }

        // Exhaustive fallback. If this finds nothing the board is genuinely
        // full, which the caller treats as a completed level.
        for (const Vec2& candidate : open)
            if (isFree(candidate, level, body))
                return candidate;

        return std::nullopt;
    }

    bool FoodField::spawn(FoodKind kind, const Level& level, const Snake& body, Rng& rng, float lifetimeSeconds)
    {
        const std::optional<Vec2> position = findFreeTile(level, body, rng);
        if (!position.has_value())
            return false;

        Food food;
        food.position = *position;
        food.kind = kind;
        food.secondsRemaining = kind == FoodKind::Bonus ? lifetimeSeconds : 0.0f;
        m_items.push_back(food);
        return true;
    }

    std::optional<std::size_t> FoodField::indexAt(Vec2 position) const
    {
        for (std::size_t i = 0; i < m_items.size(); ++i)
            if (m_items[i].position == position)
                return i;
        return std::nullopt;
    }

    void FoodField::removeAt(std::size_t index)
    {
        if (index < m_items.size())
            m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
    }

    bool FoodField::hasNormal() const
    {
        return std::any_of(m_items.begin(), m_items.end(),
            [](const Food& food) { return food.kind == FoodKind::Normal; });
    }
}
