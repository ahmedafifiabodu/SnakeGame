#pragma once

#include "../game/MatchSnapshot.h"

#include <array>
#include <cstdint>
#include <string>

namespace neoncoil::net
{
    // One seat in a session. The host owns every field; clients only ever
    // receive these, and change their own by asking the host.
    struct LobbySlot
    {
        bool occupied{ false };
        PlayerSlot slot{ kInvalidSlot };

        // Opaque identity string -- see net::PlayerIdentity. Carried in the
        // lobby so that friends, invites and stats can key off it later without
        // the lobby format changing.
        std::string identityId;
        bool authenticated{ false };

        std::wstring name{ L"---" };
        std::uint8_t colourIndex{ 0 };
        std::uint8_t typeIndex{ 0 };
        bool ready{ false };
        bool isHost{ false };
        std::uint16_t pingMs{ 0 };
    };

    // The whole shared lobby state. Small enough (four seats) that it is sent
    // whole on every change rather than diffed -- one message type, no ordering
    // problems, and a client that joins late is correct immediately.
    struct LobbyInfo
    {
        std::wstring code;          // short human-readable join code
        std::wstring hostName;
        std::uint16_t port{ 0 };
        std::uint8_t maxPlayers{ 4 };
        bool inMatch{ false };

        std::array<LobbySlot, kMaxMatchPlayers> slots{};

        int occupiedCount() const;
        bool isFull() const;
        bool everyoneReady() const;
        const LobbySlot* find(PlayerSlot slot) const;
        PlayerSlot hostSlot() const;
    };

    // Six characters from an unambiguous alphabet (no O/0, no I/1). Cosmetic
    // today -- the LAN browser shows it so players can confirm they are joining
    // the right session -- and the natural key for a future directory service
    // that maps a code to a host address.
    std::wstring makeJoinCode(std::uint64_t seed);
}
