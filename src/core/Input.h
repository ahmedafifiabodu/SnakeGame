#pragma once

#include <array>
#include <string>

namespace sf { class Event; }

namespace neoncoil
{
    // Semantic actions. Game code asks for these, never for key codes, so
    // rebinding or adding a gamepad only touches this file.
    //
    // Nav* are bound to the arrow keys ONLY, while Up/Down/Left/Right also take
    // WASD. The menu uses Nav* while the name field has focus, so typing "WASD"
    // into your name cannot also move the cursor.
    enum class Action
    {
        Up,
        Down,
        Left,
        Right,
        NavUp,
        NavDown,
        NavLeft,
        NavRight,
        Confirm,
        Back,
        Pause,
        Ability,
        Restart,
        Fullscreen,
        Count
    };

    // Edge-triggered input fed from the window's event queue.
    class Input
    {
    public:
        // Clears this frame's edges. Called once per frame before events are
        // pumped into handleEvent().
        void beginFrame();
        void handleEvent(const sf::Event& event);

        // Discards anything accumulated (e.g. keys held across a state change).
        void flush();

        bool pressed(Action action) const;
        bool anyKeyPressed() const { return m_anyKeyPressed; }

        // Printable ASCII typed this frame, in order. Used by the name field.
        const std::wstring& typedText() const { return m_typedText; }
        int backspaceCount() const { return m_backspaceCount; }

    private:
        std::array<bool, static_cast<std::size_t>(Action::Count)> m_actionPressed{};
        std::wstring m_typedText;
        int m_backspaceCount{ 0 };
        bool m_anyKeyPressed{ false };
    };
}
