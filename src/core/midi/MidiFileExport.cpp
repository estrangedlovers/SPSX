#include "MidiFileExport.h"

namespace spsx::midi
{
    bool noteForPad (PadId pad, int baseChannel, int& note, int& channel) noexcept
    {
        if (pad.bank < 0 || pad.bank >= 10 || pad.pad < 0 || pad.pad >= kPadsPerBank)
            return false;
        const int base = juce::jlimit (1, 16, baseChannel);
        channel = pad.bank < 5 ? base : base % 16 + 1;
        note = 47 + (pad.bank % 5) * kPadsPerBank + pad.pad;
        return true;
    }

    juce::MidiFile patternToMidiFile (const PatternData& pattern, int baseChannel)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60'000'000.0 / juce::jmax (1.0, pattern.tempo))), 0.0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

        for (const auto& e : pattern.events)
        {
            int note = 0, channel = 1;
            if (! noteForPad (PadId::fromIndex (e.pad), baseChannel, note, channel))
                continue;
            const int length = e.length > 0 ? e.length : kPpq / 4;
            const double on = e.tick, off = std::min (e.tick + length, pattern.lengthTicks());
            track.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) juce::jlimit (1, 127, (int) e.velocity)), on);
            track.addEvent (juce::MidiMessage::noteOff (channel, note), std::max (on + 1.0, off));
        }
        track.updateMatchedPairs();
        track.addEvent (juce::MidiMessage::endOfTrack(), (double) pattern.lengthTicks());
        track.sort();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kPpq);
        file.addTrack (track);
        return file;
    }

    bool exportPattern (const PatternData& pattern, int baseChannel, const juce::File& file)
    {
        auto midiFile = patternToMidiFile (pattern, baseChannel);
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && midiFile.writeTo (out, 0);
    }
}
