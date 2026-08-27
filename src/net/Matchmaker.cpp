#include "Matchmaker.h"

#include "NetConfig.h"
#include "Protocol.h"

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
        for (DiscoveredSession& session : m_sessions)
        {
            if (session.address == address && session.advert.port == advert.port)
            {
                session.advert = advert;
                session.secondsSinceSeen = 0.0f;
                return;
            }
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

    std::unique_ptr<IMatchmaker> makeMatchmaker()
    {
        return std::make_unique<LanMatchmaker>();
    }
}
