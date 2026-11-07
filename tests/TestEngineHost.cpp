#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"
#include "core/engine/EngineHost.h"
#include <random>

using namespace spsx;
using namespace spsx::test;

namespace
{
    /** Renders `blocks` of `blockSize`, scheduling a pad hit at (block, offset).
        Returns the index of the first output sample above half the click level. */
    long firstOnset (double rate, int blockSize, int hitBlock, int hitOffset, int* latencyOut = nullptr)
    {
        Engine e;
        EngineHost host (e);
        host.prepare (rate, blockSize, Limits::authentic());
        if (latencyOut) *latencyOut = host.latencySamples();

        e.postSample ({ 0, 0 }, makeConstant (400, 0.8f));
        e.postSettings ({ 0, 0 }, defaults (*makeConstant (400, 0.8f)));

        std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
        for (int b = 0; b < hitBlock + 40; ++b)
        {
            if (b == hitBlock)
            {
                Command c;
                c.type = Command::Type::padDown;
                c.pad = { 0, 0 };
                host.scheduleAt (c, hitOffset);
            }
            float* o[2] = { l.data(), r.data() };
            host.process (nullptr, 0, o, 2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                if (l[(size_t) i] > 0.4f)
                    return (long) b * blockSize + i;
        }
        return -1;
    }
}

TEST_CASE ("At 44.1 kHz the converter is bypassed: zero latency, exact timing", "[host]")
{
    int latency = -1;
    const auto onset = firstOnset (44100.0, 128, 3, 17, &latency);
    REQUIRE (latency == 0);
    REQUIRE (onset == 3 * 128 + 17);
}

TEST_CASE ("Converted rates: event latency is constant and equals the reported latency", "[host]")
{
    const double rate = GENERATE (48000.0, 96000.0, 32000.0);
    const int block = GENERATE (64, 512);

    int latency = 0;
    const auto a = firstOnset (rate, block, 2, 5, &latency);
    const auto b = firstOnset (rate, block, 7, block - 3);
    const auto c = firstOnset (rate, block, 11, block / 2);

    REQUIRE (latency > 0);
    // Onset detection threshold sits on the band-limited edge: allow +-2 samples.
    REQUIRE (std::abs ((a - (2 * block + 5)) - latency) <= 2);
    REQUIRE (std::abs ((b - (7 * block + block - 3)) - latency) <= 2);
    REQUIRE (std::abs ((c - (11 * block + block / 2)) - latency) <= 2);
}

TEST_CASE ("Converted path: random block sizes, no underflow, no allocation", "[host][realtime]")
{
    Engine e;
    EngineHost host (e);
    host.prepare (48000.0, 1024, Limits::authentic());
    for (int i = 0; i < 12; ++i)
    {
        e.postSample ({ 0, i }, makeSine (20000, 220.0 * (i + 1)));
        e.postSettings ({ 0, i }, defaults (*makeSine (20000, 220.0)));
    }

    std::mt19937 rng (1234);
    std::uniform_int_distribution<int> sizeDist (1, 1024);
    std::vector<float> inL (1024, 0.1f), inR (1024, -0.1f), l (1024), r (1024);

    resetRealtimeAllocations();
    for (int k = 0; k < 400; ++k)
    {
        if (k % 30 == 0)
        {
            Command c;
            c.type = Command::Type::padDown;
            c.pad = { 0, (k / 30) % 12 };
            host.scheduleAt (c, 0);
        }
        const int n = sizeDist (rng);
        const float* in[2] = { inL.data(), inR.data() };
        float* o[2] = { l.data(), r.data() };
        host.process (in, 2, o, 2, n);
        for (int i = 0; i < n; ++i)
            REQUIRE (std::isfinite (l[(size_t) i]));
    }
    REQUIRE (host.underflowCount() == 0);
    REQUIRE (realtimeAllocations() == 0);
}

TEST_CASE ("Extended native rate: engine runs at the device rate, samples keep their duration", "[host][native]")
{
    Engine e;
    EngineHost host (e);
    auto limits = Limits::extended();
    limits.internal44k = false;
    host.prepare (48000.0, 256, limits);
    REQUIRE_FALSE (host.isConverting());
    REQUIRE (host.latencySamples() == 0);
    REQUIRE (e.getRate() == 48000.0);

    auto s = makeConstant (44100, 0.5f);   // 1 s at 44.1 kHz
    e.postSample ({ 0, 0 }, s);
    e.postSettings ({ 0, 0 }, defaults (*s));
    Command c;
    c.type = Command::Type::padDown;
    c.pad = { 0, 0 };
    e.post (c);

    std::vector<float> l (256), r (256);
    long lastNonZero = -1;
    for (int b = 0; b < 250; ++b)
    {
        float* o[2] = { l.data(), r.data() };
        host.process (nullptr, 0, o, 2, 256);
        for (int i = 0; i < 256; ++i)
            if (l[(size_t) i] > 0.25f)
                lastNonZero = (long) b * 256 + i;
    }
    REQUIRE (std::abs (lastNonZero - 48000) <= 2);
}
