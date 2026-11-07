#pragma once

#include <cstdint>

namespace spsx
{
    /** The hardware runs at 44.1 kHz (manual p. 49). The engine runs at this rate
        unless the Extended "native rate" option is on. */
    constexpr double kEngineRate = 44100.0;

    constexpr int kPadsPerBank    = 12;
    constexpr int kAuthenticBanks = 10;   // A..J
    constexpr int kMaxBanks       = 26;   // Extended "extra banks" ceiling (A..Z)
    constexpr int kMaxPads        = kMaxBanks * kPadsPerBank;

    using SampleIndex = std::int64_t;

    struct PadId
    {
        int bank = 0;   // 0 = A
        int pad  = 0;   // 0 = pad [1]

        constexpr int  index() const noexcept { return bank * kPadsPerBank + pad; }
        constexpr bool isValid() const noexcept { return bank >= 0 && bank < kMaxBanks && pad >= 0 && pad < kPadsPerBank; }
        static constexpr PadId fromIndex (int i) noexcept { return { i / kPadsPerBank, i % kPadsPerBank }; }

        friend constexpr bool operator== (PadId a, PadId b) noexcept { return a.bank == b.bank && a.pad == b.pad; }
        friend constexpr bool operator!= (PadId a, PadId b) noexcept { return ! (a == b); }
    };

    constexpr PadId kNoPad { -1, -1 };
}
