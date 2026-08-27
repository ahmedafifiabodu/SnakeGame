#include "Lobby.h"

namespace neoncoil::net
{
    int LobbyInfo::occupiedCount() const
    {
        int count = 0;
        for (const LobbySlot& slot : slots)
            if (slot.occupied)
                ++count;
        return count;
    }

    bool LobbyInfo::isFull() const
    {
        return occupiedCount() >= static_cast<int>(maxPlayers);
    }

    bool LobbyInfo::everyoneReady() const
    {
        int occupied = 0;
        for (const LobbySlot& slot : slots)
        {
            if (!slot.occupied)
                continue;
            ++occupied;

            // The host is not asked to ready up: pressing START is the host's
            // consent, and requiring both is just an extra keypress.
            if (!slot.isHost && !slot.ready)
                return false;
        }
        return occupied > 0;
    }

    const LobbySlot* LobbyInfo::find(PlayerSlot slot) const
    {
        for (const LobbySlot& candidate : slots)
            if (candidate.occupied && candidate.slot == slot)
                return &candidate;
        return nullptr;
    }

    PlayerSlot LobbyInfo::hostSlot() const
    {
        for (const LobbySlot& slot : slots)
            if (slot.occupied && slot.isHost)
                return slot.slot;
        return kInvalidSlot;
    }

    std::wstring makeJoinCode(std::uint64_t seed)
    {
        static constexpr wchar_t kAlphabet[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        static constexpr std::uint64_t kBase = 32;

        std::wstring code;
        std::uint64_t value = seed;
        for (int i = 0; i < 6; ++i)
        {
            code.push_back(kAlphabet[value % kBase]);
            value /= kBase;
            if (value == 0)
                value = seed >> (8 * (i + 1));
        }
        return code;
    }
}
