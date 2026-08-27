#include "RelayTransport.h"

#include "NetConfig.h"

#include <SFML/Network/Dns.hpp>
#include <SFML/Network/IpAddress.hpp>
#include <SFML/System/Time.hpp>

#include <optional>

namespace neoncoil::net
{
    namespace
    {
        constexpr int kMaxReceivesPerPump = 64;

        std::optional<sf::IpAddress> resolveAddress(const std::string& address)
        {
            if (const std::optional<sf::IpAddress> literal = sf::IpAddress::fromString(address))
                return literal;

            if (const std::optional<std::vector<sf::IpAddress>> resolved = sf::Dns::resolve(address);
                resolved.has_value() && !resolved->empty())
            {
                return resolved->front();
            }

            return std::nullopt;
        }
    }

    // ------------------------------------------------------------------ host --

    RelayServerTransport::RelayServerTransport(std::string relayHost, std::uint16_t relayPort,
        SessionAdvert advert)
        : m_relayHost(std::move(relayHost))
        , m_relayPort(relayPort)
        , m_advert(std::move(advert))
    {
    }

    RelayServerTransport::~RelayServerTransport()
    {
        stop();
    }

    bool RelayServerTransport::start(std::uint16_t port, std::wstring& error)
    {
        (void)port;
        stop();

        if (m_relayHost.empty())
        {
            error = L"no relay is configured";
            return false;
        }

        m_socket = std::make_unique<sf::TcpSocket>();
        m_linkState.store(LinkState::Connecting);

        const float timeout = NetConfig::instance().connectTimeoutSeconds;
        const std::string host = m_relayHost;
        const std::uint16_t relayPort = m_relayPort;

        // Same reasoning as the direct client: resolving and connecting both
        // block, and a lobby that freezes for a TCP timeout is not shippable.
        m_connectThread = std::thread([this, host, relayPort, timeout]
        {
            const std::optional<sf::IpAddress> resolved = resolveAddress(host);
            if (!resolved.has_value())
            {
                m_connectFailure = L"could not resolve the relay address";
                m_linkState.store(LinkState::Failed);
                return;
            }

            if (m_socket->connect(*resolved, relayPort, sf::seconds(timeout)) != sf::Socket::Status::Done)
            {
                m_connectFailure = L"could not reach the relay";
                m_linkState.store(LinkState::Failed);
                return;
            }

            m_linkState.store(LinkState::Connected);
        });

        return true;
    }

    void RelayServerTransport::joinConnectThread()
    {
        if (m_connectThread.joinable())
            m_connectThread.join();
    }

    void RelayServerTransport::stop()
    {
        joinConnectThread();

        if (m_socket)
            m_socket->disconnect();

        m_socket.reset();
        m_outgoing.clear();
        m_events.clear();
        m_channels.clear();
        m_peers.clear();
        m_registered = false;
        m_linkState.store(LinkState::Idle);
    }

    void RelayServerTransport::fail(std::wstring reason)
    {
        m_failed = true;
        m_failure = std::move(reason);
        m_registered = false;
    }

    void RelayServerTransport::sendFrame(sf::Packet packet)
    {
        m_outgoing.push_back(std::move(packet));
    }

    void RelayServerTransport::updateAdvert(const SessionAdvert& advert)
    {
        m_advert = advert;

        if (!m_registered)
            return;

        sf::Packet frame = beginRelay(RelayMessage::UpdateAdvert);
        frame << m_advert;
        sendFrame(std::move(frame));
    }

