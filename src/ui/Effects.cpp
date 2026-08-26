#include "Effects.h"

#include "Draw.h"
#include "../core/Glyphs.h"
#include "../game/LevelGenerator.h"

#include <algorithm>
#include <cmath>

namespace neoncoil::ui
{
    namespace
    {
        constexpr wchar_t kSparkGlyphs[] = {
            glyph::Dot, glyph::Bullet, glyph::Sparkle, glyph::SquareSmall, glyph::Star
        };

        // Particle count is capped so a long chain of pickups can never turn
        // into a rendering cost the tick budget cannot absorb.
        constexpr std::size_t kMaxParticles = 400;
    }

    Effects::Effects(std::uint64_t seed)
        : m_rng(seed)
    {
    }

    void Effects::clear()
    {
        m_particles.clear();
        m_texts.clear();
        m_shakeRemaining = 0.0f;
        m_shakeStrength = 0.0f;
        m_flashRemaining = 0.0f;
    }

    void Effects::update(float deltaSeconds)
    {
        m_elapsed += deltaSeconds;

        for (Particle& particle : m_particles)
        {
            particle.x += particle.vx * deltaSeconds;
            particle.y += particle.vy * deltaSeconds;
            particle.vy += 9.0f * deltaSeconds; // gentle gravity so bursts settle
            particle.vx *= 0.94f;
            particle.life -= deltaSeconds;
        }
        std::erase_if(m_particles, [](const Particle& p) { return p.life <= 0.0f; });

        for (RisingText& text : m_texts)
        {
            text.y -= 3.2f * deltaSeconds;
            text.life -= deltaSeconds;
        }
        std::erase_if(m_texts, [](const RisingText& t) { return t.life <= 0.0f; });

        m_shakeRemaining = std::max(0.0f, m_shakeRemaining - deltaSeconds);
        if (m_shakeRemaining <= 0.0f)
            m_shakeStrength = 0.0f;

        m_flashRemaining = std::max(0.0f, m_flashRemaining - deltaSeconds);
    }

    void Effects::burst(Vec2 tile, int count, Color colour, float speed)
    {
        for (int i = 0; i < count; ++i)
        {
            if (m_particles.size() >= kMaxParticles)
                return;

            const float angle = m_rng.unit() * 6.2831853f;
            const float magnitude = speed * (0.35f + m_rng.unit() * 0.65f);

            Particle particle;
            particle.x = static_cast<float>(tile.x) + 0.5f;
            particle.y = static_cast<float>(tile.y) + 0.5f;
            particle.vx = std::cos(angle) * magnitude;
            particle.vy = std::sin(angle) * magnitude * 0.6f;
            particle.maxLife = 0.35f + m_rng.unit() * 0.45f;
            particle.life = particle.maxLife;
            particle.glyph = kSparkGlyphs[m_rng.range(0, static_cast<int>(std::size(kSparkGlyphs)) - 1)];
            particle.colour = colour;

            m_particles.push_back(particle);
        }
    }

    void Effects::popText(Vec2 tile, std::wstring text, Color colour)
    {
        RisingText rising;
        rising.x = static_cast<float>(tile.x);
        rising.y = static_cast<float>(tile.y) - 0.5f;
        rising.maxLife = 1.1f;
        rising.life = rising.maxLife;
        rising.text = std::move(text);
        rising.colour = colour;
        m_texts.push_back(std::move(rising));
    }

    void Effects::addShake(float strength)
    {
        m_shakeStrength = std::max(m_shakeStrength, strength);
        m_shakeRemaining = std::max(m_shakeRemaining, 0.28f);
    }

    void Effects::flash(Color colour, float seconds)
    {
        m_flashColour = colour;
        m_flashDuration = std::max(0.01f, seconds);
        m_flashRemaining = m_flashDuration;
    }

    float Effects::flashFraction() const
    {
        return std::clamp(m_flashRemaining / m_flashDuration, 0.0f, 1.0f);
    }

    Vec2 Effects::shakeOffset() const
    {
        if (m_shakeRemaining <= 0.0f)
            return { 0, 0 };

        // Deterministic wobble rather than random per frame, which reads as a
        // shake instead of as noise.
        // In virtual pixels now that the board has its own pixel space; the old
        // value was in console cells and would be an imperceptible nudge here.
        const float amount = m_shakeStrength * (m_shakeRemaining / 0.28f) * 5.0f;
        const int x = static_cast<int>(std::lround(std::sin(m_elapsed * 62.0f) * amount));
        const int y = static_cast<int>(std::lround(std::sin(m_elapsed * 47.0f + 1.7f) * amount * 0.7f));
        return { x, y };
    }

    void Effects::render(Screen& screen, const BoardView& view) const
    {
        const float minX = view.originX;
        const float maxX = view.originX + kBoardWidth * view.tileSize;
        const float minY = view.originY;
        const float maxY = view.originY + kBoardHeight * view.tileSize;

        const auto inBoard = [&](float x, float y)
        {
            return x >= minX && x < maxX && y >= minY && y < maxY;
        };

        const float particleSize = view.tileSize * 0.45f;

        for (const Particle& particle : m_particles)
        {
            const float fade = particle.life / particle.maxLife;
            const wchar_t glyphToDraw = fade > 0.45f ? particle.glyph : glyph::Dot;

            const float x = view.centreX(particle.x) - particleSize * 0.5f;
            const float y = view.centreY(particle.y) - particleSize * 0.5f;
            if (!inBoard(x, y))
                continue;

            const Color colour = particle.colour.withAlpha(
                static_cast<std::uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));

            screen.drawGlyph(x, y, particleSize, particleSize, glyphToDraw, colour);
            screen.glow(x + particleSize * 0.5f, y + particleSize * 0.5f,
                particleSize * 0.9f, particle.colour, fade * 0.8f);
        }

        // Floating score text is drawn glyph by glyph in pixel space so it can
        // sit between tiles instead of snapping to the cell grid.
        const float charWidth = view.tileSize * 0.55f;

        for (const RisingText& text : m_texts)
        {
            const float fade = std::clamp(text.life / text.maxLife, 0.0f, 1.0f);
            const float startX = view.centreX(text.x) - charWidth * 0.5f * static_cast<float>(text.text.size());
            const float y = view.centreY(text.y) - charWidth * 0.5f;

            const Color colour = text.colour.withAlpha(
                static_cast<std::uint8_t>(std::min(1.0f, fade * 1.6f) * 255.0f));

            for (std::size_t i = 0; i < text.text.size(); ++i)
            {
                const float x = startX + static_cast<float>(i) * charWidth;
                if (!inBoard(x, y))
                    continue;

                screen.drawGlyph(x, y, charWidth, charWidth, text.text[i], colour);
            }
        }
    }
}
