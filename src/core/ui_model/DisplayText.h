#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace spsx
{
    /** Segment bits for the 3-character 7-segment display (SPEC section 11).
        Bit 0..6 = segments a..g (a top, b top-right, c bottom-right, d bottom,
        e bottom-left, f top-left, g middle). */
    namespace seg
    {
        constexpr std::uint8_t a = 1, b = 2, c = 4, d = 8, e = 16, f = 32, g = 64;
        /** Returns segments for a character, or kUnknown if the display cannot show it.
            Glyphs follow the manual's font; note 'M' is drawn as an upside-down U
            (the manual's text extraction shows it as 'N'). */
        constexpr std::uint8_t kUnknown = 0x80;
        std::uint8_t forChar (char ch) noexcept;
    }

    /** What the display shows: three cells, each a character plus its decimal point. */
    struct DisplayText
    {
        std::array<char, 3> cells { ' ', ' ', ' ' };
        std::array<bool, 3> dots { false, false, false };

        /** Builds a display from text where '.' attaches to the previous character:
            "S.16", "98.5", "L.on", "-1.0". Numbers are right-aligned, words left-aligned,
            as on the hardware. Text that does not fit is truncated. */
        static DisplayText make (std::string_view text);
        static DisplayText number (int value) { return make (std::to_string (value)); }

        std::string toString() const;   // inverse of make() (left-aligned, spaces kept)

        friend bool operator== (const DisplayText&, const DisplayText&) = default;
    };
}
