#pragma once

#include "../game/MatchRules.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil::net
{
    // One relay, somewhere in the world.
    //
    // A relay is a byte pump, so the only thing that distinguishes a good one
    // from a bad one is how far it is from the players using it. That makes a
    // list of them, in different regions, the whole of "netcode quality" for
    // this game: both the input and the snapshot cross the gap between a player
    // and their relay, so halving that gap halves the felt latency.
    struct RelayEndpoint
    {
        std::string host;
        std::uint16_t port{ 45700 };

        // Shown in the region picker. Kept short: it sits next to a ping.
        std::wstring name{ L"RELAY" };

        // First character of every code this relay mints, so a guest typing a
        // code knows which relay to dial without being told. Zero means an
        // untagged relay, whose codes are six characters as they always were.
        wchar_t regionTag{ 0 };
    };
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
        // Every relay this build knows about, in the order they appear in the
        // config file. Set these once, in the netconfig.txt that ships beside
        // the game, and players never touch them: hosts and guests both dial OUT
        // to a relay, so nobody has to forward a port to play over the internet.
        // Leave the list empty and the game is local-network only.
        std::vector<RelayEndpoint> relays;

        // The relay THIS session will use. Sessions take a NetConfig by value,
        // so a screen picks a region by copying the config and setting these two
        // -- which is why nothing below the menu has to know a list exists.
        // After loadFromFile these point at the first relay in the list.
        std::string relayHost;
        std::uint16_t relayPort{ 45700 };

        // Prefer the relay even for hosts that could be reached directly. Off by
        // default: a LAN game should not take a detour through a datacentre.
        bool alwaysUseRelay{ false };

        // How often the multiplayer menu asks the relay what is open, and how
        // long it waits for the answer. The query is one small round trip, so
        // this is cheap; the timeout is short because it is a menu that has to
        // stay responsive, not a connection that has to succeed.
        float relayListIntervalSeconds{ 3.0f };
        float relayListTimeoutSeconds{ 2.5f };

        bool relayConfigured() const { return !relayHost.empty(); }
        bool haveRelays() const { return !relays.empty(); }

        // Which relay minted a code, by its tag. Returns -1 when the code is
        // untagged or names a region this build has never heard of.
        int relayIndexForCode(const std::wstring& code) const;

        // Points relayHost / relayPort at relays[index]. Out of range is a
        // no-op, so a stale region choice cannot leave a session dialling
        // nowhere.
        void selectRelay(int index);

        std::string identityFile{ "neoncoil_identity.txt" };

        MatchRules rules{};

        // Returns false if the file is absent; unknown keys are ignored so an
        // older build does not choke on a newer config.
        bool loadFromFile(const std::string& path);

        static NetConfig& instance();
    };
}
