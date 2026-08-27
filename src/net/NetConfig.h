#pragma once

#include "../game/MatchRules.h"

#include <cstdint>
#include <string>

namespace neoncoil::net
{
    // Every tunable the networking layer has, in one place. Nothing in net/
    // reads a literal port or timeout: they all come from here, so pointing the
    // game at a different port, region or backend is a config change rather than
    // a rebuild.
    //
    // Resolution order, lowest priority first:
    //   1. the defaults below
    //   2. netconfig.txt next to the executable, if present
    //   3. command line (--port, --discovery-port, --match-duration, ...)
    struct NetConfig
    {
        std::uint16_t hostPort{ 45711 };
        std::uint16_t discoveryPort{ 45712 };

        int maxPlayers{ 4 };
        int protocolVersion{ 1 };

        float snapshotHz{ 20.0f };
        float heartbeatHz{ 1.0f };
        float connectTimeoutSeconds{ 8.0f };
        float peerTimeoutSeconds{ 12.0f };

        // How long a discovered LAN session stays in the browser after its last
        // beacon, and how often a browsing client re-probes.
        float discoveryTtlSeconds{ 4.0f };
        float discoveryProbeInterval{ 1.0f };
        // Seconds Quick Match spends looking before it gives up and hosts.
        float quickMatchSearchSeconds{ 2.5f };

        bool advertiseOnLan{ true };

        // --- relay ------------------------------------------------------------
        // Where the shipped relay lives. Set this once, in the netconfig.txt that
        // ships beside the game, and players never touch it: hosts and guests
        // both dial OUT to it, so nobody has to forward a port to play over the
        // internet. Leave it empty and the game is local-network only.
        std::string relayHost;
        std::uint16_t relayPort{ 45700 };

        // Prefer the relay even for hosts that could be reached directly. Off by
        // default: a LAN game should not take a detour through a datacentre.
        bool alwaysUseRelay{ false };

        bool relayConfigured() const { return !relayHost.empty(); }

        std::string identityFile{ "neoncoil_identity.txt" };

        MatchRules rules{};

        // Returns false if the file is absent; unknown keys are ignored so an
        // older build does not choke on a newer config.
        bool loadFromFile(const std::string& path);

        static NetConfig& instance();
    };
}
