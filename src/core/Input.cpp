#include "Input.h"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

namespace neoncoil
{
    namespace
    {
        struct Binding
        {
            Action action;
            sf::Keyboard::Key key;
        };

        // Single source of truth for the keyboard layout.
        //
        // Confirm is Enter only and Ability is Space: keeping them apart means
        // typing a space into the name field can never also confirm the menu.
        constexpr Binding kBindings[] = {
            { Action::Up,         sf::Keyboard::Key::Up },
            { Action::Up,         sf::Keyboard::Key::W },
            { Action::Down,       sf::Keyboard::Key::Down },
            { Action::Down,       sf::Keyboard::Key::S },
            { Action::Left,       sf::Keyboard::Key::Left },
            { Action::Left,       sf::Keyboard::Key::A },
            { Action::Right,      sf::Keyboard::Key::Right },
            { Action::Right,      sf::Keyboard::Key::D },

            { Action::NavUp,      sf::Keyboard::Key::Up },
            { Action::NavDown,    sf::Keyboard::Key::Down },
            { Action::NavLeft,    sf::Keyboard::Key::Left },
            { Action::NavRight,   sf::Keyboard::Key::Right },

            { Action::Confirm,    sf::Keyboard::Key::Enter },
            { Action::Back,       sf::Keyboard::Key::Escape },
            { Action::Pause,      sf::Keyboard::Key::P },
            { Action::Ability,    sf::Keyboard::Key::Space },
            { Action::Restart,    sf::Keyboard::Key::R },
            { Action::Fullscreen, sf::Keyboard::Key::F11 },
        };

        bool isPrintableAscii(char32_t c)
        {
            return c >= 32 && c < 127;
        }
    }

    void Input::beginFrame()
    {
        m_actionPressed.fill(false);
        m_typedText.clear();
        m_backspaceCount = 0;
        m_anyKeyPressed = false;

        // Edges only. Where the pointer is survives the frame boundary, because
        // a mouse that has not moved is still hovering over something.
        m_mouseClicked = false;
        m_mouseRightClicked = false;
        m_mouseMoved = false;
    }

    void Input::setMousePosition(float canvasX, float canvasY, Vec2 cell)
    {
        if (canvasX != m_mouseX || canvasY != m_mouseY)
            m_mouseMoved = true;

        m_mouseX = canvasX;
        m_mouseY = canvasY;
        m_mouseCell = cell;
        m_mouseInside = true;
    }

    void Input::handleEvent(const sf::Event& event)
    {
        if (const auto* key = event.getIf<sf::Event::KeyPressed>())
        {
            m_anyKeyPressed = true;

            for (const Binding& binding : kBindings)
                if (binding.key == key->code)
                    m_actionPressed[static_cast<std::size_t>(binding.action)] = true;

            if (key->code == sf::Keyboard::Key::Backspace)
                ++m_backspaceCount;

            return;
        }

        if (const auto* typed = event.getIf<sf::Event::TextEntered>())
        {
            // Backspace and Enter also arrive here; only real characters are kept.
            if (isPrintableAscii(typed->unicode))
                m_typedText.push_back(static_cast<wchar_t>(typed->unicode));
            return;
        }

        if (const auto* button = event.getIf<sf::Event::MouseButtonPressed>())
        {
            // Screen has already put the pointer on the canvas for this event,
            // so a click is only the button edge.
            if (button->button == sf::Mouse::Button::Left)
                m_mouseClicked = true;
            else if (button->button == sf::Mouse::Button::Right)
                m_mouseRightClicked = true;

            m_anyKeyPressed = true;   // "press any key" prompts mean the mouse too
            return;
        }

        if (event.is<sf::Event::MouseLeft>())
            m_mouseInside = false;
    }

    void Input::flush()
    {
        beginFrame();
    }

    bool Input::pressed(Action action) const
    {
        return m_actionPressed[static_cast<std::size_t>(action)];
    }
}
