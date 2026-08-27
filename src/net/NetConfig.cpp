#include "NetConfig.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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
            else if (key == "relay_host")           relayHost = value;
            else if (key == "relay_port")           relayPort = asPort();
            else if (key == "always_use_relay")     alwaysUseRelay = asBool(value);
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

        return true;
    }
}
