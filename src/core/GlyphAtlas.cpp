#include "GlyphAtlas.h"

#include "BlockFont.h"
#include "Glyphs.h"

#include <SFML/Graphics/Image.hpp>

#include <cmath>
#include <functional>
#include <vector>

namespace neoncoil
{
    namespace
    {
        // Coverage is supersampled so circles, diamonds and triangles come out
        // smooth instead of stair-stepped.
        constexpr int kSubSamples = 4;

        using Shape = std::function<float(float, float)>;

        float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        // --- shape predicates, all in unit space with the origin at the centre --

        Shape solid(float alpha)
        {
            return [alpha](float, float) { return alpha; };
        }

        Shape halfPlane(float dx, float dy)
        {
            // Keeps the half of the cell the given direction points at.
            return [dx, dy](float x, float y)
            {
                return (x * dx + y * dy) >= 0.0f ? 1.0f : 0.0f;
            };
        }

        Shape disc(float radius)
        {
            return [radius](float x, float y)
            {
                return std::sqrt(x * x + y * y) <= radius ? 1.0f : 0.0f;
            };
        }

        Shape ring(float inner, float outer)
        {
            return [inner, outer](float x, float y)
            {
                const float d = std::sqrt(x * x + y * y);
                return (d >= inner && d <= outer) ? 1.0f : 0.0f;
            };
        }

        Shape diamond(float radius)
        {
            return [radius](float x, float y)
            {
                return (std::abs(x) + std::abs(y)) <= radius ? 1.0f : 0.0f;
            };
        }

        Shape diamondRing(float inner, float outer)
        {
            return [inner, outer](float x, float y)
            {
                const float d = std::abs(x) + std::abs(y);
                return (d >= inner && d <= outer) ? 1.0f : 0.0f;
            };
        }

        Shape box(float halfWidth, float halfHeight)
        {
            return [halfWidth, halfHeight](float x, float y)
            {
                return (std::abs(x) <= halfWidth && std::abs(y) <= halfHeight) ? 1.0f : 0.0f;
            };
        }

        // Four-pointed star: two tapering spikes plus a small core.
        Shape star(float reach, float thickness)
        {
            return [reach, thickness](float x, float y)
            {
                const float ax = std::abs(x);
                const float ay = std::abs(y);

                const bool vertical = ay <= reach && ax <= thickness * (1.0f - ay / reach);
                const bool horizontal = ax <= reach && ay <= thickness * (1.0f - ax / reach);
                const bool core = std::sqrt(x * x + y * y) <= thickness * 0.9f;

                return (vertical || horizontal || core) ? 1.0f : 0.0f;
            };
        }

        Shape saltire(float reach, float thickness)
        {
            return [reach, thickness](float x, float y)
            {
                if (std::sqrt(x * x + y * y) > reach)
                    return 0.0f;
                const bool a = std::abs(x - y) <= thickness;
                const bool b = std::abs(x + y) <= thickness;
                return (a || b) ? 1.0f : 0.0f;
            };
        }

        // Triangle pointing along (dx, dy); one of the two is always zero.
        Shape triangle(float dx, float dy)
        {
            return [dx, dy](float x, float y)
            {
                const float along = x * dx + y * dy;      // -0.5 (base) .. 0.5 (tip)
                const float across = x * dy + y * dx;     // perpendicular
                if (along < -0.34f || along > 0.40f)
                    return 0.0f;
                const float halfSpan = (0.40f - along) * 0.62f;
                return std::abs(across) <= halfSpan ? 1.0f : 0.0f;
            };
        }

        // Horizontal rule. `doubled` draws the two thin lines of a double-line
        // box character. `extend` limits it to one side so corners join up.
        Shape rule(bool horizontal, bool doubled, float extendDx, float extendDy)
        {
            return [horizontal, doubled, extendDx, extendDy](float x, float y)
            {
                const float across = horizontal ? y : x;
                const float along = horizontal ? x : y;

                if (extendDx != 0.0f || extendDy != 0.0f)
                {
                    const float direction = horizontal ? extendDx : extendDy;
                    if (along * direction < -0.06f)
                        return 0.0f;
                }

                if (doubled)
                {
                    const bool near = std::abs(across + 0.10f) <= 0.045f;
                    const bool far = std::abs(across - 0.10f) <= 0.045f;
                    return (near || far) ? 1.0f : 0.0f;
                }
                return std::abs(across) <= 0.055f ? 1.0f : 0.0f;
            };
        }

