#pragma once

#include "RelayProtocol.h"
#include "Transport.h"

#include <SFML/Network/TcpSocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace neoncoil::net
{
    // The same ServerTransport / ClientTransport contract as the direct TCP
    // pair, carried over a relay instead of a direct socket.
    //
    // This is the payoff of having made transport an interface: HostSession,
    // ClientSession, the protocol, the lobby and the simulation are untouched by
    // any of this. They still see peers connecting, messages arriving and peers
    // leaving. What changed is only how the bytes get there -- and with it, the
    // requirement that somebody configure a router.
    //
    // Both sides dial OUT to the relay, which is the entire trick.

    class RelayServerTransport : public ServerTransport
    {
    public:
        RelayServerTransport(std::string relayHost, std::uint16_t relayPort, SessionAdvert advert);
        ~RelayServerTransport() override;

        RelayServerTransport(const RelayServerTransport&) = delete;
        RelayServerTransport& operator=(const RelayServerTransport&) = delete;

        // `port` is ignored: a relayed host does not listen on anything. Returns
        // true once the connect attempt is under way; registration completes
        // asynchronously and is reported through isRunning() and code().
        bool start(std::uint16_t port, std::wstring& error) override;
        void stop() override;
        void pump() override;
        bool isRunning() const override { return m_registered; }
        std::uint16_t port() const override { return 0; }

        bool poll(TransportEvent& out) override;

        void send(PeerId peer, sf::Packet packet) override;
        void broadcast(const sf::Packet& packet) override;
        void broadcastExcept(PeerId exclude, const sf::Packet& packet) override;
        void disconnect(PeerId peer) override;

        int peerCount() const override { return static_cast<int>(m_channels.size()); }

        // The join code the relay handed out. Empty until registration lands.
        const std::wstring& code() const { return m_code; }
        bool failed() const { return m_failed; }
        const std::wstring& failure() const { return m_failure; }

        // Player counts change, and the relay's session list should say so.
        void updateAdvert(const SessionAdvert& advert);

    private:
        enum class LinkState : int
        {
            Idle = 0,
            Connecting,
            Connected,
            Failed
        };

        void joinConnectThread();
        void sendFrame(sf::Packet packet);
        void flush();
        void receive();
        void handle(sf::Packet& packet);
        void fail(std::wstring reason);

        std::string m_relayHost;
        std::uint16_t m_relayPort{ 0 };
        SessionAdvert m_advert;

        std::unique_ptr<sf::TcpSocket> m_socket;
        std::thread m_connectThread;
        std::atomic<LinkState> m_linkState{ LinkState::Idle };
        std::wstring m_connectFailure;

        std::deque<sf::Packet> m_outgoing;
        std::deque<TransportEvent> m_events;

        // PeerId is what the session layer speaks; RelayChannel is what the
        // relay speaks. Kept apart so neither leaks into the other.
        std::unordered_map<PeerId, RelayChannel> m_channels;
        std::unordered_map<RelayChannel, PeerId> m_peers;
        PeerId m_nextPeerId{ 1 };

        std::wstring m_code;
        std::wstring m_failure;
        bool m_registered{ false };
        bool m_failed{ false };
    };

    class RelayClientTransport : public ClientTransport
    {
    public:
        RelayClientTransport(std::string relayHost, std::uint16_t relayPort, std::wstring code);
        ~RelayClientTransport() override;

        RelayClientTransport(const RelayClientTransport&) = delete;
        RelayClientTransport& operator=(const RelayClientTransport&) = delete;

        // The address and port arguments are ignored: a relayed guest connects
        // to the relay and asks for a code, not to a host.
        bool beginConnect(const std::string& address, std::uint16_t port, std::wstring& error) override;
        void disconnect() override;
        bool isConnected() const override { return m_joined; }

        void pump() override;
        bool poll(TransportEvent& out) override;
        void send(sf::Packet packet) override;

    private:
        enum class LinkState : int
        {
            Idle = 0,
            Connecting,
            Connected,
            Failed
        };

        void joinConnectThread();
        void flush();
        void receive();
        void handle(sf::Packet& packet);

        std::string m_relayHost;
        std::uint16_t m_relayPort{ 0 };
        std::wstring m_code;

        std::unique_ptr<sf::TcpSocket> m_socket;
        std::thread m_connectThread;
        std::atomic<LinkState> m_linkState{ LinkState::Idle };
        std::wstring m_connectFailure;

        std::deque<sf::Packet> m_outgoing;
        std::deque<TransportEvent> m_events;

        bool m_linked{ false };   // socket up
        bool m_joined{ false };   // relay has put us through to the host
    };

    // Asks a relay what is open. One shot: connects, asks, reads, closes.
    // Blocking with a short timeout, because it runs from a menu action rather
    // than from the frame loop.
    bool queryRelaySessions(const std::string& relayHost, std::uint16_t relayPort,
        std::vector<SessionAdvert>& out, std::wstring& error, float timeoutSeconds = 3.0f);
}
