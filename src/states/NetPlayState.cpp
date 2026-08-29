#include "NetPlayState.h"

#include "../core/Glyphs.h"
#include "../core/Settings.h"
#include "../game/SnakeType.h"
#include "../ui/Art.h"
#include "../ui/Layout.h"
#include "../ui/Palette.h"

#include <algorithm>
#include <cmath>

namespace neoncoil
{
    namespace
    {
        std::wstring formatClock(float seconds)
        {
            const int total = std::max(0, static_cast<int>(seconds + 0.5f));
            const int minutes = total / 60;
            const int rest = total % 60;

            std::wstring text = std::to_wstring(minutes) + L":";
            if (rest < 10)
                text += L"0";
            text += std::to_wstring(rest);
            return text;
        }
    }

    NetPlayState::NetPlayState(net::NetGame* session)
        : m_session(session)
    {
    }

    void NetPlayState::onEnter(AppContext& context)
    {
        context.input.flush();
        m_elapsed = 0.0f;
    }

    const net::LobbySlot* NetPlayState::seatFor(PlayerSlot slot) const
    {
        return m_session->lobby().find(slot);
    }

    Color NetPlayState::colourFor(PlayerSlot slot) const
    {
        if (const net::LobbySlot* seat = seatFor(slot); seat != nullptr)
            return ui::playerColourAt(seat->colourIndex);
        return Color::Silver;
    }

    void NetPlayState::sendDirectionInput(const Input& input)
    {
        // One message per press, in the order they were pressed, so a fast
        // double-tap round a corner survives the trip to the host instead of
        // being collapsed into whichever key happened to be checked last.
        const auto forward = [this](Direction direction)
        {
            net::InputCommand command;
            command.hasDirection = true;
            command.direction = direction;
            m_session->sendInput(command);
        };

        if (input.pressed(Action::Up))    forward(Direction::Up);
        if (input.pressed(Action::Down))  forward(Direction::Down);
        if (input.pressed(Action::Left))  forward(Direction::Left);
        if (input.pressed(Action::Right)) forward(Direction::Right);

        if (input.pressed(Action::Ability))
        {
            net::InputCommand command;
            command.ability = true;
            m_session->sendInput(command);
        }
    }

    Transition NetPlayState::update(AppContext& context, float deltaSeconds)
    {
        m_elapsed += deltaSeconds;
        m_session->update(deltaSeconds);

        const net::SessionPhase phase = m_session->phase();

        if (phase == net::SessionPhase::Disconnected || phase == net::SessionPhase::Idle)
        {
            // The host quit, or the connection went. Acknowledge and fall back
            // to the lobby screen, which shows the reason and offers the way out.
            if (context.input.anyKeyPressed())
                return Transition::pop();
            return Transition::none();
        }

        if (phase == net::SessionPhase::InLobby)
            return Transition::pop();

        if (phase == net::SessionPhase::PostMatch)
        {
            if (context.input.pressed(Action::Confirm) || context.input.pressed(Action::Back) ||
                context.input.mouseClicked())
            {
                // The host reopens the lobby for everybody; a guest only asks.
                // Either way this screen closes when the session says so.
                m_session->returnToLobby();
                if (m_session->isHost())
                    return Transition::pop();
            }
            return Transition::none();
        }

        // There is deliberately no pause: pausing a four-player match would mean
        // pausing everybody, which is a host power this mode does not need.
        // Leaving is a two-step exit through the lobby screen instead.
        if (context.input.pressed(Action::Back))
            return Transition::pop();

        sendDirectionInput(context.input);
        return Transition::none();
    }

    // ------------------------------------------------------------------ render --

    void NetPlayState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        screen.clear(Color::Black);

        const net::SessionPhase phase = m_session->phase();

        if (phase == net::SessionPhase::Disconnected || phase == net::SessionPhase::Idle)
        {
            renderDisconnected(context);
            return;
        }

        renderScoreboard(context);
        renderBoard(context);

        const MatchSnapshot& snapshot = m_session->snapshot();

        if (snapshot.phase == MatchPhase::Countdown)
            renderCountdown(context);

        if (phase == net::SessionPhase::PostMatch)
        {
            renderResults(context);
            return;
        }

        // --- footer -----------------------------------------------------------
        screen.fillRect(0, ui::kFooterY, screen.width(), 1, glyph::Space, Color::Silver, Color::Black);

        const SnakeSnapshot* mine = snapshot.find(m_session->localSlot());

