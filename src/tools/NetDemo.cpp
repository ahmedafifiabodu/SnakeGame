#include "NetDemo.h"

#include "../core/Rng.h"
#include "../net/ClientSession.h"
#include "../net/HostSession.h"
#include "../net/NetConfig.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <vector>

namespace neoncoil::tools
{
    namespace
    {
        // Well away from the shipping default, so capturing while a real
        // session is open on this machine does not collide with it.
        constexpr std::uint16_t kDemoPort = 45790;


        constexpr std::array<const wchar_t*, 4> kNames = { L"ALFA", L"BRAVO", L"CHARLIE", L"DELTA" };

        // Distinct identities so the lobby shows four different names. Also a
        // second demonstration that the session layer runs against any provider.
        class DemoIdentity : public net::IIdentityProvider
        {
        public:
            DemoIdentity(std::string id, std::wstring name)
            {
                m_identity.id = std::move(id);
                m_identity.displayName = std::move(name);
                m_identity.authenticated = false;
            }

            net::PlayerIdentity local() const override { return m_identity; }
            void setDisplayName(const std::wstring&) override {}

            net::JoinTicket issueTicket() const override
            {
                net::JoinTicket ticket;
                ticket.identityId = m_identity.id;
                ticket.displayName = m_identity.displayName;
                return ticket;
            }

            bool verify(const net::JoinTicket& ticket, net::PlayerIdentity& out,
                std::wstring& reason) const override
            {
                if (ticket.identityId.empty())
                {
                    reason = L"empty identity";
                    return false;
                }
                out.id = ticket.identityId;
                out.displayName = ticket.displayName;
                out.authenticated = false;
                return true;
            }

            const wchar_t* backendName() const override { return L"DEMO"; }

        private:
            net::PlayerIdentity m_identity;
        };

        bool tileIsFatal(const Level& arena, const MatchSnapshot& snapshot, Vec2 tile)
        {
            if (arena.isBorder(tile) || arena.isWall(tile))
                return true;

            for (const SnakeSnapshot& snake : snapshot.snakes)
            {
                if (!snake.alive)
                    continue;

                // The tail vacates as the snake steps, so treating it as solid
                // makes the demo snakes needlessly timid in tight corridors.
                for (std::size_t i = 0; i + 1 < snake.body.size(); ++i)
                    if (snake.body[i] == tile)
                        return true;
            }

            for (const Vec2& sentinel : snapshot.sentinels)
                if (sentinel == tile)
                    return true;

            return false;
        }

        // How far a direction stays clear, up to a cap. Chasing food on the
        // nearest-tile test alone walks these snakes straight into pockets and
        // the capture ends up all corpses; a few tiles of look-ahead is the
        // difference between a lively board and an empty one.
        int runway(const Level& arena, const MatchSnapshot& snapshot, Vec2 head, Direction direction)
        {
            const Vec2 step = toDelta(direction);
            int clear = 0;

            for (int i = 1; i <= 8; ++i)
            {
                if (tileIsFatal(arena, snapshot, head + step * i))
                    break;
                ++clear;
            }

            return clear;
        }

        // A tile another snake's head could step onto next. Not fatal, but four
        // snakes all converging on one fruit is exactly how a capture ends up
        // being four corpses, so it is worth a heavy penalty.
        bool contested(const MatchSnapshot& snapshot, const SnakeSnapshot& me, Vec2 tile)
        {
            for (const SnakeSnapshot& snake : snapshot.snakes)
            {
                if (!snake.alive || snake.slot == me.slot || snake.body.empty())
                    continue;

                if (manhattan(snake.body.front(), tile) <= 1)
                    return true;
            }

            return false;
        }

