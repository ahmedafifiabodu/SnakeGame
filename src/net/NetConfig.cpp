#include "NetConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace neoncoil::net
{
    namespace
    {
        std::string trim(const std::string& text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        bool asBool(const std::string& value)
        {
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }

        std::wstring widen(const std::string& text)
        {
            std::wstring out;
            out.reserve(text.size());
            for (char c : text)
                out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            return out;
        }

        // `relay = M | MIDDLE EAST | 34.1.2.3 | 45700`
        //
        // Pipe-separated rather than four keys with a shared prefix, because a
        // relay is one thing and splitting it across four lines invites a config
        // where the third relay has a name and no address. Everything after the
        // host is optional.
        bool parseRelay(const std::string& value, RelayEndpoint& out)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;

            for (;;)
            {
                const std::size_t bar = value.find('|', start);
                parts.push_back(trim(value.substr(start,
                    bar == std::string::npos ? std::string::npos : bar - start)));

                if (bar == std::string::npos)
                    break;
                start = bar + 1;
            }

            // A bare `relay = 34.1.2.3` is legal and means an untagged relay,
            // which is exactly what a single-relay build already had.
            if (parts.size() == 1)
            {
                if (parts[0].empty())
                    return false;
                out.host = parts[0];
                return true;
            }

            if (parts.size() < 3 || parts[2].empty())
                return false;

            if (!parts[0].empty())
                out.regionTag = static_cast<wchar_t>(std::toupper(
                    static_cast<unsigned char>(parts[0][0])));
            if (!parts[1].empty())
                out.name = widen(parts[1]);

            out.host = parts[2];

            if (parts.size() >= 4 && !parts[3].empty())
                out.port = static_cast<std::uint16_t>(std::atoi(parts[3].c_str()));

            return true;
        }
    }

    int NetConfig::relayIndexForCode(const std::wstring& code) const
    {
        // Seven characters means the first one is a region tag; six is a code
        // from an untagged relay, and predates regions existing at all.
        if (code.size() != 7)
            return -1;

        for (std::size_t i = 0; i < relays.size(); ++i)
            if (relays[i].regionTag != 0 && relays[i].regionTag == code[0])
                return static_cast<int>(i);

        return -1;
    }

    void NetConfig::selectRelay(int index)
    {
        if (index < 0 || index >= static_cast<int>(relays.size()))
            return;

        relayHost = relays[static_cast<std::size_t>(index)].host;
        relayPort = relays[static_cast<std::size_t>(index)].port;
    }

    NetConfig& NetConfig::instance()
    {
        static NetConfig config;
        return config;
    }

    bool NetConfig::loadFromFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file)
            return false;

        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#')
                continue;

            const auto equals = trimmed.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = trim(trimmed.substr(0, equals));
            const std::string value = trim(trimmed.substr(equals + 1));
            if (key.empty() || value.empty())
                continue;

            const auto asInt = [&value] { return std::atoi(value.c_str()); };
            const auto asFloat = [&value] { return static_cast<float>(std::atof(value.c_str())); };
            const auto asPort = [&value] { return static_cast<std::uint16_t>(std::atoi(value.c_str())); };

            if (key == "host_port")                 hostPort = asPort();
            else if (key == "discovery_port")       discoveryPort = asPort();
            else if (key == "max_players")          maxPlayers = std::clamp(asInt(), 2, 4);
            else if (key == "snapshot_hz")          snapshotHz = std::clamp(asFloat(), 5.0f, 60.0f);
            else if (key == "connect_timeout")      connectTimeoutSeconds = asFloat();
            else if (key == "peer_timeout")         peerTimeoutSeconds = asFloat();
            else if (key == "discovery_ttl")        discoveryTtlSeconds = asFloat();
            else if (key == "quick_match_search")   quickMatchSearchSeconds = asFloat();
            else if (key == "advertise_on_lan")     advertiseOnLan = asBool(value);
            else if (key == "relay")
            {
                RelayEndpoint endpoint;
                endpoint.port = relayPort;   // the file's relay_port is the default
                if (parseRelay(value, endpoint))
                    relays.push_back(std::move(endpoint));
            }
            else if (key == "relay_host")           relayHost = value;
            else if (key == "relay_port")           relayPort = asPort();
            else if (key == "always_use_relay")     alwaysUseRelay = asBool(value);
            else if (key == "relay_list_interval")  relayListIntervalSeconds = std::clamp(asFloat(), 1.0f, 30.0f);
            else if (key == "relay_list_timeout")   relayListTimeoutSeconds = std::clamp(asFloat(), 0.5f, 10.0f);
            else if (key == "identity_file")        identityFile = value;
            else if (key == "match_duration")       rules.durationSeconds = asFloat();
            else if (key == "match_countdown")      rules.countdownSeconds = asFloat();
            else if (key == "match_score_limit")    rules.scoreLimit = asInt();
            else if (key == "match_respawn")        rules.respawnSeconds = asFloat();
            else if (key == "match_food_count")     rules.normalFoodCount = std::clamp(asInt(), 1, 12);
            else if (key == "match_kill_bonus")     rules.killBonus = asInt();
            else if (key == "match_death_penalty")  rules.deathPenalty = asInt();
            else if (key == "match_tick_seconds")   rules.tickSeconds = std::clamp(asFloat(), 0.05f, 0.5f);
            else if (key == "match_arena_level")    rules.arenaLevelIndex = std::max(1, asInt());
            else if (key == "match_max_length")     rules.maxLength = std::clamp(asInt(), 8, 200);
        }

        // The single-relay keys still work, and mean "one relay, no region".
        // Every build that shipped before regions existed uses them, and none of
        // those config files should have to be rewritten to keep working.
        if (relays.empty() && !relayHost.empty())
        {
            RelayEndpoint only;
            only.host = relayHost;
            only.port = relayPort;
            relays.push_back(std::move(only));
        }

        // relayHost / relayPort are what a session actually dials, so they are
        // left pointing at the first relay in the list -- the one the config
        // author put at the top.
        if (!relays.empty())
            selectRelay(0);

        return true;
    }
}
