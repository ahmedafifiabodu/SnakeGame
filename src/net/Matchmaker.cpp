#include "Matchmaker.h"

#include "NetConfig.h"
#include "Protocol.h"
#include "RelayTransport.h"

#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/Packet.hpp>

#include <algorithm>
#include <optional>

namespace neoncoil::net
{
    namespace
    {
        constexpr std::uint8_t kProbe = 'Q';
        constexpr std::uint8_t kReply = 'R';

        // A browsing client keeps at most this many sessions in view. Four
        // players cannot meaningfully choose between more, and it bounds what a
        // flood of forged replies can cost.
        constexpr std::size_t kMaxSessions = 16;

        sf::Packet makeProbe()
        {
            sf::Packet packet;
            packet << kProtocolMagic << kProbe << kProtocolVersion;
            return packet;
        }

        sf::Packet makeReply(const SessionAdvert& advert)
        {
            sf::Packet packet;
            packet << kProtocolMagic << kReply << kProtocolVersion
                << advert.code << advert.hostName << advert.port
                << advert.players << advert.maxPlayers << advert.inMatch;
            return packet;
        }

        bool isLoopback(const std::string& address)
        {
            return address.rfind("127.", 0) == 0 || address == "::1";
        }

        bool readHeader(sf::Packet& packet, std::uint8_t& kind)
        {
            std::uint32_t magic = 0;
            std::uint16_t version = 0;
            packet >> magic >> kind >> version;

            // A version mismatch is dropped here rather than surfaced: showing a
            // session in the browser that the host would then refuse is worse
            // than not showing it at all.
            return static_cast<bool>(packet) && magic == kProtocolMagic && version == kProtocolVersion;
        }
    }

    const std::vector<RelayStatus>& IMatchmaker::relayStatuses() const
    {
        static const std::vector<RelayStatus> none;
        return none;
    }

    int IMatchmaker::fastestRelay() const
    {
        const std::vector<RelayStatus>& all = relayStatuses();

        int best = -1;
        for (const RelayStatus& status : all)
        {
            if (!status.reachable || status.pingMs < 0)
                continue;
            if (best < 0 || status.pingMs < all[static_cast<std::size_t>(best)].pingMs)
                best = status.index;
        }

        return best;
    }

    std::wstring DiscoveredSession::where() const
    {
        // A relayed session has no address a player could type, dial or diagnose
        // with, so it says what it is instead of showing an empty column.
        if (viaRelay)
            return L"via relay";

        std::wstring wide;
        wide.reserve(address.size());
        for (char c : address)
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return wide;
    }

    LanMatchmaker::~LanMatchmaker()
    {
        stopAdvertising();
        stopBrowsing();
    }

    // ------------------------------------------------------------------- host --

    bool LanMatchmaker::startAdvertising(const SessionAdvert& advert)
    {
        stopAdvertising();

        const NetConfig& config = NetConfig::instance();
        if (!config.advertiseOnLan)
            return false;

        if (m_hostSocket.bind(config.discoveryPort) != sf::Socket::Status::Done)
        {
            // Not fatal: the session still works, it just will not appear in the
            // LAN browser. Another host on this machine is the usual cause.
            m_error = L"could not open the discovery port -- this session is joinable by address only";
            return false;
        }

        m_hostSocket.setBlocking(false);
        m_advert = advert;
        m_advertising = true;
        return true;
    }

    void LanMatchmaker::updateAdvert(const SessionAdvert& advert)
    {
        m_advert = advert;
    }

    void LanMatchmaker::stopAdvertising()
    {
        if (!m_advertising)
            return;

        m_hostSocket.unbind();
        m_advertising = false;
    }

    void LanMatchmaker::serviceHost()
    {
        for (int i = 0; i < 32; ++i)
        {
            sf::Packet packet;
            std::optional<sf::IpAddress> sender;
            unsigned short senderPort = 0;

            if (m_hostSocket.receive(packet, sender, senderPort) != sf::Socket::Status::Done)
                return;

            std::uint8_t kind = 0;
            if (!readHeader(packet, kind) || kind != kProbe || !sender.has_value())
                continue;

            // Unicast straight back to whoever asked, so the reply reaches a
            // client on this same machine as readily as one across the network.
            sf::Packet reply = makeReply(m_advert);
            (void)m_hostSocket.send(reply.getData(), reply.getDataSize(), *sender, senderPort);
        }
    }

