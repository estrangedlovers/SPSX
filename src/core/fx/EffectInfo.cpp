#include "EffectInfo.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace spsx::fx
{
    namespace
    {
        using K = ParamKind;
        constexpr bool S = true;    // star: Effect Limit
        constexpr bool N = false;

        const std::array<EffectInfo, kNumEffects> kTable {{
            // Direct buttons
            { "FILTER+DRIVE", {{ { "CoF", K::value, N }, { "rES", K::value, S }, { "drU", K::value, S } }}, false, false, { 0.8f, 0.3f, 0.2f, 0.5f } },
            { "VOICE TRANS",  {{ { "FrM", K::value, N }, { "EFF", K::value, N }, { "dir", K::value, N } }}, false, false, { 0.5f, 0.8f, 0.0f, 0.5f } },
            { "DELAY",        {{ { "tiM", K::noteValue, N }, { "Fdb", K::value, S }, { "bAL", K::value, N } }}, false, false, { 0.55f, 0.4f, 0.45f, 0.5f } },
            { "ISOLATOR",     {{ { "Lo",  K::value, S }, { "Mid", K::value, S }, { "Hi", K::value, S } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "DJFX LOOPER",  {{ { "LEn", K::value, S }, { "SPd", K::speed, N }, { "SW",  K::onOff, N } }}, false, false, { 0.4f, 1.0f, 0.0f, 0.5f } },
            // MFX 1..24
            { "REVERB",       {{ { "tiM", K::value, N }, { "ton", K::value, N }, { "bAL", K::value, N } }}, false, false, { 0.5f, 0.6f, 0.4f, 0.5f } },
            { "TAPE ECHO",    {{ { "rAt", K::value, N }, { "int", K::value, S }, { "bAL", K::value, N } }}, false, false, { 0.5f, 0.45f, 0.45f, 0.5f } },
            { "CHORUS",       {{ { "dPt", K::value, N }, { "rAt", K::value, N }, { "bAL", K::value, N } }}, false, false, { 0.5f, 0.3f, 0.5f, 0.5f } },
            { "FLANGER",      {{ { "dPt", K::value, N }, { "rAt", K::value, N }, { "Fdb", K::value, S } }}, true,  false, { 0.6f, 0.25f, 0.5f, 0.5f } },
            { "PHASER",       {{ { "dPt", K::value, S }, { "rAt", K::value, N }, { "MAn", K::value, N } }}, true,  false, { 0.6f, 0.3f, 0.4f, 0.5f } },
            { "TREMOLO/PAN",  {{ { "dPt", K::value, N }, { "rAt", K::value, N }, { "trM", K::value, N } }}, false, false, { 0.7f, 0.5f, 0.3f, 0.5f } },
            { "DISTORTION",   {{ { "drU", K::value, S }, { "ton", K::value, S }, { "LEU", K::value, S } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "OVERDRIVE",    {{ { "drU", K::value, S }, { "ton", K::value, S }, { "LEU", K::value, S } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "FUZZ",         {{ { "drU", K::value, S }, { "ton", K::value, S }, { "LEU", K::value, S } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "WAH",          {{ { "PEt", K::value, N }, { "rAt", K::value, N }, { "MAn", K::value, N } }}, false, false, { 0.5f, 0.4f, 0.4f, 0.5f } },
            { "OCTAVE",       {{ { "oC2", K::value, N }, { "oC1", K::value, N }, { "dir", K::value, N } }}, false, false, { 0.0f, 0.7f, 0.7f, 0.5f } },
            { "COMP",         {{ { "SUS", K::value, N }, { "Att", K::value, S }, { "LEU", K::value, S } }}, false, false, { 0.5f, 0.3f, 0.5f, 0.5f } },
            { "EQUALIZER",    {{ { "Lo",  K::value, S }, { "Mid", K::value, S }, { "Hi", K::value, S } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "BIT CRASH",    {{ { "rAt", K::value, N }, { "bit", K::value, S }, { "FLt", K::value, N } }}, false, false, { 0.5f, 0.5f, 0.3f, 0.5f } },
            { "NOISE GEN",    {{ { "UHt", K::value, N }, { "diS", K::value, N }, { "HUM", K::value, N } }}, false, true,  { 0.2f, 0.3f, 0.1f, 0.5f } },
            { "VINYL SIM",    {{ { "rnG", K::value, N }, { "noS", K::value, N }, { "FLU", K::value, N } }}, false, true,  { 0.4f, 0.4f, 0.3f, 0.5f } },
            { "RADIO TUNING", {{ { "tUn", K::value, N }, { "noS", K::value, N }, { "bAL", K::value, N } }}, false, true,  { 0.3f, 0.3f, 1.0f, 0.5f } },
            { "SLICER+FLG",   {{ { "PtN", K::slicerPattern, N }, { "rAt", K::noteValue, N }, { "Fdb", K::value, S } }}, false, false, { 0.0f, 0.6f, 0.2f, 0.5f } },
            { "RING MOD",     {{ { "Frq", K::value, S }, { "SEn", K::value, S }, { "bAL", K::value, N } }}, false, false, { 0.4f, 0.2f, 0.7f, 0.5f } },
            { "CHROMATIC PS", {{ { "P1",  K::semitone, N }, { "P2", K::semitone, N }, { "bAL", K::value, N } }}, false, false, { 0.75f, 0.646f, 0.6f, 0.5f } },
            { "PITCH",        {{ { "Pit", K::pitch, N }, { "Fdb", K::value, S }, { "bAL", K::value, N } }}, false, false, { 0.75f, 0.2f, 0.6f, 0.5f } },
            { "C.CANCELER",   {{ { "L-r", K::value, N }, { "Lo", K::value, N }, { "Hi", K::value, N } }}, false, false, { 0.5f, 0.3f, 0.3f, 0.5f } },
            { "SUBSONIC",     {{ { "Pit", K::value, N }, { "tHr", K::value, N }, { "bAL", K::value, N } }}, false, false, { 0.5f, 0.5f, 0.5f, 0.5f } },
            { "BPM LOOPER",   {{ { "LEn", K::noteValue, S }, { "tiM", K::timing8, N }, { "SW", K::onOff, N } }}, false, false, { 0.4f, 0.0f, 0.0f, 0.5f } },
        }};

        const std::array<double, 13> kBeats { 0.125, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0 };
        const std::array<const char*, 13> kCodes { "t32", "t16", "t8t", "t16.", "t8", "t4t", "t8.", "t4", "t2t", "t4.", "t2", "t2.", "t1" };

        const std::array<const char*, 16> kSlicer {
            "X===....X===....X===....X===....",
            "X=====..X=====..X=====..X=====..",
            "X=......X=......X=......X=......",
            "X=====..X=..X=..X=====..X=..X=..",
            "X=..X=..X=......X=..X=..X=......",
            "X=..X=..X=..X=..X=..X=..X=......",
            "X===....X=..X=..X===....X=..X=..",
            "X===......X===..X===......X===..",
            "X===....X=....X=====....X=......",
            "X===..X=..X=....X===......X=....",
            "..X===......X=....X===......X=..",
            "....X===..X=..X=....X===......X=",
            "X=====..X=..X=..X=..X=====..X=..",
            "X=..X=..X=..X=..X=====..X=..X=..",
            "X===....X=..X=..X=..X=..X=......",
            "X=====..X=====..X=..X=====..X=..",
        };

        int pick (float knob, int count) { return std::clamp ((int) std::lround (knob * (float) (count - 1)), 0, count - 1); }
    }

    const EffectInfo& info (int effect) { return kTable[(size_t) std::clamp (effect, 0, kNumEffects - 1)]; }
    const std::array<double, 13>& noteBeats() { return kBeats; }
    const std::array<const char*, 13>& noteCodes() { return kCodes; }
    const std::array<const char*, 16>& slicerPatterns() { return kSlicer; }

    int noteIndex (float knob, double tempo, double maxSeconds)
    {
        int count = 13;
        if (maxSeconds > 0.0)
        {
            const double beatSeconds = 60.0 / std::max (tempo, 1.0);
            count = 0;
            while (count < 13 && kBeats[(size_t) count] * beatSeconds <= maxSeconds * 1.001)
                ++count;
            count = std::max (count, 1);
        }
        return pick (knob, count);
    }

    int slicerPattern (float knob) { return pick (knob, 16); }

    float djfxSpeed (float knob)
    {
        // 12 o'clock = stopped, with a small dead band so the centre is easy to find.
        const float s = (knob - 0.5f) * 2.0f;
        if (std::abs (s) < 0.04f) return 0.0f;
        return std::clamp ((s - std::copysign (0.04f, s)) / 0.96f, -1.0f, 1.0f);
    }

    bool onOff (float knob) { return knob >= 0.5f; }
    int timing8 (float knob) { return pick (knob, 9); }
    int semitones (float knob) { return pick (knob, 25) - 12; }
    float pitchSemitones (float knob) { return (knob - 0.5f) * 24.0f; }

    DisplayText display (int effect, int param, float knob, double tempo)
    {
        const auto& p = info (effect).params[(size_t) std::clamp (param, 0, 2)];
        switch (p.kind)
        {
            case ParamKind::value:         return DisplayText::number ((int) std::lround (knob * 127.0f));
            case ParamKind::noteValue:     return DisplayText::make (kCodes[(size_t) noteIndex (knob, tempo, effect == 2 ? 2.0 : 0.0)]);
            case ParamKind::slicerPattern:
            {
                const int n = slicerPattern (knob) + 1;
                return DisplayText::make (std::string ("P") + (n < 10 ? "0" : "") + std::to_string (n));
            }
            case ParamKind::speed:
            {
                char buf[16];
                std::snprintf (buf, sizeof buf, "%.1f", (double) djfxSpeed (knob));
                return DisplayText::make (buf);
            }
            case ParamKind::onOff:         return DisplayText::make (onOff (knob) ? "on" : "oFF");
            case ParamKind::timing8:       { const int t = timing8 (knob); return t == 0 ? DisplayText::make ("oFF") : DisplayText::number (t); }
            case ParamKind::semitone:      return DisplayText::number (semitones (knob));
            case ParamKind::pitch:         return DisplayText::number ((int) std::lround (pitchSemitones (knob)));
        }
        return {};
    }
}
