#include "OptionsState.h"

#include "../core/Glyphs.h"
#include "../core/Settings.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        constexpr int kPanelX = 30;
        constexpr int kPanelY = 11;
        constexpr int kPanelW = 60;
        constexpr int kPanelH = 18;

        constexpr int kVolumeStep = 5;

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            return ((value % count) + count) % count;
        }

        std::wstring onOff(bool value)
        {
            return value ? L"ON" : L"OFF";
        }

        // "70  ##########------" -- the number first, because that is what a
        // player reads, with the bar as the shape of it.
        std::wstring volumeText(int percent)
        {
            std::wstring text = std::to_wstring(percent);
            while (text.size() < 3)
                text.insert(text.begin(), L' ');
            return text + L"%";
        }
    }

    bool OptionsState::rowEnabled(Row row)
    {
        // Nothing reads the volumes yet: this build has no audio. Saying so is
        // better than three sliders that move and change nothing.
        switch (row)
        {
        case Row::MasterVolume:
        case Row::MusicVolume:
        case Row::EffectsVolume:
            return false;
        default:
            return true;
        }
    }

    const wchar_t* OptionsState::rowLabel(Row row)
    {
        switch (row)
        {
        case Row::DisplayMode:   return L"WINDOW MODE";
        case Row::VerticalSync:  return L"VERTICAL SYNC";
        case Row::MasterVolume:  return L"MASTER VOLUME";
        case Row::MusicVolume:   return L"MUSIC";
        case Row::EffectsVolume: return L"EFFECTS";
        case Row::ScreenShake:   return L"SCREEN SHAKE";
        case Row::Bloom:         return L"BLOOM";
        case Row::ShowPing:      return L"SHOW PING";
        case Row::Back:          return L"BACK";
        case Row::Count:         break;
        }
        return L"";
    }

    const wchar_t* OptionsState::rowHint(Row row)
    {
        switch (row)
        {
        case Row::DisplayMode:
            return L"Borderless alt-tabs instantly and leaves other screens alone.";
        case Row::VerticalSync:
            return L"On removes tearing. Off lowers input delay by a frame or so.";
        case Row::MasterVolume:
        case Row::MusicVolume:
        case Row::EffectsVolume:
            return L"No audio in this build yet -- kept so your choice survives.";
        case Row::ScreenShake:
            return L"Turn off if the impacts are uncomfortable.";
        case Row::Bloom:
            return L"The glow around snakes and food. Off is cheaper to draw.";
        case Row::ShowPing:
            return L"Latency next to every name during an online match.";
        case Row::Back:
        case Row::Count:
            break;
        }
        return L"";
    }

    std::wstring OptionsState::rowValue(const AppContext& context, Row row) const
    {
        const Settings& settings = Settings::instance();

        switch (row)
        {
        case Row::DisplayMode:   return displayModeName(context.screen.displayMode());
        case Row::VerticalSync:  return onOff(settings.vsync);
        case Row::MasterVolume:  return volumeText(settings.masterVolume);
        case Row::MusicVolume:   return volumeText(settings.musicVolume);
        case Row::EffectsVolume: return volumeText(settings.effectsVolume);
        case Row::ScreenShake:   return onOff(settings.screenShake);
        case Row::Bloom:         return onOff(settings.bloom);
        case Row::ShowPing:      return onOff(settings.showPing);
        case Row::Back:
        case Row::Count:
            break;
        }
        return {};
    }

    void OptionsState::onEnter(AppContext& context)
    {
        context.input.flush();
        m_row = Row::DisplayMode;
        m_elapsed = 0.0f;
        m_message.clear();
    }

    void OptionsState::adjust(AppContext& context, int delta)
    {
        if (!rowEnabled(m_row))
        {
            m_message = L"There is no sound in this build yet.";
            return;
        }

        Settings& settings = Settings::instance();

        switch (m_row)
        {
        case Row::DisplayMode:
        {
            const int next = wrapIndex(static_cast<int>(context.screen.displayMode()) + delta,
                static_cast<int>(DisplayMode::Count));
            const DisplayMode mode = static_cast<DisplayMode>(next);

            // Applied before it is stored, so what gets written is what the
            // player is actually looking at.
            context.screen.setDisplayMode(mode);
            settings.displayMode = mode;
            break;
        }

        case Row::VerticalSync:
            settings.vsync = !settings.vsync;
            context.screen.setVerticalSync(settings.vsync);
            break;

        case Row::MasterVolume:
            settings.masterVolume = std::clamp(settings.masterVolume + delta * kVolumeStep, 0, 100);
            break;
        case Row::MusicVolume:
            settings.musicVolume = std::clamp(settings.musicVolume + delta * kVolumeStep, 0, 100);
            break;
        case Row::EffectsVolume:
            settings.effectsVolume = std::clamp(settings.effectsVolume + delta * kVolumeStep, 0, 100);
            break;

        case Row::ScreenShake:  settings.screenShake = !settings.screenShake; break;
        case Row::Bloom:        settings.bloom = !settings.bloom; break;
        case Row::ShowPing:     settings.showPing = !settings.showPing; break;

        case Row::Back:
        case Row::Count:
            return;
        }

        // Written on every change rather than on the way out: a player who
        // alt-F4s from a borderless window they just chose should still have it
        // next time.
        Settings::save();
        m_message.clear();
    }

    Transition OptionsState::activate(AppContext& context)
    {
        if (m_row == Row::Back)
            return Transition::pop();

        // Enter on a value row cycles it forward, which is what a player who
        // has not noticed the arrows will try.
        adjust(context, 1);
        return Transition::none();
    }

    Transition OptionsState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        const Input& input = context.input;
        const int count = static_cast<int>(Row::Count);

        if (input.pressed(Action::Up))
            m_row = static_cast<Row>(wrapIndex(static_cast<int>(m_row) - 1, count));
        if (input.pressed(Action::Down))
            m_row = static_cast<Row>(wrapIndex(static_cast<int>(m_row) + 1, count));

        if (input.pressed(Action::Left))
            adjust(context, -1);
        if (input.pressed(Action::Right))
            adjust(context, 1);

        if (input.pressed(Action::Back))
            return Transition::pop();

        if (input.pressed(Action::Confirm))
            return activate(context);

        return handleMouse(context);
    }

    Transition OptionsState::handleMouse(AppContext& context)
    {
        const Input& input = context.input;

        const auto focusRow = [&](int id)
        {
            if (id >= 0 && id < static_cast<int>(Row::Count))
                m_row = static_cast<Row>(id);
        };

        if (input.mouseMoved())
        {
            if (const int hovered = m_hits.hovered(input); hovered != ui::HitMap::kNone)
            {
                if (hovered >= HitNext)
                    focusRow(hovered - HitNext);
                else if (hovered >= HitPrev)
                    focusRow(hovered - HitPrev);
                else
                    focusRow(hovered);
            }
        }

        const int clicked = m_hits.clicked(input);
        if (clicked == ui::HitMap::kNone)
            return Transition::none();

        if (clicked >= HitNext)
        {
            focusRow(clicked - HitNext);
            adjust(context, 1);
            return Transition::none();
        }

        if (clicked >= HitPrev)
        {
            focusRow(clicked - HitPrev);
            adjust(context, -1);
            return Transition::none();
        }

        focusRow(clicked);
        return activate(context);
    }

    // ------------------------------------------------------------------ render --

    void OptionsState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        screen.clear(Color::Black);
        m_hits.clear();

        if (const sf::Texture* plate = screen.textures().get("ui/bg_menu.png"))
        {
            screen.sprite(*plate, 0.0f, 0.0f, ui::kCanvasWidth, ui::kCanvasHeight,
                Screen::SpriteLayer::Background, Color::White.scaled(0.24f));
        }

        ui::drawBannerCentered(screen, 3, L"OPTIONS", Color::Aqua, Color::Transparent, 2, 1, 1);
        screen.textCentered(9, L"S E T   I T   A N D   F O R G E T   I T", Color::Slate, Color::Transparent);

        screen.panel(kPanelX, kPanelY, kPanelW, kPanelH, Color::Gold, Color::Black);
        screen.text(kPanelX + 3, kPanelY, L" SETTINGS ", Color::Gold, Color::Black);

        const int inner = kPanelX + 3;
        const int width = kPanelW - 6;
        const int valueX = inner + 22;
        const int valueWidth = width - 22;

        int y = kPanelY + 2;

        const auto section = [&](const wchar_t* title)
        {
            screen.text(inner, y, title, Color::Slate, Color::Black);
            screen.horizontalLine(inner + static_cast<int>(std::wstring(title).size()) + 1, y,
                width - static_cast<int>(std::wstring(title).size()) - 1,
                glyph::ThinH, Color::Slate.scaled(0.7f), Color::Black);
            ++y;
        };

        const auto row = [&](Row which)
        {
            const bool focused = m_row == which;
            const bool enabled = rowEnabled(which);
            const int index = static_cast<int>(which);

            const Color label = focused ? Color::Gold : (enabled ? Color::Silver : Color::Slate);
            screen.text(inner, y, rowLabel(which), label, Color::Black);

            if (which != Row::Back)
            {
                const Color value = enabled ? (focused ? Color::White : Color::Silver) : Color::Slate;

                screen.put(valueX, y, glyph::TriLeft, focused && enabled ? Color::Gold : Color::Slate, Color::Black);
                screen.textCenteredIn(valueX + 2, valueWidth - 4, y,
                    ui::truncateTo(rowValue(context, which), valueWidth - 4), value, Color::Black);
                screen.put(valueX + valueWidth - 1, y, glyph::TriRight,
                    focused && enabled ? Color::Gold : Color::Slate, Color::Black);

                m_hits.add(HitPrev + index, valueX, y, 2, 1);
                m_hits.add(HitNext + index, valueX + valueWidth - 2, y, 2, 1);
            }

            m_hits.add(index, inner, y, width, 1);

            if (focused)
                screen.put(inner - 2, y, glyph::TriRight, Color::Gold, Color::Transparent);

            ++y;
        };

        section(L"DISPLAY");
        row(Row::DisplayMode);
        row(Row::VerticalSync);
        ++y;

        section(L"AUDIO");
        row(Row::MasterVolume);
        row(Row::MusicVolume);
        row(Row::EffectsVolume);
        ++y;

        section(L"COMFORT");
        row(Row::ScreenShake);
        row(Row::Bloom);
        row(Row::ShowPing);
        ++y;

        row(Row::Back);

        // --- hint for whatever is focused --------------------------------------
        const wchar_t* hint = rowHint(m_row);
        if (hint != nullptr && *hint != L'\0')
        {
            screen.textCentered(kPanelY + kPanelH + 1, ui::truncateTo(hint, screen.width() - 4),
                Color::Slate.scaled(1.3f), Color::Transparent);
        }

        if (!m_message.empty())
        {
            screen.textCentered(ui::kFooterY - 2, ui::truncateTo(m_message, screen.width() - 4),
                Color::Amber, Color::Transparent);
        }

        // --- footer -------------------------------------------------------------
        screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        int x = 6;
        x += ui::keyHint(screen, x, ui::kFooterY, L"UP/DOWN", L"Select", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"LEFT/RIGHT", L"Change", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Back", Color::Black) + 3;
        ui::keyHint(screen, x, ui::kFooterY, L"F11", L"Fullscreen", Color::Black);
    }
}