    // ---------------------------------------------------------------- browsing --

    bool LanMatchmaker::startBrowsing()
    {
        stopBrowsing();

        // Any port: the client is the one asking, so it does not need a
        // well-known address, and this is what lets it share a machine with a
        // host that already owns the discovery port.
        if (m_browseSocket.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done)
        {
            m_error = L"could not open a socket to search the network";
            return false;
        }

        m_browseSocket.setBlocking(false);
        m_sessions.clear();
        m_browsing = true;
        m_probeTimer = 0.0f;   // probe immediately
        return true;
    }

    void LanMatchmaker::stopBrowsing()
    {
        if (!m_browsing)
            return;

        m_browseSocket.unbind();
        m_sessions.clear();
        m_browsing = false;
    }

    void LanMatchmaker::sendProbe()
    {
        const NetConfig& config = NetConfig::instance();
        sf::Packet probe = makeProbe();

        // Broadcast finds hosts across the network; the explicit loopback send
        // is what makes a host and a client on the same PC find each other,
        // which is the configuration every developer tests in first.
        (void)m_browseSocket.send(probe.getData(), probe.getDataSize(),
            sf::IpAddress::Broadcast, config.discoveryPort);
        (void)m_browseSocket.send(probe.getData(), probe.getDataSize(),
            sf::IpAddress::LocalHost, config.discoveryPort);
    }

    void LanMatchmaker::recordReply(const SessionAdvert& advert, const std::string& address)
    {
        // Matched on the code, not on the address.
        //
        // A browser probes by broadcast AND to loopback, so a host on this same
        // machine answers twice, once from each -- and listing one session as
        // two, under one code, at two addresses, is confusing in exactly the
        // situation that is hardest to debug. The code identifies the session;
        // the address is only how to get to it.
        for (DiscoveredSession& session : m_sessions)
        {
            const bool sameSession = !advert.code.empty()
                ? session.advert.code == advert.code
                : session.address == address && session.advert.port == advert.port;

            if (!sameSession)
                continue;

            // Keep the address that works from the widest range of places: a
            // routable one reaches this machine too, where loopback only ever
            // reaches this machine.
            if (isLoopback(session.address) && !isLoopback(address))
                session.address = address;

            session.advert = advert;
            session.secondsSinceSeen = 0.0f;
            return;
        }

        if (m_sessions.size() >= kMaxSessions)
            return;

        DiscoveredSession session;
        session.advert = advert;
        session.address = address;
        session.secondsSinceSeen = 0.0f;
        m_sessions.push_back(std::move(session));
    }

    void LanMatchmaker::serviceBrowser(float deltaSeconds)
    {
        const NetConfig& config = NetConfig::instance();

        m_probeTimer -= deltaSeconds;
        if (m_probeTimer <= 0.0f)
        {
            m_probeTimer = config.discoveryProbeInterval;
            sendProbe();
        }

        for (int i = 0; i < 32; ++i)
        {
            sf::Packet packet;
            std::optional<sf::IpAddress> sender;
            unsigned short senderPort = 0;

            if (m_browseSocket.receive(packet, sender, senderPort) != sf::Socket::Status::Done)
                break;

            std::uint8_t kind = 0;
            if (!readHeader(packet, kind) || kind != kReply || !sender.has_value())
                continue;

            SessionAdvert advert;
            packet >> advert.code >> advert.hostName >> advert.port
                >> advert.players >> advert.maxPlayers >> advert.inMatch;

            if (!packet || advert.port == 0)
                continue;

            if (advert.code.size() > 16)
                advert.code.resize(16);
            if (advert.hostName.size() > 16)
                advert.hostName.resize(16);
            advert.maxPlayers = static_cast<std::uint8_t>(std::clamp<int>(advert.maxPlayers, 1, 4));
            advert.players = static_cast<std::uint8_t>(std::clamp<int>(advert.players, 0, advert.maxPlayers));

            recordReply(advert, sender->toString());
        }

        // Age out anything that has stopped answering, so a host that quits
        // disappears from the browser rather than lingering as a dead entry.
        for (DiscoveredSession& session : m_sessions)
            session.secondsSinceSeen += deltaSeconds;

        m_sessions.erase(std::remove_if(m_sessions.begin(), m_sessions.end(),
            [&config](const DiscoveredSession& session)
            {
                return session.secondsSinceSeen > config.discoveryTtlSeconds;
            }),
            m_sessions.end());
    }

