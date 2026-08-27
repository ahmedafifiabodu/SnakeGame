#include "RelayServer.h"

#include "../core/Rng.h"

#include <algorithm>
#include <iostream>

namespace neoncoil::relay
{
    namespace
    {
        constexpr int kMaxReceivesPerPump = 64;
        constexpr int kMaxConnections = 512;
        constexpr int kMaxSessions = 128;

        // A connection that opens and then says nothing is a port scanner or a
        // crashed client. It does not get to hold a socket open.
        constexpr float kHelloGraceSeconds = 10.0f;

        std::string narrow(const std::wstring& text)
        {
            std::string out;
            out.reserve(text.size());
            for (wchar_t c : text)
                out.push_back(c < 128 ? static_cast<char>(c) : '?');
            return out;
        }
    }

    RelayServer::~RelayServer()
    {
        stop();
    }

    void RelayServer::log(const std::string& line) const
    {
        if (m_verbose)
            std::cout << "[relay] " << line << "\n";
    }

    bool RelayServer::start(std::uint16_t port, std::wstring& error)
    {
        stop();

        if (m_listener.listen(port) != sf::Socket::Status::Done)
        {
            error = L"could not listen on port " + std::to_wstring(port);
            return false;
        }

        m_listener.setBlocking(false);
        m_port = m_listener.getLocalPort();
        m_running = true;
        m_codeSeed = Rng::seedFromClock();

        log("listening on port " + std::to_string(m_port));
        return true;
    }

    void RelayServer::stop()
    {
        if (!m_running && m_connections.empty())
            return;

        for (std::unique_ptr<Connection>& connection : m_connections)
            if (connection && connection->socket)
                connection->socket->disconnect();

        m_connections.clear();
        m_sessions.clear();
        m_listener.close();
        m_running = false;
        m_port = 0;
    }

    RelayServer::Connection* RelayServer::find(std::uint32_t id)
    {
        for (std::unique_ptr<Connection>& connection : m_connections)
            if (connection && connection->id == id)
                return connection.get();
        return nullptr;
    }

    RelayServer::Session* RelayServer::sessionFor(const std::wstring& code)
    {
        const auto it = m_sessions.find(code);
        return it == m_sessions.end() ? nullptr : &it->second;
    }

    std::wstring RelayServer::allocateCode()
    {
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            m_codeSeed = Rng::mix(m_codeSeed, static_cast<std::uint64_t>(attempt) + 1);
            std::wstring code = net::makeRelayCode(m_codeSeed);

            if (m_sessions.find(code) == m_sessions.end())
                return code;
        }

