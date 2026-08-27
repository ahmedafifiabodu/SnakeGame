#include "ClientSession.h"

#include "RelayTransport.h"
#include "TcpTransport.h"

#include <algorithm>

namespace neoncoil::net
{
    namespace
    {
        constexpr std::size_t kMaxEvents = 12;
    }

    ClientSession::ClientSession(const NetConfig& config, IIdentityProvider& identity)
        : m_config(config)
        , m_identity(identity)
    {
    }

    ClientSession::~ClientSession()
    {
        shutdown();
    }

    void ClientSession::note(std::wstring line)
    {
        m_events.push_back(std::move(line));
        if (m_events.size() > kMaxEvents)
            m_events.erase(m_events.begin(), m_events.begin() +
                static_cast<std::ptrdiff_t>(m_events.size() - kMaxEvents));
    }

    bool ClientSession::connect(const std::string& address, std::uint16_t port,
        const std::wstring& displayName, std::uint8_t colourIndex,
        std::uint8_t typeIndex, std::wstring& error)
    {
        shutdown();

        m_identity.setDisplayName(displayName);
        m_colourIndex = colourIndex;
        m_typeIndex = typeIndex;

        m_transport = std::make_unique<TcpClientTransport>();

        if (!m_transport->beginConnect(address, port, error))
        {
            m_transport.reset();
            m_phase = SessionPhase::Disconnected;
            m_status = error;
            return false;
        }

        m_phase = SessionPhase::Connecting;
        m_status = L"Connecting...";
        m_connectElapsed = 0.0f;
        return true;
    }

    bool ClientSession::connectByCode(const std::wstring& code, const std::wstring& displayName,
        std::uint8_t colourIndex, std::uint8_t typeIndex, std::wstring& error)
    {
        shutdown();

        if (!m_config.relayConfigured())
        {
            error = L"no relay is configured -- join by address instead";
            m_phase = SessionPhase::Disconnected;
            m_status = error;
            return false;
        }

        const std::wstring normalised = normaliseCode(code);
        if (normalised.empty())
        {
            error = L"type a session code first";
            m_phase = SessionPhase::Disconnected;
            m_status = error;
            return false;
        }

        m_identity.setDisplayName(displayName);
        m_colourIndex = colourIndex;
        m_typeIndex = typeIndex;

        // The whole difference between this and connect() is which transport
        // gets built. Everything below this line -- the handshake, the lobby,
        // the match -- is identical, because the session layer was written
        // against the interface rather than against a socket.
        m_transport = std::make_unique<RelayClientTransport>(
            m_config.relayHost, m_config.relayPort, normalised);

        if (!m_transport->beginConnect({}, 0, error))
        {
            m_transport.reset();
            m_phase = SessionPhase::Disconnected;
            m_status = error;
            return false;
        }

        m_phase = SessionPhase::Connecting;
        m_status = L"Finding session " + normalised + L"...";
        m_connectElapsed = 0.0f;
        return true;
    }

    void ClientSession::shutdown()
    {
        if (m_transport)
        {
            // Say goodbye so the host frees the seat immediately instead of
            // waiting for the socket to time out.
            if (m_transport->isConnected())
            {
                m_transport->send(beginClient(ClientMessage::Leave));
                m_transport->pump();
            }

            m_transport->disconnect();
            m_transport.reset();
        }

        m_localSlot = kInvalidSlot;
        m_phase = SessionPhase::Idle;
    }

    void ClientSession::sendHello()
    {
        sf::Packet packet = beginClient(ClientMessage::Hello);
        packet << kProtocolVersion << m_identity.issueTicket() << m_colourIndex << m_typeIndex;
        m_transport->send(std::move(packet));
    }

    void ClientSession::setReady(bool ready)
    {
        if (!m_transport || m_phase != SessionPhase::InLobby)
            return;

        sf::Packet packet = beginClient(ClientMessage::SetReady);
        packet << ready;
        m_transport->send(std::move(packet));

        // Optimistic local echo. The host's next LobbyUpdate is authoritative
        // and will correct this if it disagrees, but without it the tick takes a
        // round trip to appear and the button feels broken.
        if (m_localSlot < kMaxMatchPlayers)
            m_lobby.slots[static_cast<std::size_t>(m_localSlot)].ready = ready;
    }

    void ClientSession::setLoadout(std::uint8_t colourIndex, std::uint8_t typeIndex)
    {
        m_colourIndex = colourIndex;
        m_typeIndex = typeIndex;

        if (!m_transport || m_phase != SessionPhase::InLobby)
            return;

        sf::Packet packet = beginClient(ClientMessage::SetLoadout);
        packet << colourIndex << typeIndex;
        m_transport->send(std::move(packet));

        if (m_localSlot < kMaxMatchPlayers)
        {
            LobbySlot& seat = m_lobby.slots[static_cast<std::size_t>(m_localSlot)];
            seat.colourIndex = colourIndex;
            seat.typeIndex = typeIndex;
        }
    }

