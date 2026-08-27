#pragma once

#include <cstdint>
#include <string>

namespace neoncoil::net
{
    // What a host publishes about an open session, and what a browsing client
    // gets back.
    //
    // Lives in its own header because both the game and the standalone relay
    // server need it, and the relay must not have to pull in matchmaking,
    // rendering or anything else the game happens to link.
    struct SessionAdvert
    {
        std::wstring code;
        std::wstring hostName;
        std::uint16_t port{ 0 };
        std::uint8_t players{ 0 };
        std::uint8_t maxPlayers{ 4 };
        bool inMatch{ false };
    };
}
