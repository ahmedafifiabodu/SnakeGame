#pragma once

#include "../core/Colors.h"
#include "../core/Rng.h"
#include "../core/Screen.h"
#include "../core/Vec2.h"

#include <string>
#include <vector>

namespace neoncoil::ui
{
    // Short-lived visual feedback: particles, rising score text and screen
    // shake. Purely cosmetic and self-contained -- nothing here can influence
    // gameplay, so it is always safe to skip or extend.
    class Effects
    {
    public:
        explicit Effects(std::uint64_t seed);

        void clear();
        void update(float deltaSeconds);

        // Positions are in tile space; render() converts to cells.
        void burst(Vec2 tile, int count, Color colour, float speed = 7.0f);
        void popText(Vec2 tile, std::wstring text, Color colour);
        void addShake(float strength);

        // Whole-screen tint, used for death and level completion.
        void flash(Color colour, float seconds);
        bool isFlashing() const { return m_flashRemaining > 0.0f; }
        Color flashColour() const { return m_flashColour; }
        float flashFraction() const;

        // Offset in console cells that the board should be drawn at.
        Vec2 shakeOffset() const;

        // Drawn in the board's pixel space and clipped to it: a burst near an
        // edge would otherwise spray particles over the frame and the HUD.
        void render(Screen& screen, const struct BoardView& view) const;

    private:
        struct Particle
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float vx{ 0.0f };
            float vy{ 0.0f };
            float life{ 0.0f };
            float maxLife{ 1.0f };
            wchar_t glyph{ L'*' };
            Color colour{ Color::White };
        };

        struct RisingText
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float life{ 0.0f };
            float maxLife{ 1.0f };
            std::wstring text;
            Color colour{ Color::White };
        };

        mutable Rng m_rng;
        std::vector<Particle> m_particles;
        std::vector<RisingText> m_texts;

        float m_shakeRemaining{ 0.0f };
        float m_shakeStrength{ 0.0f };
        float m_elapsed{ 0.0f };

        Color m_flashColour{ Color::White };
        float m_flashRemaining{ 0.0f };
        float m_flashDuration{ 1.0f };
    };
}
