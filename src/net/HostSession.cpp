#include "HostSession.h"

#include "RelayTransport.h"
#include "TcpTransport.h"
#include "../game/LevelGenerator.h"

#include <algorithm>

namespace neoncoil::net
{
    namespace
    {
        // The event feed is a UI affordance, not a log. Keeping it short means
        // a long session cannot grow it without bound.
        constexpr std::size_t kMaxEvents = 12;

        // A peer that connects and then never says Hello is either a port
        // scanner or a crashed client. Either way it does not get to hold a
        // socket open forever.
        constexpr float kHelloGraceSeconds = 6.0f;
    }

    HostSession::HostSession(const NetConfig& config, IIdentityProvider& identity)
        : m_config(config)
        , m_identity(identity)
    {
    }

    HostSession::~HostSession()
    {
        shutdown();
    }

    void HostSession::note(std::wstring line)
    {
        m_events.push_back(std::move(line));
        if (m_events.size() > kMaxEvents)
            m_events.erase(m_events.begin(), m_events.begin() +
                static_cast<std::ptrdiff_t>(m_events.size() - kMaxEvents));
    }

    bool HostSession::open(const std::wstring& hostDisplayName, std::uint8_t colourIndex,
        std::uint8_t typeIndex, Reach reach, std::wstring& error)
    {
        m_identity.setDisplayName(hostDisplayName);
        const PlayerIdentity local = m_identity.local();

        m_reach = reach;
        m_rules = m_config.rules;
        m_localSlot = 0;

        if (reach == Reach::Relay)
        {
            if (!m_config.relayConfigured())
            {
                error = L"no relay is configured -- this build can only host on a local network";
                m_phase = SessionPhase::Disconnected;
                m_status = error;
                return false;
            }

            SessionAdvert advert;
            advert.hostName = local.displayName;
            advert.port = 0;
            advert.players = 1;
            advert.maxPlayers = static_cast<std::uint8_t>(
                std::clamp(m_config.maxPlayers, 1, kMaxMatchPlayers));
            advert.inMatch = false;

            // Which of the configured relays this is, so the lobby can name the
            // region and the ping readout can attribute itself to one.
            for (std::size_t i = 0; i < m_config.relays.size(); ++i)
            {
                if (m_config.relays[i].host == m_config.relayHost &&
                    m_config.relays[i].port == m_config.relayPort)
                {
                    m_relayIndex = static_cast<int>(i);
                    break;
                }
            }

            auto relay = std::make_unique<RelayServerTransport>(m_config.relayHost, m_config.relayPort, advert);
            if (!relay->start(0, error))
            {
                m_phase = SessionPhase::Disconnected;
                m_status = error;
                return false;
            }

            m_transport = std::move(relay);
            m_awaitingCode = true;
        }
        else
        {
            auto direct = std::make_unique<TcpServerTransport>();
            if (!direct->start(m_config.hostPort, error))
            {
                m_phase = SessionPhase::Disconnected;
                m_status = error;
                return false;
            }

            m_transport = std::move(direct);
            m_awaitingCode = false;
        }

        m_lobby = LobbyInfo{};
        m_lobby.code = m_awaitingCode ? std::wstring() : makeJoinCode(Rng::seedFromClock());
        m_lobby.hostName = local.displayName;
        m_lobby.port = m_transport->port();
        m_lobby.maxPlayers = static_cast<std::uint8_t>(std::clamp(m_config.maxPlayers, 1, kMaxMatchPlayers));
        m_lobby.inMatch = false;

        LobbySlot& seat = m_lobby.slots[m_localSlot];
        seat.occupied = true;
        seat.slot = m_localSlot;
        seat.identityId = local.id;
        seat.authenticated = local.authenticated;
        seat.name = local.displayName;
        seat.colourIndex = colourIndex;
        seat.typeIndex = typeIndex;
        seat.isHost = true;
        seat.ready = true;

        // LAN discovery only makes sense for a host that is actually on the LAN
        // and listening. A relayed host is found by its code instead.
        if (m_reach == Reach::Direct)
        {
            m_matchmaker = makeMatchmaker();

            SessionAdvert advert;
            advert.code = m_lobby.code;
            advert.hostName = m_lobby.hostName;
            advert.port = m_lobby.port;
            advert.players = static_cast<std::uint8_t>(m_lobby.occupiedCount());
            advert.maxPlayers = m_lobby.maxPlayers;
            advert.inMatch = false;

            if (!m_matchmaker->startAdvertising(advert))
                note(m_matchmaker->lastError());
        }

        m_phase = SessionPhase::InLobby;

        if (m_reach == Reach::Relay)
        {
            m_status = L"Reaching the relay...";
            note(L"Connecting to the relay");
        }
        else
        {
            m_status = L"Lobby open -- code " + m_lobby.code;
            note(L"Session opened on port " + std::to_wstring(m_lobby.port));
        }

        return true;
    }

