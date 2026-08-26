#pragma once

#include <cstdint>

namespace neoncoil
{
    // Real RGBA. This replaced a 16-slot console attribute enum when the game
    // moved to SFML; the named constants kept their spelling on purpose so the
    // thousands of existing `Color::Gold` uses did not have to change.
    struct Color
    {
        std::uint8_t r{ 0 };
        std::uint8_t g{ 0 };
        std::uint8_t b{ 0 };
        std::uint8_t a{ 255 };

        constexpr Color() = default;
        constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
            : r(red), g(green), b(blue), a(alpha) {}

        constexpr Color withAlpha(std::uint8_t alpha) const { return { r, g, b, alpha }; }

        // Multiplies brightness, keeping alpha. Used for dimmed tails, inactive
        // UI and the darker half of bevels.
        constexpr Color scaled(float factor) const
        {
            return {
                clampByte(static_cast<float>(r) * factor),
                clampByte(static_cast<float>(g) * factor),
                clampByte(static_cast<float>(b) * factor),
                a
            };
        }

        constexpr Color lerpTo(const Color& other, float t) const
        {
            return {
                clampByte(static_cast<float>(r) + (static_cast<float>(other.r) - static_cast<float>(r)) * t),
                clampByte(static_cast<float>(g) + (static_cast<float>(other.g) - static_cast<float>(g)) * t),
                clampByte(static_cast<float>(b) + (static_cast<float>(other.b) - static_cast<float>(b)) * t),
                clampByte(static_cast<float>(a) + (static_cast<float>(other.a) - static_cast<float>(a)) * t)
            };
        }

        constexpr bool operator==(const Color& rhs) const
        {
            return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
        }
        constexpr bool operator!=(const Color& rhs) const { return !(*this == rhs); }

        // Palette. Declared here, defined below the class.
        static const Color Transparent;
        static const Color Black;
        static const Color Navy;    // board floor
        static const Color Green;
        static const Color Cyan;
        static const Color Red;
        static const Color Magenta;
        static const Color Amber;
        static const Color Silver;
        static const Color Slate;   // walls and dim UI furniture
        static const Color Blue;
        static const Color Lime;
        static const Color Aqua;
        static const Color Coral;
        static const Color Pink;
        static const Color Gold;
        static const Color White;

    private:
        static constexpr std::uint8_t clampByte(float value)
        {
            return value <= 0.0f ? std::uint8_t{ 0 }
                 : value >= 255.0f ? std::uint8_t{ 255 }
                 : static_cast<std::uint8_t>(value + 0.5f);
        }
    };

    inline constexpr Color Color::Transparent{ 0x00, 0x00, 0x00, 0x00 };
    inline constexpr Color Color::Black      { 0x0d, 0x0f, 0x14 };
    inline constexpr Color Color::Navy       { 0x14, 0x1c, 0x2b };
    inline constexpr Color Color::Green      { 0x3d, 0xdc, 0x84 };
    inline constexpr Color Color::Cyan       { 0x45, 0xc8, 0xe0 };
    inline constexpr Color Color::Red        { 0xff, 0x55, 0x55 };
    inline constexpr Color Color::Magenta    { 0xd9, 0x72, 0xff };
    inline constexpr Color Color::Amber      { 0xff, 0xb4, 0x54 };
    inline constexpr Color Color::Silver     { 0x9a, 0xa5, 0xb1 };
    inline constexpr Color Color::Slate      { 0x3a, 0x47, 0x61 };
    inline constexpr Color Color::Blue       { 0x5b, 0x8c, 0xff };
    inline constexpr Color Color::Lime       { 0xa6, 0xff, 0x6a };
    inline constexpr Color Color::Aqua       { 0x7f, 0xf0, 0xd8 };
    inline constexpr Color Color::Coral      { 0xff, 0x7a, 0x5c };
    inline constexpr Color Color::Pink       { 0xff, 0x8f, 0xd0 };
    inline constexpr Color Color::Gold       { 0xff, 0xe0, 0x66 };
    inline constexpr Color Color::White      { 0xf2, 0xf5, 0xf7 };
}