    void RelayServerTransport::flush()
    {
        while (!m_outgoing.empty())
        {
            const sf::Socket::Status status = m_socket->send(m_outgoing.front());

            if (status == sf::Socket::Status::Done)
            {
                m_outgoing.pop_front();
                continue;
            }

            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady)
                return;

            fail(L"lost the connection to the relay");
            return;
        }
    }

    void RelayServerTransport::receive()
    {
        for (int i = 0; i < kMaxReceivesPerPump && !m_failed; ++i)
        {
            sf::Packet packet;
            const sf::Socket::Status status = m_socket->receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                handle(packet);
                continue;
            }

            if (status == sf::Socket::Status::NotReady || status == sf::Socket::Status::Partial)
                return;

            fail(L"lost the connection to the relay");
            return;
        }
    }

    void RelayServerTransport::handle(sf::Packet& packet)
    {
        RelayMessage id{};
        if (!readRelayHeader(packet, id))
            return;

        switch (id)
        {
        case RelayMessage::Registered:
        {
            std::wstring code;
            packet >> code;
            if (!packet)
                break;

            m_code = code;
            m_registered = true;
            break;
        }

        case RelayMessage::Rejected:
        {
            std::uint8_t reason = 0;
            packet >> reason;
            fail(describe(static_cast<RelayReject>(reason)));
            break;
        }

        case RelayMessage::PeerJoined:
        {
            RelayChannel channel = kInvalidChannel;
            packet >> channel;
            if (!packet || m_peers.count(channel) != 0)
                break;

            const PeerId peer = m_nextPeerId++;
            m_peers.emplace(channel, peer);
            m_channels.emplace(peer, channel);

            TransportEvent event;
            event.type = TransportEventType::Connected;
            event.peer = peer;
            m_events.push_back(std::move(event));
            break;
        }

        case RelayMessage::PeerLeft:
        {
            RelayChannel channel = kInvalidChannel;
            packet >> channel;
            if (!packet)
                break;

            const auto it = m_peers.find(channel);
            if (it == m_peers.end())
                break;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.peer = it->second;
            event.reason = L"peer disconnected";
            m_events.push_back(std::move(event));

            m_channels.erase(it->second);
            m_peers.erase(it);
            break;
        }

        case RelayMessage::Data:
        {
            RelayChannel channel = kInvalidChannel;
            sf::Packet payload;
            packet >> channel;

            if (!packet || !extractPayload(packet, payload))
                break;

            const auto it = m_peers.find(channel);
            if (it == m_peers.end())
                break;

            TransportEvent event;
            event.type = TransportEventType::Message;
            event.peer = it->second;
            event.packet = std::move(payload);
            m_events.push_back(std::move(event));
            break;
        }

        default:
            break;
        }
    }

    void RelayServerTransport::pump()
    {
        const LinkState state = m_linkState.load();

        if (state == LinkState::Connecting)
            return;

        if (state == LinkState::Failed)
        {
            joinConnectThread();
            fail(m_connectFailure.empty() ? L"could not reach the relay" : m_connectFailure);
            m_socket.reset();
            m_linkState.store(LinkState::Idle);
            return;
        }

        if (state == LinkState::Connected)
        {
            joinConnectThread();
            m_socket->setBlocking(false);
            m_linkState.store(LinkState::Idle);

            // Registering is the first thing on the wire, so the relay can hand
            // back a code before anybody tries to join.
            sf::Packet frame = beginRelay(RelayMessage::RegisterHost);
            frame << kRelayVersion << m_advert;
            sendFrame(std::move(frame));
        }

        if (!m_socket || m_failed)
            return;

        flush();
        receive();
    }

    bool RelayServerTransport::poll(TransportEvent& out)
    {
        if (m_events.empty())
            return false;

        out = std::move(m_events.front());
        m_events.pop_front();
        return true;
    }

    void RelayServerTransport::send(PeerId peer, sf::Packet packet)
    {
        const auto it = m_channels.find(peer);
        if (it == m_channels.end())
            return;

        sf::Packet frame = beginRelay(RelayMessage::Data);
        frame << it->second;
        appendPayload(frame, packet);
        sendFrame(std::move(frame));
    }

    void RelayServerTransport::broadcast(const sf::Packet& packet)
    {
        if (m_channels.empty())
            return;

        // One copy up the host's narrow upstream; the relay makes the rest.
        sf::Packet frame = beginRelay(RelayMessage::Broadcast);
        appendPayload(frame, packet);
        sendFrame(std::move(frame));
    }

    void RelayServerTransport::broadcastExcept(PeerId exclude, const sf::Packet& packet)
    {
        // No relay-side "everyone but one", and it is not worth adding: this is
        // only used for messages that are cheap and rare.
        for (const auto& [peer, channel] : m_channels)
        {
            (void)channel;
            if (peer != exclude)
                send(peer, packet);
        }
    }

    void RelayServerTransport::disconnect(PeerId peer)
    {
        const auto it = m_channels.find(peer);
        if (it == m_channels.end())
            return;

        sf::Packet frame = beginRelay(RelayMessage::CloseChannel);
        frame << it->second;
        sendFrame(std::move(frame));

        m_peers.erase(it->second);
        m_channels.erase(it);
    }

    // ----------------------------------------------------------------- guest --

    RelayClientTransport::RelayClientTransport(std::string relayHost, std::uint16_t relayPort,
        std::wstring code)
        : m_relayHost(std::move(relayHost))
        , m_relayPort(relayPort)
        , m_code(std::move(code))
    {
    }

    RelayClientTransport::~RelayClientTransport()
    {
        disconnect();
    }

    bool RelayClientTransport::beginConnect(const std::string& address, std::uint16_t port,
        std::wstring& error)
    {
        (void)address;
        (void)port;
        disconnect();

        if (m_relayHost.empty())
        {
            error = L"no relay is configured";
            return false;
        }

        if (m_code.empty())
        {
            error = L"no session code given";
            return false;
        }

        m_socket = std::make_unique<sf::TcpSocket>();
        m_linkState.store(LinkState::Connecting);

        const float timeout = NetConfig::instance().connectTimeoutSeconds;
        const std::string host = m_relayHost;
        const std::uint16_t relayPort = m_relayPort;

        m_connectThread = std::thread([this, host, relayPort, timeout]
        {
            const std::optional<sf::IpAddress> resolved = resolveAddress(host);
            if (!resolved.has_value())
            {
                m_connectFailure = L"could not resolve the relay address";
                m_linkState.store(LinkState::Failed);
                return;
            }

            if (m_socket->connect(*resolved, relayPort, sf::seconds(timeout)) != sf::Socket::Status::Done)
            {
                m_connectFailure = L"could not reach the relay";
                m_linkState.store(LinkState::Failed);
                return;
            }

            m_linkState.store(LinkState::Connected);
        });

        return true;
    }

    void RelayClientTransport::joinConnectThread()
    {
        if (m_connectThread.joinable())
            m_connectThread.join();
    }

    void RelayClientTransport::disconnect()
    {
        joinConnectThread();

        if (m_socket)
            m_socket->disconnect();

        m_socket.reset();
        m_outgoing.clear();
        m_events.clear();
        m_linked = false;
        m_joined = false;
        m_linkState.store(LinkState::Idle);
    }

    void RelayClientTransport::flush()
    {
        while (!m_outgoing.empty())
        {
            const sf::Socket::Status status = m_socket->send(m_outgoing.front());

            if (status == sf::Socket::Status::Done)
            {
                m_outgoing.pop_front();
                continue;
            }

            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady)
                return;

            m_linked = false;
            m_joined = false;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = L"lost the connection to the relay";
            m_events.push_back(std::move(event));
            return;
        }
    }

    void RelayClientTransport::handle(sf::Packet& packet)
    {
        RelayMessage id{};
        if (!readRelayHeader(packet, id))
            return;

        switch (id)
        {
        case RelayMessage::JoinAccepted:
        {
            // The relay has put us through. Only now does the session layer get
            // told it is connected, so its Hello cannot race the hookup.
            m_joined = true;

            TransportEvent event;
            event.type = TransportEventType::Connected;
            m_events.push_back(std::move(event));
            break;
        }

        case RelayMessage::Rejected:
        {
            std::uint8_t reason = 0;
            packet >> reason;

            m_joined = false;
            m_linked = false;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = describe(static_cast<RelayReject>(reason));
            m_events.push_back(std::move(event));
            break;
        }

        case RelayMessage::Data:
        {
            RelayChannel channel = kInvalidChannel;
            sf::Packet payload;
            packet >> channel;

            if (!packet || !extractPayload(packet, payload))
                break;

            TransportEvent event;
            event.type = TransportEventType::Message;
            event.packet = std::move(payload);
            m_events.push_back(std::move(event));
            break;
        }

        default:
            break;
        }
    }

    void RelayClientTransport::receive()
    {
        for (int i = 0; i < kMaxReceivesPerPump; ++i)
        {
            sf::Packet packet;
            const sf::Socket::Status status = m_socket->receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                handle(packet);
                continue;
            }

            if (status == sf::Socket::Status::NotReady || status == sf::Socket::Status::Partial)
                return;

            m_linked = false;
            m_joined = false;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = L"the relay closed the connection";
            m_events.push_back(std::move(event));
            return;
        }
    }

    void RelayClientTransport::pump()
    {
        const LinkState state = m_linkState.load();

        if (state == LinkState::Connecting)
            return;

        if (state == LinkState::Failed)
        {
            joinConnectThread();

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = m_connectFailure.empty() ? L"could not reach the relay" : m_connectFailure;
            m_events.push_back(std::move(event));

            m_socket.reset();
            m_linkState.store(LinkState::Idle);
            return;
        }

        if (state == LinkState::Connected)
        {
            joinConnectThread();
            m_socket->setBlocking(false);
            m_linked = true;
            m_linkState.store(LinkState::Idle);

            sf::Packet frame = beginRelay(RelayMessage::JoinByCode);
            frame << kRelayVersion << m_code;
            m_outgoing.push_back(std::move(frame));
        }

        if (!m_socket || !m_linked)
            return;

        flush();

        if (m_linked)
            receive();
    }

    bool RelayClientTransport::poll(TransportEvent& out)
    {
        if (m_events.empty())
            return false;

        out = std::move(m_events.front());
        m_events.pop_front();
        return true;
    }

    void RelayClientTransport::send(sf::Packet packet)
    {
        if (!m_joined)
            return;

        sf::Packet frame = beginRelay(RelayMessage::Data);
        frame << kInvalidChannel;   // the relay stamps the real one
        appendPayload(frame, packet);
        m_outgoing.push_back(std::move(frame));
    }

    // ------------------------------------------------------------- listing --

    bool queryRelaySessions(const std::string& relayHost, std::uint16_t relayPort,
        std::vector<SessionAdvert>& out, std::wstring& error, float timeoutSeconds)
    {
        out.clear();

        if (relayHost.empty())
        {
            error = L"no relay is configured";
            return false;
        }

        const std::optional<sf::IpAddress> resolved = resolveAddress(relayHost);
        if (!resolved.has_value())
        {
            error = L"could not resolve the relay address";
            return false;
        }

        sf::TcpSocket socket;
        if (socket.connect(*resolved, relayPort, sf::seconds(timeoutSeconds)) != sf::Socket::Status::Done)
        {
            error = L"could not reach the relay";
            return false;
        }

        sf::Packet request = beginRelay(RelayMessage::ListSessions);
        if (socket.send(request) != sf::Socket::Status::Done)
        {
            error = L"the relay did not accept the request";
            return false;
        }

        // Blocking read with the socket's own timeout. This runs from a menu
        // action rather than the frame loop, and the relay answers immediately
        // or not at all.
        sf::Packet reply;
        if (socket.receive(reply) != sf::Socket::Status::Done)
        {
            error = L"the relay did not answer";
            return false;
        }

        RelayMessage id{};
        if (!readRelayHeader(reply, id) || id != RelayMessage::SessionList)
        {
            error = L"the relay sent something unexpected";
            return false;
        }

        std::uint32_t count = 0;
        reply >> count;

        if (!reply || count > 64)
        {
            error = L"the relay sent a malformed list";
            return false;
        }

        for (std::uint32_t i = 0; i < count; ++i)
        {
            SessionAdvert advert;
            reply >> advert;
            if (!reply)
                break;
            out.push_back(std::move(advert));
        }

        return true;
    }
}
