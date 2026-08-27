#include "Protocol.h"

#include "../game/Level.h"
#include "../game/LevelGenerator.h"

#include <algorithm>

namespace neoncoil::net
{
    namespace
    {
        // Ceilings applied to every length read off the wire. A malformed or
        // hostile peer must not be able to make the receiver allocate: these are
        // all comfortably above anything the game can legitimately produce.
        constexpr std::uint32_t kMaxBodyLength = 512;
        constexpr std::uint32_t kMaxFoodItems = 64;
        constexpr std::uint32_t kMaxSentinels = 64;
        constexpr std::uint32_t kMaxOpenedWalls = 512;
        constexpr std::uint32_t kMaxWallBytes = 4096;
        constexpr std::size_t kMaxNameLength = 16;
        constexpr std::size_t kMaxIdentityLength = 128;
        constexpr std::size_t kMaxTextLength = 64;

        void clampText(std::wstring& text, std::size_t limit)
        {
            if (text.size() > limit)
                text.resize(limit);
        }

        void clampText(std::string& text, std::size_t limit)
        {
            if (text.size() > limit)
                text.resize(limit);
        }

        // Reading a count is the one place a bad peer can do damage, so it is
        // funnelled through here rather than repeated inline.
        bool readCount(sf::Packet& packet, std::uint32_t limit, std::uint32_t& out)
        {
            std::uint32_t value = 0;
            packet >> value;
            if (!packet || value > limit)
            {
                out = 0;
                return false;
            }
            out = value;
            return true;
        }

        sf::Packet& writeTile(sf::Packet& packet, const Vec2& tile)
        {
            return packet << static_cast<std::int16_t>(tile.x) << static_cast<std::int16_t>(tile.y);
        }

        sf::Packet& readTile(sf::Packet& packet, Vec2& tile)
        {
            std::int16_t x = 0;
            std::int16_t y = 0;
            packet >> x >> y;
            tile.x = x;
            tile.y = y;
            return packet;
        }
    }

    const wchar_t* describe(RejectReason reason)
    {
        switch (reason)
        {
        case RejectReason::None:              return L"";
        case RejectReason::BadProtocol:       return L"That is not a NEON COIL session.";
        case RejectReason::VersionMismatch:   return L"Different game version -- both players need the same build.";
        case RejectReason::LobbyFull:         return L"That session is full.";
        case RejectReason::MatchInProgress:   return L"That session has already started.";
        case RejectReason::BadTicket:         return L"The host could not verify your player identity.";
        case RejectReason::DuplicateIdentity: return L"You are already in that session.";
        case RejectReason::HostClosed:        return L"The host closed the session.";
        }
        return L"Refused by the host.";
    }

    // ------------------------------------------------------------------- arena

    ArenaDescription describeArena(const Level& level)
    {
        ArenaDescription arena;
        arena.width = static_cast<std::int16_t>(level.width());
        arena.height = static_cast<std::int16_t>(level.height());
        arena.archetypeName = level.archetypeName;
        arena.seed = level.seed;
        arena.levelIndex = level.index;

        const std::size_t tiles = static_cast<std::size_t>(level.width()) * static_cast<std::size_t>(level.height());
        arena.wallBits.assign((tiles + 7) / 8, 0);

        for (int y = 0; y < level.height(); ++y)
        {
            for (int x = 0; x < level.width(); ++x)
            {
                if (!level.isWall({ x, y }))
                    continue;

                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(level.width()) +
                    static_cast<std::size_t>(x);
                arena.wallBits[index / 8] |= static_cast<std::uint8_t>(1u << (index % 8));
            }
        }

        return arena;
    }

    void buildArena(Level& level, const ArenaDescription& arena)
    {
        const int width = std::clamp<int>(arena.width, 1, 256);
        const int height = std::clamp<int>(arena.height, 1, 256);

        level.resize(width, height);
        level.archetypeName = arena.archetypeName;
        level.seed = arena.seed;
        level.index = arena.levelIndex;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x);
                const std::size_t byte = index / 8;

                const bool wall = byte < arena.wallBits.size() &&
                    (arena.wallBits[byte] & (1u << (index % 8))) != 0;

