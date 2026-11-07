#pragma once

#include "core/engine/Engine.h"
#include "core/sampler/SampleEdit.h"
#include <vector>

namespace spsx::test
{
    /** Mono or stereo sample whose value at index i is (i + 1) / 2^20 (left) and the
        negative of that (right), so positions are recoverable from output. */
    inline SampleData::Ptr makeRamp (int length, int channels = 1)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int i = 0; i < length; ++i)
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, (ch == 0 ? 1.0f : -1.0f) * (float) (i + 1) / 1048576.0f);
        return new SampleData (std::move (b), false);
    }

    inline SampleData::Ptr makeConstant (int length, float value, int channels = 1)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, length);
        return new SampleData (std::move (b), false);
    }

    inline SampleData::Ptr makeSine (int length, double hz, double rate = kEngineRate, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (1, length);
        for (int i = 0; i < length; ++i)
            b.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / rate));
        return new SampleData (std::move (b), false);
    }

    struct Rendered
    {
        std::vector<float> l, r;
    };

    /** Renders n samples in blocks of `block`. */
    inline Rendered render (Engine& e, int n, int block = 64)
    {
        Rendered out;
        out.l.assign ((size_t) n, 0.0f);
        out.r.assign ((size_t) n, 0.0f);
        for (int done = 0; done < n; done += block)
        {
            const int m = std::min (block, n - done);
            float* o[2] = { out.l.data() + done, out.r.data() + done };
            e.process (nullptr, 0, o, 2, m);
        }
        return out;
    }

    inline Command cmd (Command::Type t, PadId p = kNoPad, double v = 0.0)
    {
        Command c;
        c.type = t;
        c.pad = p;
        c.value = v;
        return c;
    }

    /** Loads a sample with default settings plus an edit, directly via engine commands. */
    inline void loadPad (Engine& e, PadId p, SampleData::Ptr s, PadSettings settings)
    {
        e.postSettings (p, settings);
        e.postSample (p, s);
    }

    inline PadSettings defaults (const SampleData& s) { return edit::defaultsFor (s); }

    inline float peakOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }
}
