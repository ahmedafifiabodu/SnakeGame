#pragma once

#include "Identity.h"
#include "Matchmaker.h"
#include "NetConfig.h"
#include "NetGame.h"
#include "Transport.h"
#include "../game/MatchSimulation.h"

#include <memory>
#include <string>
#include <vector>

namespace neoncoil::net
{
    // The authoritative side of a session, running inside a playing client --
    // a listen server, not a dedicated one.
    //
    // Why this topology, for this game: four players, no ranked ladder, no
    // persistent economy. A dedicated server would add a per-match hosting bill
    // and a deployment pipeline while making latency WORSE on average, because
    // all four players would then pay a round trip to a datacentre instead of
    // three paying one to the fourth. The host's authority over the simulation
    // is what actually matters for consistency, and that is what this class has.
    //
    // The host also plays. Its own input goes straight into the simulation and
    // its own view comes from the same MatchSnapshot it sends to everyone else,
    // so the host cannot see a different game from its guests.
    class HostSession : public NetGame
    {
    public:
        HostSession(const NetConfig& config, IIdentityProvider& identity);
        ~HostSession() override;

        enum class Reach
        {
            // Listen on a port. Reachable on a LAN with no configuration, and
            // over the internet only if somebody forwards the port.
            Direct,

            // Dial out to the relay and be reachable by code from anywhere,
            // with no router configuration by anybody.
            Relay
        };

        // Opens the lobby. `error` is filled on immediate failure; a relayed
        // host also reports later failures through phase() and status(), because
        // reaching the relay is a network round trip rather than a bind.
        bool open(const std::wstring& hostDisplayName, std::uint8_t colourIndex,
            std::uint8_t typeIndex, Reach reach, std::wstring& error);

        bool isHost() const override { return true; }
        void update(float deltaSeconds) override;
        void shutdown() override;

        SessionPhase phase() const override { return m_phase; }
        const std::wstring& status() const override { return m_status; }

        const LobbyInfo& lobby() const override { return m_lobby; }
        PlayerSlot localSlot() const override { return m_localSlot; }

        // The host is the far end. Its own ping to itself is zero, and saying so
        // is more useful than leaving the readout blank.
        int pingMs() const override { return 0; }
        int relayIndex() const override { return m_relayIndex; }

        void setReady(bool ready) override;
        void setLoadout(std::uint8_t colourIndex, std::uint8_t typeIndex) override;

        bool canStartMatch() const override;
        void requestStartMatch() override;

        void sendInput(const InputCommand& input) override;
        void returnToLobby() override;

        const MatchSnapshot& snapshot() const override { return m_snapshot; }
        const Level& arena() const override { return m_arena; }
        const MatchRules& rules() const override { return m_rules; }
        const MatchResult& result() const override { return m_result; }
        const std::vector<std::wstring>& events() const override { return m_events; }

        Reach reach() const { return m_reach; }

    private:
        // A connected peer that has completed the handshake. Peers that have
        // connected but not said Hello yet have no seat and are held here with
        // slot == kInvalidSlot until they do, or until they time out.
        struct Client
        {
            PeerId peer{ kInvalidPeer };
            PlayerSlot slot{ kInvalidSlot };
            PlayerIdentity identity;
            float secondsSinceHello{ 0.0f };
            bool greeted{ false };
        };

        Client* clientForPeer(PeerId peer);
        Client* clientForSlot(PlayerSlot slot);

        void handleConnected(PeerId peer);
        void handleDisconnected(PeerId peer, const std::wstring& reason);
        void handleMessage(PeerId peer, sf::Packet& packet);

        void handleHello(Client& client, sf::Packet& packet);
        void reject(PeerId peer, RejectReason reason);

        PlayerSlot allocateSlot() const;
        void releaseSlot(PlayerSlot slot);

        void broadcastLobby();
        void sendMatchStart(PeerId peer);
        void broadcastSnapshot();
        void broadcastMatchEnd();

        void beginMatch();
        void endMatch();
        void refreshAdvert();
        void note(std::wstring line);

        NetConfig m_config;
        IIdentityProvider& m_identity;

        std::unique_ptr<ServerTransport> m_transport;
        std::unique_ptr<IMatchmaker> m_matchmaker;

        LobbyInfo m_lobby;
        std::vector<Client> m_clients;
        PlayerSlot m_localSlot{ 0 };

        MatchSimulation m_simulation;
        MatchSnapshot m_snapshot;
        Level m_arena;
        MatchRules m_rules{};
        MatchResult m_result;
        ArenaDescription m_arenaDescription;
        std::uint64_t m_matchSeed{ 0 };

        Reach m_reach{ Reach::Direct };
        int m_relayIndex{ -1 };

        // Relayed hosts get their code from the relay rather than making one up,
        // so the lobby shows nothing until registration lands.
        bool m_awaitingCode{ false };

        SessionPhase m_phase{ SessionPhase::Idle };
        std::wstring m_status;
        std::vector<std::wstring> m_events;

        float m_snapshotTimer{ 0.0f };
        float m_advertTimer{ 0.0f };

        // Seconds since the last snapshot went out, and the floor a move-driven
        // one has to clear. Without the floor, four snakes stepping on four
        // slightly different clocks could each trigger their own send and turn a
        // twenty-hertz stream into a sixty-hertz one. Set at match start from
        // snapshot_hz, so a host that asks for a faster stream is not held to a
        // slower one than it configured.
        float m_sinceSnapshot{ 0.0f };
        float m_minimumSnapshotInterval{ 1.0f / 30.0f };
        static constexpr float kMoveSnapshotCeilingHz = 30.0f;

        // Guests report their ping on every heartbeat, which is once a second
        // each. Rebroadcasting the lobby that often, for a number that only
        // needs to be roughly right, would be four times the lobby traffic for
        // nothing -- so changes are collected and sent on this timer instead.
        bool m_pingDirty{ false };
        float m_pingBroadcastTimer{ 0.0f };
        static constexpr float kPingBroadcastSeconds = 2.0f;
    };
}
