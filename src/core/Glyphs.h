#pragma once

namespace neoncoil::glyph
{
    // Written as hex escapes so the source stays plain ASCII and cannot be
    // corrupted by an editor saving in the wrong encoding.
    inline constexpr wchar_t Space = L' ';
    inline constexpr wchar_t Block = L'\x2588'; // full block
    inline constexpr wchar_t ShadeDark = L'\x2593';
    inline constexpr wchar_t ShadeMedium = L'\x2592';
    inline constexpr wchar_t ShadeLight = L'\x2591';
    inline constexpr wchar_t HalfLeft = L'\x258C';
    inline constexpr wchar_t HalfRight = L'\x2590';
    inline constexpr wchar_t HalfLower = L'\x2584';
    inline constexpr wchar_t HalfUpper = L'\x2580';

    inline constexpr wchar_t Circle = L'\x25CF';
    inline constexpr wchar_t CircleSmall = L'\x25CB';
    inline constexpr wchar_t Diamond = L'\x25C6';
    inline constexpr wchar_t DiamondSmall = L'\x25C7';
    inline constexpr wchar_t Square = L'\x25A0';
    inline constexpr wchar_t SquareSmall = L'\x25AA';
    inline constexpr wchar_t Star = L'\x2726';
    inline constexpr wchar_t Sparkle = L'\x2727';
    inline constexpr wchar_t Bullet = L'\x2022';
    inline constexpr wchar_t Dot = L'\x00B7';
    inline constexpr wchar_t Cross = L'\x2716';
    inline constexpr wchar_t Skull = L'\x2620';
    inline constexpr wchar_t Bolt = L'\x21AF';
    inline constexpr wchar_t Shield = L'\x2756';
    inline constexpr wchar_t Wave = L'\x2248';

    inline constexpr wchar_t ArrowUp = L'\x2191';
    inline constexpr wchar_t ArrowDown = L'\x2193';
    inline constexpr wchar_t ArrowLeft = L'\x2190';
    inline constexpr wchar_t ArrowRight = L'\x2192';
    inline constexpr wchar_t TriRight = L'\x25B6';
    inline constexpr wchar_t TriLeft = L'\x25C0';

    // Double-line box drawing, used for panels and framed screens.
    inline constexpr wchar_t BoxH = L'\x2550';
    inline constexpr wchar_t BoxV = L'\x2551';
    inline constexpr wchar_t BoxTopLeft = L'\x2554';
    inline constexpr wchar_t BoxTopRight = L'\x2557';
    inline constexpr wchar_t BoxBottomLeft = L'\x255A';
    inline constexpr wchar_t BoxBottomRight = L'\x255D';

    // Single-line box drawing, used for lighter internal dividers.
    inline constexpr wchar_t ThinH = L'\x2500';
    inline constexpr wchar_t ThinV = L'\x2502';
    inline constexpr wchar_t ThinTopLeft = L'\x250C';
    inline constexpr wchar_t ThinTopRight = L'\x2510';
    inline constexpr wchar_t ThinBottomLeft = L'\x2514';
    inline constexpr wchar_t ThinBottomRight = L'\x2518';
}
