#include "OverlayStates.h"

#include "MenuState.h"
#include "PlayState.h"
#include "../core/Glyphs.h"
#include "../game/Progression.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"

#include <algorithm>
#include <string>
#include <vector>

namespace neoncoil
{
    namespace
    {
        // Overlays sit over the live board, so they get their own opaque panel
        // rather than trying to tint what is underneath.
        struct PanelRect
        {
            int x{ 0 };
            int y{ 0 };
            int w{ 0 };
            int h{ 0 };
        };

        PanelRect centredPanel(const Screen& screen, int width, int height)
        {
            return PanelRect{ (screen.width() - width) / 2, (screen.height() - height) / 2, width, height };
        }

        void drawMenuOptions(Screen& screen, const PanelRect& panel, int firstRow,
            const std::vector<std::wstring>& options, int selection, Color accent)
        {
            // A fixed, centred button width. Stretching the highlight across the
            // whole panel made the selected row read as a divider rather than a
            // button.
            constexpr int kButtonWidth = 26;
            const int buttonX = panel.x + (panel.w - kButtonWidth) / 2;

            for (std::size_t i = 0; i < options.size(); ++i)
            {
                const bool isSelected = static_cast<int>(i) == selection;
                const int y = firstRow + static_cast<int>(i) * 2;   // a blank row between buttons

                const Color background = isSelected ? accent : Color::Slate.scaled(0.5f);
                const Color foreground = isSelected ? Color::Black : Color::Silver;

                screen.fillRect(buttonX, y, kButtonWidth, 1, glyph::Space, foreground, background);
                screen.textCenteredIn(buttonX, kButtonWidth, y, options[i], foreground, background);

                if (isSelected)
                {
                    screen.put(buttonX - 2, y, glyph::TriRight, accent, Color::Transparent);
                    screen.put(buttonX + kButtonWidth + 1, y, glyph::TriLeft, accent, Color::Transparent);
                }
            }
        }

        int cycleSelection(int selection, int delta, int count)
        {
            if (count <= 0)
                return 0;
            return ((selection + delta) % count + count) % count;
        }

        void statLine(Screen& screen, int x, int y, const std::wstring& label, const std::wstring& value, Color valueColour)
        {
            screen.text(x, y, label, Color::Slate, Color::Black);
            screen.text(x + 22, y, value, valueColour, Color::Black);
        }

