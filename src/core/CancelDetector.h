#pragma once

#include <array>

namespace spsx
{
    /** "Press the [CANCEL] button four times in rapid succession" stops all sound
        (manual p. 12). The window is undocumented (SPEC U3): 4 presses within 1 s. */
    class CancelDetector
    {
    public:
        static constexpr double kWindowSeconds = 1.0;
        static constexpr int    kPresses = 4;

        /** Returns true on the press that completes the sequence. */
        bool press (double nowSeconds) noexcept
        {
            times[(size_t) (next++ % kPresses)] = nowSeconds;
            if (next < kPresses)
                return false;
            const double oldest = times[(size_t) (next % kPresses)];
            if (nowSeconds - oldest <= kWindowSeconds)
            {
                next = 0;
                return true;
            }
            return false;
        }

    private:
        std::array<double, kPresses> times {};
        int next = 0;
    };
}
