#pragma once

namespace spsx
{
    /** What the MIDI input needs from the device settings and the mode (SPEC 13).
        Set on the message thread, carried to the audio thread as an engine command. */
    struct MidiSettings
    {
        enum Sync : int { autoSync = 0, tempoOnly = 1, off = 2 };   // AUt, tNp, oFF (FUNC + 10)
        enum Curve : int { fixed = 0, linear = 1, soft = 2 };

        int    baseChannel   = 1;         // Pad Base Ch, 1..16 (FUNC + 9)
        int    sync          = autoSync;
        bool   honourNoteOff = false;     // Limits::honourNoteOff (SPEC U6)
        bool   velocity      = false;     // Limits::velocity (SPEC U5)
        int    velocityCurve = soft;      // with velocity: fixed / linear / soft (SPEC U52)
        bool   controlChange = false;     // Limits::midiLearn: forward CCs for MIDI learn
        double minBpm        = 40.0;      // sync tempo range (p. 46)
        double maxBpm        = 200.0;
    };
}
