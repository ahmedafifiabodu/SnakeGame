#pragma once

#include "NetConfig.h"
#include "SessionAdvert.h"

#include <SFML/Network/UdpSocket.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
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

        // Which relay this came through, as an index into NetConfig::relays.
        // -1 for a session found on the local network, which came through none.
        int relayIndex{ -1 };

        // What to show for "where is this". A relayed session has no address a
        // player could act on, so it says so rather than showing a blank.
        std::wstring where() const;

        float secondsSinceSeen{ 0.0f };

        bool joinable() const
        {
            return !advert.inMatch && advert.players < advert.maxPlayers;
        }
    };

    // One region, as the player sees it in the picker.
    //
    // The ping is the thing that matters and the reason regions exist at all, so
    // it is part of the same struct rather than something a screen has to go and
    // fetch separately.
    struct RelayStatus
    {
        std::wstring name{ L"RELAY" };
        wchar_t regionTag{ 0 };
        int index{ -1 };

        // Round trip to this relay in milliseconds, or -1 while the first
        // measurement is still out. Measured on the session-list query, so it is
        // the real path a game would take, not a synthetic probe.
        int pingMs{ -1 };
        bool reachable{ false };
        int openSessions{ 0 };
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

        // The regions this matchmaker is watching, with their pings. Empty for a
        // backend that has no notion of one -- LAN discovery has no region, and
        // saying so with an empty list is better than making every caller ask
        // which kind of matchmaker it is holding.
        virtual const std::vector<RelayStatus>& relayStatuses() const;

        // Index into relayStatuses() of the fastest region that answered, or -1
        // if none has yet. This is what AUTO resolves to.
        int fastestRelay() const;
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

    // Sessions registered with the relay -- that is, everything anybody is
    // hosting online right now, anywhere.
    //
    // Browse-only by design. A relayed host is already in the relay's list as a
    // side effect of registering for its code, so there is no advertising for
    // this class to do; the advertising half of the interface is honoured with
    // no-ops rather than by splitting the interface in two for one backend.
    //
    // The query itself is a blocking connect-ask-read, which cannot happen on
    // the frame that asks for it, so it runs on a worker thread and the result
    // is picked up on a later update(). The list on screen is therefore always
    // a couple of seconds old, which for a lobby list is invisible.
    class RelayMatchmaker : public IMatchmaker
    {
    public:
        // `index` is the position of this relay in NetConfig::relays, stamped
        // onto every session it reports so that joining one dials the relay it
        // was actually found on rather than whichever is currently selected.
        RelayMatchmaker(RelayEndpoint endpoint, int index);
        ~RelayMatchmaker() override;

        RelayMatchmaker(const RelayMatchmaker&) = delete;
        RelayMatchmaker& operator=(const RelayMatchmaker&) = delete;

        bool startAdvertising(const SessionAdvert&) override { return true; }
        void updateAdvert(const SessionAdvert&) override {}
        void stopAdvertising() override {}
        bool isAdvertising() const override { return false; }

        bool startBrowsing() override;
        void stopBrowsing() override;
        bool isBrowsing() const override { return m_browsing; }
        const std::vector<DiscoveredSession>& sessions() const override { return m_sessions; }
        const DiscoveredSession* bestJoinable() const override;

        void update(float deltaSeconds) override;

        const wchar_t* backendName() const override { return L"ONLINE"; }
        const std::wstring& lastError() const override { return m_error; }
        const std::vector<RelayStatus>& relayStatuses() const override { return m_statuses; }

        // False until the first query has come back, however it went. The menu
        // uses it to say "looking" rather than "nothing found" while the first
        // round trip to the relay is still out.
        bool hasAnswered() const { return m_answered; }

        const RelayEndpoint& endpoint() const { return m_endpoint; }

    private:
        void beginQuery();
        void collectQuery();
        void joinWorker();

        RelayEndpoint m_endpoint;
        int m_index{ -1 };

        // Exactly one entry: this relay. A vector rather than a lone struct so
        // that a single relay and a fleet of them present the same shape to the
        // screen that draws the picker.
        std::vector<RelayStatus> m_statuses;

        // Written by the worker, read by the main thread only after the thread
        // has been joined -- which is what makes them safe without a lock.
        std::thread m_worker;
        std::atomic<bool> m_workerFinished{ false };
        std::vector<SessionAdvert> m_incoming;
        std::wstring m_incomingError;
        bool m_incomingOk{ false };
        int m_incomingPing{ -1 };

        std::vector<DiscoveredSession> m_sessions;
        std::wstring m_error;

        bool m_browsing{ false };
        bool m_querying{ false };
        bool m_answered{ false };
        float m_refreshTimer{ 0.0f };
    };

    // LAN and relay, as one list.
    //
    // Both are real answers to "what can I join", and a player does not think of
    // them as separate questions -- so they are not shown as two panels or two
    // modes. Local sessions sort first, because a game across the room will
    // always beat the same game through a datacentre.
    class CompositeMatchmaker : public IMatchmaker
    {
    public:
        CompositeMatchmaker(std::unique_ptr<IMatchmaker> lan,
            std::vector<std::unique_ptr<RelayMatchmaker>> relays);

        bool startAdvertising(const SessionAdvert& advert) override;
        void updateAdvert(const SessionAdvert& advert) override;
        void stopAdvertising() override;
        bool isAdvertising() const override;

        bool startBrowsing() override;
        void stopBrowsing() override;
        bool isBrowsing() const override;
        const std::vector<DiscoveredSession>& sessions() const override { return m_merged; }
        const DiscoveredSession* bestJoinable() const override;

        void update(float deltaSeconds) override;

        const wchar_t* backendName() const override { return L"LAN AND ONLINE"; }
        const std::wstring& lastError() const override;
        const std::vector<RelayStatus>& relayStatuses() const override { return m_statuses; }

    private:
        void merge();

        // Last measured round trip to one region, or -1 if it has not answered.
        int pingOf(int relayIndex) const;

        std::unique_ptr<IMatchmaker> m_lan;
        std::vector<std::unique_ptr<RelayMatchmaker>> m_relays;
        std::vector<DiscoveredSession> m_merged;
        std::vector<RelayStatus> m_statuses;
    };

    // The one place a different matchmaking backend gets chosen. Returns a
    // composite when this build has a relay, and plain LAN discovery when it
    // does not -- so a relay-less build behaves exactly as it did before.
    std::unique_ptr<IMatchmaker> makeMatchmaker();
}
