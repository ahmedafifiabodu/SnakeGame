#pragma once

#include "GameState.h"
#include "../net/Matchmaker.h"
#include "../ui/Hit.h"

#include <memory>
#include <string>

namespace neoncoil
{
    // The front door to online play: host a session, let matchmaking find one,
    // pick one off the local network, or type an address.
    //
    // It creates the session object and hands it to LobbyState, which owns it
    // from there. Nothing about connecting lives in here, so this screen stays a
    // menu rather than becoming a second copy of the session lifecycle.
    class MultiplayerMenuState : public GameState
    {
    public:
        void onEnter(AppContext& context) override;
        void onExit(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

    private:
        // Not called Action: that name already belongs to the input layer, and
        // having two of them in scope here reads badly at every use site.
        enum class Choice
        {
            HostOnline,    // via the relay: reachable from anywhere, no setup
            HostLan,       // direct listen: same network only, no relay needed
            QuickMatch,
            Code,          // session code entry
            JoinCode,
            Address,       // direct address entry
            JoinAddress,
            Back,
            Count
        };

        enum class Column
        {
            Actions,
            Sessions
        };

        Transition host(AppContext& context, bool viaRelay);
        Transition join(AppContext& context, const std::string& address, std::uint16_t port);
        Transition joinByCode(AppContext& context, const std::wstring& code);
        // Ids above the Choice range identify a discovered session by index.
        enum Hit : int
        {
            HitSession = 300
        };

        Transition activate(AppContext& context);
        Transition handleMouse(AppContext& context);

        void handleAddressEntry(AppContext& context);
        Transition takeQuickMatchDecision(AppContext& context);

        void renderActions(AppContext& context) const;
        void renderSessions(AppContext& context) const;

        std::unique_ptr<net::IMatchmaker> m_matchmaker;

        Column m_column{ Column::Actions };
        // onEnter picks the right default: HOST ONLINE when this build has a
        // relay, HOST ON THIS NETWORK when it does not.
        Choice m_choice{ Choice::HostLan };
        int m_sessionSelection{ 0 };

        std::wstring m_address{ L"127.0.0.1" };
        bool m_addressTouched{ false };

        std::wstring m_code;

        // Quick match is a timed search followed by a decision, so it needs a
        // little state of its own rather than being a single keypress.
        bool m_searching{ false };
        float m_searchElapsed{ 0.0f };

        std::wstring m_message;
        float m_elapsed{ 0.0f };

        mutable ui::HitMap m_hits;
    };
}
