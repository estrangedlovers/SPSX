#pragma once

#include "../state/SampleData.h"
#include "../state/PadSettings.h"
#include <optional>

namespace spsx::edit
{
    constexpr double kFineRangeSeconds = 0.130;   // manual p. 29: about 130 ms per operation
    constexpr double kMinRegionSeconds = 0.100;   // manual p. 29: start/end no closer than ~100 ms

    SampleIndex minRegion (SampleIndex sampleLength);

    /** BPM from a region length, assuming 4/4. The manual says the result can be half
        or double the musical tempo (p. 26, 30), so pick the power-of-two beat count
        that lands the BPM in [80, 160), then clamp to 40..200. Returns BPM with 0.1
        resolution. */
    double deriveBpm (SampleIndex regionLength, double sampleRate);

    /** TIME/BPM + CTRL 2: left halves, right doubles (p. 30). Stays within 40..200;
        returns the input unchanged if the result would leave that range. */
    double halveBpm (double bpm);
    double doubleBpm (double bpm);

    /** Fresh settings for a new sample: whole region, LEVEL 127, BPM derived. */
    PadSettings defaultsFor (const SampleData& sample);

    /** After a region change, BPM is recalculated (p. 29). */
    void setRegion (PadSettings& s, SampleIndex start, SampleIndex end, SampleIndex sampleLength);

    /** START/END/LEVEL fine adjust (p. 29). Knobs are relative to their centre:
        -1..+1 maps to -130..+130 ms from the point captured when the session began;
        centre restores the original. Points stay inside the sample and at least 100 ms
        apart. */
    class FineAdjust
    {
    public:
        FineAdjust (const PadSettings& original, SampleIndex sampleLength, double sampleRate);
        /** Returns updated settings for the given knob offsets. */
        PadSettings apply (double ctrl1, double ctrl2) const;

    private:
        PadSettings base;
        SampleIndex length;
        double rate;
    };

    /** Real-time MARK flows (p. 27-28). The controller feeds it play positions taken
        from the engine when MARK is pressed. */
    class MarkSession
    {
    public:
        enum class Mode
        {
            startAndEnd,   // press MARK at start, then at end
            endOnly,       // hold MARK + pad, press MARK at end
            startOnly      // press MARK at start, then stop the pad
        };

        MarkSession (Mode m, const PadSettings& original, SampleIndex sampleLength, double sampleRate);

        /** Optional BPM entered between the two presses: the end snaps to the nearest
            beat from the start point (p. 28). */
        void setSnapBpm (double bpm) { snapBpm = bpm; }

        /** MARK pressed at `position`. Returns the finished settings when the flow
            completes, otherwise nothing. */
        std::optional<PadSettings> markPressed (SampleIndex position);
        /** Pad stopped: completes the start-only flow. */
        std::optional<PadSettings> padStopped();

    private:
        PadSettings finish (SampleIndex start, SampleIndex end) const;

        Mode mode;
        PadSettings base;
        SampleIndex length;
        double rate;
        double snapBpm = 0.0;
        std::optional<SampleIndex> pendingStart;
    };

    /** Destructive truncate (p. 29): keep only the playback region. Returns the new
        sample; `settings` is updated to cover it fully (MARK goes out). */
    SampleData::Ptr truncate (const SampleData& sample, PadSettings& settings);
}