        // Head for a fruit, but never into something lethal and never down a
        // corridor that ends. Simple on purpose: this exists to make a capture
        // look alive, not to play well.
        std::optional<Direction> steer(const Level& arena, const MatchSnapshot& snapshot,
            const SnakeSnapshot& me, Rng& rng)
        {
            if (!me.alive || me.body.empty())
                return std::nullopt;

            const Vec2 head = me.body.front();

            // Each snake prefers a different fruit, offset by its seat. All four
            // chasing the nearest one put them on the same tile at the same
            // moment, which is a collision rather than a game.
            std::vector<const FoodSnapshot*> byDistance;
            byDistance.reserve(snapshot.food.size());
            for (const FoodSnapshot& food : snapshot.food)
                byDistance.push_back(&food);

            std::sort(byDistance.begin(), byDistance.end(),
                [head](const FoodSnapshot* a, const FoodSnapshot* b)
                {
                    return manhattan(head, a->position) < manhattan(head, b->position);
                });

            const Vec2* target = nullptr;
            if (!byDistance.empty())
            {
                const std::size_t pick = static_cast<std::size_t>(me.slot) % byDistance.size();
                target = &byDistance[pick]->position;
            }

            constexpr std::array<Direction, 4> kAll = {
                Direction::Up, Direction::Down, Direction::Left, Direction::Right
            };

            Direction best = me.direction;
            int bestScore = -1;

            for (Direction direction : kAll)
            {
                if (isOpposite(direction, me.direction))
                    continue;   // the snake would refuse it anyway

                const Vec2 next = head + toDelta(direction);
                if (tileIsFatal(arena, snapshot, next))
                    continue;

                // Room to move dominates; closing on food breaks the ties. A
                // little noise stops four snakes with the same rules from
                // marching in formation.
                int score = runway(arena, snapshot, head, direction) * 10;

                if (target != nullptr)
                    score += (arena.width() + arena.height()) - manhattan(next, *target);

                if (contested(snapshot, me, next))
                    score -= 60;

                score += rng.range(0, 3);

                // Carrying straight on when it is no worse keeps the movement
                // readable instead of twitching at every junction.
                if (direction == me.direction)
                    score += 4;

                if (score > bestScore)
                {
                    bestScore = score;
                    best = direction;
                }
            }

            if (bestScore < 0)
                return std::nullopt;   // boxed in; nothing to be done about it

            return best;
        }
    }

    struct NetDemo::Impl
    {
        net::NetConfig config;
        std::unique_ptr<DemoIdentity> hostIdentity;

        // Owned until a screen takes it; `host` stays valid either way, because
        // steering the guests still needs to see the session.
        std::unique_ptr<net::HostSession> ownedHost;
        net::HostSession* host{ nullptr };

        std::vector<std::unique_ptr<DemoIdentity>> guestIdentities;
        std::vector<std::unique_ptr<net::ClientSession>> guests;

        std::vector<Vec2> lastHeads;

        Rng rng{ 0 };

        // Before a screen owns the host, nothing else is pumping it.
        void pumpAll(float deltaSeconds)
        {
            if (host != nullptr && ownedHost != nullptr)
                host->update(deltaSeconds);
        }
    };

    NetDemo::NetDemo()
        : m_impl(std::make_unique<Impl>())
    {
    }

    NetDemo::~NetDemo()
    {
        stop();
    }

    net::NetGame* NetDemo::host() const
    {
        return m_impl->host;
    }

    std::unique_ptr<net::HostSession> NetDemo::takeHost()
    {
        return std::move(m_impl->ownedHost);
    }