        Shape combine(Shape a, Shape b)
        {
            return [a = std::move(a), b = std::move(b)](float x, float y)
            {
                return std::max(a(x, y), b(x, y));
            };
        }

        Shape corner(bool doubled, float horizontalDirection, float verticalDirection)
        {
            return combine(rule(true, doubled, horizontalDirection, 0.0f),
                           rule(false, doubled, 0.0f, verticalDirection));
        }

        struct ShapeEntry
        {
            wchar_t glyph;
            Shape shape;
        };

        std::vector<ShapeEntry> shapeGlyphs()
        {
            using namespace glyph;

            std::vector<ShapeEntry> entries;
            entries.push_back({ Block,        solid(1.0f) });
            entries.push_back({ ShadeDark,    solid(0.78f) });
            entries.push_back({ ShadeMedium,  solid(0.52f) });
            entries.push_back({ ShadeLight,   solid(0.26f) });

            entries.push_back({ HalfLeft,     halfPlane(-1.0f, 0.0f) });
            entries.push_back({ HalfRight,    halfPlane(1.0f, 0.0f) });
            entries.push_back({ HalfUpper,    halfPlane(0.0f, -1.0f) });
            entries.push_back({ HalfLower,    halfPlane(0.0f, 1.0f) });

            entries.push_back({ Circle,       disc(0.44f) });
            entries.push_back({ CircleSmall,  ring(0.30f, 0.44f) });
            entries.push_back({ Diamond,      diamond(0.46f) });
            entries.push_back({ DiamondSmall, diamondRing(0.30f, 0.46f) });
            entries.push_back({ Square,       box(0.36f, 0.36f) });
            entries.push_back({ SquareSmall,  box(0.20f, 0.20f) });
            entries.push_back({ Bullet,       disc(0.18f) });
            entries.push_back({ Dot,          disc(0.11f) });
            entries.push_back({ Star,         star(0.48f, 0.15f) });
            entries.push_back({ Sparkle,      star(0.36f, 0.11f) });
            entries.push_back({ Cross,        saltire(0.42f, 0.10f) });
            entries.push_back({ Bolt,         star(0.44f, 0.10f) });
            entries.push_back({ Shield,       ring(0.26f, 0.44f) });
            entries.push_back({ Wave,         rule(true, true, 0.0f, 0.0f) });
            entries.push_back({ Skull,        ring(0.24f, 0.42f) });

            entries.push_back({ TriRight,     triangle(1.0f, 0.0f) });
            entries.push_back({ TriLeft,      triangle(-1.0f, 0.0f) });
            entries.push_back({ ArrowRight,   triangle(1.0f, 0.0f) });
            entries.push_back({ ArrowLeft,    triangle(-1.0f, 0.0f) });
            entries.push_back({ ArrowDown,    triangle(0.0f, 1.0f) });
            entries.push_back({ ArrowUp,      triangle(0.0f, -1.0f) });

            entries.push_back({ BoxH,             rule(true, true, 0.0f, 0.0f) });
            entries.push_back({ BoxV,             rule(false, true, 0.0f, 0.0f) });
            entries.push_back({ BoxTopLeft,       corner(true, 1.0f, 1.0f) });
            entries.push_back({ BoxTopRight,      corner(true, -1.0f, 1.0f) });
            entries.push_back({ BoxBottomLeft,    corner(true, 1.0f, -1.0f) });
            entries.push_back({ BoxBottomRight,   corner(true, -1.0f, -1.0f) });

            entries.push_back({ ThinH,            rule(true, false, 0.0f, 0.0f) });
            entries.push_back({ ThinV,            rule(false, false, 0.0f, 0.0f) });
            entries.push_back({ ThinTopLeft,      corner(false, 1.0f, 1.0f) });
            entries.push_back({ ThinTopRight,     corner(false, -1.0f, 1.0f) });
            entries.push_back({ ThinBottomLeft,   corner(false, 1.0f, -1.0f) });
            entries.push_back({ ThinBottomRight,  corner(false, -1.0f, -1.0f) });

            return entries;
        }

