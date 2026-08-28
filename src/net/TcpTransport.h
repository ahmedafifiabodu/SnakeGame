#pragma once

#include "SocketOptions.h"
#include "Transport.h"

#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/TcpListener.hpp>
#include <SFML/Network/TcpSocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace neoncoil::net
{
    // TCP over SFML's own network module -- the same dependency the game
    // already builds, so multiplayer adds no third-party library.
    //
    // Why TCP for a real-time game: the whole session is four players at a
    // twenty-hertz snapshot rate with roughly a kilobyte per snapshot, and every
    // message the protocol defines is one the receiver genuinely must not miss
    // (lobby state, match start, wall destruction). Reimplementing reliability
    // and ordering on UDP would buy back one round trip on a packet loss and
    // cost several hundred lines that can be got wrong. The ServerTransport /
    // ClientTransport split exists precisely so a UDP or relay backend can be
    // added later without the session layer changing.
    class TcpServerTransport : public ServerTransport
    {
    public:
        TcpServerTransport() = default;
        ~TcpServerTransport() override;

        TcpServerTransport(const TcpServerTransport&) = delete;
        TcpServerTransport& operator=(const TcpServerTransport&) = delete;

        bool start(std::uint16_t port, std::wstring& error) override;
        void stop() override;
        void pump() override;
        bool isRunning() const override { return m_running; }
        std::uint16_t port() const override { return m_port; }

        bool poll(TransportEvent& out) override;

        void send(PeerId peer, sf::Packet packet) override;
        void broadcast(const sf::Packet& packet) override;
        void broadcastExcept(PeerId exclude, const sf::Packet& packet) override;
        void disconnect(PeerId peer) override;

        int peerCount() const override { return static_cast<int>(m_peers.size()); }

    private:
        struct Peer
        {
            PeerId id{ kInvalidPeer };
            std::unique_ptr<GameSocket> socket;
            std::deque<sf::Packet> outgoing;

            // Set when the peer is gone or being dropped. The peer is only
            // erased at the end of pump(), so nothing iterating can be
            // invalidated underneath itself.
            bool dead{ false };
            std::wstring reason;
        };

        Peer* findPeer(PeerId id);
        void flush(Peer& peer);
        void receive(Peer& peer);

        sf::TcpListener m_listener;
        std::vector<std::unique_ptr<Peer>> m_peers;
        std::deque<TransportEvent> m_events;

        bool m_running{ false };
        std::uint16_t m_port{ 0 };
        PeerId m_nextPeerId{ 1 };
    };

    class TcpClientTransport : public ClientTransport
    {
    public:
        TcpClientTransport() = default;
        ~TcpClientTransport() override;

        TcpClientTransport(const TcpClientTransport&) = delete;
        TcpClientTransport& operator=(const TcpClientTransport&) = delete;

        bool beginConnect(const std::string& address, std::uint16_t port, std::wstring& error) override;
        void disconnect() override;
        bool isConnected() const override { return m_connected; }

        void pump() override;
        bool poll(TransportEvent& out) override;
        void send(sf::Packet packet) override;

    private:
        void joinConnectThread();

        // Name resolution and the connect handshake both block, and a menu that
        // freezes for the length of a TCP timeout is not shippable. One
        // short-lived thread owns the socket until it reports a result; the game
        // thread only reads the atomic until then, so there is nothing to lock.
        enum class ConnectState : int
        {
            Idle = 0,
            Pending,
            Succeeded,
            Failed
        };

        std::unique_ptr<GameSocket> m_socket;
        std::thread m_connectThread;
        std::atomic<ConnectState> m_connectState{ ConnectState::Idle };
        std::wstring m_failure;

        std::deque<sf::Packet> m_outgoing;
        std::deque<TransportEvent> m_events;
        bool m_connected{ false };
    };
}