        int x = 6;
        x += ui::keyHint(screen, x, ui::kFooterY, L"WASD", L"Move", Color::Black) + 3;

        if (mine != nullptr)
        {
            const net::LobbySlot* seat = seatFor(m_session->localSlot());
            const std::wstring ability = seat != nullptr
                ? snakeTypeAt(seat->typeIndex).ability.name
                : std::wstring(L"Ability");

            const bool ready = mine->abilityCharge >= 1.0f;
            x += ui::keyHint(screen, x, ui::kFooterY, L"SPACE",
                ready ? ability : (ability + L" (charging)"), Color::Black) + 3;
        }

        ui::keyHint(screen, x, ui::kFooterY, L"ESC", L"Leave", Color::Black);
    }

    void NetPlayState::renderScoreboard(AppContext& context) const
    {
        Screen& screen = context.screen;
        const MatchSnapshot& snapshot = m_session->snapshot();
        const net::LobbyInfo& lobby = m_session->lobby();

        screen.fillRect(0, 0, screen.width(), 3, glyph::Space, Color::Silver, Color::Navy.scaled(0.7f));

        // --- clock, on a row of its own ---------------------------------------
        // The player columns own rows one and two. The clock had been centred on
        // row zero across the full width, which put it straight through whichever
        // column happened to sit at the middle of the screen.
        const bool countdown = snapshot.phase == MatchPhase::Countdown;
        const std::wstring clock = countdown
            ? L"STARTING"
            : formatClock(snapshot.phaseRemaining);

        // Under ten seconds the clock goes amber and pulses, which is the only
        // warning a player gets that the match is about to end.
        const bool urgent = !countdown && snapshot.phaseRemaining <= 10.0f;
        const Color clockColour = urgent
            ? (static_cast<int>(m_elapsed * 4.0f) % 2 == 0 ? Color::Red : Color::Amber)
            : Color::Gold;

        screen.text(2, 0, L"CODE " + lobby.code, Color::Slate, Color::Transparent);
        screen.textCenteredIn(0, screen.width(), 0, clock, clockColour, Color::Transparent);

        // Top right, out of the way of the four player columns but never off
        // screen.
        //
        // A host has no round trip to itself, so showing it a zero would read as
        // a suspiciously good ping rather than as "you are the far end". What is
        // useful to a host is the worst trip anybody else is making, because
        // that is the number deciding whether the match feels fair.
        if (Settings::instance().showPing)
        {
            std::wstring text;
            Color colour = Color::Lime;

            if (m_session->isHost())
            {
                int worst = -1;
                for (const net::LobbySlot& seat : m_session->lobby().slots)
                {
                    if (seat.occupied && !seat.isHost)
                        worst = std::max(worst, static_cast<int>(seat.pingMs));
                }

                text = worst <= 0 ? std::wstring(L"HOST")
                                  : L"WORST " + ui::pingText(worst);
                colour = worst <= 0 ? Color::Lime : ui::pingColour(worst);
            }
            else
            {
                const int ping = m_session->pingMs();
                text = L"PING " + ui::pingText(ping);
                colour = ui::pingColour(ping);
            }

            screen.text(screen.width() - static_cast<int>(text.size()) - 2, 0, text,
                colour, Color::Transparent);
        }

        // --- one column per seat ----------------------------------------------
        const int columns = std::max(1, static_cast<int>(lobby.maxPlayers));
        const int columnWidth = screen.width() / columns;

        for (int i = 0; i < columns; ++i)
        {
            const net::LobbySlot& seat = lobby.slots[static_cast<std::size_t>(i)];
            if (!seat.occupied)
                continue;

            const int x = i * columnWidth + 2;
            const int width = columnWidth - 4;

            const SnakeSnapshot* snake = snapshot.find(seat.slot);
            const Color accent = ui::playerColourAt(seat.colourIndex);
            const bool isLocal = seat.slot == m_session->localSlot();

            // The local player's name is boxed in their own colour so a glance
            // at the scoreboard finds "me" without reading four names.
            const std::wstring name = ui::truncateTo(seat.name, width);

            if (isLocal)
            {
                // Only as wide as the name plus a cell either side. Filling the
                // whole column reads as a coloured bar rather than a name tag.
                const int tag = static_cast<int>(name.size());
                screen.fillRect(x - 1, 1, tag + 2, 1, glyph::Space, Color::Black, accent);
                screen.text(x, 1, name, Color::Black, accent);
            }
            else
            {
                screen.text(x, 1, name, accent, Color::Transparent);
            }

            // Every player's ping, next to their name. Seeing that the player
            // who keeps cutting you off is 300 ms away explains a match in a way
            // that seeing only your own number never does.
            if (width > 6 && Settings::instance().showPing)
            {
                const int theirPing = seat.isHost ? 0 : static_cast<int>(seat.pingMs);
                const std::wstring text = seat.isHost ? std::wstring(L"HOST") : ui::pingText(theirPing);
                screen.text(x + width - static_cast<int>(text.size()), 1, text,
                    seat.isHost ? Color::Slate : ui::pingColour(theirPing), Color::Transparent);
            }

            if (snake == nullptr)
                continue;

            // Score and kills sit on the same row as the status, so the whole
            // column is two rows tall and the four of them never collide.
            screen.text(x, 2, ui::padTo(std::to_wstring(snake->score), 6), Color::White, Color::Transparent);

            const std::wstring kd = std::to_wstring(snake->kills) + L"/" + std::to_wstring(snake->deaths);
            screen.text(x + 7, 2, ui::padTo(kd, 5), Color::Slate.scaled(1.4f), Color::Transparent);

            const int statusX = x + 13;
            const int statusWidth = std::max(0, width - 13);
            if (statusWidth <= 0)
                continue;

            if (!snake->alive)
            {
                const std::wstring respawn = L"DOWN " +
                    std::to_wstring(std::max(1, static_cast<int>(snake->respawnRemaining + 0.99f)));
                screen.text(statusX, 2, ui::truncateTo(respawn, statusWidth), Color::Red, Color::Transparent);
            }
            else if (snake->shielded)
            {
                screen.text(statusX, 2, ui::truncateTo(L"SHIELD", statusWidth), Color::Blue, Color::Transparent);
            }
            else if (snake->phasing)
            {
                screen.text(statusX, 2, ui::truncateTo(L"PHASE", statusWidth), Color::Aqua, Color::Transparent);
            }
            else
            {
                ui::progressBar(screen, statusX, 2, std::min(8, statusWidth), snake->abilityCharge,
                    accent, Color::Slate, Color::Transparent);
            }
        }
    }

    void NetPlayState::renderBoard(AppContext& context) const
    {
        Screen& screen = context.screen;
        const Level& arena = m_session->arena();
        const MatchSnapshot& snapshot = m_session->snapshot();

        const ui::BoardView view{ ui::kBoardPixelX, ui::kBoardPixelY, ui::kTilePixels };

        const float frame = ui::kBoardFrameThickness;
        screen.rect(view.originX - frame, view.originY - frame,
            ui::kBoardPixelWidth + frame * 2.0f, ui::kBoardPixelHeight + frame * 2.0f, Color::Slate);
        screen.rect(view.originX, view.originY, ui::kBoardPixelWidth, ui::kBoardPixelHeight, Color::Navy);

        const std::wstring caption = arena.archetypeName + L"  SEED " +
            std::to_wstring(arena.seed % 100000ull);
        screen.text(ui::kScreenWidth - static_cast<int>(caption.size()) - 4, ui::kBoardCaptionRow,
            caption, Color::Slate, Color::Transparent);

        // --- walls a shield has opened ---------------------------------------
        const std::size_t tiles = static_cast<std::size_t>(arena.width()) * static_cast<std::size_t>(arena.height());
        m_openedMask.assign(tiles, false);
        for (const Vec2& opened : snapshot.openedWalls)
        {
            if (!arena.inBounds(opened))
                continue;
            m_openedMask[static_cast<std::size_t>(opened.y) * static_cast<std::size_t>(arena.width()) +
                static_cast<std::size_t>(opened.x)] = true;
        }

        // --- geometry ---------------------------------------------------------
        for (int y = 0; y < arena.height(); ++y)
        {
            for (int x = 0; x < arena.width(); ++x)
            {
                const Vec2 tile{ x, y };

                if (arena.isBorder(tile))
                {
                    ui::boardTile(screen, view, tile, Color::Blue.scaled(0.55f));
                    continue;
                }

                const std::size_t index = static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(arena.width()) + static_cast<std::size_t>(x);

                if (arena.isWall(tile) && !m_openedMask[index])
                {
                    ui::boardTile(screen, view, tile, Color::Slate);

                    if (!arena.isWall({ x, y - 1 }))
                        screen.rect(view.left(x), view.top(y), view.tileSize, 3.0f, Color::Slate.scaled(1.55f));
                    if (!arena.isWall({ x, y + 1 }))
                        screen.rect(view.left(x), view.top(y) + view.tileSize - 3.0f, view.tileSize, 3.0f,
                            Color::Slate.scaled(0.62f));
                    continue;
                }

                const std::uint32_t hash = static_cast<std::uint32_t>(x) * 73856093u ^
                    static_cast<std::uint32_t>(y) * 19349663u ^
                    static_cast<std::uint32_t>(arena.seed);
                if ((hash % 41u) == 0u)
                    ui::boardGlyph(screen, view, tile, glyph::Dot, Color::Slate.scaled(1.2f), 0.5f);
            }
        }

        const sf::Texture* foodArt = screen.textures().getEmissive("objects/food_normal.png", 22);
        const sf::Texture* bonusArt = screen.textures().getEmissive("objects/food_bonus.png", 22);
        const sf::Texture* sentinelArt = screen.textures().getEmissive("objects/sentinel.png", 22);

        const auto boardSprite = [&](const sf::Texture& texture, Vec2 tile, float scale)
        {
            const float size = view.tileSize * scale;
            const float offset = (view.tileSize - size) * 0.5f;
            screen.sprite(texture, view.left(tile.x) + offset, view.top(tile.y) + offset,
                size, size, Screen::SpriteLayer::World);
        };

        // --- food -------------------------------------------------------------
        for (const FoodSnapshot& food : snapshot.food)
        {
            if (food.kind == FoodKind::Normal)
            {
                const float pulse = 0.80f + 0.12f * std::sin(m_elapsed * 4.0f);
                screen.glow(view.centreX(static_cast<float>(food.position.x)),
                    view.centreY(static_cast<float>(food.position.y)),
                    view.tileSize * 0.95f, Color::Coral, 1.1f);

                if (foodArt != nullptr)
                    boardSprite(*foodArt, food.position, 1.7f);
                else
                    ui::boardGlyph(screen, view, food.position, glyph::Circle, Color::Coral, pulse * 0.8f);
            }
            else
            {
                const bool urgent = food.secondsRemaining < 2.5f;
                if (urgent && static_cast<int>(food.secondsRemaining * 8.0f) % 2 != 0)
                    continue;

                screen.glow(view.centreX(static_cast<float>(food.position.x)),
                    view.centreY(static_cast<float>(food.position.y)),
                    view.tileSize * 1.2f, Color::Gold, 1.4f);

                if (bonusArt != nullptr)
                    boardSprite(*bonusArt, food.position, 1.8f);
                else
                    ui::boardGlyph(screen, view, food.position, glyph::Star, Color::Gold, 1.0f);
            }
        }

        // --- hazards ----------------------------------------------------------
        for (const Vec2& sentinel : snapshot.sentinels)
        {
            screen.glow(view.centreX(static_cast<float>(sentinel.x)),
                view.centreY(static_cast<float>(sentinel.y)),
                view.tileSize, Color::Red, 1.2f);

            if (sentinelArt != nullptr)
                boardSprite(*sentinelArt, sentinel, 1.6f);
            else
                ui::boardGlyph(screen, view, sentinel, glyph::Diamond, Color::Red, 0.95f);
        }

        // --- snakes -----------------------------------------------------------
        constexpr float kSegmentInset = 2.0f;

        for (const SnakeSnapshot& snake : snapshot.snakes)
        {
            if (!snake.alive || snake.body.empty())
                continue;

            const net::LobbySlot* seat = seatFor(snake.slot);
            const SnakeType& type = snakeTypeAt(seat != nullptr ? seat->typeIndex : 0);

            const Color bodyColour = snake.phasing ? Color::Aqua : colourFor(snake.slot);
            const Color headColour = snake.shielded ? Color::Blue : Color::White;
            const float alpha = snake.phasing ? 0.45f : 1.0f;

            for (std::size_t i = snake.body.size(); i-- > 0; )
            {
                const bool isHead = i == 0;
                const std::size_t fromTail = snake.body.size() - 1 - i;

                Color colour = isHead ? headColour : bodyColour;
                if (type.altBodyGlyph != 0 && !isHead && fromTail % 2 == 0)
                    colour = colour.scaled(0.72f);
                if (!isHead && fromTail == 0)
                    colour = colour.scaled(0.62f);
                colour = colour.withAlpha(static_cast<std::uint8_t>(alpha * 255.0f));

                ui::boardTile(screen, view, snake.body[i], colour, kSegmentInset);

                if (isHead)
                {
                    screen.glowRect(view.left(snake.body[i].x), view.top(snake.body[i].y),
                        view.tileSize, view.tileSize, headColour, view.tileSize * 0.9f, 1.3f);
                }
                else if (fromTail % 2 == 0)
                {
                    screen.glowRect(view.left(snake.body[i].x) + kSegmentInset,
                        view.top(snake.body[i].y) + kSegmentInset,
                        view.tileSize - kSegmentInset * 2.0f, view.tileSize - kSegmentInset * 2.0f,
                        bodyColour, view.tileSize * 0.35f, 0.55f);
                }
            }
        }
    }

    void NetPlayState::renderCountdown(AppContext& context) const
    {
        Screen& screen = context.screen;
        const MatchSnapshot& snapshot = m_session->snapshot();

        const int seconds = std::max(1, static_cast<int>(snapshot.phaseRemaining + 0.99f));

        ui::drawBannerCentered(screen, 15, std::to_wstring(seconds), Color::Gold, Color::Transparent, 3, 2, 1);
        screen.textCentered(23, L"F I N D   Y O U R   L I N E", Color::Aqua, Color::Transparent);
    }

    void NetPlayState::renderResults(AppContext& context) const
    {
        Screen& screen = context.screen;
        const MatchResult& result = m_session->result();

        const int width = 60;
        const int height = 22;
        const int x = (screen.width() - width) / 2;
        const int y = (screen.height() - height) / 2;

        screen.panel(x, y, width, height, Color::Gold, Color::Black);

        const bool localWon = !result.draw && result.winner == m_session->localSlot();
        const wchar_t* headline = result.draw ? L"DRAW" : (localWon ? L"YOU WIN" : L"MATCH OVER");

        ui::drawBannerCentered(screen, y + 2, headline,
            localWon ? Color::Lime : Color::Gold, Color::Black, 1, 1, 1);

        const int inner = x + 4;
        const int innerWidth = width - 8;

        screen.text(inner, y + 9, L"PLAYER", Color::Slate, Color::Black);
        screen.text(inner + 20, y + 9, L"SCORE", Color::Slate, Color::Black);
        screen.text(inner + 30, y + 9, L"KILLS", Color::Slate, Color::Black);
        screen.text(inner + 40, y + 9, L"DEATHS", Color::Slate, Color::Black);
        screen.horizontalLine(inner, y + 10, innerWidth, glyph::ThinH, Color::Slate, Color::Black);

        int row = y + 11;
        for (std::size_t i = 0; i < result.standings.size() && row < y + height - 3; ++i, ++row)
        {
            const MatchStanding& standing = result.standings[i];
            const bool isLocal = standing.slot == m_session->localSlot();
            const Color accent = ui::playerColourAt(standing.colourIndex);

            const std::wstring place = std::to_wstring(i + 1) + L".";
            screen.text(inner - 3, row, place, i == 0 ? Color::Gold : Color::Slate, Color::Black);

            screen.text(inner, row, ui::truncateTo(standing.name, 18),
                isLocal ? Color::White : accent, Color::Black);
            screen.text(inner + 20, row, std::to_wstring(standing.score), Color::Gold, Color::Black);
            screen.text(inner + 30, row, std::to_wstring(standing.kills), Color::Silver, Color::Black);
            screen.text(inner + 40, row, std::to_wstring(standing.deaths), Color::Silver, Color::Black);
        }

        const wchar_t* hint = m_session->isHost()
            ? L"ENTER reopens the lobby for everybody"
            : L"Waiting for the host to reopen the lobby";
        screen.textCenteredIn(x, width, y + height - 2, hint, Color::Slate, Color::Black);
    }

    void NetPlayState::renderDisconnected(AppContext& context) const
    {
        Screen& screen = context.screen;

        ui::drawBannerCentered(screen, 14, L"DISCONNECTED", Color::Red, Color::Transparent, 2, 1, 1);
        screen.textCentered(21, ui::truncateTo(m_session->status(), screen.width() - 8),
            Color::Amber, Color::Transparent);
        screen.textCentered(ui::kFooterY, L"Press any key", Color::Slate, Color::Black);
    }
}
