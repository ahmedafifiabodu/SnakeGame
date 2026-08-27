#pragma once

#include "GameState.h"
#include "../ui/Hit.h"

#include <string>

namespace neoncoil
{
    // Suspends the world without destroying it. PlayState stays on the stack
    // underneath and keeps rendering, dimmed, behind the panel.
    class PauseState : public GameState
    {
    public:
        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;
        bool isOverlay() const override { return true; }

    private:
        int m_selection{ 0 };
        ui::HitMap m_hits;
        float m_elapsed{ 0.0f };
    };

    // Shown between levels. Popping it hands control back to PlayState, which
    // then generates the next level.
    class LevelCompleteState : public GameState
    {
    public:
        LevelCompleteState(RunSummary summary, int completionBonus);

        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;
        bool isOverlay() const override { return true; }

    private:
        RunSummary m_summary;
        int m_completionBonus{ 0 };
        float m_elapsed{ 0.0f };
    };

    class GameOverState : public GameState
    {
    public:
        explicit GameOverState(RunSummary summary);

        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;
        bool isOverlay() const override { return true; }

    private:
        RunSummary m_summary;
        int m_selection{ 0 };
        ui::HitMap m_hits;
        float m_elapsed{ 0.0f };
    };
}
