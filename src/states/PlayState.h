#pragma once

#include "GameState.h"
#include "../game/Ability.h"
#include "../game/Food.h"
#include "../game/Level.h"
#include "../game/Progression.h"
#include "../game/Snake.h"
#include "../ui/Effects.h"
#include "../ui/Hud.h"

#include <cstdint>
#include <string>

namespace neoncoil
{
    // Owns a run: the board, the snake, food, scoring and level progression.
    // Overlay states (pause / level complete / game over) are pushed on top of
    // it, so the world is never torn down and rebuilt just to show a screen.
    class PlayState : public GameState
    {
    public:
        explicit PlayState(std::uint64_t runSeed);

        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

        // Read-only views of the live run, for the headless capture driver
        // (`--demo`) that steers the snake while screenshots and GIF frames are
        // being written. Nothing in the game itself reads these.
        const Level& level() const { return m_level; }
        const Snake& snake() const { return m_snake; }
        const FoodField& food() const { return m_food; }

    private:
        void startLevel(AppContext& context, int levelIndex);
        void readDirectionInput(const Input& input);
        void stepSnake(AppContext& context);
        void consumeFood(AppContext& context, std::size_t foodIndex);
        void activateAbility(AppContext& context);
        void die(AppContext& context, std::wstring cause);
        int comboMultiplier() const;
        float currentTickSeconds(const AppContext& context) const;

        void renderBoard(AppContext& context) const;
        ui::HudModel buildHudModel(const AppContext& context) const;
        RunSummary buildSummary() const;

        std::uint64_t m_runSeed{ 0 };
        Rng m_rng;

        Level m_level;
        LevelPlan m_plan;
        Snake m_snake;
        AbilityRuntime m_ability;
        FoodField m_food;
        ui::Effects m_effects;

        int m_levelIndex{ 1 };
        int m_totalScore{ 0 };
        int m_scoreThisLevel{ 0 };
        int m_foodEatenThisLevel{ 0 };
        int m_foodEatenTotal{ 0 };
        int m_longestSnake{ 0 };
        int m_abilitiesUsed{ 0 };

        float m_elapsed{ 0.0f };
        float m_tickAccumulator{ 0.0f };
        float m_comboTimer{ 0.0f };
        int m_comboCount{ 0 };

        // Frozen beat at the start of a level so the player can read the layout.
        float m_introTimer{ 0.0f };

        // Lets the death animation finish before the result screen appears.
        bool m_dead{ false };
        float m_deathTimer{ 0.0f };
        std::wstring m_causeOfDeath;

        // Set when the level-complete overlay is pushed; the next update after
        // it pops advances the run. Avoids threading a callback through states.
        bool m_advanceOnResume{ false };
    };
}
