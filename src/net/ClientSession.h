#pragma once

#include "Identity.h"
#include "NetConfig.h"
#include "NetGame.h"
#include "Transport.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace neoncoil::net
{
    // A guest in someone else's session.
    //
    // Deliberately dumb: it holds no simulation at all. Everything it draws
    // comes from the last MatchSnapshot the host sent, and everything the player
    // does is forwarded as an intent. There is no client-side prediction and no
    // rollback, which for a grid game stepping eight times a second costs half a
    // step of felt latency and saves an entire reconciliation system that would
    // have to be kept bug-for-bug identical with the host's rules.
    //
    // InputCommand already carries a sequence number, so prediction can be added
    // later without a protocol break.
    class ClientSession : public NetGame
    {
    public:
        ClientSession(const NetConfig& config, IIdentityProvider& identity);
        ~ClientSession() override;

        // Connect straight to a host that is listening. Returns false only if
        // the address is unusable; everything else is reported asynchronously
        // through phase().
        bool connect(const std::string& address, std::uint16_t port,
            const std::wstring& displayName, std::uint8_t colourIndex,
            std::uint8_t typeIndex, std::wstring& error);

        // Connect through the relay, by session code. Neither end has to be
        // reachable from outside its own network: both dial out.
        bool connectByCode(const std::wstring& code,
            const std::wstring& displayName, std::uint8_t colourIndex,
            std::uint8_t typeIndex, std::wstring& error);

        bool isHost() const override { return false; }
        void update(float deltaSeconds) override;
        void shutdown() override;

        SessionPhase phase() const override { return m_phase; }
        const std::wstring& status() const override { return m_status; }

        const LobbyInfo& lobby() const override { return m_lobby; }
        PlayerSlot localSlot() const override { return m_localSlot; }
        int pingMs() const override { return m_pingMs; }
        int relayIndex() const override { return m_relayIndex; }
        const SnakePrediction* prediction() const override
        {
            return m_prediction.active() ? &m_prediction : nullptr;
        }

        void setReady(bool ready) override;
        void setLoadout(std::uint8_t colourIndex, std::uint8_t typeIndex) override;

        void sendInput(const InputCommand& input) override;
        void returnToLobby() override;

        const MatchSnapshot& snapshot() const override { return m_snapshot; }
        const Level& arena() const override { return m_arena; }
        const MatchRules& rules() const override { return m_rules; }
        const MatchResult& result() const override { return m_result; }
        const std::vector<std::wstring>& events() const override { return m_events; }

    private:
        void sendHello();
        void handleMessage(sf::Packet& packet);
        void note(std::wstring line);

        // Starts, stops or corrects the local prediction from whatever the last
        // snapshot said. Called on every snapshot, so the prediction can never
        // be running against a body the host has moved on from.
        void syncPrediction();

        NetConfig m_config;
        IIdentityProvider& m_identity;
        std::unique_ptr<ClientTransport> m_transport;

        LobbyInfo m_lobby;
        PlayerSlot m_localSlot{ kInvalidSlot };

        MatchSnapshot m_snapshot;
        Level m_arena;
        MatchRules m_rules{};
        MatchResult m_result;

        SessionPhase m_phase{ SessionPhase::Idle };
        std::wstring m_status;
        std::vector<std::wstring> m_events;

        std::uint8_t m_colourIndex{ 0 };
        std::uint8_t m_typeIndex{ 0 };
        std::uint32_t m_inputSequence{ 0 };

        float m_connectElapsed{ 0.0f };
        float m_heartbeatTimer{ 0.0f };

        // Round-trip measurement. One heartbeat is in flight at a time, so a
        // single outstanding nonce is enough -- and a stale echo, from a
        // heartbeat that crossed a slower one, is discarded rather than
        // producing a number that never happened.
        std::uint32_t m_heartbeatNonce{ 0 };
        std::chrono::steady_clock::time_point m_heartbeatSentAt{};
        bool m_heartbeatPending{ false };

        // Smoothed, because a raw round trip jitters by tens of milliseconds and
        // a readout that flickers is one players stop believing.
        int m_pingMs{ -1 };
        int m_relayIndex{ -1 };

        // The local snake, stepped on the host's clock so a turn is visible
        // immediately instead of a round trip later.
        SnakePrediction m_prediction;
    };
}
