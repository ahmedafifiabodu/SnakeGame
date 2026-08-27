#pragma once

#include <memory>
#include <string>

namespace neoncoil::net
{
    // Who a player is, as far as the networking layer is concerned.
    //
    // There is deliberately no account system yet. `id` is an opaque string and
    // NOTHING outside this file is allowed to assume what is in it -- today a
    // locally generated GUID, tomorrow a Steam ID or an account id from a login
    // service. The lobby, the protocol and the match all key off this string, so
    // swapping the provider is the whole of the change.
    struct PlayerIdentity
    {
        std::string id;
        std::wstring displayName;

        // False for a local identity. The host records it per player so that,
        // once logins exist, unauthenticated guests can be allowed, restricted
        // or refused by policy without touching the protocol.
        bool authenticated{ false };
    };

    // What a joining client presents to the host. `proof` is empty for local
    // identities; an auth backend will fill it with a signed token, and only
    // IIdentityProvider::verify needs to learn how to check it.
    struct JoinTicket
    {
        std::string identityId;
        std::wstring displayName;
        std::string proof;
    };

    class IIdentityProvider
    {
    public:
        virtual ~IIdentityProvider() = default;

        virtual PlayerIdentity local() const = 0;
        virtual void setDisplayName(const std::wstring& name) = 0;

        // Client side: the ticket to put in the Hello message.
        virtual JoinTicket issueTicket() const = 0;

        // Host side: turn a presented ticket into an identity, or refuse it.
        // The local provider accepts anything well-formed; a real one will
        // validate `proof` against the auth service here and nowhere else.
        virtual bool verify(const JoinTicket& ticket, PlayerIdentity& out, std::wstring& reason) const = 0;

        virtual const wchar_t* backendName() const = 0;
    };

    // Generates a stable per-installation GUID and keeps it in a file next to
    // the executable, so a player keeps the same identity between runs without
    // ever having created an account.
    class LocalIdentityProvider : public IIdentityProvider
    {
    public:
        explicit LocalIdentityProvider(std::string storagePath);

        PlayerIdentity local() const override { return m_identity; }
        void setDisplayName(const std::wstring& name) override;
        JoinTicket issueTicket() const override;
        bool verify(const JoinTicket& ticket, PlayerIdentity& out, std::wstring& reason) const override;
        const wchar_t* backendName() const override { return L"LOCAL"; }

    private:
        void load();
        void save() const;

        std::string m_storagePath;
        PlayerIdentity m_identity;
    };

    // Process-wide provider. Replacing it is the single hook an account system
    // needs; nothing else in net/ or states/ constructs a provider directly.
    IIdentityProvider& identityProvider();
    void setIdentityProvider(std::unique_ptr<IIdentityProvider> provider);
}