    void ClientSession::sendInput(const InputCommand& input)
    {
        if (!m_transport || m_phase != SessionPhase::InMatch)
            return;

        InputCommand stamped = input;
        stamped.sequence = ++m_inputSequence;

        sf::Packet packet = beginClient(ClientMessage::Input);
        packet << stamped;
        m_transport->send(std::move(packet));
    }

    void ClientSession::returnToLobby()
    {
        if (!m_transport)
            return;

        m_transport->send(beginClient(ClientMessage::ReturnToLobby));

        // The host owns the transition; this just stops the client sitting on a
        // results screen if the host has already reopened the lobby.
        if (m_phase == SessionPhase::PostMatch && !m_lobby.inMatch)
            m_phase = SessionPhase::InLobby;
    }

    void ClientSession::handleMessage(sf::Packet& packet)
    {
        ServerMessage id{};
        if (!readServerHeader(packet, id))
            return;

        switch (id)
        {
        case ServerMessage::Welcome:
        {
            std::uint16_t version = 0;
            PlayerSlot slot = kInvalidSlot;
            LobbyInfo lobby;
            packet >> version >> slot >> lobby;

            if (!packet || version != kProtocolVersion)
            {
                m_phase = SessionPhase::Disconnected;
                m_status = describe(RejectReason::VersionMismatch);
                return;
            }

            m_localSlot = slot;
            m_lobby = lobby;
            m_phase = lobby.inMatch ? SessionPhase::Starting : SessionPhase::InLobby;
            m_status = L"Joined " + lobby.hostName + L"'s session";
            note(m_status);
            break;
        }

        case ServerMessage::Rejected:
        {
            std::uint8_t reason = 0;
            packet >> reason;

            m_phase = SessionPhase::Disconnected;
            m_status = describe(static_cast<RejectReason>(reason));
            note(m_status);
            break;
        }

        case ServerMessage::LobbyUpdate:
        {
            LobbyInfo lobby;
            packet >> lobby;
            if (!packet)
                break;

            m_lobby = lobby;

            // The host reopening the lobby is what ends a client's post-match
            // screen; the client never decides that for itself.
            if (!lobby.inMatch && m_phase == SessionPhase::PostMatch)
                m_phase = SessionPhase::InLobby;
            break;
        }

        case ServerMessage::MatchStart:
        {
            std::uint64_t seed = 0;
            ArenaDescription arena;
            LobbyInfo lobby;
            packet >> seed >> m_rules >> arena >> lobby;

            if (!packet)
                break;

            buildArena(m_arena, arena);
            m_lobby = lobby;
            m_snapshot = MatchSnapshot{};
            m_result = MatchResult{};
            m_inputSequence = 0;
            m_phase = SessionPhase::InMatch;
            m_status = L"Match running";
            note(L"Match started");
            break;
        }

        case ServerMessage::Snapshot:
        {
            MatchSnapshot snapshot;
            packet >> snapshot;
            if (!packet)
                break;

            // Out-of-order delivery cannot happen over TCP, but a snapshot that
            // arrives after a MatchEnd would rewind the results screen, so old
            // ticks are dropped rather than trusted.
            if (m_phase == SessionPhase::InMatch && snapshot.tick >= m_snapshot.tick)
                m_snapshot = std::move(snapshot);
            break;
        }

        case ServerMessage::MatchEnd:
        {
            MatchResult result;
            packet >> result;
            if (!packet)
                break;

            m_result = std::move(result);
            m_phase = SessionPhase::PostMatch;
            m_status = L"Match over";
            note(m_status);
            break;
        }

        case ServerMessage::Heartbeat:
            break;
        }
    }

    void ClientSession::update(float deltaSeconds)
    {
        if (!m_transport)
            return;

        m_transport->pump();

        TransportEvent event;
        while (m_transport->poll(event))
        {
            switch (event.type)
            {
            case TransportEventType::Connected:
                m_status = L"Introducing yourself...";
                sendHello();
                break;

            case TransportEventType::Disconnected:
            {
                // A Rejected message already explains itself; a bare socket drop
                // does not, so only overwrite the status when we have nothing
                // better to say.
                const bool alreadyExplained = m_phase == SessionPhase::Disconnected && !m_status.empty();
                m_phase = SessionPhase::Disconnected;
                if (!alreadyExplained)
                    m_status = event.reason.empty() ? L"Disconnected" : event.reason;
                note(m_status);
                break;
            }

            case TransportEventType::Message:
                handleMessage(event.packet);
                break;

            case TransportEventType::None:
                break;
            }
        }

        if (m_phase == SessionPhase::Connecting)
        {
            m_connectElapsed += deltaSeconds;
            if (m_connectElapsed > m_config.connectTimeoutSeconds + 2.0f)
            {
                m_phase = SessionPhase::Disconnected;
                m_status = L"That session did not answer";
            }
            return;
        }

        if (m_phase == SessionPhase::Disconnected || m_phase == SessionPhase::Idle)
            return;

        m_heartbeatTimer -= deltaSeconds;
        if (m_heartbeatTimer <= 0.0f)
        {
            m_heartbeatTimer = 1.0f / std::max(0.1f, m_config.heartbeatHz);
            m_transport->send(beginClient(ClientMessage::Heartbeat));
        }
    }
}
