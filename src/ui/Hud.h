#pragma once

#include "../core/Colors.h"
#include "../core/Screen.h"

#include <string>

namespace neoncoil::ui
{
    // Everything the HUD shows, gathered by PlayState. Keeping it a plain struct
    // means the HUD can be drawn (and eyeballed) without a live game.
    struct HudModel
    {
        std::wstring playerName{ L"PLAYER" };
        std::wstring snakeTypeName{ L"VIPER" };
        Color snakeColour{ Color::Green };

        int score{ 0 };
        int level{ 1 };
        int levelTarget{ 100 };
        int scoreThisLevel{ 0 };

        int length{ 4 };
        int comboMultiplier{ 1 };

        std::wstring abilityName{ L"DASH" };
        float abilityCharge{ 1.0f };   // 0..1, 1 = ready
        bool abilityReady{ true };
        bool abilityActive{ false };
        float abilityActiveFraction{ 0.0f };
        bool shieldHeld{ false };

        std::wstring archetypeName{ L"Open Field" };
        unsigned long long runSeed{ 0 };
    };

    void drawHud(Screen& screen, const HudModel& model);

    // Footer key hints shown under the board during play.
    void drawPlayFooter(Screen& screen, const std::wstring& abilityName, bool abilityReady);
}
