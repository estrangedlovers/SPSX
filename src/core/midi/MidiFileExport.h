#pragma once

#include "../state/PatternData.h"
#include "../Types.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace spsx::midi
{
    /** Note and channel that trigger `pad` from MIDI IN (SPEC 13.1): banks A..E on
        Pad Base Ch, F..J on Base Ch + 1 (16 wraps to 1). Returns false for pads outside
        the hardware's note map. */
    bool noteForPad (PadId pad, int baseChannel, int& note, int& channel) noexcept;

    /** A pattern as a type 0 Standard MIDI File at 96 PPQ: tempo, 4/4, the pad hits on
        the note map (so the file plays SPSX back from a DAW), held lengths (one-shot hits
        get a sixteenth) and velocities, ending at the pattern's length. */
    juce::MidiFile patternToMidiFile (const PatternData& pattern, int baseChannel);

    /** Writes patternToMidiFile() to `file`. Message thread. */
    bool exportPattern (const PatternData& pattern, int baseChannel, const juce::File& file);
}
