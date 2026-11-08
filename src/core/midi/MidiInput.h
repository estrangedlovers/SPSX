#pragma once

#include "MidiSettings.h"
#include "../Types.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace spsx
{
    /** MIDI IN (SPEC 13): turns incoming messages into pad events and a sync transport.
        Audio thread only; no allocation, no locks.

        Recognised, per the implementation chart (manual p. 48):
        - Note On 46..106 on Pad Base Ch (banks A..E, note 46 = EXT SOURCE) and
          Pad Base Ch + 1 (banks F..J; Base Ch 16 wraps to 1, SPEC U17).
          Authentic: velocity ignored (U5) and Note Off ignored, so a Note On is a
          one-shot hit (U6). Extended: velocity scales level, Note Off releases.
        - All Sound Off (CC 120) on any channel.
        - Clock (AUt and tNp), Start / Continue / Stop and Song Position (AUt only).
        - Active Sensing: once seen, 300 ms without any message silences everything.

        The caller splits its block at the offsets where handle() returns true, so
        Start and Stop take effect on their exact sample (see PluginProcessor). */
    class MidiInput
    {
    public:
        struct Event
        {
            enum class Kind { padDown, padUp, extDown, extUp, stopAll, controlChange } kind;
            PadId pad = kNoPad;
            float velocity = 1.0f;
            bool  oneShot = false;   // no Note Off will follow (Authentic)
            int   offset = 0;        // device samples into the block
            int   channel = 0, number = 0, value = 0;   // controlChange (Extended MIDI learn)
        };

        struct Transport
        {
            bool   follow  = false;  // a sync source is active: use bpm (and playing / ppq)
            bool   playing = false;
            double bpm     = 0.0;
            double ppq     = 0.0;    // quarter notes since song start
        };

        void prepare (double deviceRate) noexcept
        {
            rate = deviceRate > 0.0 ? deviceRate : 44100.0;
            reset();
        }

        void reset() noexcept
        {
            blockStart = 0;
            numIntervals = intervalWrite = 0;
            intervalSum = 0.0;
            lastClock = lastMessage = -1;
            clocks = 0;
            playing = waitFirstClock = sensing = false;
        }

        void setSettings (const MidiSettings& s) noexcept
        {
            if (s.sync != settings.sync)
            {
                playing = false;   // a new sync mode starts from a stopped transport
                numIntervals = intervalWrite = 0;
                intervalSum = 0.0;
            }
            settings = s;
        }

        const MidiSettings& getSettings() const noexcept { return settings; }

        /** Start of a device block of n samples. Runs the Active Sensing timeout. */
        template <typename Sink>
        void beginBlock (int n, Sink&& sink) noexcept
        {
            blockLength = n;
            if (sensing && lastMessage >= 0 && blockStart - lastMessage > (std::int64_t) (kSensingTimeout * rate))
            {
                sensing = false;
                sink (Event { Event::Kind::stopAll, kNoPad, 1.0f, false, 0 });
            }
        }

        /** Handles one message at `offset` in the current block. Pad and stop events go
            to `sink`. Returns true when the transport changed state at this offset. */
        template <typename Sink>
        bool handle (const juce::MidiMessage& m, int offset, Sink&& sink) noexcept
        {
            offset = juce::jlimit (0, juce::jmax (0, blockLength - 1), offset);
            const auto t = blockStart + offset;
            lastMessage = t;

            if (m.isActiveSense())
            {
                sensing = true;
                return false;
            }
            if (m.isMidiClock())
            {
                clock (t);
                return false;
            }
            if (m.isMidiStart() || m.isMidiContinue() || m.isMidiStop() || m.isSongPositionPointer())
                return transportMessage (m);

            if (m.isAllSoundOff())   // any channel (chart note *3)
            {
                sink (Event { Event::Kind::stopAll, kNoPad, 1.0f, false, offset });
                return false;
            }
            if (m.isController())
            {
                if (settings.controlChange)
                    sink (Event { Event::Kind::controlChange, kNoPad, 1.0f, false, offset,
                                  m.getChannel(), m.getControllerNumber(), m.getControllerValue() });
                return false;
            }

            const bool on  = m.isNoteOn (false);   // velocity 0 counts as Note Off
            const bool off = m.isNoteOff (true);
            if (! on && ! off)
                return false;
            if (off && ! settings.honourNoteOff)
                return false;

            const int note = m.getNoteNumber();
            const int ch = m.getChannel();
            const int base = juce::jlimit (1, 16, settings.baseChannel);
            const int upper = base % 16 + 1;   // Base Ch 16 -> 1 (SPEC U17)
            const float vel = settings.velocity ? velocityGain (m.getFloatVelocity(), settings.velocityCurve) : 1.0f;

            if (note == kExtNote && ch == base)
            {
                if (on)
                {
                    sink (Event { Event::Kind::extDown, kNoPad, 1.0f, false, offset });
                    if (! settings.honourNoteOff)
                        sink (Event { Event::Kind::extUp, kNoPad, 1.0f, false, offset });
                }
                else
                    sink (Event { Event::Kind::extUp, kNoPad, 1.0f, false, offset });
                return false;
            }

            if (note < kFirstPadNote || note > kLastPadNote || (ch != base && ch != upper))
                return false;

            const int rel = note - kFirstPadNote;
            const PadId pad { rel / kPadsPerBank + (ch == base ? 0 : 5), rel % kPadsPerBank };
            if (on)
                sink (Event { Event::Kind::padDown, pad, vel, ! settings.honourNoteOff, offset });
            else
                sink (Event { Event::Kind::padUp, pad, 1.0f, false, offset });
            return false;
        }

        /** Sync transport for audio starting at `offset` in this block. */
        Transport transportAt (int offset) const noexcept
        {
            Transport tr;
            const auto t = blockStart + offset;
            const bool clockAlive = lastClock >= 0 && t - lastClock < (std::int64_t) (kClockTimeout * rate);
            if (settings.sync == MidiSettings::off || ! clockAlive || numIntervals < kMinIntervals)
                return tr;

            const double interval = intervalSum / numIntervals;   // device samples per clock
            tr.follow = true;
            tr.bpm = juce::jlimit (settings.minBpm, settings.maxBpm, 60.0 * rate / (24.0 * interval));
            if (settings.sync == MidiSettings::autoSync)
            {
                tr.playing = playing;
                double frac = 0.0;
                if (playing && ! waitFirstClock)
                    frac = juce::jlimit (-1.0, 1.0, (double) (t - lastClock) / interval);
                tr.ppq = juce::jmax (0.0, ((double) clocks + frac) / 24.0);
            }
            return tr;
        }

        /** End of the block: advances the sample clock. */
        void endBlock() noexcept { blockStart += blockLength; }

        /** Extended velocity curves (SPEC U52): fixed (always full level), linear
            amplitude, or soft (squared amplitude, velocity 64 about -12 dB). */
        static float velocityGain (float velocity01, int curve = MidiSettings::soft) noexcept
        {
            const float v = juce::jlimit (0.0f, 1.0f, velocity01);
            switch (curve)
            {
                case MidiSettings::fixed:  return 1.0f;
                case MidiSettings::linear: return v;
                default:                   return v * v;
            }
        }

        static constexpr int kExtNote = 46, kFirstPadNote = 47, kLastPadNote = 106;

    private:
        static constexpr double kSensingTimeout = 0.3;   // MIDI 1.0 spec: 300 ms
        static constexpr double kClockTimeout   = 0.5;   // longest clock gap at 40 BPM is 62.5 ms
        static constexpr int    kMaxIntervals   = 24;    // average over one beat
        static constexpr int    kMinIntervals   = 6;

        void clock (std::int64_t t) noexcept
        {
            if (settings.sync == MidiSettings::off)
                return;

            if (lastClock >= 0)
            {
                const auto gap = (double) (t - lastClock);
                if (gap > kClockTimeout * rate)
                {
                    numIntervals = intervalWrite = 0;   // clock restarted: forget the old tempo
                    intervalSum = 0.0;
                }
                else if (gap > 0.0)
                {
                    if (numIntervals == kMaxIntervals)
                        intervalSum -= intervals[(size_t) intervalWrite];
                    else
                        ++numIntervals;
                    intervals[(size_t) intervalWrite] = gap;
                    intervalSum += gap;
                    intervalWrite = (intervalWrite + 1) % kMaxIntervals;
                }
            }
            lastClock = t;

            if (playing && settings.sync == MidiSettings::autoSync)
            {
                // The first clock after Start / Continue is the position itself.
                if (waitFirstClock) waitFirstClock = false;
                else                ++clocks;
            }
        }

        bool transportMessage (const juce::MidiMessage& m) noexcept
        {
            if (settings.sync != MidiSettings::autoSync)
                return false;   // tNp and oFF ignore Start / Stop / Song Position (p. 47)

            if (m.isMidiStart())
            {
                clocks = 0;
                playing = true;
                waitFirstClock = true;
            }
            else if (m.isMidiContinue())
            {
                playing = true;
                waitFirstClock = true;
            }
            else if (m.isMidiStop())
            {
                playing = false;
            }
            else   // Song Position Pointer: 16th notes = 6 clocks
            {
                clocks = (std::int64_t) m.getSongPositionPointerMidiBeat() * 6;
                waitFirstClock = true;
            }
            return true;
        }

        MidiSettings settings;
        double rate = 44100.0;
        std::int64_t blockStart = 0;
        int blockLength = 0;

        std::array<double, kMaxIntervals> intervals {};
        int numIntervals = 0, intervalWrite = 0;
        double intervalSum = 0.0;
        std::int64_t lastClock = -1, lastMessage = -1;
        std::int64_t clocks = 0;   // song position in clocks (24 per quarter)
        bool playing = false, waitFirstClock = false, sensing = false;
    };
}
