#include "MenuState.h"

#include "PlayState.h"
#include "../core/Glyphs.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace neoncoil
{
    namespace
    {
        struct ColourOption
        {
            Color colour;
            const wchar_t* name;
        };

        constexpr std::array<ColourOption, 8> kColourOptions = { {
            { Color::Green,   L"EMERALD" },
            { Color::Aqua,    L"MINT" },
            { Color::Cyan,    L"AZURE" },
            { Color::Blue,    L"COBALT" },
            { Color::Magenta, L"ORCHID" },
            { Color::Coral,   L"CORAL" },
            { Color::Gold,    L"GOLD" },
            { Color::White,   L"BONE" },
        } };

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
        std::string portraitPathFor(const SnakeType& type)
        {
            std::string name;
            name.reserve(type.name.size());
            for (wchar_t c : type.name)
                name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            return "portraits/snake_" + name + ".png";
        }

        int colourIndexOf(Color colour)
        {
            for (std::size_t i = 0; i < kColourOptions.size(); ++i)
                if (kColourOptions[i].colour == colour)
                    return static_cast<int>(i);
            return 0;
        }

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            return ((value % count) + count) % count;
        }

        // Small stat readout: "SPEED  ####------".
        void statBar(Screen& screen, int x, int y, const wchar_t* label, float fraction, Color colour)
        {
            screen.text(x, y, label, Color::Silver, Color::Black);
            ui::progressBar(screen, x + 9, y, 14, fraction, colour, Color::Slate, Color::Black);
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
        {
            if (m_field == Field::Start)
            {
                if (context.profile.name.empty())
                    context.profile.name = L"PLAYER";

                return Transition::reset(std::make_unique<PlayState>(context.nextRunSeed()));
            }

            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) + 1, fieldCount));
        }

        return Transition::none();
    }

    void MenuState::render(AppContext& context)
    {
        context.screen.clear(Color::Black);

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
        ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Quit", Color::Black);
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

        if (focusedName && static_cast<int>(m_elapsed * 2.0f) % 2 == 0)
            screen.put(inner + 1 + static_cast<int>(profile.name.size()), kConfigY + 3,
                glyph::HalfLeft, Color::Gold, Color::Navy);

        // --- colour -----------------------------------------------------------
        const int colourY = kConfigY + 6;
        screen.text(inner, colourY, L"SNAKE COLOUR", focusedColour ? Color::Gold : Color::Silver, Color::Black);

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

        const std::wstring counter = std::to_wstring(profile.snakeTypeIndex + 1) + L" / " +
            std::to_wstring(snakeTypeCount());
        screen.textCenteredIn(inner, boxWidth, typeY + 2, counter, Color::Slate, Color::Black);

        // --- how to play ------------------------------------------------------
        const int tipsY = kConfigY + 17;
        screen.horizontalLine(inner, tipsY, boxWidth, glyph::ThinH, Color::Slate, Color::Black);
        screen.text(inner, tipsY + 1, L"Reach the level target to advance.", Color::Slate, Color::Black);
        screen.text(inner, tipsY + 2, L"SPACE fires your ability.  P pauses.", Color::Slate, Color::Black);

        // --- start ------------------------------------------------------------
        const int startY = kConfigY + 21;
        const Color buttonBackground = focusedStart ? profile.colour : Color::Slate;
        const Color buttonText = focusedStart ? Color::Black : Color::Silver;

        screen.fillRect(inner, startY, boxWidth, 3, glyph::Space, buttonText, buttonBackground);
        screen.textCenteredIn(inner, boxWidth, startY + 1, L"START  GAME", buttonText, buttonBackground);

        if (focusedStart)
        {
            screen.put(inner - 1, startY + 1, glyph::TriRight, Color::Gold, Color::Black);
            screen.put(inner + boxWidth, startY + 1, glyph::TriLeft, Color::Gold, Color::Black);
        }
    }

    void MenuState::renderPortrait(AppContext& context) const
    {
        Screen& screen = context.screen;
        const SnakeType& type = context.profile.type();

        const sf::Texture* portrait = screen.textures().get(portraitPathFor(type));
        if (portrait == nullptr)
            return;

        // The reserved column, in virtual pixels.
        const float columnX = static_cast<float>(kTypeX + 2 + kPortraitColumn) * Screen::kCellWidth;
        const float columnY = static_cast<float>(kTypeY + 2) * Screen::kCellHeight;
        const float columnW = static_cast<float>(kTypeW - 4 - kPortraitColumn) * Screen::kCellWidth;
        const float columnH = static_cast<float>(kTypeH - 5) * Screen::kCellHeight;

        // A soft wash of the snake's own colour behind the art, so each entry
        // reads as a different card rather than the same dark rectangle.
        screen.overlayGlowRect(columnX, columnY + columnH * 0.15f, columnW, columnH * 0.7f,
            type.accent, 14.0f, 0.45f);

        screen.spriteFitted(*portrait, columnX, columnY, columnW, columnH, Screen::SpriteLayer::Ui);
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

        // Live preview: the snake you picked, crawling, in the colour you picked.
        const int track = previewWidth + 12;
        const int offset = static_cast<int>(m_elapsed * 9.0f) % track;

        screen.fillRect(inner, y, previewWidth, 1, glyph::Space, Color::Silver, Color::Navy);
        for (int i = 0; i < 10; ++i)
        {
            const int x = inner + ((offset - i + track) % track) - 6;
            if (x < inner || x >= inner + previewWidth)
                continue;

            wchar_t body = type.bodyGlyph;
            if (type.altBodyGlyph != 0 && i % 2 == 0)
                body = type.altBodyGlyph;

            screen.put(x, y, i == 0 ? type.headGlyph : body,
                i == 0 ? Color::White : profile.colour, Color::Navy);
        }
        y += 2;

        screen.text(inner, y, ui::truncateTo(type.tagline, previewWidth), Color::Silver, Color::Black);
        y += 2;

        for (const std::wstring& note : type.notes)
        {
            screen.put(inner, y, glyph::Bullet, type.accent, Color::Black);
            screen.text(inner + 2, y, ui::truncateTo(note, previewWidth - 2), Color::Silver, Color::Black);
            ++y;
        }

        // --- ability ----------------------------------------------------------
        screen.horizontalLine(inner, y, previewWidth, glyph::ThinH, Color::Slate, Color::Black);
        ++y;

        screen.put(inner, y, glyph::Bolt, Color::Gold, Color::Black);
        screen.text(inner + 2, y, type.ability.name, Color::Gold, Color::Black);
        ++y;

        for (const std::wstring& line : ui::wrapText(type.ability.summary, previewWidth))
        {
            screen.text(inner, y, line, Color::Silver, Color::Black);
            ++y;
        }

        std::wstring timing = L"Cooldown " + std::to_wstring(static_cast<int>(type.ability.cooldownSeconds)) + L"s";
        if (type.ability.durationSeconds > 0.0f)
            timing += L"   Duration " + std::to_wstring(static_cast<int>(type.ability.durationSeconds)) + L"s";
        screen.text(inner, y, timing, Color::Slate, Color::Black);
        ++y;

        // --- stats ------------------------------------------------------------
        statBar(screen, inner, y++, L"SPEED", (type.speedMultiplier - 0.8f) / 0.6f, type.accent);
        statBar(screen, inner, y++, L"GROWTH", static_cast<float>(type.growthPerFood) / 3.0f, type.accent);
        statBar(screen, inner, y++, L"SCORING", (type.scoreMultiplier - 0.8f) / 0.6f, type.accent);
    }
}
