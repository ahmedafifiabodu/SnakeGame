#pragma once

#include "Identity.h"
#include "Lobby.h"
#include "../game/Direction.h"
#include "../game/MatchRules.h"
#include "../game/MatchSnapshot.h"

#include <SFML/Network/Packet.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil
{
    class Level;
}

namespace neoncoil::net
{
    // Bumped whenever anything below changes shape. The host refuses a client
    // whose version differs rather than letting it desync silently.
    //
    // 2: heartbeats carry a nonce and a reported ping, so every player can see
    //    every player's latency.
    inline constexpr std::uint16_t kProtocolVersion = 2;

    // Stamped on the front of every message and on every discovery datagram, so
    // a stray packet from something else on the port is dropped rather than
    // parsed. "NCO" plus the major protocol generation.
    inline constexpr std::uint32_t kProtocolMagic = 0x4E434F31u;

    enum class ClientMessage : std::uint8_t
    {
        Hello = 1,      // first message on the wire; carries the join ticket
        SetLoadout,     // colour / snake type change in the lobby
        SetReady,
        RequestStart,   // honoured only from the host's own seat
        Input,
        ReturnToLobby,  // acknowledge a finished match
        Heartbeat,      // + Heartbeat payload
        Leave
    };

    // What a heartbeat carries, in both directions.
    //
    // A heartbeat was already being sent once a second purely to prove the
    // connection was alive, and an empty packet crossing the wire on a timer is
    // exactly the thing a round-trip measurement needs. So the client stamps a
    // nonce on its heartbeat, the host echoes that nonce straight back, and the
    // difference in the client's own clock is the round trip -- no new message,
    // no new timer, no clock synchronisation between two machines.
    //
    // The client then reports the number it measured on its NEXT heartbeat, so
    // the host can put it in the lobby and everybody sees everybody's ping
    // rather than only their own.
    struct Heartbeat
    {
        std::uint32_t nonce{ 0 };
        std::uint16_t reportedPingMs{ 0 };   // client -> host; ignored the other way
    };

    enum class ServerMessage : std::uint8_t
    {
        Welcome = 1,
        Rejected,
        LobbyUpdate,
        MatchStart,
        Snapshot,
        MatchEnd,
        Heartbeat
    };

    enum class RejectReason : std::uint8_t
    {
        None = 0,
        BadProtocol,
        VersionMismatch,
        LobbyFull,
        MatchInProgress,
        BadTicket,
        DuplicateIdentity,
        HostClosed
    };

    const wchar_t* describe(RejectReason reason);

    // One key press. Sent the moment it happens rather than batched at the tick
    // rate: on a game that steps eight times a second, waiting for the next
    // network tick to forward a turn is the difference between responsive and
    // mushy. `sequence` is not used for reconciliation today -- the host is
    // authoritative and clients do not predict -- but it is on the wire so that
    // prediction can be added without a protocol break.
    struct InputCommand
    {
        std::uint32_t sequence{ 0 };
        bool hasDirection{ false };
        Direction direction{ Direction::Right };
        bool ability{ false };
    };

    // The static board, sent once at match start.
    //
    // The generator is deterministic, so in principle the host could send a seed
    // and let every client generate the same arena. It deliberately does not:
    // the generator draws from std::uniform_int_distribution, whose sequence is
    // NOT specified across standard library implementations, so a Linux or macOS
    // client would generate a different board from a Windows host. A packed
    // bitset of a 56x32 board is 224 bytes. Sending it removes the entire class
    // of problem for the price of one small message.
    struct ArenaDescription
    {
        std::int16_t width{ 0 };
        std::int16_t height{ 0 };
        std::vector<std::uint8_t> wallBits;   // row-major, one bit per tile
        std::wstring archetypeName;
        std::uint64_t seed{ 0 };
        std::int32_t levelIndex{ 1 };
    };

    ArenaDescription describeArena(const Level& level);
    void buildArena(Level& level, const ArenaDescription& description);

    // --- message builders -----------------------------------------------------
    // Every message starts with the magic and the message id, so the readers
    // below can validate before they trust a single field.

    sf::Packet beginClient(ClientMessage id);
    sf::Packet beginServer(ServerMessage id);

    // Returns false when the packet is not one of ours or is truncated.
    bool readClientHeader(sf::Packet& packet, ClientMessage& out);
    bool readServerHeader(sf::Packet& packet, ServerMessage& out);

    // --- payload serialisation ------------------------------------------------

    sf::Packet& operator<<(sf::Packet& packet, const JoinTicket& ticket);
    sf::Packet& operator>>(sf::Packet& packet, JoinTicket& ticket);

    sf::Packet& operator<<(sf::Packet& packet, const LobbySlot& slot);
    sf::Packet& operator>>(sf::Packet& packet, LobbySlot& slot);

    sf::Packet& operator<<(sf::Packet& packet, const LobbyInfo& lobby);
    sf::Packet& operator>>(sf::Packet& packet, LobbyInfo& lobby);

    sf::Packet& operator<<(sf::Packet& packet, const InputCommand& input);
    sf::Packet& operator>>(sf::Packet& packet, InputCommand& input);

    sf::Packet& operator<<(sf::Packet& packet, const Heartbeat& beat);
    sf::Packet& operator>>(sf::Packet& packet, Heartbeat& beat);

    sf::Packet& operator<<(sf::Packet& packet, const MatchRules& rules);
    sf::Packet& operator>>(sf::Packet& packet, MatchRules& rules);

    sf::Packet& operator<<(sf::Packet& packet, const ArenaDescription& arena);
    sf::Packet& operator>>(sf::Packet& packet, ArenaDescription& arena);

    sf::Packet& operator<<(sf::Packet& packet, const MatchSnapshot& snapshot);
    sf::Packet& operator>>(sf::Packet& packet, MatchSnapshot& snapshot);

    sf::Packet& operator<<(sf::Packet& packet, const MatchResult& result);
    sf::Packet& operator>>(sf::Packet& packet, MatchResult& result);
}