    void HostSession::shutdown()
    {
        if (m_transport)
        {
            // Tell everyone why before the socket goes away, so guests see "the
            // host closed the session" rather than a bare connection drop.
            sf::Packet packet = beginServer(ServerMessage::Rejected);
            packet << static_cast<std::uint8_t>(RejectReason::HostClosed);
            m_transport->broadcast(packet);
            m_transport->pump();
            m_transport->stop();
            m_transport.reset();
        }

        if (m_matchmaker)
        {
            m_matchmaker->stopAdvertising();
            m_matchmaker.reset();
        }

        m_clients.clear();
        m_phase = SessionPhase::Idle;
    }

    // ---------------------------------------------------------------- clients --

    HostSession::Client* HostSession::clientForPeer(PeerId peer)
    {
        for (Client& client : m_clients)
            if (client.peer == peer)
                return &client;
        return nullptr;
    }

    HostSession::Client* HostSession::clientForSlot(PlayerSlot slot)
    {
        for (Client& client : m_clients)
            if (client.slot == slot)
                return &client;
        return nullptr;
    }

    PlayerSlot HostSession::allocateSlot() const
    {
        for (int i = 0; i < static_cast<int>(m_lobby.maxPlayers); ++i)
        {
            if (!m_lobby.slots[static_cast<std::size_t>(i)].occupied)
                return static_cast<PlayerSlot>(i);
        }
        return kInvalidSlot;
    }

    void HostSession::releaseSlot(PlayerSlot slot)
    {
        if (slot == kInvalidSlot || slot >= kMaxMatchPlayers)
            return;
        m_lobby.slots[static_cast<std::size_t>(slot)] = LobbySlot{};
    }

    void HostSession::reject(PeerId peer, RejectReason reason)
    {
        sf::Packet packet = beginServer(ServerMessage::Rejected);
        packet << static_cast<std::uint8_t>(reason);
        m_transport->send(peer, std::move(packet));
        m_transport->disconnect(peer);
    }

    void HostSession::handleConnected(PeerId peer)
    {
        Client client;
        client.peer = peer;
        client.slot = kInvalidSlot;
        m_clients.push_back(std::move(client));
    }

    void HostSession::handleDisconnected(PeerId peer, const std::wstring& reason)
    {
        (void)reason;

        const auto it = std::find_if(m_clients.begin(), m_clients.end(),
            [peer](const Client& client) { return client.peer == peer; });

        if (it == m_clients.end())
            return;

        if (it->slot != kInvalidSlot)
        {
            const std::wstring name = m_lobby.slots[static_cast<std::size_t>(it->slot)].name;

            // A player leaving mid-match takes their snake with them and the
            // match carries on. This is the whole of "handle players leaving
            // gracefully": no pause, no vote, no stall.
            m_simulation.removePlayer(it->slot);
            releaseSlot(it->slot);
            note(name + L" left");
            broadcastLobby();
            refreshAdvert();
        }

        m_clients.erase(it);
    }

