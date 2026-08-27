#pragma once

#include "GameState.h"
#include "../net/NetGame.h"
#include "../ui/Hit.h"

#include <memory>
#include <string>

namespace neoncoil
{
    // The waiting room, and the owner of the session for its whole lifetime.
    //
    // It holds the NetGame as a unique_ptr and hands NetPlayState a raw pointer.
    // That is safe because NetPlayState is pushed ON TOP of this state, so this
    // one is guaranteed to outlive it -- and it means leaving the lobby is the
    // single place a session is torn down, however the player got there.
    //
    // Nothing in here asks whether the session is a host or a client except to
    // decide which button to draw.
    class LobbyState : public GameState
    {
    public:
        explicit LobbyState(std::unique_ptr<net::NetGame> session);
        ~LobbyState() override;

        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

    private:
        enum class Field
        {
            Colour,
            Type,
            Ready,      // START for the host
            Leave,
            Count
        };

        enum Hit : int
        {
            HitColourPrev = 100,
            HitColourNext = 101,
            HitTypePrev = 102,
            HitTypeNext = 103
        };

        void adjust(AppContext& context, int delta);
        void pushLoadout(AppContext& context);
        Transition activate(AppContext& context);
        Transition handleMouse(AppContext& context);

        // What is stopping the match from starting, phrased for the host.
        // Empty when nothing is.
        std::wstring blockingReason() const;

        void renderSlots(AppContext& context) const;
        void renderSidebar(AppContext& context) const;
        void renderConnecting(AppContext& context) const;
        void renderDisconnected(AppContext& context) const;

        std::unique_ptr<net::NetGame> m_session;

        Field m_field{ Field::Ready };
        bool m_ready{ false };
        float m_elapsed{ 0.0f };

        mutable ui::HitMap m_hits;

        // Set once the match screen has been pushed, so a match that is still
        // running does not get a second NetPlayState stacked on top of it.
        bool m_matchScreenOpen{ false };
    };
}
