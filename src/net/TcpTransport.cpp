#include "TcpTransport.h"

#include "NetConfig.h"

#include <SFML/Network/Dns.hpp>
#include <SFML/System/Time.hpp>

#include <algorithm>
#include <optional>

namespace neoncoil::net
{
    namespace
    {
        // A hard ceiling on how much one peer can be drained in a single frame.
        // Without it a peer that floods could starve every other peer, and a
        // malicious one could hold the game thread indefinitely.
        constexpr int kMaxReceivesPerPumpPerPeer = 64;

        std::optional<sf::IpAddress> resolveAddress(const std::string& address)
        {
            // Dotted quads and IPv6 literals resolve without touching DNS, which
            // is the case that matters: LAN discovery hands us a literal, and a
            // player typing an address types a literal too.
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

    // ------------------------------------------------------------------ server

    TcpServerTransport::~TcpServerTransport()
    {
        stop();
    }

    bool TcpServerTransport::start(std::uint16_t port, std::wstring& error)
    {
        stop();

        if (m_listener.listen(port) != sf::Socket::Status::Done)
        {
            error = L"could not listen on port " + std::to_wstring(port) +
                L" -- another session may already be hosting on this machine";
            return false;
        }

        m_listener.setBlocking(false);
        m_port = m_listener.getLocalPort();
        m_running = true;
        return true;
    }

    void TcpServerTransport::stop()
    {
        if (!m_running && m_peers.empty())
            return;

        for (std::unique_ptr<Peer>& peer : m_peers)
            if (peer && peer->socket)
                peer->socket->disconnect();

        m_peers.clear();
        m_events.clear();
        m_listener.close();
        m_running = false;
        m_port = 0;
    }

    TcpServerTransport::Peer* TcpServerTransport::findPeer(PeerId id)
    {
        for (std::unique_ptr<Peer>& peer : m_peers)
            if (peer && peer->id == id)
                return peer.get();
        return nullptr;
    }

    void TcpServerTransport::flush(Peer& peer)
    {
        while (!peer.outgoing.empty() && !peer.dead)
        {
            // SFML tracks the send offset inside the packet itself, so a Partial
            // has to be retried with the very same object -- hence the queue.
            const sf::Socket::Status status = peer.socket->send(peer.outgoing.front());

            if (status == sf::Socket::Status::Done)
            {
                peer.outgoing.pop_front();
                continue;
            }

            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady)
                return;   // the send buffer is full; try again next frame

            peer.dead = true;
            peer.reason = L"connection lost";
            return;
        }
    }

    void TcpServerTransport::receive(Peer& peer)
    {
        for (int i = 0; i < kMaxReceivesPerPumpPerPeer && !peer.dead; ++i)
        {
            sf::Packet packet;
            const sf::Socket::Status status = peer.socket->receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                TransportEvent event;
                event.type = TransportEventType::Message;
                event.peer = peer.id;
                event.packet = std::move(packet);
                m_events.push_back(std::move(event));
                continue;
            }

            // Partial: SFML has buffered what arrived and will complete the
            // packet on a later call. Nothing to do but come back next frame.
            if (status == sf::Socket::Status::NotReady || status == sf::Socket::Status::Partial)
                return;

            peer.dead = true;
            peer.reason = status == sf::Socket::Status::Disconnected ? L"peer disconnected" : L"socket error";
            return;
        }
    }

