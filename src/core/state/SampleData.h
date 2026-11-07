#pragma once

#include "../Types.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace spsx
{
    /** Immutable audio for one pad. Created on a worker or message thread, published to
        the audio thread by pointer, never modified afterwards. Always at kEngineRate. */
    class SampleData final : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

        SampleData (juce::AudioBuffer<float>&& audioIn, bool isQuantized16)
            : audio (std::move (audioIn)), quantized16 (isQuantized16) {}

        int         numChannels() const noexcept { return audio.getNumChannels(); }
        SampleIndex length() const noexcept      { return audio.getNumSamples(); }
        const float* channel (int ch) const noexcept { return audio.getReadPointer (juce::jmin (ch, numChannels() - 1)); }
        const juce::AudioBuffer<float>& buffer() const noexcept { return audio; }
        bool isQuantized16() const noexcept { return quantized16; }
        double seconds() const noexcept { return (double) length() / kEngineRate; }

    private:
        juce::AudioBuffer<float> audio;
        bool quantized16;

        JUCE_DECLARE_NON_COPYABLE (SampleData)
    };
}
