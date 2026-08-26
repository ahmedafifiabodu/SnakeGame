#include "Hud.h"

#include "Draw.h"
#include "Layout.h"
#include "../core/Glyphs.h"

#include <algorithm>

namespace neoncoil::ui
{
    namespace
    {
        constexpr Color kHudBackground = Color::Slate;

        std::wstring number(int value)
        {
            return std::to_wstring(value);
        }
    }

    void drawHud(Screen& screen, const HudModel& model)
    {
        screen.fillRect(0, 0, screen.width(), kHudHeight, glyph::Space, Color::Silver, kHudBackground);
        screen.horizontalLine(0, kHudHeight - 1, screen.width(), glyph::ThinH, Color::Navy, kHudBackground);

        // --- left: who is playing -------------------------------------------
        int x = 2;
        screen.put(x, 0, glyph::Block, model.snakeColour, kHudBackground);
        screen.put(x + 1, 0, glyph::Block, model.snakeColour, kHudBackground);
        x += 3;

        screen.text(x, 0, truncateTo(model.playerName, 14), Color::White, kHudBackground);
        screen.text(x, 1, truncateTo(model.snakeTypeName, 14), model.snakeColour, kHudBackground);

        // --- level and progress ---------------------------------------------
        const int progressX = 26;
        screen.text(progressX, 0, L"LEVEL " + number(model.level), Color::Gold, kHudBackground);
        screen.text(progressX + 10, 0,
            number(model.scoreThisLevel) + L" / " + number(model.levelTarget), Color::Silver, kHudBackground);

        const float progress = model.levelTarget > 0
            ? static_cast<float>(model.scoreThisLevel) / static_cast<float>(model.levelTarget)
            : 1.0f;
        progressBar(screen, progressX, 1, 28, progress, Color::Lime, Color::Navy, kHudBackground);

        // --- ability ---------------------------------------------------------
        const int abilityX = 60;
        const Color abilityColour = model.abilityActive ? Color::Gold
            : (model.abilityReady ? Color::Aqua : Color::Silver);

        std::wstring abilityLabel = truncateTo(model.abilityName, 16);
        if (model.shieldHeld)
            abilityLabel += L" ";

        screen.text(abilityX, 0, abilityLabel, abilityColour, kHudBackground);
        if (model.shieldHeld)
            screen.put(abilityX + static_cast<int>(abilityLabel.size()), 0, glyph::Shield, Color::Blue, kHudBackground);

        if (model.abilityActive)
        {
            progressBar(screen, abilityX, 1, 20, model.abilityActiveFraction, Color::Gold, Color::Navy, kHudBackground);
        }
        else if (model.abilityReady)
        {
            screen.text(abilityX, 1, L"READY - press SPACE", Color::Aqua, kHudBackground);
        }
        else
        {
            progressBar(screen, abilityX, 1, 20, model.abilityCharge, Color::Blue, Color::Navy, kHudBackground);
        }

        // --- right: score, length, combo -------------------------------------
        const std::wstring scoreText = L"SCORE " + number(model.score);
        screen.text(screen.width() - 2 - static_cast<int>(scoreText.size()), 0, scoreText, Color::White, kHudBackground);

        std::wstring detail = L"LEN " + number(model.length);
        if (model.comboMultiplier > 1)
            detail += L"   COMBO x" + number(model.comboMultiplier);

        screen.text(screen.width() - 2 - static_cast<int>(detail.size()), 1,
            detail, model.comboMultiplier > 1 ? Color::Gold : Color::Silver, kHudBackground);
    }

    void drawPlayFooter(Screen& screen, const std::wstring& abilityName, bool abilityReady)
    {
        screen.fillRect(0, kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        int x = 4;
        x += keyHint(screen, x, kFooterY, L"WASD/ARROWS", L"Move", Color::Black) + 3;

        const int hintStart = x;
        x += keyHint(screen, x, kFooterY, L"SPACE", abilityName, Color::Black) + 3;
        if (abilityReady)
            screen.put(hintStart - 1, kFooterY, glyph::TriRight, Color::Gold, Color::Black);

        x += keyHint(screen, x, kFooterY, L"P", L"Pause", Color::Black) + 3;
        keyHint(screen, x, kFooterY, L"ESC", L"Pause menu", Color::Black);
    }
}
