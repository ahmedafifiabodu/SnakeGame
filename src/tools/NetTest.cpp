#include "NetTest.h"

#include "../game/LevelGenerator.h"
#include "../game/MatchSimulation.h"
#include "../net/ClientSession.h"
#include "../net/HostSession.h"
#include "../net/Matchmaker.h"
#include "../net/NetConfig.h"
#include "../relay/RelayServer.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace neoncoil
{
    namespace
    {
        int g_checks = 0;
        int g_failures = 0;

        void check(bool condition, const std::string& what)
        {
            ++g_checks;
            if (condition)
            {
                std::cout << "  ok    " << what << "\n";
                return;
            }

            ++g_failures;
            std::cout << "  FAIL  " << what << "\n";
        }

        std::string narrow(const std::wstring& text)
        {
            std::string out;
            out.reserve(text.size());
            for (wchar_t c : text)
                out.push_back(c < 128 ? static_cast<char>(c) : '?');
            return out;
        }

        // A stand-in for an account service, and the point of the exercise: the
        // whole session layer runs against a provider it has never seen before,
        // without a line of it changing. This is the seam authentication will
        // arrive through.
        class FixedIdentityProvider : public net::IIdentityProvider
        {
        public:
            FixedIdentityProvider(std::string id, std::wstring name, bool authenticated)
            {
                m_identity.id = std::move(id);
                m_identity.displayName = std::move(name);
                m_identity.authenticated = authenticated;
            }

            net::PlayerIdentity local() const override { return m_identity; }
            void setDisplayName(const std::wstring&) override {}

            net::JoinTicket issueTicket() const override
            {
                net::JoinTicket ticket;
                ticket.identityId = m_identity.id;
                ticket.displayName = m_identity.displayName;
                ticket.proof = m_identity.authenticated ? "signed-by-test" : "";
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

                // Exactly where a real provider would validate a signature.
                out.authenticated = !ticket.proof.empty();
                return true;
            }

            const wchar_t* backendName() const override { return L"TEST"; }

        private:
            net::PlayerIdentity m_identity;
        };

        // When set, every pump also drives a relay running in this process, so
        // the relayed half of the test needs no second binary and no daemon.
        relay::RelayServer* g_relay = nullptr;

        // Drives every session forward in real time until `done` says so. Real
        // time, not simulated: these are real sockets, and a test that pumped a
        // thousand zero-length frames would prove nothing about them.
        bool pumpUntil(const std::vector<net::NetGame*>& sessions,
            const std::function<bool()>& done, float timeoutSeconds)
        {
            using Clock = std::chrono::steady_clock;
            const auto deadline = Clock::now() + std::chrono::milliseconds(
                static_cast<int>(timeoutSeconds * 1000.0f));

            constexpr float kStep = 1.0f / 120.0f;

            while (Clock::now() < deadline)
            {
                if (g_relay != nullptr)
                {
                    g_relay->pump();
                    g_relay->tickTimers(kStep);
                }

                for (net::NetGame* session : sessions)
                    session->update(kStep);

                if (done())
                    return true;

                std::this_thread::sleep_for(std::chrono::milliseconds(2));

                // One more pass after the sleep, so a reply that lands during it
                // is not left sitting in a socket until the next iteration.
                if (g_relay != nullptr)
                    g_relay->pump();
            }

            return done();
        }

        void pumpFor(const std::vector<net::NetGame*>& sessions, float seconds)
        {
            (void)pumpUntil(sessions, [] { return false; }, seconds);
        }
    }

    int runNetworkSelfTest()
    {
        std::cout << "NEON COIL network self-test\n"
                     "  loopback host + three clients, one full match\n\n";

        net::NetConfig config = net::NetConfig::instance();

        // A port well away from the shipping default, so running this while a
        // real session is open on this machine does not collide with it.
        config.hostPort = 45799;
        config.discoveryPort = 45798;
        config.advertiseOnLan = true;

        // A short match: the point is to watch it start, run and finish, not to
        // sit through three minutes of it.
        config.rules.countdownSeconds = 0.5f;
        config.rules.durationSeconds = 10.0f;
        config.rules.respawnSeconds = 1.0f;

        FixedIdentityProvider hostIdentity("acct-host", L"HOSTER", true);
        FixedIdentityProvider guest1("acct-one", L"ONE", true);
        FixedIdentityProvider guest2("acct-two", L"TWO", true);
        FixedIdentityProvider guest3("acct-three", L"THREE", true);
        FixedIdentityProvider guest4("acct-four", L"FOUR", true);
        FixedIdentityProvider duplicate("acct-one", L"CLONE", true);

        // ------------------------------------------------------------- hosting --
        std::cout << "opening the session\n";

        auto host = std::make_unique<net::HostSession>(config, hostIdentity);
        std::wstring error;

        if (!host->open(L"HOSTER", 0, 0, net::HostSession::Reach::Direct, error))
        {
            std::cout << "  FAIL  could not open a session: " << narrow(error) << "\n";
            return 1;
        }

        check(host->phase() == net::SessionPhase::InLobby, "host opens straight into a lobby");
        check(host->lobby().occupiedCount() == 1, "host occupies seat one");
        check(!host->lobby().code.empty(), "session has a join code");

        std::vector<net::NetGame*> all{ host.get() };

        // ------------------------------------------------------------- joining --
        std::cout << "\nthree clients joining\n";

        auto makeClient = [&](net::IIdentityProvider& identity, const std::wstring& name,
            std::uint8_t colour, std::uint8_t type)
        {
            auto client = std::make_unique<net::ClientSession>(config, identity);
            std::wstring clientError;
            client->connect("127.0.0.1", config.hostPort, name, colour, type, clientError);
            return client;
        };

        auto clientOne = makeClient(guest1, L"ONE", 1, 1);
        auto clientTwo = makeClient(guest2, L"TWO", 2, 2);
        auto clientThree = makeClient(guest3, L"THREE", 3, 3);

        all.push_back(clientOne.get());
        all.push_back(clientTwo.get());
        all.push_back(clientThree.get());

        const bool everyoneIn = pumpUntil(all, [&]
        {
            return host->lobby().occupiedCount() == 4 &&
                clientOne->phase() == net::SessionPhase::InLobby &&
                clientTwo->phase() == net::SessionPhase::InLobby &&
                clientThree->phase() == net::SessionPhase::InLobby;
        }, 10.0f);

        check(everyoneIn, "all three clients reach the lobby");
        check(host->lobby().occupiedCount() == 4, "host sees four occupied seats");
        check(clientOne->localSlot() != kInvalidSlot, "client one was given a seat");
        check(clientOne->lobby().occupiedCount() == 4, "clients see the same four seats");
        check(clientTwo->lobby().slots[0].isHost, "clients can tell which seat is the host");

        // ----------------------------------------------------------- a full lobby --
        std::cout << "\nrefusing a fifth player\n";

        auto clientFour = makeClient(guest4, L"FOUR", 4, 4);
        all.push_back(clientFour.get());

        const bool refused = pumpUntil(all, [&]
        {
            return clientFour->phase() == net::SessionPhase::Disconnected;
        }, 10.0f);

        check(refused, "a fifth player is refused rather than dropped silently");
        check(clientFour->status() == std::wstring(net::describe(net::RejectReason::LobbyFull)),
            "the refusal says the session is full");
        check(host->lobby().occupiedCount() == 4, "a refused join does not disturb the lobby");

        all.pop_back();
        clientFour.reset();

        // ------------------------------------------------- duplicate identity --
        std::cout << "\nrefusing the same account twice\n";

        auto clone = makeClient(duplicate, L"CLONE", 5, 0);
        all.push_back(clone.get());

        const bool cloneRefused = pumpUntil(all, [&]
        {
            return clone->phase() == net::SessionPhase::Disconnected;
        }, 10.0f);

        check(cloneRefused, "an authenticated identity cannot hold two seats");
        all.pop_back();
        clone.reset();

        // The mirror of the check above, and the reason it is conditional: four
        // copies of the game in one folder share one guest id file. If that were
        // treated as a duplicate account, the most common way anyone tests a
        // four-player session would be impossible.
        std::cout << "\nallowing two guests that share a local id\n";

        // Free two seats, then fill them with two clients presenting the SAME
        // unauthenticated id -- which is exactly what four copies of the game in
        // one folder do.
        clientTwo->shutdown();
        clientThree->shutdown();

        const bool seatsFreed = pumpUntil({ host.get(), clientOne.get() }, [&]
        {
            return host->lobby().occupiedCount() == 2;
        }, 10.0f);

        check(seatsFreed, "two guests leaving frees two seats");

        FixedIdentityProvider guestA("local-shared", L"GUEST A", false);
        FixedIdentityProvider guestB("local-shared", L"GUEST B", false);

        clientTwo = makeClient(guestA, L"GUEST A", 6, 0);
        clientThree = makeClient(guestB, L"GUEST B", 7, 0);

        all[2] = clientTwo.get();
        all[3] = clientThree.get();

        const bool bothGuestsIn = pumpUntil(all, [&]
        {
            return clientTwo->phase() == net::SessionPhase::InLobby &&
                clientThree->phase() == net::SessionPhase::InLobby;
        }, 10.0f);

        check(bothGuestsIn, "two guests sharing one local id both get seats");
        check(host->lobby().occupiedCount() == 4, "the lobby fills back up");

        // ------------------------------------------------------------- readying --
        std::cout << "\nreadying up\n";

        check(!host->canStartMatch(), "the host cannot start while players are not ready");

        clientOne->setReady(true);
        clientTwo->setReady(true);
        clientThree->setReady(true);

        const bool ready = pumpUntil(all, [&] { return host->canStartMatch(); }, 10.0f);
        check(ready, "the host can start once everybody is ready");

        // -------------------------------------------------------------- match --
        std::cout << "\nstarting the match\n";

        host->requestStartMatch();

        const bool started = pumpUntil(all, [&]
        {
            return clientOne->phase() == net::SessionPhase::InMatch &&
                clientTwo->phase() == net::SessionPhase::InMatch &&
                clientThree->phase() == net::SessionPhase::InMatch;
        }, 10.0f);

        check(started, "every client receives MatchStart");
        check(host->phase() == net::SessionPhase::InMatch, "the host enters the match");
        check(clientOne->arena().width() > 0, "clients rebuilt the arena from the wire");
        check(clientOne->arena().width() == host->arena().width() &&
              clientOne->arena().height() == host->arena().height(),
              "client and host arenas are the same size");

        {
            // The arena is sent as a bitset rather than regenerated from a seed,
            // so this has to match tile for tile, not merely approximately.
            bool identical = true;
            for (int y = 0; y < host->arena().height() && identical; ++y)
                for (int x = 0; x < host->arena().width() && identical; ++x)
                    identical = host->arena().isWall({ x, y }) == clientOne->arena().isWall({ x, y });

            check(identical, "every wall tile survives the trip intact");
        }

        const bool snapshots = pumpUntil(all, [&]
        {
            return clientOne->snapshot().snakes.size() == 4 &&
                clientOne->snapshot().phase == MatchPhase::Running;
        }, 10.0f);

        check(snapshots, "clients receive snapshots carrying all four snakes");

        // ---------------------------------------------------------- steering --
        std::cout << "\nsteering, and a player leaving mid-match\n";

        const std::uint32_t tickBefore = clientOne->snapshot().tick;

        net::InputCommand turn;
        turn.hasDirection = true;
        turn.direction = Direction::Up;
        clientOne->sendInput(turn);

        net::InputCommand fire;
        fire.ability = true;
        clientOne->sendInput(fire);

        pumpFor(all, 1.5f);

        check(clientOne->snapshot().tick > tickBefore, "the simulation advances");

        {
            bool anySnakeHasBody = false;
            for (const SnakeSnapshot& snake : clientOne->snapshot().snakes)
                anySnakeHasBody = anySnakeHasBody || !snake.body.empty();
            check(anySnakeHasBody, "snapshots carry snake bodies");
        }

        // A client leaving mid-match must not stall the other three.
        clientThree->shutdown();

        const bool leftCleanly = pumpUntil({ host.get(), clientOne.get(), clientTwo.get() }, [&]
        {
            return host->lobby().occupiedCount() == 3;
        }, 10.0f);

        check(leftCleanly, "a player leaving mid-match frees their seat");

        pumpFor({ host.get(), clientOne.get(), clientTwo.get() }, 1.0f);
        check(host->phase() == net::SessionPhase::InMatch, "the match carries on without them");
        check(clientOne->snapshot().snakes.size() == 3, "their snake is removed from the board");

        // --------------------------------------------------------- match end --
        std::cout << "\nplaying the match out\n";

        const bool finished = pumpUntil({ host.get(), clientOne.get(), clientTwo.get() }, [&]
        {
            return clientOne->phase() == net::SessionPhase::PostMatch &&
                clientTwo->phase() == net::SessionPhase::PostMatch;
        }, 20.0f);

        check(finished, "the match ends on the clock and clients are told");
        check(!clientOne->result().standings.empty(), "clients receive the final standings");
        check(clientOne->result().standings.size() == 3, "the standings cover everybody still playing");

        {
            bool sorted = true;
            const std::vector<MatchStanding>& standings = clientOne->result().standings;
            for (std::size_t i = 1; i < standings.size(); ++i)
                sorted = sorted && standings[i - 1].score >= standings[i].score;
            check(sorted, "the standings are ordered best first");
        }

        // ----------------------------------------------------- back to lobby --
        std::cout << "\nreturning to the lobby\n";

        host->returnToLobby();

        const bool backInLobby = pumpUntil({ host.get(), clientOne.get(), clientTwo.get() }, [&]
        {
            return clientOne->phase() == net::SessionPhase::InLobby &&
                clientTwo->phase() == net::SessionPhase::InLobby;
        }, 10.0f);

        check(backInLobby, "reopening the lobby brings every client back with it");
        check(!host->lobby().inMatch, "the lobby is joinable again");
        check(!host->lobby().slots[1].ready, "everybody has to ready up again");

        // ------------------------------------------------------ host quitting --
        std::cout << "\nthe host quitting\n";

        host->shutdown();

        const bool clientsTold = pumpUntil({ clientOne.get(), clientTwo.get() }, [&]
        {
            return clientOne->phase() == net::SessionPhase::Disconnected &&
                clientTwo->phase() == net::SessionPhase::Disconnected;
        }, 10.0f);

        check(clientsTold, "clients are told when the host closes the session");
        check(!clientOne->status().empty(), "and they are given a reason to show");

        clientOne.reset();
        clientTwo.reset();
        clientThree.reset();
        host.reset();

        // ----------------------------------------------------------- regions --
        //
        // With one relay a code is just a code. With several, it has to say
        // which relay minted it, or a guest typing one has no way to know where
        // to dial and the whole point of running relays in more than one region
        // collapses back to "everybody uses the first one in the list".
        std::cout << "\nregion-tagged codes\n";

        {
            const std::wstring tagged = net::makeRelayCode(0x1234567890ABCDEFull, L'M');
            const std::wstring plain = net::makeRelayCode(0x1234567890ABCDEFull);

            check(tagged.size() == 7, "a tagged code is seven characters");
            check(plain.size() == 6, "an untagged one is six, as before regions existed");
            check(tagged[0] == L'M', "the tag is the first character");
            check(tagged.substr(1) == plain, "and the rest of the code is unchanged by tagging");

            net::NetConfig regions;
            regions.relays.push_back({ "10.0.0.1", 45700, L"MIDDLE EAST", L'M' });
            regions.relays.push_back({ "10.0.0.2", 45700, L"EUROPE", L'E' });
            regions.selectRelay(0);

            check(regions.relayIndexForCode(tagged) == 0, "a code routes to the region that minted it");
            check(regions.relayIndexForCode(L'E' + plain) == 1, "and a different tag routes elsewhere");
            check(regions.relayIndexForCode(plain) == -1, "an untagged code names no region");
            check(regions.relayIndexForCode(L"Z" + plain) == -1, "nor does a tag this build does not have");

            regions.selectRelay(1);
            check(regions.relayHost == "10.0.0.2", "selecting a region is what a session then dials");
            check(regions.relayPort == 45700, "with its port");

            regions.selectRelay(99);
            check(regions.relayHost == "10.0.0.2", "an out-of-range choice is ignored rather than obeyed");

            // A code is read aloud and typed back in. The tag has to survive
            // that trip exactly as the rest of the code does.
            check(net::normaliseCode(L"m-4kp zw3") == L"M4KPZW3",
                "a tagged code survives being lowercased, spaced and hyphenated");
        }

        // ---------------------------------------------------------- spawning --
        //
        // Every spawn and every respawn has to leave the player room to react.
        // A snake cannot stop, so a wall one tile in front of a fresh head is
        // not a hard start, it is a death the player was never offered a say in
        // -- and over a relay their turn does not even reach the host in time to
        // be refused. The arena is generated at its densest band so the check is
        // made against the worst boards the game ships.
        std::cout << "\nspawning with room to react\n";

        {
            constexpr int kFloor = 3;      // MatchSimulation's own minimum
            constexpr int kBoards = 40;

            int worstRunway = 999;
            int totalSpawns = 0;
            int boardsWithoutRoom = 0;

            for (int board = 0; board < kBoards; ++board)
            {
                const std::uint64_t seed = 0xC0FFEEull + static_cast<std::uint64_t>(board) * 7919ull;

                MatchRules rules;
                const Level arena = LevelGenerator::generate(rules.arenaLevelIndex, seed, rules.startLength);

                std::vector<MatchPlayerInit> players;
                for (int i = 0; i < 4; ++i)
                {
                    MatchPlayerInit player;
                    player.slot = static_cast<PlayerSlot>(i + 1);
                    player.name = L"P" + std::to_wstring(i + 1);
                    player.colourIndex = static_cast<std::uint8_t>(i);
                    player.typeIndex = static_cast<std::uint8_t>(i);
                    players.push_back(player);
                }

                MatchSimulation simulation;
                simulation.start(arena, rules, seed, players);

                MatchSnapshot snapshot;
                simulation.buildSnapshot(snapshot);

                bool boardShort = false;

                for (const SnakeSnapshot& snake : snapshot.snakes)
                {
                    if (!snake.alive || snake.body.size() < 2)
                        continue;

                    // The snapshot does not carry a heading, but it does not
                    // need to: the head and the segment behind it are one step
                    // apart, and that step is the heading.
                    const Vec2 head = snake.body[0];
                    const Vec2 forward = head - snake.body[1];

                    int runway = 0;
                    for (int i = 1; i <= 8; ++i)
                    {
                        const Vec2 tile = head + forward * i;
                        if (!simulation.arena().inBounds(tile) || simulation.arena().isWall(tile))
                            break;
                        ++runway;
                    }

                    ++totalSpawns;
                    worstRunway = std::min(worstRunway, runway);
                    if (runway < kFloor)
                        boardShort = true;
                }

                if (boardShort)
                    ++boardsWithoutRoom;
            }

            check(totalSpawns == kBoards * 4, "every player is placed on every board");
            check(boardsWithoutRoom == 0, "no spawn faces a wall inside its reaction window");

            std::cout << "  shortest runway across " << totalSpawns << " spawns on "
                      << kBoards << " boards: " << worstRunway << " tiles ("
                      << static_cast<int>(static_cast<float>(worstRunway) * MatchRules{}.tickSeconds * 1000.0f)
                      << " ms of warning)\n";
        }

        // ------------------------------------------------------- matchmaking --
        std::cout << "\nLAN discovery\n";

        {
            // Named rather than taken from the factory: makeMatchmaker() now
            // returns LAN and relay together when a relay is configured, and
            // this test is about the LAN half on its own.
            auto advertiser = std::make_unique<net::LanMatchmaker>();
            auto browser = std::make_unique<net::LanMatchmaker>();

            net::NetConfig::instance().discoveryPort = config.discoveryPort;

            net::SessionAdvert advert;
            advert.code = L"TESTME";
            advert.hostName = L"HOSTER";
            advert.port = config.hostPort;
            advert.players = 2;
            advert.maxPlayers = 4;
            advert.inMatch = false;

            const bool advertising = advertiser->startAdvertising(advert);
            check(advertising, "a host can advertise on the local network");

            const bool browsing = browser->startBrowsing();
            check(browsing, "a client can browse while a host advertises on the same machine");

            bool found = false;
            using Clock = std::chrono::steady_clock;
            const auto deadline = Clock::now() + std::chrono::seconds(6);

            while (!found && Clock::now() < deadline)
            {
                advertiser->update(1.0f / 60.0f);
                browser->update(1.0f / 60.0f);
                found = browser->bestJoinable() != nullptr;
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }

            check(found, "the browser discovers the advertised session");

            if (const net::DiscoveredSession* session = browser->bestJoinable(); session != nullptr)
            {
                check(session->advert.code == L"TESTME", "the join code survives discovery");
                check(session->advert.port == config.hostPort, "the discovered port is the one to connect to");
                check(session->joinable(), "a session with room is reported as joinable");
                check(!session->viaRelay, "a session found on the network is not marked as relayed");
                check(narrow(session->where()) == session->address,
                    "and its browser row shows the address to reach it on");
            }
        }

        // ------------------------------------------------------- via the relay --
        //
        // The same session layer again, over a relay this time. Nothing below
        // the transport changes, which is the whole claim being tested: a host
        // that cannot accept a connection and guests that cannot reach it still
        // play a match, because both ends dial OUT to the relay.
        std::cout << "\nthe same thing again, through a relay\n";

        {
            relay::RelayServer relayServer;
            relayServer.setVerbose(false);

            std::wstring relayError;
            const bool relayUp = relayServer.start(45797, relayError);
            check(relayUp, "the relay starts");

            if (!relayUp)
            {
                std::cout << "  (skipping the relayed half: " << narrow(relayError) << ")\n";
            }
            else
            {
                g_relay = &relayServer;

                net::NetConfig relayConfig = config;
                relayConfig.relayHost = "127.0.0.1";
                relayConfig.relayPort = relayServer.port();

                FixedIdentityProvider relayHostIdentity("acct-relay-host", L"RHOST", true);
                FixedIdentityProvider relayGuestA("acct-relay-a", L"RONE", true);
                FixedIdentityProvider relayGuestB("acct-relay-b", L"RTWO", true);

                auto relayHost = std::make_unique<net::HostSession>(relayConfig, relayHostIdentity);

                std::wstring hostError;
                const bool opened = relayHost->open(L"RHOST", 0, 0,
                    net::HostSession::Reach::Relay, hostError);
                check(opened, "a host can open a relayed session");

                std::vector<net::NetGame*> relayAll{ relayHost.get() };

                const bool gotCode = pumpUntil(relayAll, [&]
                {
                    return !relayHost->lobby().code.empty();
                }, 10.0f);

                check(gotCode, "the relay hands the host a join code");

                const std::wstring code = relayHost->lobby().code;
                check(code.size() == 6, "the code is six characters");

                // Browsing the relay. This is what puts an online session in
                // front of a player who was never told a code -- the same list
                // the local network fills, from the other direction.
                {
                    net::NetConfig::instance().relayListIntervalSeconds = 1.0f;

                    net::RelayEndpoint endpoint;
                    endpoint.host = relayConfig.relayHost;
                    endpoint.port = relayConfig.relayPort;
                    endpoint.name = L"TEST";

                    net::RelayMatchmaker browser(endpoint, 0);
                    check(browser.startBrowsing(), "the relay browser starts");

                    constexpr float step = 1.0f / 120.0f;

                    // The query runs on a worker thread, so the relay has to
                    // keep being pumped on this one for it to be answered --
                    // which is exactly the arrangement the game is in.
                    using Clock = std::chrono::steady_clock;
                    const auto deadline = Clock::now() + std::chrono::seconds(10);
                    bool listed = false;

                    while (!listed && Clock::now() < deadline)
                    {
                        relayServer.pump();
                        relayServer.tickTimers(step);
                        relayHost->update(step);
                        browser.update(step);
                        listed = !browser.sessions().empty();
                        std::this_thread::sleep_for(std::chrono::milliseconds(4));
                    }

                    check(listed, "an open online session appears in the browser with no code typed");

                    if (listed)
                    {
                        const std::vector<net::RelayStatus>& statuses = browser.relayStatuses();
                        check(statuses.size() == 1, "the browser reports the region it is watching");
                        check(statuses.front().reachable, "and says whether it answered");
                        check(statuses.front().pingMs >= 0,
                            "and how long the round trip to it took");
                        check(browser.fastestRelay() == 0,
                            "a single reachable region is the fastest one by default");

                        const net::DiscoveredSession& found = browser.sessions().front();
                        check(found.relayIndex == 0,
                            "a listed session remembers which relay it was found on");
                        check(found.advert.code == code, "the listed session carries the host's code");
                        check(found.viaRelay, "it is marked as reachable through the relay");
                        check(found.address.empty(), "and carries no address, because its code is its address");
                        check(found.where() == L"via relay",
                            "the browser row says how it is reached rather than showing a blank");
                        check(found.joinable(), "a relayed session with room is reported as joinable");
                        check(browser.bestJoinable() == &found, "quick match would pick it");
                    }

                    browser.stopBrowsing();
                }

                auto relayOne = std::make_unique<net::ClientSession>(relayConfig, relayGuestA);
                auto relayTwo = std::make_unique<net::ClientSession>(relayConfig, relayGuestB);

                std::wstring joinError;
                relayOne->connectByCode(code, L"RONE", 1, 1, joinError);

                // Typed the way a person would: lowercase, with a separator in
                // the middle. It has to reach the same session.
                std::wstring typed = code;
                for (wchar_t& c : typed)
                    c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
                typed.insert(typed.begin() + 3, L'-');

                relayTwo->connectByCode(typed, L"RTWO", 2, 2, joinError);

                relayAll.push_back(relayOne.get());
                relayAll.push_back(relayTwo.get());

                const bool bothJoined = pumpUntil(relayAll, [&]
                {
                    return relayOne->phase() == net::SessionPhase::InLobby &&
                        relayTwo->phase() == net::SessionPhase::InLobby;
                }, 15.0f);

                check(bothJoined, "two guests reach the lobby through the relay");
                check(relayHost->lobby().occupiedCount() == 3, "the host sees all three seats");
                check(relayTwo->phase() == net::SessionPhase::InLobby,
                    "a code typed in lowercase with a dash still finds the session");

                // A wrong code has to fail cleanly rather than hang.
                FixedIdentityProvider strayIdentity("acct-relay-stray", L"STRAY", true);
                auto stray = std::make_unique<net::ClientSession>(relayConfig, strayIdentity);
                stray->connectByCode(L"ZZZZZZ", L"STRAY", 3, 3, joinError);
                relayAll.push_back(stray.get());

                const bool strayRefused = pumpUntil(relayAll, [&]
                {
                    return stray->phase() == net::SessionPhase::Disconnected;
                }, 10.0f);

                check(strayRefused, "an unknown code is refused with a reason");
                relayAll.pop_back();
                stray.reset();

                relayOne->setReady(true);
                relayTwo->setReady(true);

                const bool ready = pumpUntil(relayAll, [&]
                {
                    return relayHost->canStartMatch();
                }, 10.0f);

                check(ready, "readying up works over the relay");

                relayHost->requestStartMatch();

                const bool started = pumpUntil(relayAll, [&]
                {
                    return relayOne->phase() == net::SessionPhase::InMatch &&
                        relayTwo->phase() == net::SessionPhase::InMatch;
                }, 15.0f);

                check(started, "the match starts for both relayed guests");

                // Latency, end to end. The guest times its own heartbeat against
                // the host's echo, reports the number on its next one, and the
                // host puts it in the lobby -- so every player ends up holding
                // every player's ping rather than only their own.
                {
                    const bool measured = pumpUntil(relayAll, [&]
                    {
                        return relayOne->pingMs() >= 0 && relayTwo->pingMs() >= 0;
                    }, 15.0f);

                    check(measured, "a guest measures its own round trip to the host");
                    check(relayHost->pingMs() == 0, "and the host, being the far end, reports zero");

                    const bool shared = pumpUntil(relayAll, [&]
                    {
                        const net::LobbySlot* seat = relayHost->lobby().find(relayOne->localSlot());
                        return seat != nullptr && seat->pingMs > 0;
                    }, 15.0f);

                    // Over loopback the real answer is under a millisecond, so
                    // this only checks that a number arrived and is not absurd.
                    // What it is actually proving is the path, not the value.
                    check(shared || relayOne->pingMs() == 0,
                        "the host learns each guest's ping, or it was too fast to register");

                    const net::LobbySlot* mirrored = relayTwo->lobby().find(relayOne->localSlot());
                    check(mirrored != nullptr,
                        "and every other guest sees that seat in their own copy of the lobby");
                }
                check(relayOne->arena().width() > 0, "the arena crosses the relay intact");

                // Bandwidth is the number that decides what the relay costs to
                // run, so it is measured rather than estimated.
                const std::uint64_t bytesAtStart = relayServer.stats().bytesForwarded;
                const std::uint64_t framesAtStart = relayServer.stats().framesForwarded;

                {
                    bool identical = relayHost->arena().width() == relayOne->arena().width();
                    for (int y = 0; y < relayHost->arena().height() && identical; ++y)
                        for (int x = 0; x < relayHost->arena().width() && identical; ++x)
                            identical = relayHost->arena().isWall({ x, y }) ==
                                relayOne->arena().isWall({ x, y });

                    check(identical, "every wall tile survives the relay too");
                }

                const bool snapshots = pumpUntil(relayAll, [&]
                {
                    return relayOne->snapshot().snakes.size() == 3 &&
                        relayTwo->snapshot().snakes.size() == 3 &&
                        relayOne->snapshot().phase == MatchPhase::Running;
                }, 15.0f);

                check(snapshots, "snapshots reach both guests through the relay");

                // One broadcast from the host should have become one frame per
                // guest at the relay, not three uploads from the host.
                check(relayServer.stats().framesForwarded > 0, "the relay forwarded traffic");

                net::InputCommand turn;
                turn.hasDirection = true;
                turn.direction = Direction::Down;
                relayOne->sendInput(turn);

                const std::uint32_t before = relayOne->snapshot().tick;
                pumpFor(relayAll, 1.5f);
                check(relayOne->snapshot().tick > before, "the relayed match advances");

                // A guest leaving a relayed match has to free its seat the same
                // way a direct one does.
                relayTwo->shutdown();

                const bool freed = pumpUntil({ relayHost.get(), relayOne.get() }, [&]
                {
                    return relayHost->lobby().occupiedCount() == 2;
                }, 10.0f);

                check(freed, "a guest leaving through the relay frees its seat");

                const bool finished = pumpUntil({ relayHost.get(), relayOne.get() }, [&]
                {
                    return relayOne->phase() == net::SessionPhase::PostMatch;
                }, 25.0f);

                check(finished, "the relayed match plays out to a result");
                check(!relayOne->result().standings.empty(), "standings arrive through the relay");

                {
                    // What matters for hosting cost is the size of a snapshot,
                    // not the rate this test happens to produce them at: the
                    // harness steps a fixed delta per iteration, so its
                    // wall-clock rate is an artefact of the loop rather than a
                    // measurement of the game. Size is harness-independent, and
                    // the rate is a number the host chooses (snapshot_hz).
                    const std::uint64_t bytes = relayServer.stats().bytesForwarded - bytesAtStart;
                    const std::uint64_t frames = relayServer.stats().framesForwarded - framesAtStart;

                    if (frames > 0)
                    {
                        const double bytesPerFrame = static_cast<double>(bytes) / static_cast<double>(frames);

                        // Egress only: what the relay sends out. What it takes
                        // in from the host is ingress, which is free nearly
                        // everywhere and is one copy regardless of guest count.
                        const int guests = static_cast<int>(relayConfig.maxPlayers) - 1;
                        const double egressPerSecond = bytesPerFrame * relayConfig.snapshotHz *
                            static_cast<double>(guests);

                        std::cout << "\n  measured snapshot size: "
                                  << static_cast<int>(bytesPerFrame + 0.5) << " bytes"
                                  << " (" << frames << " frames, " << (bytes / 1024) << " KiB)\n"
                                  << "  projected relay egress at " << static_cast<int>(relayConfig.snapshotHz)
                                  << " Hz with " << guests << " guests: "
                                  << static_cast<int>(egressPerSecond / 1024.0 + 0.5) << " KB/s"
                                  << " = " << static_cast<int>(egressPerSecond * 3600.0 / 1e9 * 1000.0 + 0.5)
                                  << " MB per match-hour\n"
                                  << "  (snakes are short in a ten-second match; a long one runs higher)\n";
                    }
                }

                relayHost->shutdown();

                const bool told = pumpUntil({ relayOne.get() }, [&]
                {
                    return relayOne->phase() == net::SessionPhase::Disconnected;
                }, 10.0f);

                check(told, "a relayed guest is told when the host quits");

                relayOne.reset();
                relayTwo.reset();
                relayHost.reset();

                g_relay = nullptr;
            }
        }

        // ------------------------------------------------------------ report --
        std::cout << "\n" << (g_checks - g_failures) << " / " << g_checks << " checks passed\n";

        if (g_failures > 0)
        {
            std::cout << g_failures << " FAILED\n";
            return 1;
        }

        std::cout << "network self-test passed\n";
        return 0;
    }
}