    void TcpServerTransport::pump()
    {
        if (!m_running)
            return;

        // --- accept -----------------------------------------------------------
        for (;;)
        {
            auto socket = std::make_unique<sf::TcpSocket>();
            if (m_listener.accept(*socket) != sf::Socket::Status::Done)
                break;

            socket->setBlocking(false);

            auto peer = std::make_unique<Peer>();
            peer->id = m_nextPeerId++;
            peer->socket = std::move(socket);

            TransportEvent event;
            event.type = TransportEventType::Connected;
            event.peer = peer->id;
            m_events.push_back(std::move(event));

            m_peers.push_back(std::move(peer));
        }

        // --- service ----------------------------------------------------------
        for (std::unique_ptr<Peer>& peer : m_peers)
        {
            if (!peer || peer->dead)
                continue;
            flush(*peer);
            receive(*peer);
        }

        // --- reap -------------------------------------------------------------
        for (std::unique_ptr<Peer>& peer : m_peers)
        {
            if (!peer || !peer->dead)
                continue;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.peer = peer->id;
            event.reason = peer->reason.empty() ? L"connection closed" : peer->reason;
            m_events.push_back(std::move(event));

            peer->socket->disconnect();
        }

        m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(),
            [](const std::unique_ptr<Peer>& peer) { return !peer || peer->dead; }),
            m_peers.end());
    }

    bool TcpServerTransport::poll(TransportEvent& out)
    {
        if (m_events.empty())
            return false;

        out = std::move(m_events.front());
        m_events.pop_front();
        return true;
    }

    void TcpServerTransport::send(PeerId id, sf::Packet packet)
    {
        if (Peer* peer = findPeer(id); peer != nullptr && !peer->dead)
            peer->outgoing.push_back(std::move(packet));
    }

    void TcpServerTransport::broadcast(const sf::Packet& packet)
    {
        broadcastExcept(kInvalidPeer, packet);
    }

    void TcpServerTransport::broadcastExcept(PeerId exclude, const sf::Packet& packet)
    {
        for (std::unique_ptr<Peer>& peer : m_peers)
        {
            if (!peer || peer->dead || peer->id == exclude)
                continue;
            peer->outgoing.push_back(packet);
        }
    }

    void TcpServerTransport::disconnect(PeerId id)
    {
        if (Peer* peer = findPeer(id); peer != nullptr && !peer->dead)
        {
            // Give queued bytes -- typically a Rejected or Kicked message -- one
            // last chance to leave before the socket goes away.
            flush(*peer);
            peer->dead = true;
            peer->reason = L"disconnected by host";
        }
    }

    // ------------------------------------------------------------------ client

    TcpClientTransport::~TcpClientTransport()
    {
        disconnect();
    }

    bool TcpClientTransport::beginConnect(const std::string& address, std::uint16_t port, std::wstring& error)
    {
        disconnect();

        if (address.empty())
        {
            error = L"no address given";
            return false;
        }

        m_socket = std::make_unique<sf::TcpSocket>();
        m_failure.clear();
        m_connectState.store(ConnectState::Pending);

        const float timeout = NetConfig::instance().connectTimeoutSeconds;

        m_connectThread = std::thread([this, address, port, timeout]
        {
            const std::optional<sf::IpAddress> resolved = resolveAddress(address);
            if (!resolved.has_value())
            {
                m_failure = L"could not resolve that address";
                m_connectState.store(ConnectState::Failed);
                return;
            }

            const sf::Socket::Status status = m_socket->connect(*resolved, port, sf::seconds(timeout));
            if (status != sf::Socket::Status::Done)
            {
                m_failure = L"no session answered on that address";
                m_connectState.store(ConnectState::Failed);
                return;
            }

            m_connectState.store(ConnectState::Succeeded);
        });

        return true;
    }

    void TcpClientTransport::joinConnectThread()
    {
        if (m_connectThread.joinable())
            m_connectThread.join();
    }

    void TcpClientTransport::disconnect()
    {
        joinConnectThread();

        if (m_socket)
            m_socket->disconnect();

        m_socket.reset();
        m_outgoing.clear();
        m_events.clear();
        m_connected = false;
        m_connectState.store(ConnectState::Idle);
    }

    void TcpClientTransport::pump()
    {
        // --- resolve a pending connect ---------------------------------------
        const ConnectState state = m_connectState.load();

        if (state == ConnectState::Pending)
            return;

        if (state == ConnectState::Failed)
        {
            joinConnectThread();

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = m_failure.empty() ? L"could not connect" : m_failure;
            m_events.push_back(std::move(event));

            m_socket.reset();
            m_connectState.store(ConnectState::Idle);
            return;
        }

        if (state == ConnectState::Succeeded)
        {
            joinConnectThread();
            m_socket->setBlocking(false);
            m_connected = true;
            m_connectState.store(ConnectState::Idle);

            TransportEvent event;
            event.type = TransportEventType::Connected;
            m_events.push_back(std::move(event));
        }

        if (!m_connected || !m_socket)
            return;

        // --- flush ------------------------------------------------------------
        while (!m_outgoing.empty())
        {
            const sf::Socket::Status status = m_socket->send(m_outgoing.front());

            if (status == sf::Socket::Status::Done)
            {
                m_outgoing.pop_front();
                continue;
            }

            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady)
                break;

            m_connected = false;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = L"connection lost";
            m_events.push_back(std::move(event));
            return;
        }

        // --- receive ----------------------------------------------------------
        for (int i = 0; i < kMaxReceivesPerPumpPerPeer; ++i)
        {
            sf::Packet packet;
            const sf::Socket::Status status = m_socket->receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                TransportEvent event;
                event.type = TransportEventType::Message;
                event.packet = std::move(packet);
                m_events.push_back(std::move(event));
                continue;
            }

            if (status == sf::Socket::Status::NotReady || status == sf::Socket::Status::Partial)
                return;

            m_connected = false;

            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.reason = status == sf::Socket::Status::Disconnected
                ? L"the host closed the session"
                : L"connection lost";
            m_events.push_back(std::move(event));
            return;
        }
    }

    bool TcpClientTransport::poll(TransportEvent& out)
    {
        if (m_events.empty())
            return false;

        out = std::move(m_events.front());
        m_events.pop_front();
        return true;
    }

    void TcpClientTransport::send(sf::Packet packet)
    {
        if (m_connected)
            m_outgoing.push_back(std::move(packet));
    }
}
