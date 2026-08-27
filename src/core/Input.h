#pragma once

#include "Vec2.h"

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

        // --- mouse -----------------------------------------------------------
        // Positions arrive already mapped onto the virtual canvas: Screen owns
        // the letterbox and the cell size, so nothing above here has to know the
        // window was resized, moved to another monitor or made fullscreen.
        //
        // Screen calls this; states never do.
        void setMousePosition(float canvasX, float canvasY, Vec2 cell);
        void setMouseInside(bool inside) { m_mouseInside = inside; }

        Vec2 mouseCell() const { return m_mouseCell; }
        float mouseX() const { return m_mouseX; }
        float mouseY() const { return m_mouseY; }

        bool mouseInside() const { return m_mouseInside; }
        bool mouseClicked() const { return m_mouseClicked; }
        bool mouseRightClicked() const { return m_mouseRightClicked; }

        // True only on frames the pointer actually moved. Hover-to-focus is
        // gated on this so a resting mouse cannot fight the keyboard for which
        // field is selected.
        bool mouseMoved() const { return m_mouseMoved; }

    private:
        std::array<bool, static_cast<std::size_t>(Action::Count)> m_actionPressed{};
        std::wstring m_typedText;
        int m_backspaceCount{ 0 };
        bool m_anyKeyPressed{ false };

        // Position persists across frames; the edges do not.
        float m_mouseX{ -1.0f };
        float m_mouseY{ -1.0f };
        Vec2 m_mouseCell{ -1, -1 };
        bool m_mouseInside{ false };
        bool m_mouseClicked{ false };
        bool m_mouseRightClicked{ false };
        bool m_mouseMoved{ false };
    };
}