    bool NetDemo::start(int guests, bool readyUp, std::wstring& error)
    {
        guests = std::clamp(guests, 0, kMaxMatchPlayers - 1);

        Impl& impl = *m_impl;
        impl.config = net::NetConfig::instance();
        impl.config.hostPort = kDemoPort;
        impl.config.advertiseOnLan = false;      // a capture should not beacon
        impl.config.rules.countdownSeconds = 0.6f;
        impl.config.rules.respawnSeconds = 1.0f;   // a capture should not be all corpses
        impl.rng.reseed(0xC0FFEEull);

        impl.lastHeads.assign(1, Vec2{ -1, -1 });   // slot zero is the host
        impl.hostIdentity = std::make_unique<DemoIdentity>("demo-host", kNames[0]);
        impl.ownedHost = std::make_unique<net::HostSession>(impl.config, *impl.hostIdentity);
        impl.host = impl.ownedHost.get();

        if (!impl.host->open(kNames[0], 0, 0, net::HostSession::Reach::Direct, error))
        {
            impl.ownedHost.reset();
            impl.host = nullptr;
            return false;
        }

        for (int i = 0; i < guests; ++i)
        {
            auto identity = std::make_unique<DemoIdentity>(
                "demo-guest-" + std::to_string(i), kNames[static_cast<std::size_t>(i + 1)]);

            auto guest = std::make_unique<net::ClientSession>(impl.config, *identity);

            std::wstring guestError;
            guest->connect("127.0.0.1", kDemoPort, kNames[static_cast<std::size_t>(i + 1)],
                static_cast<std::uint8_t>((i + 1) * 2),   // spread the colours apart
                static_cast<std::uint8_t>(i + 1),         // and the snake types
                guestError);

            impl.guestIdentities.push_back(std::move(identity));
            impl.guests.push_back(std::move(guest));
            impl.lastHeads.push_back(Vec2{ -1, -1 });
        }

        // Let the handshake finish before anybody photographs the lobby. Real
        // time, because these are real sockets.
        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::seconds(5);

        while (Clock::now() < deadline)
        {
            impl.pumpAll(1.0f / 120.0f);
            tick(1.0f / 120.0f);

            if (impl.host->lobby().occupiedCount() >= guests + 1)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }

        // A guest that has a seat on the host has not necessarily processed its
        // own Welcome yet, and setReady is ignored until it has. Wait for the
        // client side to catch up before asking it to ready.
        const auto everyGuestInLobby = [&impl]
        {
            for (const std::unique_ptr<net::ClientSession>& guest : impl.guests)
                if (guest->phase() != net::SessionPhase::InLobby)
                    return false;
            return true;
        };

        const auto settleDeadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < settleDeadline && !everyGuestInLobby())
        {
            impl.pumpAll(1.0f / 120.0f);
            tick(1.0f / 120.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }

        if (readyUp)
        {
            for (std::unique_ptr<net::ClientSession>& guest : impl.guests)
                guest->setReady(true);
        }
        else if (!impl.guests.empty())
        {
            // One seat left un-readied on purpose: the lobby screenshot should
            // show both states, because that is what a real one looks like.
            for (std::size_t i = 0; i + 1 < impl.guests.size(); ++i)
                impl.guests[i]->setReady(true);
        }

        for (int i = 0; i < 30; ++i)
        {
            impl.pumpAll(1.0f / 120.0f);
            tick(1.0f / 120.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        return true;
    }

    void NetDemo::startMatch()
    {
        Impl& impl = *m_impl;
        if (!impl.host)
            return;

        for (std::unique_ptr<net::ClientSession>& guest : impl.guests)
            guest->setReady(true);

        for (int i = 0; i < 60 && !impl.host->canStartMatch(); ++i)
        {
            impl.pumpAll(1.0f / 120.0f);
            tick(1.0f / 120.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        impl.host->requestStartMatch();
    }

    void NetDemo::tick(float deltaSeconds)
    {
        Impl& impl = *m_impl;
        if (!impl.host)
            return;

        // Re-steer when the snake has actually moved, rather than on a timer:
        // the snake types run at different speeds, so any fixed interval is
        // either wasteful for the slow ones or too coarse for the fast ones.
        const auto driveOne = [&impl](net::NetGame& session, std::size_t index)
        {
            if (session.phase() != net::SessionPhase::InMatch)
                return;

            const MatchSnapshot& snapshot = session.snapshot();
            const SnakeSnapshot* me = snapshot.find(session.localSlot());

            if (me == nullptr || !me->alive || me->body.empty())
            {
                // Forget where the head was, so the first frame after a respawn
                // always re-steers instead of matching a stale position and
                // letting the new snake run straight on into whatever is ahead.
                if (index < impl.lastHeads.size())
                    impl.lastHeads[index] = Vec2{ -1, -1 };
                return;
            }

            const Vec2 head = me->body.front();
            if (index < impl.lastHeads.size() && impl.lastHeads[index] == head)
                return;

            if (index < impl.lastHeads.size())
                impl.lastHeads[index] = head;

            if (const std::optional<Direction> turn = steer(session.arena(), snapshot, *me, impl.rng))
            {
                net::InputCommand command;
                command.hasDirection = true;
                command.direction = *turn;
                session.sendInput(command);
            }
        };

        // The host plays too. Leaving it unsteered was why the first capture had
        // one live snake and three corpses.
        driveOne(*impl.host, 0);

        for (std::size_t i = 0; i < impl.guests.size(); ++i)
        {
            net::ClientSession& guest = *impl.guests[i];
            guest.update(deltaSeconds);
            driveOne(guest, i + 1);
        }
    }

    void NetDemo::stop()
    {
        Impl& impl = *m_impl;

        for (std::unique_ptr<net::ClientSession>& guest : impl.guests)
            if (guest)
                guest->shutdown();

        impl.guests.clear();
        impl.guestIdentities.clear();
        impl.lastHeads.clear();

        // Only tear the host down if a screen has not taken it: if one has, that
        // screen owns its lifetime and will have shut it down already.
        if (impl.ownedHost)
            impl.ownedHost->shutdown();
        impl.ownedHost.reset();
        impl.host = nullptr;
        impl.hostIdentity.reset();
    }
}
