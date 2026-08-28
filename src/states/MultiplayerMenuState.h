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
            Region,        // which relay to host through, and what it pings
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

        // One entry in the browser, joined the way that entry is reachable: a
        // local session by its address, an online one by its code. Everything
        // that can pick a session out of the list goes through here, so the two
        // kinds can never be confused for one another.
        Transition joinDiscovered(AppContext& context, const net::DiscoveredSession& session);
        // Ids above the Choice range identify a discovered session by index, or
        // the two arrows either side of the region field.
        enum Hit : int
        {
            HitRegionPrev = 200,
            HitRegionNext,
            HitSession = 300
        };

        Transition activate(AppContext& context);
        Transition handleMouse(AppContext& context);

        void handleAddressEntry(AppContext& context);
        Transition takeQuickMatchDecision(AppContext& context);

        void renderActions(AppContext& context) const;
        void renderSessions(AppContext& context) const;

        // AUTO is m_region == kAutoRegion; anything else is an index into
        // NetConfig::relays. Resolves to a concrete index, or -1 when nothing
        // has answered yet.
        int chosenRelay() const;
        void cycleRegion(int direction);

        // "MIDDLE EAST   24 MS", or what to say instead when it has not answered.
        std::wstring regionLabel(int relayIndex) const;

        // First row of the list that is on screen. The browser now carries
        // online sessions as well as local ones, so it can hold more than the
        // panel does and has to scroll to keep the selection in view.
        int firstVisibleSession(int sessionCount, int rows) const;

        std::unique_ptr<net::IMatchmaker> m_matchmaker;

        // -1 means AUTO: whichever relay is answering fastest right now. It is
        // the default because it is the right answer for almost every player,
        // and the picker exists for the ones it is not right for.
        static constexpr int kAutoRegion = -1;

        Column m_column{ Column::Actions };
        int m_region{ kAutoRegion };
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
