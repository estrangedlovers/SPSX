#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace spsx::tempo
{
    /** Allowed pattern/sample BPM values (manual p. 15):
        40..60 even only, 61..159 every integer, 160..180 even only,
        above 180 only 183, 186, 190, 193, 196, 200. 127 values in total. */
    inline const std::array<int, 127>& table()
    {
        static const std::array<int, 127> t = []
        {
            std::array<int, 127> a {};
            size_t i = 0;
            for (int b = 40;  b <= 60;  b += 2) a[i++] = b;
            for (int b = 61;  b <= 159; ++b)    a[i++] = b;
            for (int b = 160; b <= 180; b += 2) a[i++] = b;
            for (int b : { 183, 186, 190, 193, 196, 200 }) a[i++] = b;
            return a;
        }();
        return t;
    }

    constexpr double kMin = 40.0, kMax = 200.0;

    /** Nearest allowed value. */
    inline int snap (double bpm)
    {
        const auto& t = table();
        int best = t[0];
        for (int v : t)
            if (std::abs (v - bpm) < std::abs (best - bpm))
                best = v;
        return best;
    }

    /** Index 0..126 <-> value: one knob detent per allowed value [assumed mapping]. */
    inline int fromIndex (int index) { return table()[(size_t) std::clamp (index, 0, 126)]; }

    inline int indexOf (int bpm)
    {
        const auto& t = table();
        const auto it = std::lower_bound (t.begin(), t.end(), bpm);
        return (int) std::min<std::ptrdiff_t> (it - t.begin(), 126);
    }
}