    void LanMatchmaker::update(float deltaSeconds)
    {
        if (m_advertising)
            serviceHost();
        if (m_browsing)
            serviceBrowser(deltaSeconds);
    }

    const DiscoveredSession* LanMatchmaker::bestJoinable() const
    {
        const DiscoveredSession* best = nullptr;

        for (const DiscoveredSession& session : m_sessions)
        {
            if (!session.joinable())
                continue;

            // Fullest-first. Filling one lobby beats scattering four players
            // across three sessions that never start.
            if (best == nullptr || session.advert.players > best->advert.players)
                best = &session;
        }

        return best;
    }

    // ------------------------------------------------------------ relay list --

    RelayMatchmaker::RelayMatchmaker(RelayEndpoint endpoint, int index)
        : m_endpoint(std::move(endpoint))
        , m_index(index)
    {
        RelayStatus status;
        status.name = m_endpoint.name;
        status.regionTag = m_endpoint.regionTag;
        status.index = index;
        m_statuses.push_back(std::move(status));
    }

    RelayMatchmaker::~RelayMatchmaker()
    {
        stopBrowsing();
    }

    void RelayMatchmaker::joinWorker()
    {
        if (m_worker.joinable())
            m_worker.join();

        m_querying = false;
        m_workerFinished.store(false);
    }

    bool RelayMatchmaker::startBrowsing()
    {
        stopBrowsing();

        if (m_endpoint.host.empty())
        {
            m_error = L"no relay is configured";
            return false;
        }

        m_browsing = true;
        m_answered = false;
        m_refreshTimer = 0.0f;   // ask immediately
        return true;
    }

    void RelayMatchmaker::stopBrowsing()
    {
        // The worker owns a socket with a connect timeout on it, so this can
        // block for as long as that timeout. It runs when the menu closes rather
        // than during play, which is the reason the timeout is kept short.
        joinWorker();

        m_sessions.clear();
        m_incoming.clear();
        m_browsing = false;
        m_answered = false;

        // The ping is kept. It was true when it was measured, and a picker that
        // blanks every region the moment a menu closes tells the player less
        // than one that shows the last thing it knew.
        m_statuses[0].openSessions = 0;
    }

    void RelayMatchmaker::beginQuery()
    {
        m_querying = true;
        m_workerFinished.store(false);

        const std::string host = m_endpoint.host;
        const std::uint16_t port = m_endpoint.port;
        const float timeout = NetConfig::instance().relayListTimeoutSeconds;

        m_worker = std::thread([this, host, port, timeout]
        {
            m_incoming.clear();
            m_incomingError.clear();
            m_incomingPing = -1;
            m_incomingOk = queryRelaySessions(host, port, m_incoming, m_incomingError,
                timeout, &m_incomingPing);

            // Last write in the thread, and the only one the main thread waits
            // on: everything above it is published by the join that follows.
            m_workerFinished.store(true);
        });
    }

    void RelayMatchmaker::collectQuery()
    {
        joinWorker();
        m_answered = true;

        RelayStatus& status = m_statuses[0];

        if (!m_incomingOk)
        {
            // A relay that cannot be reached empties its half of the list rather
            // than leaving stale sessions on screen that nobody could join. The
            // region itself stays in the picker, marked unreachable -- a region
            // that vanishes looks like a bug, one that says it is down does not.
            m_error = m_incomingError;
            m_sessions.clear();
            status.reachable = false;
            status.pingMs = -1;
            status.openSessions = 0;
            return;
        }

        m_error.clear();
        m_sessions.clear();

        status.reachable = true;
        status.pingMs = m_incomingPing;

        for (SessionAdvert& advert : m_incoming)
        {
            if (advert.code.empty())
                continue;

            DiscoveredSession session;
            session.advert = std::move(advert);
            session.viaRelay = true;   // the code is the address
            session.relayIndex = m_index;
            session.secondsSinceSeen = 0.0f;
            m_sessions.push_back(std::move(session));

            if (m_sessions.size() >= kMaxSessions)
                break;
        }

        status.openSessions = static_cast<int>(m_sessions.size());
        m_incoming.clear();
    }

