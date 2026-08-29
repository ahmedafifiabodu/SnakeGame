#include "MenuState.h"

#include "MultiplayerMenuState.h"
#include "OptionsState.h"
#include "PlayState.h"
#include "../core/Glyphs.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"
#include "../ui/Palette.h"
#include "../ui/SnakeCard.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace neoncoil
{
    namespace
    {
        // The palette moved to ui/Palette.h when multiplayer arrived: the lobby
        // has to draw other players' colours, and the wire protocol carries the
        // index rather than an RGBA triple, so there can only be one table.
        constexpr const std::array<ui::ColourOption, 8>& kColourOptions = ui::kPlayerColours;

        constexpr int kMaxNameLength = 12;

        constexpr int kConfigX = 8;
        constexpr int kConfigY = 11;
        constexpr int kConfigW = 48;
        constexpr int kConfigH = 25;

        constexpr int kTypeX = 62;
        constexpr int kTypeY = 11;
        constexpr int kTypeW = 50;
        constexpr int kTypeH = 25;

        // Cells of the FIELD REPORT panel given over to text; the rest is the
        // reserved portrait column on the right.
        constexpr int kPortraitColumn = 30;

        // Maps a roster entry to its illustration. Derived from the type name so
        // adding a snake needs no extra table -- drop in assets/portraits/
        // snake_<lowercase name>.png and it is picked up.
        int colourIndexOf(Color colour)
        {
            return ui::playerColourIndex(colour);
        }

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            return ((value % count) + count) % count;
        }

    }

    void MenuState::onEnter(AppContext& context)
    {
        context.input.flush();
        m_field = Field::Name;
        m_elapsed = 0.0f;
    }

    void MenuState::handleNameEntry(AppContext& context)
    {
        PlayerProfile& profile = context.profile;

        // First keystroke replaces the placeholder rather than appending to it.
        for (int i = 0; i < context.input.backspaceCount(); ++i)
        {
            if (!m_nameTouched)
            {
                profile.name.clear();
                m_nameTouched = true;
                break;
            }
            if (!profile.name.empty())
                profile.name.pop_back();
        }

        for (wchar_t character : context.input.typedText())
        {
            if (!m_nameTouched)
            {
                profile.name.clear();
                m_nameTouched = true;
            }

            if (static_cast<int>(profile.name.size()) < kMaxNameLength)
                profile.name.push_back(character);
        }
    }

    void MenuState::adjust(AppContext& context, int delta)
    {
        PlayerProfile& profile = context.profile;

        if (m_field == Field::Colour)
        {
            const int index = wrapIndex(colourIndexOf(profile.colour) + delta, static_cast<int>(kColourOptions.size()));
            profile.colour = kColourOptions[static_cast<std::size_t>(index)].colour;
        }
        else if (m_field == Field::Type)
        {
            profile.snakeTypeIndex = wrapIndex(profile.snakeTypeIndex + delta, snakeTypeCount());
        }
    }

    Transition MenuState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        const Input& input = context.input;
        const bool editingName = m_field == Field::Name;

        // While the name field is focused, only the arrow keys navigate --
        // otherwise typing "WASD" into your name would also move the cursor.
        const bool up = editingName ? input.pressed(Action::NavUp) : input.pressed(Action::Up);
        const bool down = editingName ? input.pressed(Action::NavDown) : input.pressed(Action::Down);
        const bool left = editingName ? input.pressed(Action::NavLeft) : input.pressed(Action::Left);
        const bool right = editingName ? input.pressed(Action::NavRight) : input.pressed(Action::Right);

        if (editingName)
            handleNameEntry(context);

        const int fieldCount = static_cast<int>(Field::Count);
        if (up)
            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) - 1, fieldCount));
        if (down)
            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) + 1, fieldCount));

        if (left)
            adjust(context, -1);
        if (right)
            adjust(context, 1);

        if (input.pressed(Action::Back))
            return Transition::quit();

        if (input.pressed(Action::Confirm))
            return confirmField(context);

        return handleMouse(context);
    }

    Transition MenuState::confirmField(AppContext& context)
    {
        const int fieldCount = static_cast<int>(Field::Count);

        // Options needs no name and starts nothing, so it is checked first and
        // on its own.
        if (m_field == Field::Options)
            return Transition::push(std::make_unique<OptionsState>());

        if (m_field == Field::Start || m_field == Field::Multiplayer)
        {
            if (context.profile.name.empty())
                context.profile.name = L"PLAYER";

            // Single player replaces the menu -- there is nothing to come back
            // to. Multiplayer is pushed, so leaving a session returns here with
            // the player's name and snake still set.
            if (m_field == Field::Start)
                return Transition::reset(std::make_unique<PlayState>(context.nextRunSeed()));

            return Transition::push(std::make_unique<MultiplayerMenuState>());
        }

        m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) + 1, fieldCount));
        return Transition::none();
    }

    Transition MenuState::handleMouse(AppContext& context)
    {
        const Input& input = context.input;

        // Hover follows the pointer, but only while it is actually moving --
        // otherwise a mouse left sitting over a field would keep stealing focus
        // back from the arrow keys.
        if (input.mouseMoved())
        {
            const int hovered = m_hits.hovered(input);
            if (hovered >= 0 && hovered < static_cast<int>(Field::Count))
                m_field = static_cast<Field>(hovered);
        }

        const int clicked = m_hits.clicked(input);
        if (clicked == ui::HitMap::kNone)
            return Transition::none();

        if (clicked >= HitColourSwatch && clicked < HitColourSwatch + ui::playerColourCount())
        {
            context.profile.colour = ui::playerColourAt(clicked - HitColourSwatch);
            m_field = Field::Colour;
            return Transition::none();
        }

        if (clicked == HitTypePrev || clicked == HitTypeNext)
        {
            m_field = Field::Type;
            adjust(context, clicked == HitTypeNext ? 1 : -1);
            return Transition::none();
        }

        if (clicked >= 0 && clicked < static_cast<int>(Field::Count))
        {
            m_field = static_cast<Field>(clicked);

            // Clicking a button presses it; clicking a field only focuses it,
            // because there is nothing else a click on a text box should mean.
            if (m_field == Field::Start || m_field == Field::Multiplayer ||
                m_field == Field::Options)
            {
                return confirmField(context);
            }
        }

        return Transition::none();
    }

    void MenuState::render(AppContext& context)
    {
        context.screen.clear(Color::Black);
        m_hits.clear();

        // Background plate, dimmed hard so it stays behind the UI rather than
        // competing with it.
        if (const sf::Texture* plate = context.screen.textures().get("ui/bg_menu.png"))
        {
            context.screen.sprite(*plate, 0.0f, 0.0f, ui::kCanvasWidth, ui::kCanvasHeight,
                Screen::SpriteLayer::Background, Color::White.scaled(0.32f));
        }

        renderTitle(context);
        renderConfigPanel(context);
        renderTypePanel(context);

        Screen& screen = context.screen;
        screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        int x = 8;
        x += ui::keyHint(screen, x, ui::kFooterY, L"UP/DOWN", L"Field", Color::Black) + 4;
        x += ui::keyHint(screen, x, ui::kFooterY, L"LEFT/RIGHT", L"Change", Color::Black) + 4;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ENTER", L"Next / Start", Color::Black) + 4;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Quit", Color::Black) + 4;
        ui::keyHint(screen, x, ui::kFooterY, L"MOUSE", L"Click anything", Color::Black);
    }

    void MenuState::renderTitle(AppContext& context) const
    {
        Screen& screen = context.screen;

        // The neon wordmark is the brand, so the store logo and the in-game
        // title are the same image. Falls back to the block font if the asset is
        // missing, which keeps a bare build playable.
        if (const sf::Texture* logo = screen.textures().getEmissive("ui/logo.png"))
        {
            // Additive: the source is emissive neon on black, so adding it lets
            // the glow bloom over the background instead of punching a dark box.
            screen.spriteFitted(*logo, ui::kCanvasWidth * 0.5f - 460.0f, 24.0f, 920.0f, 150.0f,
                Screen::SpriteLayer::Ui, Color::White, true);
        }
        else
        {
            ui::drawBannerCentered(screen, 2, L"NEON COIL", context.profile.colour, Color::Black, 2, 1, 1);
        }

        screen.textCentered(8, L"P I C K   Y O U R   S E R P E N T", Color::Slate, Color::Transparent);

        // Two snakes crawling towards the centre, purely for flavour.
        const int drift = static_cast<int>(m_elapsed * 6.0f) % 18;
        ui::drawSnakeFlourish(screen, 12 + drift, 9, 10, Color::Slate, Color::Transparent);
        ui::drawSnakeFlourish(screen, screen.width() - 30 - drift, 9, 10, Color::Slate, Color::Transparent);
    }

    void MenuState::renderConfigPanel(AppContext& context) const
    {
        Screen& screen = context.screen;
        const PlayerProfile& profile = context.profile;

        const bool focusedName = m_field == Field::Name;
        const bool focusedColour = m_field == Field::Colour;
        const bool focusedType = m_field == Field::Type;
        const bool focusedStart = m_field == Field::Start;
        const bool focusedMultiplayer = m_field == Field::Multiplayer;

        screen.panel(kConfigX, kConfigY, kConfigW, kConfigH, Color::Slate, Color::Black);
        screen.text(kConfigX + 3, kConfigY, L" YOUR SNAKE ", Color::Gold, Color::Black);

        const int inner = kConfigX + 3;

        // --- name -------------------------------------------------------------
        screen.text(inner, kConfigY + 2, L"PLAYER NAME", focusedName ? Color::Gold : Color::Silver, Color::Black);

        const int boxWidth = kConfigW - 8;
        screen.fillRect(inner, kConfigY + 3, boxWidth, 1, glyph::Space, Color::White,
            focusedName ? Color::Navy : Color::Black);
        screen.text(inner + 1, kConfigY + 3, ui::truncateTo(profile.name, boxWidth - 3),
            Color::White, focusedName ? Color::Navy : Color::Black);

        // The label row is included so the whole line is a target, not just the
        // one-cell-tall box.
        m_hits.add(static_cast<int>(Field::Name), inner, kConfigY + 2, boxWidth, 2);

        if (focusedName && static_cast<int>(m_elapsed * 2.0f) % 2 == 0)
            screen.put(inner + 1 + static_cast<int>(profile.name.size()), kConfigY + 3,
                glyph::HalfLeft, Color::Gold, Color::Navy);

        // --- colour -----------------------------------------------------------
        const int colourY = kConfigY + 6;
        screen.text(inner, colourY, L"SNAKE COLOUR", focusedColour ? Color::Gold : Color::Silver, Color::Black);
        m_hits.add(static_cast<int>(Field::Colour), inner, colourY, boxWidth, 5);

        // Swatches are 3 cells wide on a 5-cell pitch, which leaves the gap the
        // selection carets need without running past the panel.
        constexpr int kSwatchWidth = 3;
        constexpr int kSwatchPitch = 5;

        const int selected = colourIndexOf(profile.colour);
        for (std::size_t i = 0; i < kColourOptions.size(); ++i)
        {
            const int x = inner + 1 + static_cast<int>(i) * kSwatchPitch;
            const bool isSelected = static_cast<int>(i) == selected;

            screen.fillRect(x, colourY + 1, kSwatchWidth, 2, glyph::Block, kColourOptions[i].colour, Color::Black);

            // A swatch is its own target: clicking a colour picks that colour
            // rather than merely focusing the row and making you arrow to it.
            m_hits.add(HitColourSwatch + static_cast<int>(i), x, colourY + 1, kSwatchWidth, 2);

            if (isSelected)
            {
                screen.put(x - 1, colourY + 1, glyph::TriRight, Color::Gold, Color::Black);
                screen.put(x + kSwatchWidth, colourY + 1, glyph::TriLeft, Color::Gold, Color::Black);
                screen.horizontalLine(x, colourY + 3, kSwatchWidth, glyph::BoxH, Color::Gold, Color::Black);
            }
        }
        screen.text(inner, colourY + 4, ui::padTo(kColourOptions[static_cast<std::size_t>(selected)].name, 12),
            profile.colour, Color::Black);

        // --- type -------------------------------------------------------------
        const int typeY = kConfigY + 13;
        const SnakeType& type = profile.type();

        screen.text(inner, typeY, L"SNAKE TYPE", focusedType ? Color::Gold : Color::Silver, Color::Black);

        screen.put(inner, typeY + 1, glyph::TriLeft, focusedType ? Color::Gold : Color::Slate, Color::Black);
        screen.put(inner + boxWidth - 1, typeY + 1, glyph::TriRight, focusedType ? Color::Gold : Color::Slate, Color::Black);
        screen.textCenteredIn(inner, boxWidth, typeY + 1, type.name, type.accent, Color::Black);

        // Row first, then the arrows on top of it: clicking the middle focuses
        // the field, clicking an arrow steps the roster.
        m_hits.add(static_cast<int>(Field::Type), inner, typeY, boxWidth, 3);
        m_hits.add(HitTypePrev, inner - 1, typeY + 1, 3, 1);
        m_hits.add(HitTypeNext, inner + boxWidth - 2, typeY + 1, 3, 1);

        const std::wstring counter = std::to_wstring(profile.snakeTypeIndex + 1) + L" / " +
            std::to_wstring(snakeTypeCount());
        screen.textCenteredIn(inner, boxWidth, typeY + 2, counter, Color::Slate, Color::Black);

        // --- how to play ------------------------------------------------------
        const int tipsY = kConfigY + 16;
        screen.horizontalLine(inner, tipsY, boxWidth, glyph::ThinH, Color::Slate, Color::Black);
        screen.text(inner, tipsY + 1, L"Reach the level target to advance.", Color::Slate, Color::Black);
        screen.text(inner, tipsY + 2, L"SPACE fires your ability.  P pauses.", Color::Slate, Color::Black);

        // --- start / multiplayer ----------------------------------------------
        // Single player sits above and is focused first, because it is the mode
        // that needs no connection, no session and no account -- the game has to
        // be playable the moment it is installed.
        const auto button = [&](int y, Field field, bool focused, const std::wstring& label,
            Color background, const std::wstring& note)
        {
            const Color fill = focused ? background : Color::Slate;
            const Color text = focused ? Color::Black : Color::Silver;

            screen.fillRect(inner, y, boxWidth, 1, glyph::Space, text, fill);
            screen.textCenteredIn(inner, boxWidth, y, label, text, fill);
            m_hits.add(static_cast<int>(field), inner, y, boxWidth, 1);

            if (focused)
            {
                screen.put(inner - 1, y, glyph::TriRight, Color::Gold, Color::Black);
                screen.put(inner + boxWidth, y, glyph::TriLeft, Color::Gold, Color::Black);
            }

            if (!note.empty())
                screen.textCenteredIn(inner, boxWidth, y + 1, note, Color::Slate, Color::Black);
        };

        // Rows 19..23 of a 25-row panel. OPTIONS goes last and without a note:
        // it is the one button whose name already says everything it does, and
        // dropping its note is what makes three of them fit above the border.
        button(kConfigY + 19, Field::Start, focusedStart, L"START  GAME", profile.colour,
            L"offline, on your own");
        button(kConfigY + 21, Field::Multiplayer, focusedMultiplayer, L"MULTIPLAYER", Color::Aqua,
            L"two to four players online");
        button(kConfigY + 23, Field::Options, m_field == Field::Options, L"OPTIONS",
            Color::Gold, L"");
    }

    void MenuState::renderPortrait(AppContext& context) const
    {
        Screen& screen = context.screen;
        const SnakeType& type = context.profile.type();

        ui::drawSnakePortrait(screen, kTypeX + 2 + kPortraitColumn, kTypeY + 2,
            kTypeW - 4 - kPortraitColumn, kTypeH - 5, type);
    }

    void MenuState::renderTypePanel(AppContext& context) const
    {
        Screen& screen = context.screen;
        const PlayerProfile& profile = context.profile;
        const SnakeType& type = profile.type();

        screen.panel(kTypeX, kTypeY, kTypeW, kTypeH, Color::Slate, Color::Black);
        screen.text(kTypeX + 3, kTypeY, L" FIELD REPORT ", Color::Gold, Color::Black);

        const int inner = kTypeX + 3;
        int y = kTypeY + 2;

        // The roster illustration owns a reserved column on the right; every
        // text row below is narrowed to match so nothing ever runs under it.
        const int previewWidth = kPortraitColumn - 2;

        renderPortrait(context);

        // Plain text rather than the block font: with the portrait column
        // reserved there is no longer room for the longer names, and switching
        // per name made the panel look different for every snake.
        screen.textCenteredIn(inner, previewWidth, y + 2, type.name, type.accent, Color::Transparent);
        screen.horizontalLine(inner, y + 3, previewWidth, glyph::BoxH, type.accent, Color::Transparent);
        y += 6;

        // The rest of the panel is the shared field report, so the lobby and this
        // screen cannot end up describing the same snake differently.
        ui::drawSnakeReport(screen, inner, y, previewWidth, kTypeY + kTypeH - y - 1,
            type, profile.colour, m_elapsed);
    }
}
