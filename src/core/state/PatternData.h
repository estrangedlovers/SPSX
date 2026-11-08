#pragma once

#include "../Types.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spsx
{
    /** Pattern sequencer resolution and limits (manual p. 49). */
    constexpr int kPpq = 96;                       // ticks per quarter note
    constexpr int kTicksPerBar = 4 * kPpq;         // 4/4
    constexpr int kMaxPatternEvents = 16000;       // "approx. 16,000 notes"

    /** One recorded pad hit. `pad` is a PadId index (bank * 12 + pad). */
    struct SeqEvent
    {
        std::int32_t tick = 0;       // 0 .. length-1
        std::int32_t length = 1;     // ticks the pad was held (for GATE samples)
        std::int16_t pad = 0;
        std::uint16_t pass = 0;      // recording pass (playback skips events recorded this pass)
        std::uint32_t id = 0;        // stable id while recording (to set the length on release)

        friend bool operator== (const SeqEvent& a, const SeqEvent& b)
        {
            return a.tick == b.tick && a.length == b.length && a.pad == b.pad;
        }
    };

    /** Immutable pattern published to the audio thread (like SampleData). Events are
        sorted by tick. */
    class PatternData final : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<PatternData>;

        PatternData (int lengthBarsIn, double tempoIn, std::vector<SeqEvent> eventsIn)
            : lengthBars (juce::jlimit (1, 99, lengthBarsIn)), tempo (tempoIn), events (sortedByTick (std::move (eventsIn)))
        {
        }

        int lengthTicks() const noexcept { return lengthBars * kTicksPerBar; }

        const int lengthBars;
        const double tempo;
        const std::vector<SeqEvent> events;

    private:
        static std::vector<SeqEvent> sortedByTick (std::vector<SeqEvent> v)
        {
            std::stable_sort (v.begin(), v.end(), [] (const SeqEvent& a, const SeqEvent& b) { return a.tick < b.tick; });
            return v;
        }

        JUCE_DECLARE_NON_COPYABLE (PatternData)
    };

    /** Quantize settings (p. 32). Grid in ticks; 0 = off. S.8 / S.16 swing the offbeats. */
    struct Quantize
    {
        static constexpr std::array<int, 10> grids { 48, 24, 96, 64, 48, 32, 24, 16, 12, 0 };   // S.8 S.16 4 4.3 8 8.3 16 16.3 32 oFF
        static bool isShuffle (int index) noexcept { return index == 0 || index == 1; }

        /** Quantize a raw tick within a pattern of `length` ticks. Shuffle 50 = straight;
            higher values delay every second grid step by (shuffle - 50) % of a step pair. */
        static std::int32_t apply (double rawTick, int index, int shuffle, int length) noexcept
        {
            const int grid = grids[(size_t) juce::jlimit (0, 9, index)];
            if (grid <= 0)
            {
                const auto t = (std::int64_t) std::floor (rawTick) % std::max (1, length);
                return (std::int32_t) (t < 0 ? t + length : t);
            }
            const double steps = std::round (rawTick / grid);
            auto tick = (std::int64_t) steps * grid;
            if (isShuffle (index) && ((std::int64_t) steps % 2) != 0)
                tick += (std::int64_t) std::lround ((shuffle - 50) / 50.0 * grid);
            tick %= length;
            if (tick < 0) tick += length;
            return (std::int32_t) tick;
        }
    };
}
