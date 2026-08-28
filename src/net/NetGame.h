#pragma once

#include "Lobby.h"
#include "Protocol.h"
#include "../game/Level.h"
#include "../game/MatchSnapshot.h"

#include <string>
#include <vector>

namespace neoncoil::net
{
    enum class SessionPhase
    {
        Idle,
        Connecting,
        InLobby,
        Starting,     // MatchStart received, arena built, countdown about to run
        InMatch,
        PostMatch,
        Disconnected
    };

    // What a screen is allowed to know about a multiplayer session.
    //
    // Everything above this line is host-or-client specific; everything above it
    // in the UI is not. LobbyState and NetPlayState hold a NetGame* and never
    // ask which one they have -- which is what stops "if (isHost)" branching
    // from leaking through the whole front end, and what would let a dedicated
    // server backend be added as a third implementation.
    class NetGame
    {
    public:
        virtual ~NetGame() = default;

        virtual bool isHost() const = 0;
        virtual void update(float deltaSeconds) = 0;
        virtual void shutdown() = 0;

        virtual SessionPhase phase() const = 0;
        virtual const std::wstring& status() const = 0;

        virtual const LobbyInfo& lobby() const = 0;
        virtual PlayerSlot localSlot() const = 0;

        // Round trip to the host in milliseconds, or -1 before the first
        // heartbeat has come back. Zero for the host, which is not going
        // anywhere. Every other player's ping is in their LobbySlot.
        virtual int pingMs() const { return 0; }

        // Which relay this session is going through, as an index into
        // NetConfig::relays, or -1 for a direct connection.
        virtual int relayIndex() const { return -1; }

        virtual void setReady(bool ready) = 0;
        virtual void setLoadout(std::uint8_t colourIndex, std::uint8_t typeIndex) = 0;

        // Host-only in practice; the client implementations return false / do
        // nothing rather than making callers ask.
        virtual bool canStartMatch() const { return false; }
        virtual void requestStartMatch() {}

        virtual void sendInput(const InputCommand& input) = 0;
        virtual void returnToLobby() = 0;

        virtual const MatchSnapshot& snapshot() const = 0;
        virtual const Level& arena() const = 0;
        virtual const MatchRules& rules() const = 0;
        virtual const MatchResult& result() const = 0;

        // Rolling feed of human-readable events, newest last. Bounded by the
        // implementation; screens just render the tail.
        virtual const std::vector<std::wstring>& events() const = 0;
    };
}