    void RelayMatchmaker::update(float deltaSeconds)
    {
        if (!m_browsing)
            return;

        if (m_querying)
        {
            if (m_workerFinished.load())
                collectQuery();
            return;
        }

        m_refreshTimer -= deltaSeconds;
        if (m_refreshTimer <= 0.0f)
        {
            m_refreshTimer = NetConfig::instance().relayListIntervalSeconds;
            beginQuery();
        }
    }

    const DiscoveredSession* RelayMatchmaker::bestJoinable() const
    {
        const DiscoveredSession* best = nullptr;

        for (const DiscoveredSession& session : m_sessions)
        {
            if (!session.joinable())
                continue;
            if (best == nullptr || session.advert.players > best->advert.players)
                best = &session;
        }

        return best;
    }

    // ------------------------------------------------------------- composite --

    CompositeMatchmaker::CompositeMatchmaker(std::unique_ptr<IMatchmaker> lan,
        std::vector<std::unique_ptr<RelayMatchmaker>> relays)
        : m_lan(std::move(lan))
        , m_relays(std::move(relays))
    {
        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            m_statuses.push_back(relay->relayStatuses().front());
    }

    bool CompositeMatchmaker::startAdvertising(const SessionAdvert& advert)
    {
        // Only the LAN side advertises. A relayed host is listed by the relay
        // the moment it registers for a code, so there is nothing to publish.
        return m_lan && m_lan->startAdvertising(advert);
    }

    void CompositeMatchmaker::updateAdvert(const SessionAdvert& advert)
    {
        if (m_lan)
            m_lan->updateAdvert(advert);
    }

    void CompositeMatchmaker::stopAdvertising()
    {
        if (m_lan)
            m_lan->stopAdvertising();
    }

    bool CompositeMatchmaker::isAdvertising() const
    {
        return m_lan && m_lan->isAdvertising();
    }

    bool CompositeMatchmaker::startBrowsing()
    {
        // Any one part succeeding is enough to be browsing: a machine with no
        // usable broadcast should still see what is open online, a player with
        // no route to one region should still see the others, and a player with
        // no route to any of them should still find the game next door.
        bool any = m_lan && m_lan->startBrowsing();

        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            any = relay->startBrowsing() || any;

        m_merged.clear();
        return any;
    }

    void CompositeMatchmaker::stopBrowsing()
    {
        if (m_lan)
            m_lan->stopBrowsing();
        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            relay->stopBrowsing();
        m_merged.clear();
    }

    bool CompositeMatchmaker::isBrowsing() const
    {
        if (m_lan && m_lan->isBrowsing())
            return true;

        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            if (relay->isBrowsing())
                return true;

        return false;
    }

    const std::wstring& CompositeMatchmaker::lastError() const
    {
        static const std::wstring none;

        // The LAN error is the one worth surfacing: it means something about
        // this machine. A relay that is slow to answer is not a player problem
        // and would only add noise to a screen that is otherwise working.
        if (m_lan && !m_lan->lastError().empty())
            return m_lan->lastError();

        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            if (!relay->lastError().empty())
                return relay->lastError();

        return none;
    }

