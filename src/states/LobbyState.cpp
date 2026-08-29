#include "LobbyState.h"

#include "NetPlayState.h"
#include "../core/Glyphs.h"
#include "../core/Settings.h"
#include "../game/SnakeType.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"
#include "../ui/Palette.h"
#include "../ui/SnakeCard.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        constexpr int kSlotsX = 6;
        constexpr int kSlotsY = 11;
        constexpr int kSlotW = 26;
        constexpr int kSlotH = 12;
        constexpr int kSlotGap = 1;

        constexpr int kSideX = 6;
        constexpr int kSideY = 24;
        constexpr int kSideW = 108;
        constexpr int kSideH = 14;

        // The panel is three columns: what you are flying, what you can press,
        // and what your snake actually does. The last one is new -- a player
        // picking SHED or GOLD RUSH before a match had no way to find out what
        // either meant without leaving the session.
        //
        // It gets the whole right-hand half, and the event feed gives up its
        // column for it. Knowing what the space bar does is worth more before a
        // match than a scrolling list of who joined, and the newest line of that
        // list still shows under the buttons.

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            return ((value % count) + count) % count;
        }
    }

    LobbyState::LobbyState(std::unique_ptr<net::NetGame> session)
        : m_session(std::move(session))
    {
    }

    LobbyState::~LobbyState()
    {
        // Whatever happened -- left, kicked, host quit, window closed -- the
        // session is shut down exactly once, here.
        if (m_session)
            m_session->shutdown();
    }

    void LobbyState::onEnter(AppContext& context)
    {
        context.input.flush();
        m_field = Field::Ready;
        m_elapsed = 0.0f;
        m_matchScreenOpen = false;
    }

    void LobbyState::pushLoadout(AppContext& context)
    {
        m_session->setLoadout(
            static_cast<std::uint8_t>(ui::playerColourIndex(context.profile.colour)),
            static_cast<std::uint8_t>(context.profile.snakeTypeIndex));
    }

    void LobbyState::adjust(AppContext& context, int delta)
    {
        if (delta == 0)
            return;

        if (m_field == Field::Colour)
        {
            const int index = wrapIndex(ui::playerColourIndex(context.profile.colour) + delta,
                ui::playerColourCount());
            context.profile.colour = ui::playerColourAt(index);
            pushLoadout(context);
        }
        else if (m_field == Field::Type)
        {
            context.profile.snakeTypeIndex = wrapIndex(context.profile.snakeTypeIndex + delta, snakeTypeCount());
            pushLoadout(context);
        }
    }

    Transition LobbyState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;
        m_session->update(deltaSeconds);

        const net::SessionPhase phase = m_session->phase();

        // --- connecting / failed ----------------------------------------------
        if (phase == net::SessionPhase::Connecting)
        {
            if (context.input.pressed(Action::Back))
                return Transition::pop();
            return Transition::none();
        }

        if (phase == net::SessionPhase::Disconnected || phase == net::SessionPhase::Idle)
        {
            // Any key acknowledges the reason and goes back to the multiplayer
            // menu. Dropping the player straight out would hide why it failed.
            if (context.input.anyKeyPressed())
                return Transition::pop();
            return Transition::none();
        }

        // --- the match owns the screen while it runs --------------------------
        if (phase == net::SessionPhase::InMatch || phase == net::SessionPhase::Starting ||
            phase == net::SessionPhase::PostMatch)
        {
            if (!m_matchScreenOpen)
            {
                m_matchScreenOpen = true;
                return Transition::push(std::make_unique<NetPlayState>(m_session.get()));
            }

            // Control came back while the session still thinks a match is on:
            // the player stepped out of the match screen. Leaving the session
            // entirely is the only other thing left to offer them.
            if (context.input.pressed(Action::Back))
                return Transition::pop();

            return Transition::none();
        }

        m_matchScreenOpen = false;

        // --- lobby ------------------------------------------------------------
        const Input& input = context.input;
        const int fieldCount = static_cast<int>(Field::Count);

        if (input.pressed(Action::Up))
            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) - 1, fieldCount));
        if (input.pressed(Action::Down))
            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) + 1, fieldCount));

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

    Transition LobbyState::activate(AppContext& context)
    {
        (void)context;
        const int fieldCount = static_cast<int>(Field::Count);

        switch (m_field)
        {
        case Field::Ready:
            if (m_session->isHost())
            {
                if (m_session->canStartMatch())
                    m_session->requestStartMatch();
            }
            else
            {
                m_ready = !m_ready;
                m_session->setReady(m_ready);
            }
            break;

        case Field::Leave:
            return Transition::pop();

        default:
            m_field = static_cast<Field>(wrapIndex(static_cast<int>(m_field) + 1, fieldCount));
            break;
        }

        return Transition::none();
    }

    Transition LobbyState::handleMouse(AppContext& context)
    {
        const Input& input = context.input;

        if (input.mouseMoved())
        {
            const int hovered = m_hits.hovered(input);
            if (hovered >= 0 && hovered < static_cast<int>(Field::Count))
                m_field = static_cast<Field>(hovered);
        }

        const int clicked = m_hits.clicked(input);
        if (clicked == ui::HitMap::kNone)
            return Transition::none();

        switch (clicked)
        {
        case HitColourPrev:
            m_field = Field::Colour;
            adjust(context, -1);
            return Transition::none();

        case HitColourNext:
            m_field = Field::Colour;
            adjust(context, 1);
            return Transition::none();

        case HitTypePrev:
            m_field = Field::Type;
            adjust(context, -1);
            return Transition::none();

        case HitTypeNext:
            m_field = Field::Type;
            adjust(context, 1);
            return Transition::none();

        default:
            break;
        }

        if (clicked >= 0 && clicked < static_cast<int>(Field::Count))
        {
            m_field = static_cast<Field>(clicked);

            // Clicking the colour or snake row only focuses it -- the arrows on
            // either side are what change the value.
            if (m_field == Field::Ready || m_field == Field::Leave)
                return activate(context);
        }

        return Transition::none();
    }

    std::wstring LobbyState::blockingReason() const
    {
        // The lobby never has to be full. A match runs with two players, or even
        // with one; the only thing that holds a start up is somebody who has
        // joined and not yet readied. Naming them is the difference between the
        // host thinking the game wants four players and the host knowing it is
        // waiting on one keypress.
        const net::LobbyInfo& lobby = m_session->lobby();

        for (const net::LobbySlot& seat : lobby.slots)
        {
            if (seat.occupied && !seat.isHost && !seat.ready)
                return L"WAITING FOR " + ui::truncateTo(seat.name, 11);
        }

        return L"WAITING";
    }

    // ------------------------------------------------------------------ render --

    void LobbyState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        screen.clear(Color::Black);
        m_hits.clear();

        if (const sf::Texture* plate = screen.textures().get("ui/bg_menu.png"))
        {
            screen.sprite(*plate, 0.0f, 0.0f, ui::kCanvasWidth, ui::kCanvasHeight,
                Screen::SpriteLayer::Background, Color::White.scaled(0.22f));
        }

        const net::SessionPhase phase = m_session->phase();

        if (phase == net::SessionPhase::Connecting)
        {
            renderConnecting(context);
            return;
        }

        if (phase == net::SessionPhase::Disconnected || phase == net::SessionPhase::Idle)
        {
            renderDisconnected(context);
            return;
        }

        const net::LobbyInfo& lobby = m_session->lobby();

        ui::drawBannerCentered(screen, 2, L"LOBBY", Color::Aqua, Color::Transparent, 2, 1, 1);

        const std::wstring subtitle = L"CODE " + lobby.code + L"   -   " +
            std::to_wstring(lobby.occupiedCount()) + L" / " + std::to_wstring(lobby.maxPlayers) + L" PLAYERS";
        screen.textCentered(8, subtitle, Color::Gold, Color::Transparent);

        renderSlots(context);
        renderSidebar(context);

        // --- footer -----------------------------------------------------------
        screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        int x = 6;
        x += ui::keyHint(screen, x, ui::kFooterY, L"UP/DOWN", L"Field", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"LEFT/RIGHT", L"Change", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY,
            L"ENTER", m_session->isHost() ? L"Start match" : L"Ready up", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Leave", Color::Black) + 3;
        ui::keyHint(screen, x, ui::kFooterY, L"MOUSE", L"Click anything", Color::Black);
    }

    void LobbyState::renderSlots(AppContext& context) const
    {
        Screen& screen = context.screen;
        const net::LobbyInfo& lobby = m_session->lobby();
        const PlayerSlot localSlot = m_session->localSlot();

        for (int i = 0; i < static_cast<int>(lobby.maxPlayers); ++i)
        {
            const net::LobbySlot& seat = lobby.slots[static_cast<std::size_t>(i)];
            const int x = kSlotsX + i * (kSlotW + kSlotGap + 1);
            const bool isLocal = seat.occupied && seat.slot == localSlot;

            const Color accent = seat.occupied ? ui::playerColourAt(seat.colourIndex) : Color::Slate;

            screen.panel(x, kSlotsY, kSlotW, kSlotH, isLocal ? Color::Gold : accent.scaled(0.8f), Color::Black);

            const std::wstring header = L" P" + std::to_wstring(i + 1) + L" ";
            screen.text(x + 2, kSlotsY, header, isLocal ? Color::Gold : Color::Slate, Color::Black);

            const int inner = x + 2;
            const int width = kSlotW - 4;

            if (!seat.occupied)
            {
                screen.textCenteredIn(inner, width, kSlotsY + 5, L"OPEN SEAT", Color::Slate, Color::Black);

                // The dotted outline reads as "waiting for somebody" rather
                // than as a broken panel.
                screen.horizontalLine(inner, kSlotsY + 6, width, glyph::ThinH, Color::Slate.scaled(0.7f), Color::Black);
                continue;
            }

            const SnakeType& type = snakeTypeAt(seat.typeIndex);

            screen.fillRect(inner, kSlotsY + 2, width, 1, glyph::Space, Color::Black, accent);
            screen.textCenteredIn(inner, width, kSlotsY + 2, ui::truncateTo(seat.name, width - 2),
                Color::Black, accent);

            screen.textCenteredIn(inner, width, kSlotsY + 4, type.name, type.accent, Color::Black);
            screen.textCenteredIn(inner, width, kSlotsY + 5,
                ui::truncateTo(type.ability.name, width), Color::Slate, Color::Black);

            // A short crawling snake in the player's own colour, so a full lobby
            // reads at a glance rather than as four identical name cards. Same
            // strip the field report and the main menu draw, offset per seat so
            // four of them do not move in lockstep.
            ui::drawSnakeStrip(screen, inner, kSlotsY + 7, width, type, accent,
                m_elapsed + static_cast<float>(i) * 0.4f);

            const wchar_t* tag = seat.isHost ? L"HOST" : (seat.ready ? L"READY" : L"NOT READY");
            const Color tagColour = seat.isHost ? Color::Gold : (seat.ready ? Color::Lime : Color::Slate);
            screen.textCenteredIn(inner, width, kSlotsY + 9, tag, tagColour, Color::Black);

            // Latency on the seat card, before the match rather than during it.
            // A lobby is where a player can still do something about a bad
            // connection -- once the countdown has run, the number is only ever
            // an explanation.
            if (!seat.isHost && Settings::instance().showPing)
            {
                const int ping = seat.pingMs == 0 ? -1 : static_cast<int>(seat.pingMs);
                screen.textCenteredIn(inner, width, kSlotsY + 8, ui::pingText(ping),
                    ui::pingColour(ping), Color::Black);
            }

            // Guest identity is local and unverified today. Saying so on the
            // card is honest, and it is the line that will read "SIGNED IN" once
            // accounts exist without the layout changing.
            screen.textCenteredIn(inner, width, kSlotsY + 10,
                seat.authenticated ? L"VERIFIED" : L"GUEST", Color::Slate.scaled(1.2f), Color::Black);
        }
    }

    void LobbyState::renderSidebar(AppContext& context) const
    {
        Screen& screen = context.screen;
        const net::LobbyInfo& lobby = m_session->lobby();

        screen.panel(kSideX, kSideY, kSideW, kSideH, Color::Slate, Color::Black);
        screen.text(kSideX + 3, kSideY, m_session->isHost() ? L" YOUR SESSION " : L" THIS SESSION ",
            Color::Gold, Color::Black);

        const int inner = kSideX + 3;

        // --- how to be joined -------------------------------------------------
        if (m_session->isHost())
        {
            // A relayed host listens on nothing, so it has no port and no
            // address to offer -- it is found in the browser's online half, or
            // by its code. Telling that host to hand out "port 0" was the old
            // text's way of saying it had not been taught the difference.
            if (lobby.port == 0)
            {
                screen.text(inner, kSideY + 2, L"Anyone will see you in OPEN SESSIONS, wherever they are.",
                    Color::Silver, Color::Black);
                screen.text(inner, kSideY + 3, L"Or give them the code above -- no port to forward.",
                    Color::Slate, Color::Black);
            }
            else
            {
                screen.text(inner, kSideY + 2, L"Others on this network will see you in OPEN SESSIONS.",
                    Color::Silver, Color::Black);
                screen.text(inner, kSideY + 3, L"From outside it, they join by your address on port " +
                    std::to_wstring(lobby.port) + L".", Color::Slate, Color::Black);
            }
        }
        else
        {
            screen.text(inner, kSideY + 2, L"Connected to " + lobby.hostName + L"'s session.",
                Color::Silver, Color::Black);
            screen.text(inner, kSideY + 3, L"The host decides when the match starts.",
                Color::Slate, Color::Black);
        }

        // --- loadout ----------------------------------------------------------
        const int optionsX = inner;
        const int optionsY = kSideY + 5;

        const bool colourFocused = m_field == Field::Colour;
        const bool typeFocused = m_field == Field::Type;

        screen.text(optionsX, optionsY, L"COLOUR", colourFocused ? Color::Gold : Color::Silver, Color::Black);
        screen.put(optionsX + 9, optionsY, glyph::TriLeft, colourFocused ? Color::Gold : Color::Slate, Color::Black);
        screen.fillRect(optionsX + 11, optionsY, 3, 1, glyph::Block, context.profile.colour, Color::Black);
        screen.text(optionsX + 15, optionsY,
            ui::padTo(ui::playerColourName(ui::playerColourIndex(context.profile.colour)), 9),
            context.profile.colour, Color::Black);
        screen.put(optionsX + 25, optionsY, glyph::TriRight, colourFocused ? Color::Gold : Color::Slate, Color::Black);

        m_hits.add(static_cast<int>(Field::Colour), optionsX, optionsY, 26, 1);
        m_hits.add(HitColourPrev, optionsX + 8, optionsY, 3, 1);
        m_hits.add(HitColourNext, optionsX + 24, optionsY, 3, 1);

        const SnakeType& type = context.profile.type();
        screen.text(optionsX, optionsY + 1, L"SNAKE", typeFocused ? Color::Gold : Color::Silver, Color::Black);
        screen.put(optionsX + 9, optionsY + 1, glyph::TriLeft, typeFocused ? Color::Gold : Color::Slate, Color::Black);
        screen.text(optionsX + 11, optionsY + 1, ui::padTo(type.name, 14), type.accent, Color::Black);
        screen.put(optionsX + 25, optionsY + 1, glyph::TriRight, typeFocused ? Color::Gold : Color::Slate, Color::Black);

        m_hits.add(static_cast<int>(Field::Type), optionsX, optionsY + 1, 26, 1);
        m_hits.add(HitTypePrev, optionsX + 8, optionsY + 1, 3, 1);
        m_hits.add(HitTypeNext, optionsX + 24, optionsY + 1, 3, 1);

        // --- buttons ----------------------------------------------------------
        const int buttonX = optionsX + 30;
        const int buttonW = 24;

        const bool readyFocused = m_field == Field::Ready;
        const bool leaveFocused = m_field == Field::Leave;

        const bool canStart = m_session->canStartMatch();
        const std::wstring blocked = blockingReason();
        const std::wstring readyLabel = m_session->isHost()
            ? (canStart ? L"START MATCH" : blocked)
            : (m_ready ? L"READY  -  CANCEL" : L"READY UP");

        // Greyed rather than hidden: a host who cannot start yet should see the
        // button and why, not an empty space.
        const bool enabled = m_session->isHost() ? canStart : true;
        const Color readyBackground = readyFocused
            ? (enabled ? Color::Aqua : Color::Slate)
            : Color::Slate.scaled(0.5f);
        const Color readyForeground = readyFocused && enabled ? Color::Black : Color::Silver;

        screen.fillRect(buttonX, optionsY, buttonW, 1, glyph::Space, readyForeground, readyBackground);
        screen.textCenteredIn(buttonX, buttonW, optionsY, ui::truncateTo(readyLabel, buttonW - 2),
            readyForeground, readyBackground);
        m_hits.add(static_cast<int>(Field::Ready), buttonX, optionsY, buttonW, 1);
        if (readyFocused)
            screen.put(buttonX - 2, optionsY, glyph::TriRight, Color::Gold, Color::Transparent);

        // Spelled out under the button, because "waiting" on its own reads as
        // "waiting for the lobby to fill" when it means "waiting for one person
        // to press a key".
        if (m_session->isHost() && !canStart)
        {
            screen.text(buttonX, optionsY + 1,
                ui::truncateTo(L"Two players is enough.", buttonW), Color::Slate, Color::Black);
        }

        const Color leaveBackground = leaveFocused ? Color::Coral : Color::Slate.scaled(0.5f);
        const Color leaveForeground = leaveFocused ? Color::Black : Color::Silver;
        screen.fillRect(buttonX, optionsY + 2, buttonW, 1, glyph::Space, leaveForeground, leaveBackground);
        screen.textCenteredIn(buttonX, buttonW, optionsY + 2, L"LEAVE SESSION", leaveForeground, leaveBackground);
        m_hits.add(static_cast<int>(Field::Leave), buttonX, optionsY + 2, buttonW, 1);
        if (leaveFocused)
            screen.put(buttonX - 2, optionsY + 2, glyph::TriRight, Color::Gold, Color::Transparent);

        // --- newest activity line ---------------------------------------------
        //
        // One line, under the buttons, rather than a column of six. What a
        // player needs from it is "did that person actually join", which the
        // most recent entry answers.
        const std::vector<std::wstring>& events = m_session->events();
        if (!events.empty())
        {
            screen.text(buttonX, optionsY + 4, ui::truncateTo(events.back(), buttonW + 4),
                Color::Slate.scaled(1.3f), Color::Black);
        }

        // --- field report -----------------------------------------------------
        //
        // Right-hand half, and the thing that changes when the player presses
        // left or right on SNAKE. Showing the ability here rather than only its
        // name is the difference between choosing a snake and guessing at one.
        const int reportX = buttonX + buttonW + 4;
        const int reportWidth = kSideX + kSideW - reportX - 3;
        const int reportY = kSideY + 2;

        if (reportWidth > 16)
        {
            screen.text(reportX, reportY, L"FIELD REPORT", Color::Gold, Color::Black);
            ui::drawSnakeReport(screen, reportX, reportY + 1, reportWidth,
                kSideY + kSideH - reportY - 2, type, context.profile.colour, m_elapsed);
        }
    }

    void LobbyState::renderConnecting(AppContext& context) const
    {
        Screen& screen = context.screen;

        ui::drawBannerCentered(screen, 12, L"CONNECTING", Color::Aqua, Color::Transparent, 2, 1, 1);

        const int dots = static_cast<int>(m_elapsed * 3.0f) % 4;
        screen.textCentered(20, m_session->status() + std::wstring(static_cast<std::size_t>(dots), L'.'),
            Color::Silver, Color::Transparent);

        ui::drawSnakeFlourish(screen, screen.width() / 2 - 10, 23,
            4 + static_cast<int>(m_elapsed * 6.0f) % 12, Color::Slate, Color::Transparent);

        screen.textCentered(ui::kFooterY, L"ESC to cancel", Color::Slate, Color::Black);
    }

    void LobbyState::renderDisconnected(AppContext& context) const
    {
        Screen& screen = context.screen;

        ui::drawBannerCentered(screen, 12, L"NO SESSION", Color::Red, Color::Transparent, 2, 1, 1);

        const std::wstring reason = m_session->status().empty()
            ? L"The session ended."
            : m_session->status();

        screen.textCentered(20, ui::truncateTo(reason, screen.width() - 8), Color::Amber, Color::Transparent);
        screen.textCentered(ui::kFooterY, L"Press any key to go back", Color::Slate, Color::Black);
    }
}
