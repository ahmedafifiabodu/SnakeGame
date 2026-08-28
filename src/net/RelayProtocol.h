#pragma once

#include "SessionAdvert.h"

#include <SFML/Network/Packet.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil::net
{
    // The relay protocol: how a host and its guests reach each other when
    // neither of them can accept an incoming connection.
    //
    // The whole idea is that BOTH sides dial out. A home router will happily let
    // a machine open an outbound connection and send the replies back; what it
    // will not do, without being told, is let a stranger open a connection
    // inward. So the host connects out to the relay and registers, the guests
    // connect out to the relay and ask for that host's code, and the relay pumps
    // bytes between the two. Nobody forwards a port, nobody installs anything,
    // and it works behind carrier-grade NAT and on mobile hotspots, where hole
    // punching does not.
    //
    // The relay is deliberately, aggressively dumb. It never parses a game
    // message, never knows the rules, never holds game state, and never needs
    // redeploying when the game changes: to it, a snapshot is an opaque blob
    // with a channel number on it. That is what keeps it cheap to run and
    // impossible to desync.
    inline constexpr std::uint32_t kRelayMagic = 0x524C5931u;   // "RLY1"
    inline constexpr std::uint16_t kRelayVersion = 1;

    // A guest's connection to a host, as the host sees it. The relay hands out
    // these ids; the host maps them onto its own PeerIds.
    using RelayChannel = std::uint32_t;
    inline constexpr RelayChannel kInvalidChannel = 0;

    enum class RelayMessage : std::uint8_t
    {
        // --- endpoint -> relay ------------------------------------------------
        RegisterHost = 1,   // + version + advert       : "I am a host, give me a code"
        UpdateAdvert,       // + advert                 : player count changed
        JoinByCode,         // + version + code         : "put me through to this host"
        ListSessions,       // -                        : what is open right now
        Data,               // + channel + payload      : to one guest, or to the host
        Broadcast,          // + payload                : host only; relay fans out
        CloseChannel,       // + channel                : host drops one guest

        // --- relay -> endpoint ------------------------------------------------
        Registered,         // + code
        Rejected,           // + reason
        PeerJoined,         // + channel
        PeerLeft,           // + channel
        JoinAccepted,       // + channel
        SessionList         // + count + adverts
    };

    enum class RelayReject : std::uint8_t
    {
        None = 0,
        BadProtocol,
        VersionMismatch,
        UnknownCode,
        SessionFull,
        SessionInMatch,
        RelayFull,
        HostGone
    };

    const wchar_t* describe(RelayReject reason);

    // Relay frames carry the game's own packets as opaque payloads, so this is
    // the one place that has to turn a packet into bytes and back.
    void appendPayload(sf::Packet& frame, const sf::Packet& payload);
    bool extractPayload(sf::Packet& frame, sf::Packet& payload);

    sf::Packet beginRelay(RelayMessage id);
    bool readRelayHeader(sf::Packet& packet, RelayMessage& out);

    sf::Packet& operator<<(sf::Packet& packet, const SessionAdvert& advert);
    sf::Packet& operator>>(sf::Packet& packet, SessionAdvert& advert);

    // Six characters from an unambiguous alphabet. Over a relay this is not
    // decoration: it is the address, so it has to survive being read aloud.
    //
    // `regionTag`, when a relay has been given one, is prepended -- making the
    // code seven characters, the first of which says which relay minted it. That
    // is what lets a guest type a code and be dialled to the right relay out of
    // several, without anybody having to say "and it is the Frankfurt one". Pass
    // zero for an untagged relay and the code is six characters, exactly as
    // before regions existed.
    std::wstring makeRelayCode(std::uint64_t seed, wchar_t regionTag = 0);
    std::wstring normaliseCode(const std::wstring& code);
}