    void CompositeMatchmaker::merge()
    {
        m_merged.clear();

        if (m_lan)
        {
            for (const DiscoveredSession& session : m_lan->sessions())
                m_merged.push_back(session);
        }

        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
        {
            for (const DiscoveredSession& session : relay->sessions())
            {
                // A host that is on this network AND registered with a relay
                // appears in both lists under one code. Local wins: same game,
                // shorter path. Two relays cannot collide the same way, because
                // a code carries the tag of the relay that minted it.
                const bool duplicate = std::any_of(m_merged.begin(), m_merged.end(),
                    [&session](const DiscoveredSession& existing)
                    {
                        return !existing.advert.code.empty() &&
                            existing.advert.code == session.advert.code;
                    });

                if (!duplicate)
                    m_merged.push_back(session);
            }
        }

        for (std::size_t i = 0; i < m_relays.size() && i < m_statuses.size(); ++i)
            m_statuses[i] = m_relays[i]->relayStatuses().front();

        // Nearest first.
        //
        // With relays on several continents the list is no longer a handful of
        // sessions that are all equally reachable -- it is every open session on
        // Earth, and most of them are ones this player should not join. Ordering
        // by distance is what keeps the top of the list the part worth reading,
        // and it is why the sort key is the region's ping rather than anything
        // about the session itself.
        //
        // Truncation happens after the sort, so what gets cut is the far end of
        // the world rather than whichever region answered last.
        std::stable_sort(m_merged.begin(), m_merged.end(),
            [this](const DiscoveredSession& a, const DiscoveredSession& b)
            {
                // A session on this network is closer than anything a relay can
                // offer, by definition.
                if (a.viaRelay != b.viaRelay)
                    return !a.viaRelay;

                if (a.viaRelay)
                {
                    const int pa = pingOf(a.relayIndex);
                    const int pb = pingOf(b.relayIndex);

                    // A region that has not answered sorts last rather than
                    // first: an unknown distance is not a short one.
                    if (pa != pb)
                        return pa >= 0 && (pb < 0 || pa < pb);
                }

                // Same distance: the fullest lobby, for the reason it has always
                // been -- one session that starts beats three that do not.
                if (a.advert.players != b.advert.players)
                    return a.advert.players > b.advert.players;

                // Codes are unique and stable, so ties break the same way every
                // refresh and rows stop swapping under the player's cursor.
                return a.advert.code < b.advert.code;
            });

        if (m_merged.size() > kMaxSessions)
            m_merged.resize(kMaxSessions);
    }

    void CompositeMatchmaker::update(float deltaSeconds)
    {
        if (m_lan)
            m_lan->update(deltaSeconds);
        for (const std::unique_ptr<RelayMatchmaker>& relay : m_relays)
            relay->update(deltaSeconds);

        merge();
    }

    int CompositeMatchmaker::pingOf(int relayIndex) const
    {
        for (const RelayStatus& status : m_statuses)
            if (status.index == relayIndex)
                return status.reachable ? status.pingMs : -1;
        return -1;
    }

    const DiscoveredSession* CompositeMatchmaker::bestJoinable() const
    {
        const DiscoveredSession* best = nullptr;

        for (const DiscoveredSession& session : m_merged)
        {
            if (!session.joinable())
                continue;

            if (best == nullptr)
            {
                best = &session;
                continue;
            }

            // A local session beats a relayed one outright.
            if (best->viaRelay != session.viaRelay)
            {
                if (!session.viaRelay)
                    best = &session;
                continue;
            }

            // Between two relayed sessions, the nearer region wins before the
            // fullest lobby does. A seat in a session 200 ms away is not a
            // better offer than a seat in one 30 ms away, however full it is.
            if (best->viaRelay && session.relayIndex != best->relayIndex)
            {
                const int here = pingOf(session.relayIndex);
                const int there = pingOf(best->relayIndex);

                if (here >= 0 && (there < 0 || here < there))
                    best = &session;
                continue;
            }

            // Same kind, same region: the fullest wins, for the same reason as
            // ever -- one lobby that starts beats three that do not.
            if (session.advert.players > best->advert.players)
                best = &session;
        }

        return best;
    }

    std::unique_ptr<IMatchmaker> makeMatchmaker()
    {
        const NetConfig& config = NetConfig::instance();

        if (!config.haveRelays())
            return std::make_unique<LanMatchmaker>();

        std::vector<std::unique_ptr<RelayMatchmaker>> relays;
        relays.reserve(config.relays.size());

        for (std::size_t i = 0; i < config.relays.size(); ++i)
            relays.push_back(std::make_unique<RelayMatchmaker>(config.relays[i], static_cast<int>(i)));

        return std::make_unique<CompositeMatchmaker>(
            std::make_unique<LanMatchmaker>(), std::move(relays));
    }
}