        // Overlays replace the play footer: leaving "SPACE fires your ability"
        // on screen while the game is paused or over is just wrong.
        void replaceFooter(Screen& screen, std::wstring_view hint)
        {
            screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);
            screen.textCentered(ui::kFooterY, hint, Color::Slate, Color::Black);
        }
    }

    // ---------------------------------------------------------------- pause --

    void PauseState::onEnter(AppContext& context)
    {
        (void)context;
        m_selection = 0;
        m_elapsed = 0.0f;
    }

    Transition PauseState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        const Input& input = context.input;

        if (input.pressed(Action::Up))
            m_selection = cycleSelection(m_selection, -1, 3);
        if (input.pressed(Action::Down))
            m_selection = cycleSelection(m_selection, 1, 3);

        // Esc and P both resume: whichever key opened the menu also closes it.
        if (input.pressed(Action::Back) || input.pressed(Action::Pause))
            return Transition::pop();

        if (input.pressed(Action::Confirm))
        {
            switch (m_selection)
            {
            case 0: return Transition::pop();
            case 1: return Transition::reset(std::make_unique<MenuState>());
            default: return Transition::quit();
            }
        }

        return Transition::none();
    }

    void PauseState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        const PanelRect panel = centredPanel(screen, 54, 22);

        screen.panel(panel.x, panel.y, panel.w, panel.h, Color::Aqua, Color::Black);

        ui::drawBannerCentered(screen, panel.y + 3, L"PAUSED", Color::Aqua, Color::Black, 1, 1, 1);

        screen.textCentered(panel.y + 10, context.profile.name + L"   -   " + context.profile.type().name,
            context.profile.colour, Color::Black);

        drawMenuOptions(screen, panel, panel.y + 12,
            { L"RESUME", L"BACK TO MENU", L"QUIT GAME" }, m_selection, Color::Aqua);

        screen.textCentered(panel.y + panel.h - 2, L"ENTER SELECT    ESC RESUME", Color::Slate, Color::Transparent);
        replaceFooter(screen, L"Game paused");
    }

    // ------------------------------------------------------- level complete --

    LevelCompleteState::LevelCompleteState(RunSummary summary, int completionBonus)
        : m_summary(std::move(summary))
        , m_completionBonus(completionBonus)
    {
    }

    void LevelCompleteState::onEnter(AppContext& context)
    {
        (void)context;
        m_elapsed = 0.0f;
    }

    Transition LevelCompleteState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        // Short lockout so a key held at the moment of completion cannot skip
        // the screen before it is readable.
        if (m_elapsed < 0.45f)
            return Transition::none();

        if (context.input.pressed(Action::Confirm) || context.input.pressed(Action::Ability))
            return Transition::pop();

        return Transition::none();
    }

    void LevelCompleteState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        const PanelRect panel = centredPanel(screen, 62, 24);

        screen.panel(panel.x, panel.y, panel.w, panel.h, Color::Lime, Color::Black);

        ui::drawBannerCentered(screen, panel.y + 2, L"LEVEL", Color::Lime, Color::Black, 1, 1, 1);
        ui::drawBannerCentered(screen, panel.y + 8, L"CLEAR", Color::Lime, Color::Black, 1, 1, 1);

        const int x = panel.x + 8;
        int y = panel.y + 15;

        statLine(screen, x, y++, L"LEVEL CLEARED", std::to_wstring(m_summary.level), Color::White);
        statLine(screen, x, y++, L"CLEAR BONUS", L"+" + std::to_wstring(m_completionBonus), Color::Gold);
        statLine(screen, x, y++, L"TOTAL SCORE", std::to_wstring(m_summary.score), Color::White);
        statLine(screen, x, y++, L"NEXT TARGET", std::to_wstring(planFor(m_summary.level + 1).targetScore), Color::Aqua);

        const bool blink = static_cast<int>(m_elapsed * 2.0f) % 2 == 0;
        screen.textCentered(panel.y + panel.h - 3,
            blink ? L"PRESS ENTER FOR LEVEL " + std::to_wstring(m_summary.level + 1) : L"",
            Color::Gold, Color::Black);

        replaceFooter(screen, L"Level complete");
    }

    // ------------------------------------------------------------ game over --

    GameOverState::GameOverState(RunSummary summary)
        : m_summary(std::move(summary))
    {
    }

    void GameOverState::onEnter(AppContext& context)
    {
        context.input.flush();
        m_selection = 0;
        m_elapsed = 0.0f;
    }

    Transition GameOverState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        if (m_elapsed < 0.4f)
            return Transition::none();

        const Input& input = context.input;

        if (input.pressed(Action::Up))
            m_selection = cycleSelection(m_selection, -1, 3);
        if (input.pressed(Action::Down))
            m_selection = cycleSelection(m_selection, 1, 3);

        if (input.pressed(Action::Restart))
            return Transition::reset(std::make_unique<PlayState>(context.nextRunSeed()));

        if (input.pressed(Action::Back))
            return Transition::reset(std::make_unique<MenuState>());

        if (input.pressed(Action::Confirm))
        {
            switch (m_selection)
            {
            case 0: return Transition::reset(std::make_unique<PlayState>(context.nextRunSeed()));
            case 1: return Transition::reset(std::make_unique<MenuState>());
            default: return Transition::quit();
            }
        }

        return Transition::none();
    }

    void GameOverState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        const PanelRect panel = centredPanel(screen, 68, 30);

        screen.panel(panel.x, panel.y, panel.w, panel.h, Color::Red, Color::Black);

        // One line rather than two: it fits this panel width and buys back the
        // six rows the stats and buttons need.
        ui::drawBannerCentered(screen, panel.y + 2, L"GAME OVER", Color::Red, Color::Transparent, 1, 1, 1);

        screen.textCentered(panel.y + 10, m_summary.causeOfDeath, Color::Coral, Color::Transparent);

        const int x = panel.x + 10;
        int y = panel.y + 13;

        statLine(screen, x, y++, L"FINAL SCORE", std::to_wstring(m_summary.score), Color::Gold);
        statLine(screen, x, y++, L"REACHED LEVEL", std::to_wstring(m_summary.level), Color::White);
        statLine(screen, x, y++, L"FOOD EATEN", std::to_wstring(m_summary.foodEaten), Color::White);
        statLine(screen, x, y++, L"LONGEST SNAKE", std::to_wstring(m_summary.longestSnake), Color::White);
        statLine(screen, x, y++, L"ABILITIES USED", std::to_wstring(m_summary.abilitiesUsed), Color::White);
        statLine(screen, x, y++, L"RUN SEED", std::to_wstring(m_summary.runSeed), Color::Slate);

        drawMenuOptions(screen, panel, panel.y + 21,
            { L"RETRY", L"MAIN MENU", L"QUIT" }, m_selection, Color::Red);

        screen.textCentered(panel.y + panel.h - 3, L"R RETRY    ESC MENU", Color::Slate, Color::Transparent);
        replaceFooter(screen, L"UP / DOWN to choose    ENTER to confirm");
    }
}
