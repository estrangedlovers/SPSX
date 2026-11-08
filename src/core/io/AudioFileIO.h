#pragma once

#include "../state/SampleData.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace spsx::io
{
    /** Round to the 16-bit grid, no dither (prompt: match hardware). Values that would
        exceed +32767 clip, like a fixed-point converter. */
    void requantize16 (juce::AudioBuffer<float>& buffer);

    /** Converts any rate to kEngineRate with the engine's sinc resampler (the hardware's
        Wave Converter also converted on import). Keeps at most 2 channels. */
    juce::AudioBuffer<float> toEngineRate (const juce::AudioBuffer<float>& in, double inRate);

    /** Loudest sample value accepted from a file (+18 dBFS). */
    constexpr float kMaxImportLevel = 8.0f;
    /** NaN / Inf to 0, everything else clamped to +-kMaxImportLevel. */
    void sanitise (juce::AudioBuffer<float>& buffer) noexcept;

    SampleData::Ptr makeSample (const juce::AudioBuffer<float>& audio, double rate, bool quantize16);

    /** Hardware limit per sample: about 2 GB (manual p. 20) -> "FUL". */
    constexpr juce::int64 kMaxSampleBytes = 2'000'000'000LL;

    struct LoadResult
    {
        SampleData::Ptr sample;
        juce::String error;   // empty on success; "UnS" style codes in `code`
        juce::String code;
    };

    LoadResult loadFile (const juce::File& file, bool quantize16);

    enum class FileType { wav, aiff };

    /** Writes 16-bit (Authentic) or 24-bit audio at the sample's rate. */
    juce::Result writeFile (const juce::File& file, const SampleData& sample, FileType type, int bitDepth = 16);
}