    void HostSession::handleHello(Client& client, sf::Packet& packet)
    {
        std::uint16_t version = 0;
        JoinTicket ticket;
        std::uint8_t colourIndex = 0;
        std::uint8_t typeIndex = 0;

        packet >> version >> ticket >> colourIndex >> typeIndex;

        if (!packet)
        {
            reject(client.peer, RejectReason::BadProtocol);
            return;
        }

        if (version != kProtocolVersion)
        {
            reject(client.peer, RejectReason::VersionMismatch);
            return;
        }

        // Identity is checked here and nowhere else. Today the local provider
        // waves everything through; when logins arrive, the same call verifies a
        // signed ticket and the rest of this file is unchanged.
        PlayerIdentity identity;
        std::wstring reason;
        if (!m_identity.verify(ticket, identity, reason))
        {
            reject(client.peer, RejectReason::BadTicket);
            return;
        }

        // One seat per identity -- but only for identities that actually mean
        // something. A local guest id is just a file next to the executable, so
        // four copies of the game in one folder legitimately present the same
        // one; refusing them would block the most common way anybody tests a
        // four-player session. Once logins exist, authenticated ids ARE unique
        // and this check starts doing real work with no other change.
        if (identity.authenticated)
        {
            for (const LobbySlot& seat : m_lobby.slots)
            {
                if (seat.occupied && seat.authenticated && seat.identityId == identity.id)
                {
                    reject(client.peer, RejectReason::DuplicateIdentity);
                    return;
                }
            }
        }

        if (m_lobby.inMatch)
        {
            // Joining a running match is refused rather than half-supported. A
            // late joiner would need spectating and mid-match spawn rules that
            // this mode does not have; sending them away with a clear reason is
            // honest, and the lobby reopens the moment the match ends.
            reject(client.peer, RejectReason::MatchInProgress);
            return;
        }

        const PlayerSlot slot = allocateSlot();
        if (slot == kInvalidSlot)
        {
            reject(client.peer, RejectReason::LobbyFull);
            return;
        }

        client.slot = slot;
        client.identity = identity;
        client.greeted = true;

        LobbySlot& seat = m_lobby.slots[static_cast<std::size_t>(slot)];
        seat.occupied = true;
        seat.slot = slot;
        seat.identityId = identity.id;
        seat.authenticated = identity.authenticated;
        seat.name = identity.displayName;
        seat.colourIndex = colourIndex;
        seat.typeIndex = typeIndex;
        seat.ready = false;
        seat.isHost = false;

        sf::Packet welcome = beginServer(ServerMessage::Welcome);
        welcome << kProtocolVersion << slot << m_lobby;
        m_transport->send(client.peer, std::move(welcome));

        note(identity.displayName + L" joined");
        broadcastLobby();
        refreshAdvert();
    }

