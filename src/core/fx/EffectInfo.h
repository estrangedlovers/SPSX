#pragma once

#include "Effect.h"
#include "../ui_model/DisplayText.h"

namespace spsx::fx
{
    /** How a parameter is shown on the 3-character display and quantised. */
    enum class ParamKind
    {
        value,          // 0..127
        noteValue,      // t32 .. t1, tempo-synced (*1); DELAY limits to ~2 s
        slicerPattern,  // P01 .. P16
        speed,          // -1.0 .. 1.0 (DJFX LOOPER)
        onOff,          // oFF / on
        timing8,        // oFF, 1..8 eighth notes (BPM LOOPER)
        semitone,       // -12 .. 12 in semitone steps (CHROMATIC PS)
        pitch           // -12 .. 12 continuous (PITCH)
    };

    struct ParamInfo
    {
        const char* code;      // display code (SPEC 7.3/7.4)
        ParamKind kind;
        bool limited;          // marked with the star: affected by Effect Limit
    };

    struct EffectInfo
    {
        const char* name;
        std::array<ParamInfo, 3> params;
        bool hiddenBalance;    // MFX + CTRL 1 balance (FLANGER, PHASER)
        bool generatesNoise;   // produces sound from silence (noise generators)
        std::array<float, 4> defaults;
    };

    const EffectInfo& info (int effect);

    /** 13 note values of footnote *1, in order, as beats (quarter = 1). */
    const std::array<double, 13>& noteBeats();
    const std::array<const char*, 13>& noteCodes();

    /** Note value for a knob position. DELAY (maxSeconds > 0) can only reach values
        whose time is within about 2 s at the current tempo (p. 44). */
    int noteIndex (float knob, double tempo, double maxSeconds = 0.0);

    int slicerPattern (float knob);          // 0..15
    float djfxSpeed (float knob);            // -1..1 with a dead band at 12 o'clock
    bool onOff (float knob);
    int timing8 (float knob);                // 0 = off, 1..8
    int semitones (float knob);              // -12..12
    float pitchSemitones (float knob);       // -12..12 continuous

    /** What the display shows for a parameter at this knob position. */
    DisplayText display (int effect, int param, float knob, double tempo);

    /** Slicer timing patterns P01..P16 (manual p. 44), 32 steps per bar:
        'X' onset, '=' sustain, '.' muted. Transcribed in tools/slicer_patterns.py. */
    const std::array<const char*, 16>& slicerPatterns();
}
