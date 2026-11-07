#include "SampleEdit.h"
#include <cmath>
#include <algorithm>

namespace spsx::edit
{
    SampleIndex minRegion (SampleIndex sampleLength)
    {
        return std::min<SampleIndex> (sampleLength, (SampleIndex) std::llround (kMinRegionSeconds * kEngineRate));
    }

    static double round1 (double v) { return std::round (v * 10.0) / 10.0; }

    double deriveBpm (SampleIndex regionLength, double sampleRate)
    {
        if (regionLength <= 0)
            return 0.0;

        const double seconds = (double) regionLength / sampleRate;
        double bpm = 60.0 / seconds;                 // region = 1 beat
        while (bpm < 80.0)  bpm *= 2.0;              // 2, 4, 8 ... beats
        while (bpm >= 160.0) bpm *= 0.5;             // 1/2, 1/4 ... beat
        return round1 (std::clamp (bpm, 40.0, 200.0));
    }

    double halveBpm (double bpm)  { return bpm * 0.5 >= 40.0  ? round1 (bpm * 0.5) : bpm; }
    double doubleBpm (double bpm) { return bpm * 2.0 <= 200.0 ? round1 (bpm * 2.0) : bpm; }

    PadSettings defaultsFor (const SampleData& sample)
    {
        PadSettings s;
        s.start = 0;
        s.end = sample.length();
        s.bpm = deriveBpm (sample.length(), kEngineRate);
        return s;
    }

    void setRegion (PadSettings& s, SampleIndex start, SampleIndex end, SampleIndex sampleLength)
    {
        s.start = std::clamp<SampleIndex> (start, 0, sampleLength);
        s.end = std::clamp<SampleIndex> (end, s.start, sampleLength);
        s.bpm = deriveBpm (s.end - s.start, kEngineRate);
    }

    // ---- FineAdjust -------------------------------------------------------------------

    FineAdjust::FineAdjust (const PadSettings& original, SampleIndex sampleLength, double sampleRate)
        : base (original), length (sampleLength), rate (sampleRate) {}

    PadSettings FineAdjust::apply (double ctrl1, double ctrl2) const
    {
        if (juce::exactlyEqual (ctrl1, 0.0) && juce::exactlyEqual (ctrl2, 0.0))
            return base;   // both knobs centred: cancel (p. 29)

        const double range = kFineRangeSeconds * rate;
        const SampleIndex minLen = minRegion (length);

        SampleIndex st = base.start + (SampleIndex) std::llround (std::clamp (ctrl1, -1.0, 1.0) * range);
        SampleIndex en = base.end   + (SampleIndex) std::llround (std::clamp (ctrl2, -1.0, 1.0) * range);

        st = std::clamp<SampleIndex> (st, 0, length - minLen);
        en = std::clamp<SampleIndex> (en, minLen, length);

        if (en - st < minLen)
        {
            // Respect the minimum by moving the point that the user is turning.
            if (! juce::exactlyEqual (ctrl1, 0.0)) st = en - minLen;
            else              en = st + minLen;
        }

        PadSettings s = base;
        setRegion (s, st, en, length);
        return s;
    }

    // ---- MarkSession ------------------------------------------------------------------

    MarkSession::MarkSession (Mode m, const PadSettings& original, SampleIndex sampleLength, double sampleRate)
        : mode (m), base (original), length (sampleLength), rate (sampleRate)
    {
        if (mode == Mode::endOnly)
            pendingStart = original.start;
    }

    PadSettings MarkSession::finish (SampleIndex start, SampleIndex end) const
    {
        const SampleIndex minLen = minRegion (length);
        start = std::clamp<SampleIndex> (start, 0, length - minLen);

        if (snapBpm > 0.0 && end > start)
        {
            const double beat = 60.0 / snapBpm * rate;
            const double beats = std::max (1.0, std::round ((double) (end - start) / beat));
            end = start + (SampleIndex) std::llround (beats * beat);
        }
        end = std::clamp<SampleIndex> (end, start + minLen, length);

        PadSettings s = base;
        setRegion (s, start, end, length);
        if (snapBpm > 0.0)
            s.bpm = snapBpm;   // the user specified it
        return s;
    }

    std::optional<PadSettings> MarkSession::markPressed (SampleIndex position)
    {
        position = std::clamp<SampleIndex> (position, 0, length);

        switch (mode)
        {
            case Mode::startAndEnd:
                if (! pendingStart)
                {
                    pendingStart = position;
                    return std::nullopt;
                }
                return finish (*pendingStart, position);

            case Mode::endOnly:
                return finish (*pendingStart, position);

            case Mode::startOnly:
                pendingStart = position;
                return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<PadSettings> MarkSession::padStopped()
    {
        // A start point was marked and the pad stopped before a second press: that is the
        // manual's "adjusting only the start point" flow (p. 28).
        if (mode != Mode::endOnly && pendingStart)
            return finish (*pendingStart, base.end);
        return std::nullopt;
    }

    // ---- truncate ---------------------------------------------------------------------

    SampleData::Ptr truncate (const SampleData& sample, PadSettings& settings)
    {
        const SampleIndex st = std::clamp<SampleIndex> (settings.start, 0, sample.length());
        const SampleIndex en = std::clamp<SampleIndex> (settings.end, st, sample.length());
        const int n = (int) (en - st);

        juce::AudioBuffer<float> out (sample.numChannels(), std::max (n, 1));
        out.clear();
        for (int ch = 0; ch < sample.numChannels(); ++ch)
            out.copyFrom (ch, 0, sample.buffer(), ch, (int) st, n);

        settings.start = 0;
        settings.end = n;
        // BPM is kept: the region did not change length.
        return new SampleData (std::move (out), sample.isQuantized16());
    }
}