        void paintShape(sf::Image& image, int originX, int originY, const Shape& shape)
        {
            constexpr int size = GlyphAtlas::kGlyphPixels;
            constexpr float inv = 1.0f / static_cast<float>(kSubSamples);

            for (int py = 0; py < size; ++py)
            {
                for (int px = 0; px < size; ++px)
                {
                    float total = 0.0f;

                    for (int sy = 0; sy < kSubSamples; ++sy)
                    {
                        for (int sx = 0; sx < kSubSamples; ++sx)
                        {
                            const float u = (static_cast<float>(px) + (static_cast<float>(sx) + 0.5f) * inv)
                                / static_cast<float>(size) - 0.5f;
                            const float v = (static_cast<float>(py) + (static_cast<float>(sy) + 0.5f) * inv)
                                / static_cast<float>(size) - 0.5f;
                            total += shape(u, v);
                        }
                    }

                    const float coverage = clamp01(total / static_cast<float>(kSubSamples * kSubSamples));
                    if (coverage <= 0.0f)
                        continue;

                    const auto alpha = static_cast<std::uint8_t>(coverage * 255.0f + 0.5f);
                    image.setPixel({ static_cast<unsigned>(originX + px), static_cast<unsigned>(originY + py) },
                        sf::Color(255, 255, 255, alpha));
                }
            }
        }

        // Text glyphs keep square font pixels and sit centred in the cell, so
        // captions stay crisp no matter what the cell aspect ratio is.
        void paintText(sf::Image& image, int originX, int originY, wchar_t character)
        {
            constexpr int size = GlyphAtlas::kGlyphPixels;
            const int scale = size / (font::kWidth + 1);          // 1px of padding
            const int glyphW = font::kWidth * scale;
            const int glyphH = font::kHeight * scale;
            const int offsetX = (size - glyphW) / 2;
            const int offsetY = (size - glyphH) / 2;

            const font::Rows& rows = font::rowsFor(character);

            for (int row = 0; row < font::kHeight; ++row)
            {
                const char* line = rows[static_cast<std::size_t>(row)];
                for (int column = 0; column < font::kWidth; ++column)
                {
                    if (line[column] != '#')
                        continue;

                    for (int py = 0; py < scale; ++py)
                    {
                        for (int px = 0; px < scale; ++px)
                        {
                            image.setPixel({
                                static_cast<unsigned>(originX + offsetX + column * scale + px),
                                static_cast<unsigned>(originY + offsetY + row * scale + py) },
                                sf::Color::White);
                        }
                    }
                }
            }
        }
    }

    GlyphAtlas::GlyphAtlas()
    {
        std::vector<wchar_t> textGlyphs;
        for (wchar_t c = 32; c < 127; ++c)
            if (font::isSupported(c))
                textGlyphs.push_back(c);

        const std::vector<ShapeEntry> shapes = shapeGlyphs();

        const int total = static_cast<int>(textGlyphs.size() + shapes.size()) + 1; // +1 blank
        const int rows = (total + kColumns - 1) / kColumns;

        sf::Image image(
            sf::Vector2u{ static_cast<unsigned>(kColumns * kGlyphPixels),
                          static_cast<unsigned>(rows * kGlyphPixels) },
            sf::Color::Transparent);

        int slot = 0;
        const auto originOf = [](int index)
        {
            return sf::Vector2i{ (index % kColumns) * kGlyphPixels, (index / kColumns) * kGlyphPixels };
        };

        // Slot 0 is deliberately blank and is what unknown glyphs resolve to.
        m_blankSlot = slot++;

        for (wchar_t c : textGlyphs)
        {
            const sf::Vector2i origin = originOf(slot);
            if (c != L' ')
                paintText(image, origin.x, origin.y, c);
            m_slots[c] = slot++;
        }

        for (const ShapeEntry& entry : shapes)
        {
            const sf::Vector2i origin = originOf(slot);
            paintShape(image, origin.x, origin.y, entry.shape);
            m_slots[entry.glyph] = slot++;
        }

        if (!m_texture.loadFromImage(image))
            return;

        m_texture.setSmooth(true);
    }

    sf::FloatRect GlyphAtlas::rect(wchar_t glyph) const
    {
        int slot = m_blankSlot;
        if (auto it = m_slots.find(glyph); it != m_slots.end())
            slot = it->second;

        const float x = static_cast<float>((slot % kColumns) * kGlyphPixels);
        const float y = static_cast<float>((slot / kColumns) * kGlyphPixels);
        return sf::FloatRect({ x, y }, { static_cast<float>(kGlyphPixels), static_cast<float>(kGlyphPixels) });
    }
}