                level.set({ x, y }, wall ? Tile::Wall : Tile::Empty);
            }
        }

        level.rebuildOpenTiles();
    }

    // ----------------------------------------------------------------- headers

    sf::Packet beginClient(ClientMessage id)
    {
        sf::Packet packet;
        packet << kProtocolMagic << static_cast<std::uint8_t>(id);
        return packet;
    }

    sf::Packet beginServer(ServerMessage id)
    {
        sf::Packet packet;
        packet << kProtocolMagic << static_cast<std::uint8_t>(id);
        return packet;
    }

    bool readClientHeader(sf::Packet& packet, ClientMessage& out)
    {
        std::uint32_t magic = 0;
        std::uint8_t id = 0;
        packet >> magic >> id;

        if (!packet || magic != kProtocolMagic)
            return false;
        if (id < static_cast<std::uint8_t>(ClientMessage::Hello) ||
            id > static_cast<std::uint8_t>(ClientMessage::Leave))
            return false;

        out = static_cast<ClientMessage>(id);
        return true;
    }

    bool readServerHeader(sf::Packet& packet, ServerMessage& out)
    {
        std::uint32_t magic = 0;
        std::uint8_t id = 0;
        packet >> magic >> id;

        if (!packet || magic != kProtocolMagic)
            return false;
        if (id < static_cast<std::uint8_t>(ServerMessage::Welcome) ||
            id > static_cast<std::uint8_t>(ServerMessage::Heartbeat))
            return false;

        out = static_cast<ServerMessage>(id);
        return true;
    }

    // ------------------------------------------------------------------ ticket

    sf::Packet& operator<<(sf::Packet& packet, const JoinTicket& ticket)
    {
        return packet << ticket.identityId << ticket.displayName << ticket.proof;
    }

    sf::Packet& operator>>(sf::Packet& packet, JoinTicket& ticket)
    {
        packet >> ticket.identityId >> ticket.displayName >> ticket.proof;
        clampText(ticket.identityId, kMaxIdentityLength);
        clampText(ticket.displayName, kMaxNameLength);
        clampText(ticket.proof, 1024);
        return packet;
    }

    // ------------------------------------------------------------------- lobby

    sf::Packet& operator<<(sf::Packet& packet, const LobbySlot& slot)
    {
        return packet << slot.occupied << slot.slot << slot.identityId << slot.authenticated
            << slot.name << slot.colourIndex << slot.typeIndex << slot.ready << slot.isHost << slot.pingMs;
    }

    sf::Packet& operator>>(sf::Packet& packet, LobbySlot& slot)
    {
        packet >> slot.occupied >> slot.slot >> slot.identityId >> slot.authenticated
            >> slot.name >> slot.colourIndex >> slot.typeIndex >> slot.ready >> slot.isHost >> slot.pingMs;
        clampText(slot.identityId, kMaxIdentityLength);
        clampText(slot.name, kMaxNameLength);
        return packet;
    }

    sf::Packet& operator<<(sf::Packet& packet, const LobbyInfo& lobby)
    {
        packet << lobby.code << lobby.hostName << lobby.port << lobby.maxPlayers << lobby.inMatch;
        for (const LobbySlot& slot : lobby.slots)
            packet << slot;
        return packet;
    }

    sf::Packet& operator>>(sf::Packet& packet, LobbyInfo& lobby)
    {
        packet >> lobby.code >> lobby.hostName >> lobby.port >> lobby.maxPlayers >> lobby.inMatch;
        clampText(lobby.code, kMaxTextLength);
        clampText(lobby.hostName, kMaxNameLength);
        lobby.maxPlayers = static_cast<std::uint8_t>(std::clamp<int>(lobby.maxPlayers, 1, kMaxMatchPlayers));

        for (LobbySlot& slot : lobby.slots)
            packet >> slot;
        return packet;
    }

    // ------------------------------------------------------------------- input

    sf::Packet& operator<<(sf::Packet& packet, const InputCommand& input)
    {
        return packet << input.sequence << input.hasDirection
            << static_cast<std::uint8_t>(input.direction) << input.ability;
    }

    sf::Packet& operator>>(sf::Packet& packet, InputCommand& input)
    {
        std::uint8_t direction = 0;
        packet >> input.sequence >> input.hasDirection >> direction >> input.ability;
        input.direction = static_cast<Direction>(std::min<std::uint8_t>(direction,
            static_cast<std::uint8_t>(Direction::Right)));
        return packet;
    }

    // ------------------------------------------------------------------- rules

    sf::Packet& operator<<(sf::Packet& packet, const MatchRules& rules)
    {
        return packet << rules.countdownSeconds << rules.durationSeconds
            << static_cast<std::int32_t>(rules.scoreLimit) << rules.respawnSeconds
            << static_cast<std::int32_t>(rules.startLength) << static_cast<std::int32_t>(rules.foodValue)
            << static_cast<std::int32_t>(rules.bonusFoodValue) << static_cast<std::int32_t>(rules.killBonus)
            << static_cast<std::int32_t>(rules.deathPenalty) << static_cast<std::int32_t>(rules.normalFoodCount)
            << rules.bonusFoodInterval << rules.bonusLifetimeSeconds << rules.tickSeconds
            << static_cast<std::int32_t>(rules.arenaLevelIndex) << static_cast<std::int32_t>(rules.maxLength);
    }

    sf::Packet& operator>>(sf::Packet& packet, MatchRules& rules)
    {
        std::int32_t scoreLimit = 0;
        std::int32_t startLength = 0;
        std::int32_t foodValue = 0;
        std::int32_t bonusFoodValue = 0;
        std::int32_t killBonus = 0;
        std::int32_t deathPenalty = 0;
        std::int32_t normalFoodCount = 0;
        std::int32_t arenaLevelIndex = 0;
        std::int32_t maxLength = 0;

        packet >> rules.countdownSeconds >> rules.durationSeconds >> scoreLimit >> rules.respawnSeconds
            >> startLength >> foodValue >> bonusFoodValue >> killBonus >> deathPenalty >> normalFoodCount
            >> rules.bonusFoodInterval >> rules.bonusLifetimeSeconds >> rules.tickSeconds
            >> arenaLevelIndex >> maxLength;

        // Clients only use these for display and for pacing their own view, but
        // clamping keeps a hostile host from producing a divide-by-zero or an
        // absurd allocation on the client side.
        rules.scoreLimit = std::max(0, scoreLimit);
        rules.startLength = std::clamp(startLength, 2, 32);
        rules.foodValue = std::clamp(foodValue, 0, 10000);
        rules.bonusFoodValue = std::clamp(bonusFoodValue, 0, 10000);
        rules.killBonus = std::clamp(killBonus, 0, 10000);
        rules.deathPenalty = std::clamp(deathPenalty, 0, 10000);
        rules.normalFoodCount = std::clamp(normalFoodCount, 1, static_cast<std::int32_t>(kMaxFoodItems));
        rules.arenaLevelIndex = std::max(1, arenaLevelIndex);
        rules.maxLength = std::clamp(maxLength, 8, static_cast<std::int32_t>(kMaxBodyLength));
        rules.tickSeconds = std::clamp(rules.tickSeconds, 0.02f, 1.0f);
        rules.countdownSeconds = std::clamp(rules.countdownSeconds, 0.0f, 30.0f);
        rules.durationSeconds = std::clamp(rules.durationSeconds, 10.0f, 3600.0f);
        rules.respawnSeconds = std::clamp(rules.respawnSeconds, 0.0f, 30.0f);
        return packet;
    }

    // ------------------------------------------------------------------- arena

    sf::Packet& operator<<(sf::Packet& packet, const ArenaDescription& arena)
    {
        packet << arena.width << arena.height << arena.archetypeName << arena.seed << arena.levelIndex;
        packet << static_cast<std::uint32_t>(arena.wallBits.size());
        for (std::uint8_t byte : arena.wallBits)
            packet << byte;
        return packet;
    }

    sf::Packet& operator>>(sf::Packet& packet, ArenaDescription& arena)
    {
        packet >> arena.width >> arena.height >> arena.archetypeName >> arena.seed >> arena.levelIndex;
        clampText(arena.archetypeName, kMaxTextLength);

        std::uint32_t count = 0;
        if (!readCount(packet, kMaxWallBytes, count))
            return packet;

        arena.wallBits.resize(count);
        for (std::uint32_t i = 0; i < count; ++i)
            packet >> arena.wallBits[i];
        return packet;
    }

    // ---------------------------------------------------------------- snapshot

    sf::Packet& operator<<(sf::Packet& packet, const MatchSnapshot& snapshot)
    {
        packet << snapshot.tick << static_cast<std::uint8_t>(snapshot.phase) << snapshot.phaseRemaining;

        packet << static_cast<std::uint32_t>(snapshot.snakes.size());
        for (const SnakeSnapshot& snake : snapshot.snakes)
        {
            packet << snake.slot << snake.alive << snake.respawnRemaining
                << static_cast<std::uint8_t>(snake.direction)
                << static_cast<std::int32_t>(snake.score)
                << static_cast<std::int32_t>(snake.kills)
                << static_cast<std::int32_t>(snake.deaths)
                << snake.phasing << snake.shielded << snake.abilityActive << snake.abilityCharge;

            packet << static_cast<std::uint32_t>(snake.body.size());
            for (const Vec2& segment : snake.body)
                writeTile(packet, segment);
        }

        packet << static_cast<std::uint32_t>(snapshot.food.size());
        for (const FoodSnapshot& food : snapshot.food)
        {
            writeTile(packet, food.position);
            packet << static_cast<std::uint8_t>(food.kind) << food.secondsRemaining;
        }

        packet << static_cast<std::uint32_t>(snapshot.sentinels.size());
        for (const Vec2& sentinel : snapshot.sentinels)
            writeTile(packet, sentinel);

        packet << static_cast<std::uint32_t>(snapshot.openedWalls.size());
        for (const Vec2& wall : snapshot.openedWalls)
            writeTile(packet, wall);

        return packet;
    }

    sf::Packet& operator>>(sf::Packet& packet, MatchSnapshot& snapshot)
    {
        std::uint8_t phase = 0;
        packet >> snapshot.tick >> phase >> snapshot.phaseRemaining;
        snapshot.phase = static_cast<MatchPhase>(std::min<std::uint8_t>(phase,
            static_cast<std::uint8_t>(MatchPhase::Finished)));

        std::uint32_t snakeCount = 0;
        if (!readCount(packet, static_cast<std::uint32_t>(kMaxMatchPlayers), snakeCount))
            return packet;

        snapshot.snakes.clear();
        snapshot.snakes.reserve(snakeCount);

        for (std::uint32_t i = 0; i < snakeCount; ++i)
        {
            SnakeSnapshot snake;
            std::uint8_t direction = 0;
            std::int32_t score = 0;
            std::int32_t kills = 0;
            std::int32_t deaths = 0;

            packet >> snake.slot >> snake.alive >> snake.respawnRemaining >> direction
                >> score >> kills >> deaths
                >> snake.phasing >> snake.shielded >> snake.abilityActive >> snake.abilityCharge;

            snake.direction = static_cast<Direction>(std::min<std::uint8_t>(direction,
                static_cast<std::uint8_t>(Direction::Right)));
            snake.score = score;
            snake.kills = kills;
            snake.deaths = deaths;

            std::uint32_t bodyCount = 0;
            if (!readCount(packet, kMaxBodyLength, bodyCount))
                return packet;

            snake.body.resize(bodyCount);
            for (std::uint32_t s = 0; s < bodyCount; ++s)
                readTile(packet, snake.body[s]);

            snapshot.snakes.push_back(std::move(snake));
        }

        std::uint32_t foodCount = 0;
        if (!readCount(packet, kMaxFoodItems, foodCount))
            return packet;

        snapshot.food.clear();
        snapshot.food.reserve(foodCount);
        for (std::uint32_t i = 0; i < foodCount; ++i)
        {
            FoodSnapshot food;
            std::uint8_t kind = 0;
            readTile(packet, food.position);
            packet >> kind >> food.secondsRemaining;
            food.kind = kind == 0 ? FoodKind::Normal : FoodKind::Bonus;
            snapshot.food.push_back(food);
        }

        std::uint32_t sentinelCount = 0;
        if (!readCount(packet, kMaxSentinels, sentinelCount))
            return packet;

        snapshot.sentinels.resize(sentinelCount);
        for (std::uint32_t i = 0; i < sentinelCount; ++i)
            readTile(packet, snapshot.sentinels[i]);

        std::uint32_t wallCount = 0;
        if (!readCount(packet, kMaxOpenedWalls, wallCount))
            return packet;

        snapshot.openedWalls.resize(wallCount);
        for (std::uint32_t i = 0; i < wallCount; ++i)
            readTile(packet, snapshot.openedWalls[i]);

        return packet;
    }

    // ------------------------------------------------------------------ result

    sf::Packet& operator<<(sf::Packet& packet, const MatchResult& result)
    {
        packet << result.winner << result.draw;
        packet << static_cast<std::uint32_t>(result.standings.size());

        for (const MatchStanding& standing : result.standings)
        {
            packet << standing.slot << standing.name << standing.colourIndex << standing.typeIndex
                << static_cast<std::int32_t>(standing.score)
                << static_cast<std::int32_t>(standing.kills)
                << static_cast<std::int32_t>(standing.deaths);
        }

        return packet;
    }

    sf::Packet& operator>>(sf::Packet& packet, MatchResult& result)
    {
        packet >> result.winner >> result.draw;

        std::uint32_t count = 0;
        if (!readCount(packet, static_cast<std::uint32_t>(kMaxMatchPlayers), count))
            return packet;

        result.standings.clear();
        result.standings.reserve(count);

        for (std::uint32_t i = 0; i < count; ++i)
        {
            MatchStanding standing;
            std::int32_t score = 0;
            std::int32_t kills = 0;
            std::int32_t deaths = 0;

            packet >> standing.slot >> standing.name >> standing.colourIndex >> standing.typeIndex
                >> score >> kills >> deaths;

            clampText(standing.name, kMaxNameLength);
            standing.score = score;
            standing.kills = kills;
            standing.deaths = deaths;
            result.standings.push_back(std::move(standing));
        }

        return packet;
    }
}
