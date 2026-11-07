#include "StretchRenderer.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>

namespace spsx
{
    SampleData::Ptr renderStretched (const SampleData& sample, SampleIndex start, SampleIndex end, double speed)
    {
        start = juce::jlimit<SampleIndex> (0, sample.length(), start);
        end   = juce::jlimit<SampleIndex> (start, sample.length(), end);
        const int inLen  = (int) (end - start);
        const int outLen = juce::jmax (1, (int) std::llround (inLen / speed));
        const int channels = sample.numChannels();

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        stretch.presetDefault (channels, (float) kEngineRate);

        std::vector<const float*> in ((size_t) channels);
        for (int ch = 0; ch < channels; ++ch)
            in[(size_t) ch] = sample.channel (ch) + start;

        juce::AudioBuffer<float> out (channels, outLen);
        std::vector<float*> outPtrs ((size_t) channels);
        for (int ch = 0; ch < channels; ++ch)
            outPtrs[(size_t) ch] = out.getWritePointer (ch);

        stretch.exact (in, inLen, outPtrs, outLen);
        return new SampleData (std::move (out), false);
    }
}
