#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "core/io/AudioFileIO.h"

using namespace spsx;

namespace
{
    juce::File tempDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spsx-tests");
        d.createDirectory();
        return d;
    }
}

TEST_CASE ("16-bit requantize: exact grid, no dither, clips at +32767", "[io]")
{
    juce::AudioBuffer<float> b (1, 4);
    b.setSample (0, 0, 0.1f);
    b.setSample (0, 1, 1.0f);
    b.setSample (0, 2, -1.0f);
    b.setSample (0, 3, 1.0f / 65536.0f * 1.2f);   // rounds to one LSB
    io::requantize16 (b);
    REQUIRE (b.getSample (0, 0) == std::nearbyint (0.1f * 32768.0f) / 32768.0f);
    REQUIRE (b.getSample (0, 1) == 32767.0f / 32768.0f);
    REQUIRE (b.getSample (0, 2) == -1.0f);
    REQUIRE (b.getSample (0, 3) == 1.0f / 32768.0f);

    io::requantize16 (b);   // idempotent: no dither noise added
    REQUIRE (b.getSample (0, 0) == std::nearbyint (0.1f * 32768.0f) / 32768.0f);
}

TEST_CASE ("WAV and AIFF 16-bit round trips are bit exact", "[io]")
{
    juce::AudioBuffer<float> src (2, 5000);
    for (int i = 0; i < 5000; ++i)
    {
        src.setSample (0, i, 0.7f * (float) std::sin (i * 0.01));
        src.setSample (1, i, 0.3f * (float) std::cos (i * 0.013));
    }
    auto sample = io::makeSample (src, kEngineRate, true);

    for (auto type : { io::FileType::wav, io::FileType::aiff })
    {
        const auto f = tempDir().getChildFile (type == io::FileType::wav ? "rt.wav" : "rt.aif");
        REQUIRE (io::writeFile (f, *sample, type).wasOk());
        auto r = io::loadFile (f, true);
        REQUIRE (r.sample != nullptr);
        REQUIRE (r.sample->length() == sample->length());
        REQUIRE (r.sample->numChannels() == 2);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 5000; ++i)
                REQUIRE (r.sample->channel (ch)[i] == sample->channel (ch)[i]);
    }
}

TEST_CASE ("Import at 48 kHz converts to 44.1 kHz keeping pitch and duration", "[io]")
{
    const int n = 48000;
    juce::AudioBuffer<float> src (1, n);
    for (int i = 0; i < n; ++i)
        src.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0));

    juce::WavAudioFormat wav;
    const auto f = tempDir().getChildFile ("48k.wav");
    f.deleteFile();
    {
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (src, 0, n);
    }

    auto r = io::loadFile (f, true);
    REQUIRE (r.sample != nullptr);
    REQUIRE (r.sample->length() == 44100);
    int crossings = 0;
    const float* x = r.sample->channel (0);
    for (int i = 1; i < 44100; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++crossings;
    REQUIRE (std::abs (crossings - 1000) <= 2);
}

TEST_CASE ("Unreadable files report UnS", "[io]")
{
    const auto f = tempDir().getChildFile ("junk.wav");
    f.replaceWithText ("this is not audio");
    auto r = io::loadFile (f, true);
    REQUIRE (r.sample == nullptr);
    REQUIRE (r.code == "UnS");
}
