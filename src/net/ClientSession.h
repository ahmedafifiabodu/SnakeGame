#pragma once

#include "Identity.h"
#include "NetConfig.h"
#include "NetGame.h"
#include "Transport.h"

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
    };
}
