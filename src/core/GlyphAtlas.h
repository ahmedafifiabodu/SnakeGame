#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <unordered_map>

namespace neoncoil
{
    // Rasterises every glyph the game uses into one texture at start-up:
    // the 5x5 block font for text, and analytically drawn shapes for the
    // blocks, shades, box-drawing runs and markers.
    //
    // Pixels are white with varying alpha, so a cell is one textured quad whose
    // vertex colour does the tinting. That keeps the whole UI at a single draw
    // call and means the game ships with no font file and no rasteriser.
    class GlyphAtlas
    {
    public:
        // Generous enough that glyphs stay crisp when the canvas is upscaled to
        // 4K, small enough that the whole atlas is one 1024x512 texture.
        static constexpr int kGlyphPixels = 48;
        static constexpr int kColumns = 16;

        GlyphAtlas();

        const sf::Texture& texture() const { return m_texture; }

        // Texture-space rect for a glyph. Unknown glyphs map to blank.
        sf::FloatRect rect(wchar_t glyph) const;

        bool has(wchar_t glyph) const { return m_slots.contains(glyph); }

    private:
        std::unordered_map<wchar_t, int> m_slots;
        sf::Texture m_texture;
        int m_blankSlot{ 0 };
    };
}
