#include "Ability.h"

#include <algorithm>

namespace neoncoil
{
    void AbilityRuntime::configure(const AbilityDef& definition)
    {
        m_definition = definition;
        reset();
    }

    void AbilityRuntime::reset()
    {
        m_cooldownRemaining = 0.0f;
        m_activeRemaining = 0.0f;
        m_shieldCharges = 0;
    }

    void AbilityRuntime::update(float deltaSeconds)
    {
        m_cooldownRemaining = std::max(0.0f, m_cooldownRemaining - deltaSeconds);
        m_activeRemaining = std::max(0.0f, m_activeRemaining - deltaSeconds);
    }

    bool AbilityRuntime::isReady() const
    {
        if (m_cooldownRemaining > 0.0f || isActive())
            return false;

        // A shield that has not been spent yet is the ability, still running.
        if (m_definition.kind == AbilityKind::IronScales && m_shieldCharges > 0)
            return false;

        return true;
    }

    std::optional<AbilityKind> AbilityRuntime::tryActivate()
    {
        if (!isReady())
            return std::nullopt;

        m_cooldownRemaining = m_definition.cooldownSeconds;
        m_activeRemaining = m_definition.durationSeconds;

        if (m_definition.kind == AbilityKind::IronScales)
            m_shieldCharges = 1;

        return m_definition.kind;
    }

    float AbilityRuntime::chargeFraction() const
    {
        if (m_definition.cooldownSeconds <= 0.0f)
            return 1.0f;
        const float used = m_definition.cooldownSeconds - m_cooldownRemaining;
        return std::clamp(used / m_definition.cooldownSeconds, 0.0f, 1.0f);
    }

    float AbilityRuntime::activeFraction() const
    {
        if (m_definition.durationSeconds <= 0.0f)
            return isActive() ? 1.0f : 0.0f;
        return std::clamp(m_activeRemaining / m_definition.durationSeconds, 0.0f, 1.0f);
    }

    float AbilityRuntime::speedScale() const
    {
        if (m_definition.kind == AbilityKind::Dash && isActive())
            return 2.0f;
        return 1.0f;
    }

    bool AbilityRuntime::canPhaseWalls() const
    {
        return m_definition.kind == AbilityKind::Phase && isActive();
    }

    bool AbilityRuntime::canPhaseSelf() const
    {
        return canPhaseWalls();
    }

    float AbilityRuntime::scoreMultiplier() const
    {
        if (m_definition.kind == AbilityKind::GoldRush && isActive())
            return 3.0f;
        return 1.0f;
    }

    bool AbilityRuntime::spawnsBonusOnEat() const
    {
        return m_definition.kind == AbilityKind::GoldRush && isActive();
    }

    bool AbilityRuntime::consumeShield()
    {
        if (m_shieldCharges <= 0)
            return false;

        --m_shieldCharges;
        return true;
    }
}
