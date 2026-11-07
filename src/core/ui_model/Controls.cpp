#include "Controls.h"

namespace spsx
{
    const char* controlName (Control c) noexcept
    {
        static const char* names[] = {
            "FILTER+DRIVE", "VOICE TRANS", "DELAY", "ISOLATOR", "DJFX LOOPER", "MFX",
            "PATTERN SELECT", "LENGTH", "QUANTIZE",
            "DEL", "REC", "RESAMPLE", "CANCEL", "REMAIN", "MARK",
            "START/END/LEVEL", "TIME/BPM", "TAP TEMPO",
            "LO-FI", "STEREO", "GATE", "LOOP", "REVERSE",
            "A/F", "B/G", "C/H", "D/I", "E/J", "FUNC",
            "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
            "HOLD", "EXT SOURCE", "SUB PAD",
            "MIC"
        };
        static_assert (sizeof (names) / sizeof (names[0]) == kNumControls);
        return names[(int) c];
    }

    const char* knobName (Knob k) noexcept
    {
        static const char* names[] = { "CTRL 1", "CTRL 2", "CTRL 3", "VOLUME", "MIC LEVEL" };
        static_assert (sizeof (names) / sizeof (names[0]) == kNumKnobs);
        return names[(int) k];
    }
}
