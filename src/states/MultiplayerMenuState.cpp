#include "MultiplayerMenuState.h"

#include "LobbyState.h"
#include "../core/Glyphs.h"
#include "../net/ClientSession.h"
#include "../net/HostSession.h"
#include "../net/NetConfig.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"
#include "../ui/Palette.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace neoncoil
{
    namespace
    {
        constexpr int kActionsX = 8;
        constexpr int kActionsY = 12;
        constexpr int kActionsW = 46;
        constexpr int kActionsH = 23;

        constexpr int kSessionsX = 60;
        constexpr int kSessionsY = 12;
        constexpr int kSessionsW = 52;
        constexpr int kSessionsH = 23;

        constexpr int kMaxAddressLength = 30;
        constexpr int kMaxCodeLength = 8;

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            return ((value % count) + count) % count;
        }

        std::string narrow(const std::wstring& text)
        {
            std::string out;
            out.reserve(text.size());
            for (wchar_t c : text)
                out.push_back(c < 128 ? static_cast<char>(c) : '?');
            return out;
        }

        // "127.0.0.1" or "192.168.0.5:45711". A port in the address wins over
        // the configured default, which is what makes two sessions on one
        // machine reachable during testing.
        void splitAddress(const std::wstring& text, std::string& host, std::uint16_t& port)
        {
            const std::string full = narrow(text);
            const std::size_t colon = full.rfind(':');

            // An IPv6 literal has colons of its own; only treat the last one as
            // a port separator when what follows is entirely digits.
            if (colon != std::string::npos && colon + 1 < full.size())
            {
                const std::string tail = full.substr(colon + 1);
                if (std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
                {
                    host = full.substr(0, colon);
                    port = static_cast<std::uint16_t>(std::atoi(tail.c_str()));
                    return;
                }
            }

            host = full;
        }

        std::wstring padded(std::wstring text, int width)
        {
            return ui::padTo(ui::truncateTo(std::move(text), width), width);
        }
    }

    int MultiplayerMenuState::chosenRelay() const
    {
        if (m_region != kAutoRegion)
            return m_region;

        // AUTO is resolved from live pings, so it follows the network rather
        // than a guess made when the menu opened. Before anything has answered
        // it falls back to the first configured relay -- the one whoever wrote
        // netconfig.txt put at the top.
        const int fastest = m_matchmaker->fastestRelay();
        if (fastest >= 0)
            return fastest;

        return net::NetConfig::instance().haveRelays() ? 0 : -1;
    }

    void MultiplayerMenuState::cycleRegion(int direction)
    {
        const int count = static_cast<int>(net::NetConfig::instance().relays.size());
        if (count <= 0)
            return;

        // AUTO sits at the front of the ring, so one press from it reaches the
        // first region and one press back reaches the last.
        const int current = m_region == kAutoRegion ? 0 : m_region + 1;
        const int next = wrapIndex(current + direction, count + 1);

        m_region = next == 0 ? kAutoRegion : next - 1;
    }

    std::wstring MultiplayerMenuState::regionLabel(int relayIndex) const
    {
        const net::NetConfig& config = net::NetConfig::instance();
        if (relayIndex < 0 || relayIndex >= static_cast<int>(config.relays.size()))
            return L"NONE";

        std::wstring label = config.relays[static_cast<std::size_t>(relayIndex)].name;

        for (const net::RelayStatus& status : m_matchmaker->relayStatuses())
        {
            if (status.index != relayIndex)
                continue;

            if (!status.reachable && status.pingMs < 0)
                return label + L"  --";

            return label + L"  " + std::to_wstring(std::max(0, status.pingMs)) + L" MS";
        }

        return label;
    }

    void MultiplayerMenuState::onEnter(AppContext& context)
    {
        context.input.flush();

        m_matchmaker = net::makeMatchmaker();
        if (!m_matchmaker->startBrowsing())
            m_message = m_matchmaker->lastError();

        m_column = Column::Actions;
        m_choice = net::NetConfig::instance().relayConfigured() ? Choice::HostOnline : Choice::HostLan;
        m_region = kAutoRegion;
        m_sessionSelection = 0;
        m_searching = false;
        m_elapsed = 0.0f;
    }

    void MultiplayerMenuState::onExit(AppContext& context)
    {
        (void)context;

        // The browse socket is released here rather than left bound while a
        // lobby is on top of this screen.
        if (m_matchmaker)
            m_matchmaker->stopBrowsing();
    }

    // ------------------------------------------------------------- transitions --

    Transition MultiplayerMenuState::host(AppContext& context, bool viaRelay)
    {
        net::NetConfig& config = net::NetConfig::instance();

        if (viaRelay && !config.relayConfigured())
        {
            m_message = L"This build has no relay configured -- host on this network instead";
            return Transition::none();
        }

        // always_use_relay is a testing and deployment switch: it makes even a
        // local session take the relay path, which is how the relayed code gets
        // exercised without two machines on two networks.
        if (!viaRelay && config.alwaysUseRelay && config.relayConfigured())
        {
            viaRelay = true;
            m_message = L"Hosting through the relay -- always_use_relay is on";
        }

        // The region choice is applied to the copy the session takes, which is
        // the whole mechanism: nothing below this line knows a list of relays
        // exists, it just dials the one it was handed.
        net::NetConfig chosen = config;
        if (viaRelay)
        {
            const int index = chosenRelay();
            if (index < 0)
            {
                m_message = L"No relay has answered yet -- host on this network instead";
                return Transition::none();
            }

            chosen.selectRelay(index);
            m_message = L"Hosting in " + regionLabel(index);
        }

        auto session = std::make_unique<net::HostSession>(chosen, net::identityProvider());

        std::wstring error;
        if (!session->open(context.profile.name,
            static_cast<std::uint8_t>(ui::playerColourIndex(context.profile.colour)),
            static_cast<std::uint8_t>(context.profile.snakeTypeIndex),
            viaRelay ? net::HostSession::Reach::Relay : net::HostSession::Reach::Direct, error))
        {
            m_message = error;
            return Transition::none();
        }

        return Transition::push(std::make_unique<LobbyState>(std::move(session)));
    }

    Transition MultiplayerMenuState::joinByCode(AppContext& context, const std::wstring& code)
    {
        net::NetConfig& config = net::NetConfig::instance();

        auto session = std::make_unique<net::ClientSession>(config, net::identityProvider());

        std::wstring error;
        if (!session->connectByCode(code, context.profile.name,
            static_cast<std::uint8_t>(ui::playerColourIndex(context.profile.colour)),
            static_cast<std::uint8_t>(context.profile.snakeTypeIndex), error))
        {
            m_message = error;
            return Transition::none();
        }

        return Transition::push(std::make_unique<LobbyState>(std::move(session)));
    }

    Transition MultiplayerMenuState::join(AppContext& context, const std::string& address, std::uint16_t port)
    {
        net::NetConfig& config = net::NetConfig::instance();

        auto session = std::make_unique<net::ClientSession>(config, net::identityProvider());

        std::wstring error;
        if (!session->connect(address, port, context.profile.name,
            static_cast<std::uint8_t>(ui::playerColourIndex(context.profile.colour)),
            static_cast<std::uint8_t>(context.profile.snakeTypeIndex), error))
        {
            m_message = error;
            return Transition::none();
        }

        return Transition::push(std::make_unique<LobbyState>(std::move(session)));
    }

    Transition MultiplayerMenuState::joinDiscovered(AppContext& context, const net::DiscoveredSession& session)
    {
        // A relayed session has no address worth dialling -- its code IS its
        // address -- so it is joined the same way a typed code is, and the
        // player never has to know which of the two they picked.
        if (!session.viaRelay)
            return join(context, session.address, session.advert.port);

        // Found through a particular relay, so joined through that one --
        // whatever region is currently selected for hosting. The code carries
        // the same answer, but the browser already knows it for certain.
        net::NetConfig::instance().selectRelay(session.relayIndex);
        return joinByCode(context, session.advert.code);
    }

    Transition MultiplayerMenuState::takeQuickMatchDecision(AppContext& context)
    {
        m_searching = false;

        // Matchmaking, for a four-player game: join the fullest session that
        // still has room, and if there is not one, become the session other
        // people will find. That is the whole policy, and it is the one that
        // actually gathers players instead of scattering them.
        if (const net::DiscoveredSession* best = m_matchmaker->bestJoinable(); best != nullptr)
        {
            m_message = L"Joining " + best->advert.hostName + L"'s session";
            return joinDiscovered(context, *best);
        }

        // Nothing open anywhere. If there is a relay, become a session anyone
        // can reach rather than one only this network can see.
        const bool viaRelay = net::NetConfig::instance().relayConfigured();
        m_message = viaRelay ? L"No session found -- hosting an online one instead"
                             : L"No session found -- hosting one instead";
        return host(context, viaRelay);
    }

    Transition MultiplayerMenuState::activate(AppContext& context)
    {
        if (m_column == Column::Sessions)
        {
            const std::vector<net::DiscoveredSession>& sessions = m_matchmaker->sessions();
            if (sessions.empty())
                return Transition::none();

            const net::DiscoveredSession& chosen =
                sessions[static_cast<std::size_t>(std::clamp(m_sessionSelection, 0,
                    static_cast<int>(sessions.size()) - 1))];

            if (!chosen.joinable())
            {
                m_message = chosen.advert.inMatch ? L"That session is already playing"
                                                  : L"That session is full";
                return Transition::none();
            }

            return joinDiscovered(context, chosen);
        }

        switch (m_choice)
        {
        case Choice::Region:
            cycleRegion(1);
            return Transition::none();

        case Choice::HostOnline:
            return host(context, true);

        case Choice::HostLan:
            return host(context, false);

        case Choice::QuickMatch:
            m_searching = true;
            m_searchElapsed = 0.0f;
            m_message = L"Searching for a session...";
            return Transition::none();

        case Choice::Code:
            m_choice = Choice::JoinCode;
            return Transition::none();

        case Choice::JoinCode:
            if (m_code.empty())
            {
                m_message = L"Type the code the host gave you";
                return Transition::none();
            }
            return joinByCode(context, m_code);

        case Choice::Address:
            m_choice = Choice::JoinAddress;
            return Transition::none();

        case Choice::JoinAddress:
        {
            std::string address;
            std::uint16_t port = net::NetConfig::instance().hostPort;
            splitAddress(m_address, address, port);

            if (address.empty())
            {
                m_message = L"Type an address first";
                return Transition::none();
            }

            return join(context, address, port);
        }

        case Choice::Back:
        case Choice::Count:
            return Transition::pop();
        }

        return Transition::none();
    }

    void MultiplayerMenuState::handleAddressEntry(AppContext& context)
    {
        // The code field is uppercase-only and short; the address field takes
        // anything. Which one is being typed into follows the focus.
        const bool editingCode = m_choice == Choice::Code;

        std::wstring& target = editingCode ? m_code : m_address;
        const int limit = editingCode ? kMaxCodeLength : kMaxAddressLength;

        for (int i = 0; i < context.input.backspaceCount(); ++i)
        {
            if (!editingCode && !m_addressTouched)
            {
                // The address field ships pre-filled with loopback, so the first
                // keystroke replaces it rather than appending to it.
                target.clear();
                m_addressTouched = true;
                break;
            }
            if (!target.empty())
                target.pop_back();
        }

        for (wchar_t character : context.input.typedText())
        {
            if (!editingCode && !m_addressTouched)
            {
                target.clear();
                m_addressTouched = true;
            }

            if (static_cast<int>(target.size()) >= limit)
                break;

            target.push_back(editingCode
                ? static_cast<wchar_t>(towupper(static_cast<wint_t>(character)))
                : character);
        }
    }

    // ------------------------------------------------------------------ update --

    Transition MultiplayerMenuState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;
        m_matchmaker->update(deltaSeconds);

        if (m_searching)
        {
            m_searchElapsed += deltaSeconds;

            // Cancelling a search has to work, otherwise a player who changed
            // their mind is stuck watching a spinner.
            if (context.input.pressed(neoncoil::Action::Back))
            {
                m_searching = false;
                m_message = L"Search cancelled";
                return Transition::none();
            }

            if (m_searchElapsed >= net::NetConfig::instance().quickMatchSearchSeconds)
                return takeQuickMatchDecision(context);

            return Transition::none();
        }

        const Input& input = context.input;
        const bool editing = m_column == Column::Actions &&
            (m_choice == Choice::Address || m_choice == Choice::Code);

        // While the address field has focus only the arrow keys navigate, so
        // typing an address cannot also move the cursor -- the same rule the
        // main menu applies to the name field.
        const bool up = editing ? input.pressed(neoncoil::Action::NavUp) : input.pressed(neoncoil::Action::Up);
        const bool down = editing ? input.pressed(neoncoil::Action::NavDown) : input.pressed(neoncoil::Action::Down);
        const bool left = editing ? input.pressed(neoncoil::Action::NavLeft) : input.pressed(neoncoil::Action::Left);
        const bool right = editing ? input.pressed(neoncoil::Action::NavRight) : input.pressed(neoncoil::Action::Right);

        if (editing)
            handleAddressEntry(context);

        // On the region row the arrows change the region; everywhere else they
        // move between the two panels. A row that is a horizontal chooser has to
        // own the horizontal keys or it cannot be operated.
        const bool onRegion = m_column == Column::Actions && m_choice == Choice::Region;

        if (onRegion && (left || right))
        {
            cycleRegion(right ? 1 : -1);
        }
        else
        {
            if (left)
                m_column = Column::Actions;
            if (right && !m_matchmaker->sessions().empty())
                m_column = Column::Sessions;
        }

        const int sessionCount = static_cast<int>(m_matchmaker->sessions().size());

        if (m_column == Column::Actions)
        {
            const int count = static_cast<int>(Choice::Count);
            if (up)
                m_choice = static_cast<Choice>(wrapIndex(static_cast<int>(m_choice) - 1, count));
            if (down)
                m_choice = static_cast<Choice>(wrapIndex(static_cast<int>(m_choice) + 1, count));
        }
        else
        {
            if (sessionCount == 0)
                m_column = Column::Actions;
            else
            {
                if (up)
                    m_sessionSelection = wrapIndex(m_sessionSelection - 1, sessionCount);
                if (down)
                    m_sessionSelection = wrapIndex(m_sessionSelection + 1, sessionCount);
                m_sessionSelection = std::clamp(m_sessionSelection, 0, sessionCount - 1);
            }
        }

        if (input.pressed(neoncoil::Action::Back))
            return Transition::pop();

        if (input.pressed(neoncoil::Action::Confirm))
            return activate(context);

        return handleMouse(context);
    }

    Transition MultiplayerMenuState::handleMouse(AppContext& context)
    {
        const Input& input = context.input;
        const int sessionCount = static_cast<int>(m_matchmaker->sessions().size());

        const auto focus = [&](int id)
        {
            // The arrows are hovered, not focused: pointing at one should not
            // move the cursor off the row it belongs to.
            if (id == HitRegionPrev || id == HitRegionNext)
                return;

            if (id >= HitSession)
            {
                m_column = Column::Sessions;
                m_sessionSelection = std::clamp(id - HitSession, 0, std::max(0, sessionCount - 1));
            }
            else if (id >= 0 && id < static_cast<int>(Choice::Count))
            {
                m_column = Column::Actions;
                m_choice = static_cast<Choice>(id);
            }
        };

        if (input.mouseMoved())
        {
            if (const int hovered = m_hits.hovered(input); hovered != ui::HitMap::kNone)
                focus(hovered);
        }

        const int clicked = m_hits.clicked(input);
        if (clicked == ui::HitMap::kNone)
            return Transition::none();

        if (clicked == HitRegionPrev || clicked == HitRegionNext)
        {
            m_column = Column::Actions;
            m_choice = Choice::Region;
            cycleRegion(clicked == HitRegionNext ? 1 : -1);
            return Transition::none();
        }

        focus(clicked);

        // The text boxes only take focus on a click; everything else on this
        // screen is a button, so a click on it presses it.
        if (clicked == static_cast<int>(Choice::Address) || clicked == static_cast<int>(Choice::Code))
            return Transition::none();

        return activate(context);
    }

    // ------------------------------------------------------------------ render --

    void MultiplayerMenuState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        screen.clear(Color::Black);
        m_hits.clear();

        if (const sf::Texture* plate = screen.textures().get("ui/bg_menu.png"))
        {
            screen.sprite(*plate, 0.0f, 0.0f, ui::kCanvasWidth, ui::kCanvasHeight,
                Screen::SpriteLayer::Background, Color::White.scaled(0.24f));
        }

        ui::drawBannerCentered(screen, 3, L"MULTIPLAYER", Color::Aqua, Color::Transparent, 2, 1, 1);
        screen.textCentered(9, L"U P   T O   F O U R   S E R P E N T S", Color::Slate, Color::Transparent);

        renderActions(context);
        renderSessions(context);

        // --- status line ------------------------------------------------------
        if (m_searching)
        {
            const int dots = static_cast<int>(m_elapsed * 3.0f) % 4;
            const std::wstring text = L"SEARCHING FOR A SESSION" + std::wstring(static_cast<std::size_t>(dots), L'.');
            screen.textCentered(ui::kFooterY - 2, text, Color::Gold, Color::Transparent);
        }
        else if (!m_message.empty())
        {
            screen.textCentered(ui::kFooterY - 2, ui::truncateTo(m_message, screen.width() - 4),
                Color::Amber, Color::Transparent);
        }

        // --- footer -----------------------------------------------------------
        screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        int x = 6;
        x += ui::keyHint(screen, x, ui::kFooterY, L"UP/DOWN", L"Select", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"LEFT/RIGHT", L"Panel", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ENTER", L"Confirm", Color::Black) + 3;
        x += ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Back", Color::Black) + 3;
        ui::keyHint(screen, x, ui::kFooterY, L"MOUSE", L"Click anything", Color::Black);
    }

    void MultiplayerMenuState::renderActions(AppContext& context) const
    {
        Screen& screen = context.screen;
        const bool columnFocused = m_column == Column::Actions;

        screen.panel(kActionsX, kActionsY, kActionsW, kActionsH,
            columnFocused ? Color::Gold : Color::Slate, Color::Black);
        screen.text(kActionsX + 3, kActionsY, L" PLAY ONLINE ", Color::Gold, Color::Black);

        const int inner = kActionsX + 3;
        const int width = kActionsW - 6;

        const bool haveRelay = net::NetConfig::instance().relayConfigured();

        const auto button = [&](int y, Choice choice, const std::wstring& label,
            const std::wstring& hint, bool enabled = true)
        {
            const bool focused = columnFocused && m_choice == choice;

            // Greyed rather than hidden: if a build has no relay, the player
            // should see that hosting online exists and is unavailable, not be
            // left wondering where the option went.
            const Color background = focused ? (enabled ? Color::Aqua : Color::Slate)
                                             : Color::Slate.scaled(0.5f);
            const Color foreground = focused && enabled ? Color::Black
                                                        : (enabled ? Color::Silver : Color::Slate.scaled(1.6f));

            screen.fillRect(inner, y, width, 1, glyph::Space, foreground, background);
            screen.textCenteredIn(inner, width, y, label, foreground, background);
            m_hits.add(static_cast<int>(choice), inner, y, width, 1);

            if (focused)
            {
                screen.put(inner - 2, y, glyph::TriRight, Color::Gold, Color::Transparent);
                screen.put(inner + width + 1, y, glyph::TriLeft, Color::Gold, Color::Transparent);
            }

            if (!hint.empty())
                screen.text(inner, y + 1, ui::truncateTo(hint, width), Color::Slate, Color::Black);
        };

        // A text box plus its label, returning nothing: the two on this screen
        // are laid out identically and only differ in what they hold.
        const auto field = [&](int y, Choice choice, const std::wstring& label,
            const std::wstring& value, const std::wstring& hint)
        {
            const bool focused = columnFocused && m_choice == choice;

            screen.text(inner, y, label, focused ? Color::Gold : Color::Silver, Color::Black);
            screen.fillRect(inner, y + 1, width, 1, glyph::Space, Color::White,
                focused ? Color::Navy : Color::Black);
            m_hits.add(static_cast<int>(choice), inner, y, width, 2);

            screen.text(inner + 1, y + 1, ui::truncateTo(value, width - 3), Color::White,
                focused ? Color::Navy : Color::Black);

            if (focused && static_cast<int>(m_elapsed * 2.0f) % 2 == 0)
            {
                screen.put(inner + 1 + static_cast<int>(value.size()), y + 1,
                    glyph::HalfLeft, Color::Gold, Color::Navy);
            }

            if (!hint.empty())
                screen.text(inner, y + 2, ui::truncateTo(hint, width), Color::Slate, Color::Black);
        };

        // Region first, because it decides how the two host buttons under it
        // will feel. A player who never touches it gets AUTO, which is the right
        // answer for them; a player on a bad route to the nearest relay can see
        // that in the numbers and pick another.
        const auto regionRow = [&](int y)
        {
            const bool focused = columnFocused && m_choice == Choice::Region;
            const int resolved = chosenRelay();

            screen.text(inner, y, L"REGION", focused ? Color::Gold : Color::Silver, Color::Black);

            const std::wstring value = m_region == kAutoRegion
                ? (resolved >= 0 ? L"AUTO -- " + regionLabel(resolved) : std::wstring(L"AUTO"))
                : regionLabel(m_region);

            const Color background = focused ? Color::Aqua : Color::Slate.scaled(0.5f);
            const Color foreground = focused ? Color::Black : Color::Silver;

            screen.fillRect(inner, y + 1, width, 1, glyph::Space, foreground, background);
            screen.textCenteredIn(inner, width, y + 1, ui::truncateTo(value, width - 4),
                foreground, background);

            // Arrows are hit targets of their own, so the region can be changed
            // with the mouse without first selecting the row.
            screen.put(inner, y + 1, glyph::TriLeft, focused ? Color::Black : Color::Gold, background);
            screen.put(inner + width - 1, y + 1, glyph::TriRight,
                focused ? Color::Black : Color::Gold, background);

            m_hits.add(static_cast<int>(Choice::Region), inner, y, width, 2);
            m_hits.add(HitRegionPrev, inner, y + 1, 2, 1);
            m_hits.add(HitRegionNext, inner + width - 2, y + 1, 2, 1);

            if (!haveRelay)
                screen.text(inner, y + 2, L"No relay in this build.", Color::Slate, Color::Black);
            else
                screen.text(inner, y + 2, L"Lower ms is closer. AUTO picks it.", Color::Slate, Color::Black);
        };

        regionRow(kActionsY + 1);

        // Online first, because it is the one that works for everybody without
        // anyone touching a router.
        button(kActionsY + 4, Choice::HostOnline, L"HOST ONLINE",
            haveRelay ? L"Anyone, anywhere. No setup."
                      : L"No relay in this build.", haveRelay);

        button(kActionsY + 7, Choice::HostLan, L"HOST ON THIS NETWORK",
            L"Same wifi only. Needs no relay.");

        button(kActionsY + 10, Choice::QuickMatch, L"QUICK MATCH",
            L"Join one, or host if none exists.");

        field(kActionsY + 13, Choice::Code, L"SESSION CODE", m_code,
            L"The code the host gave you.");
        button(kActionsY + 15, Choice::JoinCode, L"JOIN BY CODE", L"");

        field(kActionsY + 17, Choice::Address, L"ADDRESS", m_address, L"");
        button(kActionsY + 19, Choice::JoinAddress, L"JOIN BY ADDRESS", L"");

        button(kActionsY + 21, Choice::Back, L"BACK", L"");
    }

    int MultiplayerMenuState::firstVisibleSession(int sessionCount, int rows) const
    {
        if (sessionCount <= rows)
            return 0;

        // Keep the selection near the middle of the window rather than only
        // nudging the view when it would fall off an edge. Edge-following needs
        // to remember which way the cursor was travelling to look right, and a
        // renderer is the wrong place to keep that; centring needs no memory and
        // scrolls one row per keypress in both directions.
        return std::clamp(m_sessionSelection - rows / 2, 0, sessionCount - rows);
    }

    void MultiplayerMenuState::renderSessions(AppContext& context) const
    {
        Screen& screen = context.screen;
        const bool columnFocused = m_column == Column::Sessions;

        screen.panel(kSessionsX, kSessionsY, kSessionsW, kSessionsH,
            columnFocused ? Color::Gold : Color::Slate, Color::Black);
        screen.text(kSessionsX + 3, kSessionsY, L" OPEN SESSIONS ", Color::Gold, Color::Black);

        const int inner = kSessionsX + 3;
        const int width = kSessionsW - 6;

        const bool haveRelay = net::NetConfig::instance().relayConfigured();
        screen.text(inner, kSessionsY + 2,
            haveRelay ? L"ON THIS NETWORK AND ONLINE" : L"ON THIS NETWORK",
            Color::Slate, Color::Black);
        screen.horizontalLine(inner, kSessionsY + 3, width, glyph::ThinH, Color::Slate, Color::Black);

        const std::vector<net::DiscoveredSession>& sessions = m_matchmaker->sessions();

        if (sessions.empty())
        {
            const int dots = static_cast<int>(m_elapsed * 3.0f) % 4;
            screen.text(inner, kSessionsY + 5,
                L"Looking" + std::wstring(static_cast<std::size_t>(dots), L'.'), Color::Slate, Color::Black);
            screen.text(inner, kSessionsY + 7,
                haveRelay ? L"Nothing open here or online yet."
                          : L"Nothing on this network yet.",
                Color::Slate, Color::Black);
            screen.text(inner, kSessionsY + 9, L"Host one, or join by code.", Color::Slate, Color::Black);
            return;
        }

        // Two rows per entry plus a blank one, inside the panel's own border.
        const int rows = (kSessionsH - 6) / 3;
        const int first = firstVisibleSession(static_cast<int>(sessions.size()), rows);
        const int lastShown = std::min(static_cast<int>(sessions.size()), first + rows);

        int y = kSessionsY + 5;
        for (int i = first; i < lastShown; ++i, y += 3)
        {
            const net::DiscoveredSession& session = sessions[static_cast<std::size_t>(i)];
            const bool selected = columnFocused && i == m_sessionSelection;

            const Color background = selected ? Color::Aqua : Color::Black;
            const Color foreground = selected ? Color::Black : Color::White;

            screen.fillRect(inner, y, width, 1, glyph::Space, foreground, background);
            m_hits.add(HitSession + i, inner, y, width, 2);
            screen.text(inner + 1, y, padded(session.advert.hostName, 14), foreground, background);
            screen.text(inner + 16, y, session.advert.code, selected ? Color::Black : Color::Gold, background);

            const std::wstring count = std::to_wstring(session.advert.players) + L"/" +
                std::to_wstring(session.advert.maxPlayers);
            screen.text(inner + width - 5, y, count, foreground, background);

            const wchar_t* state = session.advert.inMatch ? L"PLAYING"
                : (session.joinable() ? L"OPEN" : L"FULL");
            const Color stateColour = session.advert.inMatch ? Color::Amber
                : (session.joinable() ? Color::Lime : Color::Red);

            screen.text(inner + 1, y + 1, state, stateColour, Color::Black);

            // Which of the two a session is matters to a player -- an online one
            // will always be the slower of the two -- so it is labelled rather
            // than left to be inferred from whether an address is shown.
            screen.text(inner + 9, y + 1, session.viaRelay ? L"ONLINE" : L"LOCAL",
                session.viaRelay ? Color::Aqua : Color::Lime, Color::Black);

            // An online row says which region it is in and what that region
            // pings, because that number is the difference between a session
            // worth joining and one that will feel awful.
            const std::wstring detail = session.viaRelay
                ? regionLabel(session.relayIndex)
                : session.where();

            screen.text(inner + 17, y + 1, ui::truncateTo(detail, width - 18),
                Color::Slate, Color::Black);

            if (selected)
                screen.put(inner - 2, y, glyph::TriRight, Color::Gold, Color::Transparent);
        }

        if (lastShown < static_cast<int>(sessions.size()) || first > 0)
        {
            const int hidden = static_cast<int>(sessions.size()) - (lastShown - first);
            screen.text(inner, kSessionsY + kSessionsH - 2,
                std::to_wstring(hidden) + L" more -- scroll with UP/DOWN",
                Color::Slate, Color::Black);
        }
    }
}
