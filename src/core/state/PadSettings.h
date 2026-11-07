#pragma once

#include "../Types.h"

namespace spsx
{
    enum class TimeModifyMode : std::uint8_t
    {
        off,       // display "oFF": original length (manual p. 30)
        bpm,       // fixed target BPM, pitch preserved
        pattern    // display "Ptn": follow pattern tempo
    };

    /** Per-sample settings stored with the sample (manual p. 12, 27-30).
        Plain value type: copied through the UI->audio FIFO. */
    struct PadSettings
    {
        bool gate    = false;   // GATE lit: plays only while held
        bool loop    = false;   // LOOP lit
        bool reverse = false;   // REVERSE lit
        bool lofi    = false;   // LO-FI lit
        int  level   = 127;     // 0..127, default 127 after sampling (p. 27)

        SampleIndex start = 0;  // playback region, end exclusive
        SampleIndex end   = 0;

        double bpm = 0.0;       // sample BPM, auto-derived from region length (p. 30)

        TimeModifyMode timeModify = TimeModifyMode::off;
        double timeModifyBpm      = 0.0;  // target BPM when timeModify == bpm

        /** MARK lights when the region differs from the whole sample (p. 29). */
        bool isMarked (SampleIndex sampleLength) const noexcept { return start > 0 || end < sampleLength; }

        friend bool operator== (const PadSettings&, const PadSettings&) = default;
    };
}