    void HostSession::handleMessage(PeerId peer, sf::Packet& packet)
    {
        Client* client = clientForPeer(peer);
        if (client == nullptr)
            return;

        ClientMessage id{};
        if (!readClientHeader(packet, id))
        {
            reject(peer, RejectReason::BadProtocol);
            return;
        }

        // Nothing but Hello is accepted before a seat exists, so an unhandshaken
        // peer cannot touch the lobby or the simulation.
        if (!client->greeted && id != ClientMessage::Hello)
        {
            reject(peer, RejectReason::BadProtocol);
            return;
        }

        switch (id)
        {
        case ClientMessage::Hello:
            if (!client->greeted)
                handleHello(*client, packet);
            break;

        case ClientMessage::SetLoadout:
        {
            std::uint8_t colourIndex = 0;
            std::uint8_t typeIndex = 0;
            packet >> colourIndex >> typeIndex;

            // Loadout is frozen once the match starts: changing snake type
            // mid-match would mean re-configuring a live AbilityRuntime.
            if (packet && !m_lobby.inMatch)
            {
                LobbySlot& seat = m_lobby.slots[static_cast<std::size_t>(client->slot)];
                seat.colourIndex = colourIndex;
                seat.typeIndex = typeIndex;
                broadcastLobby();
            }
            break;
        }

        case ClientMessage::SetReady:
        {
            bool ready = false;
            packet >> ready;
            if (packet && !m_lobby.inMatch)
            {
                m_lobby.slots[static_cast<std::size_t>(client->slot)].ready = ready;
                broadcastLobby();
            }
            break;
        }

        case ClientMessage::RequestStart:
            // Only the host starts matches. A client asking is not an error --
            // an older or modified build might -- it simply does nothing.
            break;

        case ClientMessage::Input:
        {
            InputCommand input;
            packet >> input;
            if (!packet || m_phase != SessionPhase::InMatch)
                break;

            if (input.hasDirection)
                m_simulation.queueDirection(client->slot, input.direction, input.sequence);
            if (input.ability)
                m_simulation.requestAbility(client->slot);
            break;
        }

        case ClientMessage::Heartbeat:
        {
            Heartbeat beat;
            packet >> beat;
            if (!packet)
                break;

            // Echoed straight back, unread apart from the nonce: the host is not
            // measuring anything here, it is being the far end of somebody
            // else's measurement.
            sf::Packet reply = beginServer(ServerMessage::Heartbeat);
            reply << beat;
            m_transport->send(peer, std::move(reply));

            // What the guest measured last time round. Put in the lobby so that
            // every player sees every player's latency rather than only their
            // own -- which is the difference between "the game feels bad" and
            // "the game feels bad because that player is 300 ms away".
            if (client->slot != kInvalidSlot)
            {
                LobbySlot& seat = m_lobby.slots[client->slot];
                if (seat.occupied && seat.pingMs != beat.reportedPingMs)
                {
                    seat.pingMs = beat.reportedPingMs;
                    m_pingDirty = true;
                }
            }
            break;
        }

        case ClientMessage::ReturnToLobby:
            break;

        case ClientMessage::Leave:
            m_transport->disconnect(peer);
            break;
        }
    }

    // --------------------------------------------------------------- outgoing --

    void HostSession::broadcastLobby()
    {
        if (!m_transport)
            return;

        sf::Packet packet = beginServer(ServerMessage::LobbyUpdate);
        packet << m_lobby;
        m_transport->broadcast(packet);
    }

    void HostSession::sendMatchStart(PeerId peer)
    {
        sf::Packet packet = beginServer(ServerMessage::MatchStart);
        packet << m_matchSeed << m_rules << m_arenaDescription << m_lobby;
        m_transport->send(peer, std::move(packet));
    }

    void HostSession::broadcastSnapshot()
    {
        sf::Packet packet = beginServer(ServerMessage::Snapshot);
        packet << m_snapshot;
        m_transport->broadcast(packet);
    }

    void HostSession::broadcastMatchEnd()
    {
        sf::Packet packet = beginServer(ServerMessage::MatchEnd);
        packet << m_result;
        m_transport->broadcast(packet);
    }

    void HostSession::refreshAdvert()
    {
        SessionAdvert advert;
        advert.code = m_lobby.code;
        advert.hostName = m_lobby.hostName;
        advert.port = m_lobby.port;
        advert.players = static_cast<std::uint8_t>(m_lobby.occupiedCount());
        advert.maxPlayers = m_lobby.maxPlayers;
        advert.inMatch = m_lobby.inMatch;

        if (m_matchmaker && m_matchmaker->isAdvertising())
            m_matchmaker->updateAdvert(advert);

        // The relay keeps its own session list, and a stale player count there
        // is what makes somebody click a lobby that turns out to be full.
        if (m_reach == Reach::Relay)
        {
            if (auto* relay = dynamic_cast<RelayServerTransport*>(m_transport.get()); relay != nullptr)
                relay->updateAdvert(advert);
        }
    }

    // ------------------------------------------------------------------ match --

