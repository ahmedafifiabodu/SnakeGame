#pragma once

#include "SessionAdvert.h"

#include <SFML/Network/UdpSocket.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neoncoil::net
{
    struct DiscoveredSession
    {
        SessionAdvert advert;

        // Where to connect. For a LAN session this is the host's own address;
        // for a relayed one it is empty and the code is what matters.
        std::string address;
        bool viaRelay{ false };

        float secondsSinceSeen{ 0.0f };

        bool joinable() const
        {
            return !advert.inMatch && advert.players < advert.maxPlayers;
        }
    };

    // Finding a session to join, behind an interface.
    //
    // The implementation shipped today is LAN discovery, which needs no server
    // and no account and therefore works the moment the game is installed. The
    // interface is what matters architecturally: a directory service, a Steam
    // lobby search or a skill-based queue all fit behind exactly these calls,
    // and only the factory at the bottom of this file has to change.
    class IMatchmaker
    {
    public:
        virtual ~IMatchmaker() = default;

        // --- host side --------------------------------------------------------
        virtual bool startAdvertising(const SessionAdvert& advert) = 0;
        virtual void updateAdvert(const SessionAdvert& advert) = 0;
        virtual void stopAdvertising() = 0;
        virtual bool isAdvertising() const = 0;

        // --- client side ------------------------------------------------------
        virtual bool startBrowsing() = 0;
        virtual void stopBrowsing() = 0;
        virtual bool isBrowsing() const = 0;
        virtual const std::vector<DiscoveredSession>& sessions() const = 0;

        // Best joinable session, or nullptr. This is the whole of "matchmaking"
        // for a four-player game: pick the fullest session that still has room,
        // so players end up in one lobby rather than three half-empty ones.
        virtual const DiscoveredSession* bestJoinable() const = 0;

        virtual void update(float deltaSeconds) = 0;

        virtual const wchar_t* backendName() const = 0;
        virtual const std::wstring& lastError() const = 0;
    };

    // Broadcast-based discovery on the local network.
    //
    // Clients probe rather than hosts beaconing, for one practical reason: only
    // the host binds the fixed discovery port, so any number of clients can
    // browse on the same machine as the host. That is what makes it possible to
    // test a full four-player session with four processes on one PC.
    class LanMatchmaker : public IMatchmaker
    {
    public:
        LanMatchmaker() = default;
        ~LanMatchmaker() override;

        LanMatchmaker(const LanMatchmaker&) = delete;
        LanMatchmaker& operator=(const LanMatchmaker&) = delete;

        bool startAdvertising(const SessionAdvert& advert) override;
        void updateAdvert(const SessionAdvert& advert) override;
        void stopAdvertising() override;
        bool isAdvertising() const override { return m_advertising; }

        bool startBrowsing() override;
        void stopBrowsing() override;
        bool isBrowsing() const override { return m_browsing; }
        const std::vector<DiscoveredSession>& sessions() const override { return m_sessions; }
        const DiscoveredSession* bestJoinable() const override;

        void update(float deltaSeconds) override;

        const wchar_t* backendName() const override { return L"LAN"; }
        const std::wstring& lastError() const override { return m_error; }

    private:
        void serviceHost();
        void serviceBrowser(float deltaSeconds);
        void sendProbe();
        void recordReply(const SessionAdvert& advert, const std::string& address);

        sf::UdpSocket m_hostSocket;
        sf::UdpSocket m_browseSocket;

        SessionAdvert m_advert;
        std::vector<DiscoveredSession> m_sessions;
        std::wstring m_error;

        bool m_advertising{ false };
        bool m_browsing{ false };
        float m_probeTimer{ 0.0f };
    };

    // The one place a different matchmaking backend gets chosen.
    std::unique_ptr<IMatchmaker> makeMatchmaker();
}
