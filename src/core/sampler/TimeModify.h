#pragma once

#include "../state/PadSettings.h"
#include <algorithm>

namespace spsx::timemodify
{
    /** Range rules (manual p. 30): half to about 1.3x the original BPM, and always
        inside 40..200 BPM. */
    constexpr double kMinFactor = 0.5;
    constexpr double kMaxFactor = 1.3;

    inline double minBpm (double originalBpm) { return std::max (40.0,  kMinFactor * originalBpm); }
    inline double maxBpm (double originalBpm) { return std::min (200.0, kMaxFactor * originalBpm); }

    /** Target BPM in effect for these settings, or the original BPM when off. */
    inline double targetBpm (const PadSettings& s, double patternTempo)
    {
        if (s.bpm <= 0.0)
            return s.bpm;
        switch (s.timeModify)
        {
            case TimeModifyMode::off:     return s.bpm;
            case TimeModifyMode::bpm:     return std::clamp (s.timeModifyBpm, minBpm (s.bpm), maxBpm (s.bpm));
            case TimeModifyMode::pattern: return std::clamp (patternTempo, minBpm (s.bpm), maxBpm (s.bpm));
        }
        return s.bpm;
    }

    /** Source-read speed (1 = original length). Time Modify is ignored while REVERSE is on. */
    inline double sourceSpeed (const PadSettings& s, double patternTempo)
    {
        if (s.reverse || s.timeModify == TimeModifyMode::off || s.bpm <= 0.0)
            return 1.0;
        return targetBpm (s, patternTempo) / s.bpm;
    }

    /** CTRL 1 (TIME) knob, 0..127: 0 = "oFF", 127 = "Ptn", 1..126 spread linearly over
        the allowed BPM range [assumed mapping], 0.1 BPM resolution. */
    inline void applyKnob (PadSettings& s, int knob)
    {
        if (knob <= 0)        { s.timeModify = TimeModifyMode::off; return; }
        if (knob >= 127)      { s.timeModify = TimeModifyMode::pattern; return; }
        const double lo = minBpm (s.bpm), hi = maxBpm (s.bpm);
        const double t = (knob - 1) / 125.0;
        s.timeModify = TimeModifyMode::bpm;
        s.timeModifyBpm = std::round ((lo + t * (hi - lo)) * 10.0) / 10.0;
    }
}