    bool HostSession::canStartMatch() const
    {
        return m_phase == SessionPhase::InLobby && m_lobby.occupiedCount() >= 1 && m_lobby.everyoneReady();
    }

    void HostSession::requestStartMatch()
    {
        if (canStartMatch())
            beginMatch();
    }

    void HostSession::beginMatch()
    {
        m_matchSeed = Rng::seedFromClock();

        // The arena comes out of the existing level generator, so multiplayer
        // boards are the same validated, always-playable layouts single player
        // uses -- no second generator, no second set of invariants.
        m_arena = LevelGenerator::generate(m_rules.arenaLevelIndex, m_matchSeed, m_rules.startLength);
        m_arenaDescription = describeArena(m_arena);

        std::vector<MatchPlayerInit> players;
        for (const LobbySlot& seat : m_lobby.slots)
        {
            if (!seat.occupied)
                continue;

            MatchPlayerInit player;
            player.slot = seat.slot;
            player.name = seat.name;
            player.colourIndex = seat.colourIndex;
            player.typeIndex = seat.typeIndex;
            players.push_back(std::move(player));
        }

        m_simulation.start(m_arena, m_rules, m_matchSeed, players);
        m_simulation.buildSnapshot(m_snapshot);

        m_lobby.inMatch = true;
        m_phase = SessionPhase::InMatch;
        m_status = L"Match running";
        m_snapshotTimer = 0.0f;
        m_minimumSnapshotInterval = 1.0f / std::max(kMoveSnapshotCeilingHz, m_config.snapshotHz);
        m_sinceSnapshot = m_minimumSnapshotInterval;

        for (const Client& client : m_clients)
        {
            if (client.greeted)
                sendMatchStart(client.peer);
        }

        broadcastLobby();
        refreshAdvert();
        note(L"Match started");
    }

    void HostSession::endMatch()
    {
        m_result = m_simulation.result();
        m_phase = SessionPhase::PostMatch;
        m_status = L"Match over";

        broadcastMatchEnd();
    }

    void HostSession::returnToLobby()
    {
        if (m_phase != SessionPhase::PostMatch)
            return;

        m_lobby.inMatch = false;

        // Everyone un-readies, so returning to the lobby is a real decision
        // point rather than a one-frame bounce straight into another match.
        for (LobbySlot& seat : m_lobby.slots)
            seat.ready = seat.isHost;

        m_phase = SessionPhase::InLobby;
        m_status = L"Lobby open -- code " + m_lobby.code;

        broadcastLobby();
        refreshAdvert();
    }

    // ----------------------------------------------------------------- update --

    void HostSession::setReady(bool ready)
    {
        // The host's own seat is always ready: pressing START is its consent.
        (void)ready;
    }

    void HostSession::setLoadout(std::uint8_t colourIndex, std::uint8_t typeIndex)
    {
        if (m_lobby.inMatch)
            return;

        LobbySlot& seat = m_lobby.slots[static_cast<std::size_t>(m_localSlot)];
        if (seat.colourIndex == colourIndex && seat.typeIndex == typeIndex)
            return;

        seat.colourIndex = colourIndex;
        seat.typeIndex = typeIndex;
        broadcastLobby();
    }

    void HostSession::sendInput(const InputCommand& input)
    {
        if (m_phase != SessionPhase::InMatch)
            return;

        // The host does not send anything: it IS the authority, so its input
        // goes straight into the simulation on the same frame it was pressed.
        if (input.hasDirection)
            m_simulation.queueDirection(m_localSlot, input.direction);
        if (input.ability)
            m_simulation.requestAbility(m_localSlot);
    }