        return {};   // wildly improbable; the caller reports RelayFull
    }

    void RelayServer::send(std::uint32_t connectionId, sf::Packet packet)
    {
        if (Connection* connection = find(connectionId); connection != nullptr && !connection->dead)
            connection->outgoing.push_back(std::move(packet));
    }

    void RelayServer::reject(Connection& connection, net::RelayReject reason)
    {
        sf::Packet packet = net::beginRelay(net::RelayMessage::Rejected);
        packet << static_cast<std::uint8_t>(reason);
        connection.outgoing.push_back(std::move(packet));

        // Flushed now so the reason actually reaches them before the socket goes.
        flush(connection);
        connection.dead = true;
    }

    // ------------------------------------------------------------ connections --

    void RelayServer::accept()
    {
        for (;;)
        {
            auto socket = std::make_unique<sf::TcpSocket>();
            if (m_listener.accept(*socket) != sf::Socket::Status::Done)
                return;

            if (static_cast<int>(m_connections.size()) >= kMaxConnections)
            {
                socket->disconnect();
                continue;
            }

            socket->setBlocking(false);

            auto connection = std::make_unique<Connection>();
            connection->id = m_nextConnectionId++;
            connection->socket = std::move(socket);
            m_connections.push_back(std::move(connection));
        }
    }

    void RelayServer::flush(Connection& connection)
    {
        while (!connection.outgoing.empty() && !connection.dead)
        {
            const sf::Socket::Status status = connection.socket->send(connection.outgoing.front());

            if (status == sf::Socket::Status::Done)
            {
                connection.outgoing.pop_front();
                continue;
            }

            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady)
                return;

            connection.dead = true;
            return;
        }
    }

    void RelayServer::receive(Connection& connection)
    {
        for (int i = 0; i < kMaxReceivesPerPump && !connection.dead; ++i)
        {
            sf::Packet packet;
            const sf::Socket::Status status = connection.socket->receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                handle(connection, packet);
                continue;
            }

            if (status == sf::Socket::Status::NotReady || status == sf::Socket::Status::Partial)
                return;

            connection.dead = true;
            return;
        }
    }

    void RelayServer::service(Connection& connection)
    {
        flush(connection);
        receive(connection);
    }

    void RelayServer::pump()
    {
        if (!m_running)
            return;

        accept();

        for (std::unique_ptr<Connection>& connection : m_connections)
        {
            if (!connection || connection->dead)
                continue;
            service(*connection);
        }

        reap();
    }

    void RelayServer::reap()
    {
        for (std::unique_ptr<Connection>& connection : m_connections)
        {
            if (!connection || !connection->dead)
                continue;

            if (connection->isHost)
            {
                if (Session* session = sessionFor(connection->code); session != nullptr)
                {
                    // The host is gone, so every guest in that session is told
                    // why rather than being left on a socket that will never
                    // speak again.
                    closeSession(*session, net::RelayReject::HostGone);
                    m_sessions.erase(connection->code);
                    log("session " + narrow(connection->code) + " closed (host left)");
                }
            }
            else if (connection->isGuest)
            {
                if (Session* session = sessionFor(connection->code); session != nullptr)
                    dropGuest(*session, connection->channel, true);
            }

            connection->socket->disconnect();
        }

        m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
            [](const std::unique_ptr<Connection>& connection) { return !connection || connection->dead; }),
            m_connections.end());

        m_stats.liveConnections = static_cast<int>(m_connections.size());
        m_stats.liveSessions = static_cast<int>(m_sessions.size());
    }

    void RelayServer::closeSession(Session& session, net::RelayReject reason)
    {
        for (const auto& [channel, connectionId] : session.guests)
        {
            sf::Packet packet = net::beginRelay(net::RelayMessage::Rejected);
            packet << static_cast<std::uint8_t>(reason);
            send(connectionId, std::move(packet));

            if (Connection* guest = find(connectionId); guest != nullptr)
            {
                flush(*guest);
                guest->dead = true;
            }
        }

        session.guests.clear();
    }

    void RelayServer::dropGuest(Session& session, net::RelayChannel channel, bool tellHost)
    {
        if (session.guests.erase(channel) == 0)
            return;

        if (!tellHost)
            return;

        sf::Packet packet = net::beginRelay(net::RelayMessage::PeerLeft);
        packet << channel;
        send(session.hostConnection, std::move(packet));
    }

    // --------------------------------------------------------------- messages --

    void RelayServer::handle(Connection& connection, sf::Packet& packet)
    {
        net::RelayMessage id{};
        if (!net::readRelayHeader(packet, id))
        {
            reject(connection, net::RelayReject::BadProtocol);
            return;
        }

        if (connection.isHost)
        {
            handleFromHost(connection, id, packet);
            return;
        }

        if (connection.isGuest)
        {
            if (id == net::RelayMessage::Data)
                handleFromGuest(connection, packet);
            return;
        }

        switch (id)
        {
        case net::RelayMessage::RegisterHost: handleRegisterHost(connection, packet); break;
        case net::RelayMessage::JoinByCode:   handleJoinByCode(connection, packet); break;
        case net::RelayMessage::ListSessions: handleListSessions(connection); break;
        default:
            // Anything else before identifying yourself is not a client we know.
            reject(connection, net::RelayReject::BadProtocol);
            break;
        }
    }

    void RelayServer::handleRegisterHost(Connection& connection, sf::Packet& packet)
    {
        std::uint16_t version = 0;
        net::SessionAdvert advert;
        packet >> version >> advert;

        if (!packet)
        {
            reject(connection, net::RelayReject::BadProtocol);
            return;
        }

        if (version != net::kRelayVersion)
        {
            reject(connection, net::RelayReject::VersionMismatch);
            return;
        }

        if (static_cast<int>(m_sessions.size()) >= kMaxSessions)
        {
            reject(connection, net::RelayReject::RelayFull);
            return;
        }

        const std::wstring code = allocateCode();
        if (code.empty())
        {
            reject(connection, net::RelayReject::RelayFull);
            return;
        }

        Session session;
        session.code = code;
        session.hostConnection = connection.id;
        session.advert = advert;
        session.advert.code = code;
        m_sessions.emplace(code, std::move(session));

        connection.isHost = true;
        connection.code = code;

        sf::Packet reply = net::beginRelay(net::RelayMessage::Registered);
        reply << code;
        connection.outgoing.push_back(std::move(reply));

        ++m_stats.sessionsOpened;
        log("session " + narrow(code) + " opened by " + narrow(advert.hostName));
    }

    void RelayServer::handleJoinByCode(Connection& connection, sf::Packet& packet)
    {
        std::uint16_t version = 0;
        std::wstring code;
        packet >> version >> code;

        if (!packet)
        {
            reject(connection, net::RelayReject::BadProtocol);
            return;
        }

        if (version != net::kRelayVersion)
        {
            reject(connection, net::RelayReject::VersionMismatch);
            return;
        }

        Session* session = sessionFor(net::normaliseCode(code));
        if (session == nullptr)
        {
            reject(connection, net::RelayReject::UnknownCode);
            return;
        }

        if (session->advert.inMatch)
        {
            reject(connection, net::RelayReject::SessionInMatch);
            return;
        }

        // The relay enforces the seat count too. It has to: the host's own limit
        // is behind the game protocol the relay deliberately cannot read, and a
        // fifth socket held open costs the relay whether the host wants it or not.
        if (static_cast<int>(session->guests.size()) + 1 >= static_cast<int>(session->advert.maxPlayers))
        {
            reject(connection, net::RelayReject::SessionFull);
            return;
        }

        const net::RelayChannel channel = session->nextChannel++;
        session->guests.emplace(channel, connection.id);
        session->secondsIdle = 0.0f;

        connection.isGuest = true;
        connection.code = session->code;
        connection.channel = channel;

        sf::Packet accepted = net::beginRelay(net::RelayMessage::JoinAccepted);
        accepted << channel;
        connection.outgoing.push_back(std::move(accepted));

        sf::Packet joined = net::beginRelay(net::RelayMessage::PeerJoined);
        joined << channel;
        send(session->hostConnection, std::move(joined));

        ++m_stats.guestsJoined;
        log("guest joined session " + narrow(session->code));
    }

    void RelayServer::handleListSessions(Connection& connection)
    {
        sf::Packet reply = net::beginRelay(net::RelayMessage::SessionList);

        std::vector<const Session*> open;
        for (const auto& [code, session] : m_sessions)
        {
            if (session.advert.inMatch)
                continue;
            if (static_cast<int>(session.guests.size()) + 1 >= static_cast<int>(session.advert.maxPlayers))
                continue;
            open.push_back(&session);
        }

        // Fullest first, so a browsing player gathers into an existing lobby
        // rather than starting a fourth half-empty one.
        std::sort(open.begin(), open.end(), [](const Session* a, const Session* b)
        {
            return a->guests.size() > b->guests.size();
        });

        if (open.size() > 32)
            open.resize(32);

        reply << static_cast<std::uint32_t>(open.size());
        for (const Session* session : open)
        {
            net::SessionAdvert advert = session->advert;
            advert.players = static_cast<std::uint8_t>(session->guests.size() + 1);
            reply << advert;
        }

        connection.outgoing.push_back(std::move(reply));

        // A pure lister is not a host or a guest and has nothing else to say,
        // so it is closed once answered rather than left holding a socket.
        flush(connection);
        connection.dead = true;
    }

    void RelayServer::handleFromHost(Connection& connection, net::RelayMessage id, sf::Packet& packet)
    {
        Session* session = sessionFor(connection.code);
        if (session == nullptr)
        {
            connection.dead = true;
            return;
        }

        session->secondsIdle = 0.0f;

        switch (id)
        {
        case net::RelayMessage::UpdateAdvert:
        {
            net::SessionAdvert advert;
            packet >> advert;
            if (packet)
            {
                advert.code = session->code;   // the relay owns the code, not the host
                session->advert = advert;
            }
            break;
        }

        case net::RelayMessage::Data:
        {
            net::RelayChannel channel = net::kInvalidChannel;
            sf::Packet payload;
            packet >> channel;

            if (!packet || !net::extractPayload(packet, payload))
                break;

            const auto it = session->guests.find(channel);
            if (it == session->guests.end())
                break;

            sf::Packet frame = net::beginRelay(net::RelayMessage::Data);
            frame << channel;
            net::appendPayload(frame, payload);

            m_stats.bytesForwarded += payload.getDataSize();
            ++m_stats.framesForwarded;
            send(it->second, std::move(frame));
            break;
        }

        case net::RelayMessage::Broadcast:
        {
            sf::Packet payload;
            if (!net::extractPayload(packet, payload))
                break;

            // The host uploads one copy and the relay makes the rest. On a home
            // connection the upstream is the narrow pipe, so this is the
            // difference between one snapshot on the wire and three.
            for (const auto& [channel, connectionId] : session->guests)
            {
                sf::Packet frame = net::beginRelay(net::RelayMessage::Data);
                frame << channel;
                net::appendPayload(frame, payload);

                m_stats.bytesForwarded += payload.getDataSize();
                ++m_stats.framesForwarded;
                send(connectionId, std::move(frame));
            }
            break;
        }

        case net::RelayMessage::CloseChannel:
        {
            net::RelayChannel channel = net::kInvalidChannel;
            packet >> channel;
            if (!packet)
                break;

            if (const auto it = session->guests.find(channel); it != session->guests.end())
            {
                if (Connection* guest = find(it->second); guest != nullptr)
                    guest->dead = true;
                session->guests.erase(channel);
            }
            break;
        }

        default:
            break;
        }
    }

    void RelayServer::handleFromGuest(Connection& connection, sf::Packet& packet)
    {
        Session* session = sessionFor(connection.code);
        if (session == nullptr)
        {
            connection.dead = true;
            return;
        }

        session->secondsIdle = 0.0f;

        // A guest sends Data with no channel of its own: it has exactly one
        // destination, and letting it name a channel would let it impersonate
        // another player.
        net::RelayChannel ignored = net::kInvalidChannel;
        sf::Packet payload;
        packet >> ignored;

        if (!packet || !net::extractPayload(packet, payload))
            return;

        sf::Packet frame = net::beginRelay(net::RelayMessage::Data);
        frame << connection.channel;
        net::appendPayload(frame, payload);

        m_stats.bytesForwarded += payload.getDataSize();
        ++m_stats.framesForwarded;
        send(session->hostConnection, std::move(frame));
    }

    // ----------------------------------------------------------------- timers --

    void RelayServer::tickTimers(float deltaSeconds)
    {
        for (std::unique_ptr<Connection>& connection : m_connections)
        {
            if (!connection || connection->dead)
                continue;

            if (connection->isHost || connection->isGuest)
                continue;

            connection->secondsSinceHello += deltaSeconds;
            if (connection->secondsSinceHello > kHelloGraceSeconds)
                connection->dead = true;
        }

        std::vector<std::wstring> expired;
        for (auto& [code, session] : m_sessions)
        {
            session.secondsIdle += deltaSeconds;
            if (session.secondsIdle > m_idleTimeout)
                expired.push_back(code);
        }

        for (const std::wstring& code : expired)
        {
            if (Session* session = sessionFor(code); session != nullptr)
            {
                closeSession(*session, net::RelayReject::HostGone);
                if (Connection* host = find(session->hostConnection); host != nullptr)
                    host->dead = true;
            }

            m_sessions.erase(code);
            log("session " + narrow(code) + " expired");
        }
    }
}
