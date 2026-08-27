#include "RelayProtocol.h"

#include <algorithm>
#include <cwctype>

namespace neoncoil::net
{
    namespace
    {
        // A game snapshot is around a kilobyte. This is a long way above
        // anything legitimate and exists so a hostile or broken endpoint cannot
        // make the relay allocate.
        constexpr std::uint32_t kMaxPayloadBytes = 64 * 1024;
        constexpr std::size_t kMaxNameLength = 16;
        constexpr std::size_t kMaxCodeLength = 12;

        // Deliberately excludes O/0 and I/1/L: this code gets read aloud over
        // voice chat and typed by somebody who did not hear it clearly.
        constexpr wchar_t kAlphabet[] = L"ABCDEFGHJKMNPQRSTUVWXYZ23456789";
        constexpr std::uint64_t kAlphabetSize = 31;
    }

    const wchar_t* describe(RelayReject reason)
    {
        switch (reason)
        {
        case RelayReject::None:            return L"";
        case RelayReject::BadProtocol:     return L"That is not a NEON COIL relay.";
        case RelayReject::VersionMismatch: return L"The relay is running a different game version.";
        case RelayReject::UnknownCode:     return L"No session with that code.";
        case RelayReject::SessionFull:     return L"That session is full.";
        case RelayReject::SessionInMatch:  return L"That session has already started.";
        case RelayReject::RelayFull:       return L"The relay is at capacity -- try again shortly.";
        case RelayReject::HostGone:        return L"The host closed the session.";
        }
        return L"The relay refused the connection.";
    }

    void appendPayload(sf::Packet& frame, const sf::Packet& payload)
    {
        // std::string is the only length-prefixed binary container sf::Packet
        // offers, and it is binary safe -- embedded nulls survive, because the
        // length travels with it rather than being inferred from a terminator.
        const std::string bytes(static_cast<const char*>(payload.getData()), payload.getDataSize());
        frame << bytes;
    }

    bool extractPayload(sf::Packet& frame, sf::Packet& payload)
    {
        std::string bytes;
        frame >> bytes;

        if (!frame || bytes.size() > kMaxPayloadBytes)
            return false;

        payload.clear();
        payload.append(bytes.data(), bytes.size());
        return true;
    }

    sf::Packet beginRelay(RelayMessage id)
    {
        sf::Packet packet;
        packet << kRelayMagic << static_cast<std::uint8_t>(id);
        return packet;
    }

    bool readRelayHeader(sf::Packet& packet, RelayMessage& out)
    {
        std::uint32_t magic = 0;
        std::uint8_t id = 0;
        packet >> magic >> id;

        if (!packet || magic != kRelayMagic)
            return false;
        if (id < static_cast<std::uint8_t>(RelayMessage::RegisterHost) ||
            id > static_cast<std::uint8_t>(RelayMessage::SessionList))
            return false;

        out = static_cast<RelayMessage>(id);
        return true;
    }

    sf::Packet& operator<<(sf::Packet& packet, const SessionAdvert& advert)
    {
        return packet << advert.code << advert.hostName << advert.port
            << advert.players << advert.maxPlayers << advert.inMatch;
    }

    sf::Packet& operator>>(sf::Packet& packet, SessionAdvert& advert)
    {
        packet >> advert.code >> advert.hostName >> advert.port
            >> advert.players >> advert.maxPlayers >> advert.inMatch;

        if (advert.code.size() > kMaxCodeLength)
            advert.code.resize(kMaxCodeLength);
        if (advert.hostName.size() > kMaxNameLength)
            advert.hostName.resize(kMaxNameLength);

        advert.maxPlayers = static_cast<std::uint8_t>(std::clamp<int>(advert.maxPlayers, 1, 4));
        advert.players = static_cast<std::uint8_t>(std::clamp<int>(advert.players, 0, advert.maxPlayers));
        return packet;
    }

    std::wstring makeRelayCode(std::uint64_t seed)
    {
        std::wstring code;
        std::uint64_t value = seed;

        for (int i = 0; i < 6; ++i)
        {
            code.push_back(kAlphabet[value % kAlphabetSize]);
            value /= kAlphabetSize;
            if (value == 0)
                value = seed >> (8 * (i + 1));
        }

        return code;
    }

    std::wstring normaliseCode(const std::wstring& code)
    {
        // Typed by hand, so uppercase it and drop the separators people add.
        // The confusable characters are folded onto the ones the alphabet
        // actually uses, which is the whole reason for leaving them out of it.
        std::wstring out;
        out.reserve(code.size());

        for (wchar_t c : code)
        {
            if (c == L' ' || c == L'-' || c == L'_')
                continue;

            // Case only. The confusable characters -- I, L, O, 0, 1 -- are not
            // in the alphabet at all, so there is nothing to fold them onto: a
            // code containing one was misheard, and "no session with that code"
            // is the honest answer rather than a guess at what was meant.
            out.push_back(static_cast<wchar_t>(towupper(static_cast<wint_t>(c))));

            if (out.size() >= kMaxCodeLength)
                break;
        }

        return out;
    }
}
