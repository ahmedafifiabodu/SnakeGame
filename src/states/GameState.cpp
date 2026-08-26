#include "GameState.h"

namespace neoncoil
{
    void StateMachine::apply(Transition&& transition, AppContext& context)
    {
        switch (transition.kind)
        {
        case TransitionKind::None:
            break;

        case TransitionKind::Push:
            if (transition.state)
            {
                m_stack.push_back(std::move(transition.state));
                m_stack.back()->onEnter(context);
            }
            break;

        case TransitionKind::Pop:
            if (!m_stack.empty())
            {
                m_stack.back()->onExit(context);
                m_stack.pop_back();
            }
            break;

        case TransitionKind::Replace:
            if (!m_stack.empty())
            {
                m_stack.back()->onExit(context);
                m_stack.pop_back();
            }
            if (transition.state)
            {
                m_stack.push_back(std::move(transition.state));
                m_stack.back()->onEnter(context);
            }
            break;

        case TransitionKind::Reset:
            while (!m_stack.empty())
            {
                m_stack.back()->onExit(context);
                m_stack.pop_back();
            }
            if (transition.state)
            {
                m_stack.push_back(std::move(transition.state));
                m_stack.back()->onEnter(context);
            }
            break;

        case TransitionKind::Quit:
            m_wantsQuit = true;
            break;
        }
    }

    void StateMachine::update(AppContext& context, float deltaSeconds)
    {
        if (m_stack.empty())
            return;

        // Only the top of the stack updates: an overlay genuinely suspends the
        // world rather than leaving it running behind a curtain.
        Transition transition = m_stack.back()->update(context, deltaSeconds);
        apply(std::move(transition), context);
    }

    void StateMachine::render(AppContext& context)
    {
        if (m_stack.empty())
            return;

        // Walk back to the last state that paints a full screen, then draw
        // forwards so overlays composite on top of the live board.
        std::size_t first = m_stack.size() - 1;
        while (first > 0 && m_stack[first]->isOverlay())
            --first;

        for (std::size_t i = first; i < m_stack.size(); ++i)
            m_stack[i]->render(context);
    }
}