    void HostSession::update(float deltaSeconds)
    {
        if (!m_transport)
            return;

        m_transport->pump();

        // A relayed host is not really open until the relay has handed back a
        // code, and it can still fail after open() returned.
        if (m_reach == Reach::Relay)
        {
            if (auto* relay = dynamic_cast<RelayServerTransport*>(m_transport.get()); relay != nullptr)
            {
                if (relay->failed())
                {
                    m_phase = SessionPhase::Disconnected;
                    m_status = relay->failure();
                    return;
                }

                if (m_awaitingCode && !relay->code().empty())
                {
                    m_awaitingCode = false;
                    m_lobby.code = relay->code();
                    m_status = L"Lobby open -- code " + m_lobby.code;
                    note(L"Relay session " + m_lobby.code + L" is open");
                    broadcastLobby();
                }
            }
        }

        TransportEvent event;
        while (m_transport->poll(event))
        {
            switch (event.type)
            {
            case TransportEventType::Connected:
                handleConnected(event.peer);
                break;
            case TransportEventType::Disconnected:
                handleDisconnected(event.peer, event.reason);
                break;
            case TransportEventType::Message:
                handleMessage(event.peer, event.packet);
                break;
            case TransportEventType::None:
                break;
            }
        }

        // Drop peers that connected and never handshook.
        for (Client& client : m_clients)
        {
            if (client.greeted)
                continue;

            client.secondsSinceHello += deltaSeconds;
            if (client.secondsSinceHello > kHelloGraceSeconds)
                m_transport->disconnect(client.peer);
        }

        // Pings change constantly and are worth almost nothing individually, so
        // they ride a slow timer of their own rather than making every heartbeat
        // broadcast the whole lobby to everybody.
        m_pingBroadcastTimer -= deltaSeconds;
        if (m_pingDirty && m_pingBroadcastTimer <= 0.0f)
        {
            m_pingDirty = false;
            m_pingBroadcastTimer = kPingBroadcastSeconds;
            broadcastLobby();
        }

        if (m_matchmaker)
        {
            m_matchmaker->update(deltaSeconds);

            m_advertTimer -= deltaSeconds;
            if (m_advertTimer <= 0.0f)
            {
                m_advertTimer = 1.0f;
                refreshAdvert();
            }
        }

        if (m_phase != SessionPhase::InMatch)
            return;

        m_simulation.update(deltaSeconds);

        for (std::wstring& line : m_simulation.drainEvents())
            note(std::move(line));

        // The host refreshes its own view every frame -- it costs nothing
        // locally and keeps the host's board stepping at the simulation rate
        // rather than the snapshot rate.
        m_simulation.buildSnapshot(m_snapshot);

        // What goes on the wire is event-driven, not clock-driven.
        //
        // Sixty snapshots a second would be sixty times the bandwidth for no
        // visible difference, because the board only changes when a snake steps
        // -- roughly eight times a second. But a fixed twenty-hertz timer is the
        // other mistake: it lands wherever it lands relative to the step, so on
        // average it sat half a snapshot interval on a move that had already
        // happened. Over a relay that delay is stacked on top of an already long
        // round trip, and it is the part of the lag that is ours to remove.
        //
        // So: send the instant the board changes, with the timer demoted to a
        // ceiling on the rate and a keepalive for the clock and the food timers.
        m_snapshotTimer -= deltaSeconds;

        const bool changed = m_simulation.steppedLastUpdate();
        const bool due = m_snapshotTimer <= 0.0f;
        const bool tooSoon = m_sinceSnapshot < m_minimumSnapshotInterval;

        if ((changed && !tooSoon) || due)
        {
            m_snapshotTimer = 1.0f / std::max(1.0f, m_config.snapshotHz);
            m_sinceSnapshot = 0.0f;
            broadcastSnapshot();
        }
        else
        {
            m_sinceSnapshot += deltaSeconds;
        }

        if (m_simulation.finished())
        {
            m_simulation.buildSnapshot(m_snapshot);
            broadcastSnapshot();
            endMatch();
        }

        // Same reasoning as on the client: the snapshot this function just built
        // was queued after the pump at the top, so one pump per frame would hold
        // it until the next one. Sending the moment the board changes is the
        // point of the timer above -- and it is undone entirely if the packet
        // then waits a frame to leave.
        m_transport->pump();
    }
}
