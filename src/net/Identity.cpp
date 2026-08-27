#include "Identity.h"

#include "../core/Rng.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace neoncoil::net
{
    namespace
    {
        std::string makeGuid()
        {
            // Two clock-seeded 64-bit draws, mixed. Not cryptographic -- it only
            // has to be unique across installations, and the host never trusts
            // it for anything but telling two players apart.
            const std::uint64_t a = Rng::mix(Rng::seedFromClock(), 0x9E3779B97F4A7C15ull);
            const std::uint64_t b = Rng::mix(a, Rng::seedFromClock());

            char buffer[40] = {};
            std::snprintf(buffer, sizeof(buffer), "local-%016llx%016llx",
                static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
            return buffer;
        }

        std::string narrow(const std::wstring& text)
        {
            std::string out;
            out.reserve(text.size());
            for (wchar_t c : text)
                out.push_back(c < 128 ? static_cast<char>(c) : '?');
            return out;
        }

        std::wstring widen(const std::string& text)
        {
            return std::wstring(text.begin(), text.end());
        }

        std::unique_ptr<IIdentityProvider>& providerSlot()
        {
            static std::unique_ptr<IIdentityProvider> provider;
            return provider;
        }
    }

    LocalIdentityProvider::LocalIdentityProvider(std::string storagePath)
        : m_storagePath(std::move(storagePath))
    {
        load();
    }

    void LocalIdentityProvider::load()
    {
        std::ifstream file(m_storagePath);
        if (file)
        {
            std::string id;
            std::string name;
            std::getline(file, id);
            std::getline(file, name);

            if (!id.empty())
            {
                m_identity.id = id;
                m_identity.displayName = name.empty() ? L"PLAYER" : widen(name);
                m_identity.authenticated = false;
                return;
            }
        }

        m_identity.id = makeGuid();
        m_identity.displayName = L"PLAYER";
        m_identity.authenticated = false;
        save();
    }

    void LocalIdentityProvider::save() const
    {
        std::ofstream file(m_storagePath, std::ios::trunc);
        if (!file)
            return;   // a read-only install directory is not worth failing over
        file << m_identity.id << "\n" << narrow(m_identity.displayName) << "\n";
    }

    void LocalIdentityProvider::setDisplayName(const std::wstring& name)
    {
        if (name.empty() || name == m_identity.displayName)
            return;
        m_identity.displayName = name;
        save();
    }

    JoinTicket LocalIdentityProvider::issueTicket() const
    {
        JoinTicket ticket;
        ticket.identityId = m_identity.id;
        ticket.displayName = m_identity.displayName;
        ticket.proof.clear();
        return ticket;
    }

    bool LocalIdentityProvider::verify(const JoinTicket& ticket, PlayerIdentity& out, std::wstring& reason) const
    {
        // No trust model yet, only sanity: an id has to exist and be short
        // enough that a malformed client cannot make the host allocate wildly.
        if (ticket.identityId.empty() || ticket.identityId.size() > 128)
        {
            reason = L"malformed identity";
            return false;
        }

        out.id = ticket.identityId;
        out.displayName = ticket.displayName.empty() ? L"PLAYER" : ticket.displayName;
        if (out.displayName.size() > 16)
            out.displayName.resize(16);
        out.authenticated = false;
        return true;
    }

    IIdentityProvider& identityProvider()
    {
        std::unique_ptr<IIdentityProvider>& provider = providerSlot();
        if (!provider)
            provider = std::make_unique<LocalIdentityProvider>("neoncoil_identity.txt");
        return *provider;
    }

    void setIdentityProvider(std::unique_ptr<IIdentityProvider> provider)
    {
        providerSlot() = std::move(provider);
    }
}
