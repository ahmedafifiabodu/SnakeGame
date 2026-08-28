#pragma once

#include "../net/RelayProtocol.h"
#include "../net/SocketOptions.h"

#include <SFML/Network/TcpListener.hpp>
#include <SFML/Network/TcpSocket.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neoncoil::relay
{
    struct RelayStats
    {
        std::uint64_t sessionsOpened{ 0 };
        std::uint64_t guestsJoined{ 0 };
        std::uint64_t framesForwarded{ 0 };
        std::uint64_t bytesForwarded{ 0 };
        int liveSessions{ 0 };
        int liveConnections{ 0 };
    };

    // Rendezvous and byte-pump for players who cannot accept inbound
    // connections -- which, on a home router, is all of them.
    //
    // It knows nothing about snakes. It moves opaque payloads between one host
    // connection and up to four guest connections, keyed by a short code. That
    // is the entire job, and keeping it that small is what makes it cheap to run
    // and safe to leave up: a game update never needs a relay update, and a bug
    // in the game rules cannot be a bug in the relay.
    //
    // Single-threaded and poll-driven, like the rest of the networking in this
    // project, so it can be pumped inline by the self-test as easily as it can
    // be run as a daemon.
    class RelayServer
    {
    public:
        RelayServer() = default;
        ~RelayServer();

        RelayServer(const RelayServer&) = delete;
        RelayServer& operator=(const RelayServer&) = delete;

        bool start(std::uint16_t port, std::wstring& error);
        void stop();
        bool isRunning() const { return m_running; }
        std::uint16_t port() const { return m_port; }

        // One pass: accept, read, forward, reap. Call it in a loop.
        void pump();

        const RelayStats& stats() const { return m_stats; }

        // Sessions with no guests and no traffic for this long are dropped, so a
        // crashed host does not hold its code forever.
        void setIdleTimeoutSeconds(float seconds) { m_idleTimeout = seconds; }
        void tickTimers(float deltaSeconds);

        void setVerbose(bool verbose) { m_verbose = verbose; }

        // One letter, stamped on the front of every code this relay hands out,
        // so a guest typing that code can be dialled to this relay rather than
        // to whichever one their build happens to list first. Set it per
        // deployment -- 'M' for the Middle East box, 'E' for the European one.
        void setRegionTag(wchar_t tag) { m_regionTag = tag; }
        wchar_t regionTag() const { return m_regionTag; }

    private:
        struct Connection
        {
            std::uint32_t id{ 0 };
            std::unique_ptr<net::GameSocket> socket;
            std::deque<sf::Packet> outgoing;
            bool dead{ false };

            // Set once the connection has said what it is. An unclassified
            // connection may only send RegisterHost, JoinByCode or ListSessions.
            bool isHost{ false };
            bool isGuest{ false };

            std::wstring code;              // host: its own code; guest: its host's
            net::RelayChannel channel{ net::kInvalidChannel };   // guest only
            float secondsSinceHello{ 0.0f };
        };

        struct Session
        {
            std::wstring code;
            std::uint32_t hostConnection{ 0 };
            net::SessionAdvert advert;
            std::unordered_map<net::RelayChannel, std::uint32_t> guests;  // channel -> connection id
            net::RelayChannel nextChannel{ 1 };
            float secondsIdle{ 0.0f };
        };

        Connection* find(std::uint32_t id);
        Session* sessionFor(const std::wstring& code);

        void accept();
        void service(Connection& connection);
        void receive(Connection& connection);
        void flush(Connection& connection);
        void reap();

        void handle(Connection& connection, sf::Packet& packet);
        void handleRegisterHost(Connection& connection, sf::Packet& packet);
        void handleJoinByCode(Connection& connection, sf::Packet& packet);
        void handleListSessions(Connection& connection);
        void handleFromHost(Connection& connection, net::RelayMessage id, sf::Packet& packet);
        void handleFromGuest(Connection& connection, sf::Packet& packet);

        void reject(Connection& connection, net::RelayReject reason);
        void send(std::uint32_t connectionId, sf::Packet packet);
        void closeSession(Session& session, net::RelayReject reason);
        void dropGuest(Session& session, net::RelayChannel channel, bool tellHost);

        std::wstring allocateCode();
        void log(const std::string& line) const;

        sf::TcpListener m_listener;
        std::vector<std::unique_ptr<Connection>> m_connections;
        std::unordered_map<std::wstring, Session> m_sessions;

        bool m_running{ false };
        bool m_verbose{ false };
        std::uint16_t m_port{ 0 };
        std::uint32_t m_nextConnectionId{ 1 };
        std::uint64_t m_codeSeed{ 0 };
        float m_idleTimeout{ 300.0f };
        wchar_t m_regionTag{ 0 };

        RelayStats m_stats;
    };
}
