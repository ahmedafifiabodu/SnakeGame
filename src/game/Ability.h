#pragma once

#include "SnakeType.h"

#include <optional>

namespace neoncoil
{
    // Every ability effect in the game resolves here. Gameplay code never
    // branches on AbilityKind: it asks questions ("am I phasing?", "what is my
    // score multiplier?") and this class answers them. That keeps ability logic
    // in one file instead of scattered through movement, scoring and collision.
    class AbilityRuntime
    {
    public:
        void configure(const AbilityDef& definition);

        // Called at the start of a level. The ability starts charged so the
        // player is never waiting at a level boundary.
        void reset();

        void update(float deltaSeconds);

        // Returns the kind that fired, or nothing if still on cooldown.
        // One-shot consequences that need the world (Shed) are applied by the
        // caller; everything self-contained is handled in here.
        std::optional<AbilityKind> tryActivate();

        const AbilityDef& definition() const { return m_definition; }

        bool isReady() const;
        bool isActive() const { return m_activeRemaining > 0.0f; }
        float activeSecondsRemaining() const { return m_activeRemaining; }

        // 0 when just fired, 1 when ready. Drives the HUD meter.
        float chargeFraction() const;
        // 1 when just activated, 0 when it expires. Drives the active bar.
        float activeFraction() const;

        // --- Queries the rest of the game asks -------------------------------
        float speedScale() const;
        bool canPhaseWalls() const;
        bool canPhaseSelf() const;
        float scoreMultiplier() const;
        bool spawnsBonusOnEat() const;

        bool hasShield() const { return m_shieldCharges > 0; }
        // Spends a shield charge. Returns false if there was none to spend.
        bool consumeShield();

    private:
        AbilityDef m_definition{};
        float m_cooldownRemaining{ 0.0f };
        float m_activeRemaining{ 0.0f };
        int m_shieldCharges{ 0 };
    };
}
