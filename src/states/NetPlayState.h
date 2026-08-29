#pragma once

#include "GameState.h"
#include "../net/NetGame.h"
#include "../ui/Draw.h"

#include <vector>

namespace neoncoil
{
    // The online match screen.
    //
    // It owns no simulation and no game rules. Everything it draws comes out of
    // the MatchSnapshot the host produced, and everything the player presses is
    // forwarded as an intent -- which is exactly as true on the host, where the
    // "send" happens to be a direct call into the simulation. One render path,
    // one input path, no branch on who is hosting.
    //
    // The session pointer is owned by the LobbyState directly below this one on
    // the stack, so it cannot dangle.
    class NetPlayState : public GameState
    {
    public:
        explicit NetPlayState(net::NetGame* session);

        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

    private:
        void sendDirectionInput(const Input& input);

        void renderScoreboard(AppContext& context) const;
        void renderBoard(AppContext& context) const;
        void renderCountdown(AppContext& context) const;
        void renderResults(AppContext& context) const;
        void renderDisconnected(AppContext& context) const;

        const net::LobbySlot* seatFor(PlayerSlot slot) const;
        Color colourFor(PlayerSlot slot) const;

        net::NetGame* m_session{ nullptr };
        float m_elapsed{ 0.0f };

        // 0..1 through the current step, for sliding the snakes this client does
        // not simulate. Reset whenever a snapshot changes the board.
        float m_remoteSlide{ 0.0f };

        // Rebuilt each frame from the snapshot's cumulative list of destroyed
        // walls, so a wall a shield shattered stops being drawn without the
        // client needing a mutable copy of the arena.
        mutable std::vector<bool> m_openedMask;
    };
}
