#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"
#include "core/RealtimeGuard.h"
#include "core/engine/EngineHost.h"
#include "core/engine/OutputGuard.h"
#include "core/fx/EffectInfo.h"
#include "core/io/AudioFileIO.h"
#include <limits>
#include <random>

using namespace spsx;
using namespace spsx::test;

namespace
{
    bool allSafe (const std::vector<float>& x, float ceiling = 1.0f)
    {
        for (auto v : x)
            if (! std::isfinite (v) || std::abs (v) > ceiling)
                return false;
        return true;
    }
}

TEST_CASE ("Output guard is bit-transparent below full scale", "[safety]")
{
    OutputGuard g;
    g.prepare (48000.0, false);
    std::vector<float> l (4800), r (4800);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d (rng); r[i] = d (rng); }
    l[100] = 1.0f;
    r[200] = -1.0f;
    auto l0 = l, r0 = r;
    float* ch[2] = { l.data(), r.data() };
    g.process (ch, 2, (int) l.size());
    REQUIRE (l == l0);
    REQUIRE (r == r0);
    REQUIRE (g.limited() == 0);
}

TEST_CASE ("Output guard: overs never pass full scale, gain recovers afterwards", "[safety]")
{
    OutputGuard g;
    g.prepare (48000.0, false);
    std::vector<float> l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const float s = (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / 48000.0);
        l[i] = (i < 24000 ? 8.0f : 0.5f) * s;   // +18 dBFS, then a normal level
        r[i] = -l[i];
    }
    float* ch[2] = { l.data(), r.data() };
    g.process (ch, 2, (int) l.size());
    REQUIRE (allSafe (l));
    REQUIRE (allSafe (r));
    REQUIRE (g.limited() > 0);
    REQUIRE (g.currentGain() == 1.0f);   // back to exact unity (about 1 s after a +18 dB over)
}

TEST_CASE ("Output guard: NaN or Inf mutes the block, then fades back in", "[safety]")
{
    OutputGuard g;
    g.prepare (48000.0, false);
    std::vector<float> l (512, 0.5f), r (512, 0.5f);
    l[100] = std::numeric_limits<float>::quiet_NaN();
    float* ch[2] = { l.data(), r.data() };
    g.process (ch, 2, 512);
    REQUIRE (g.faults() == 1);
    REQUIRE (l[99] == 0.5f);
    for (size_t i = 100; i < 512; ++i)
        REQUIRE ((l[i] == 0.0f && r[i] == 0.0f));

    std::fill (l.begin(), l.end(), 0.5f);
    std::fill (r.begin(), r.end(), 0.5f);
    r[3] = std::numeric_limits<float>::infinity();
    g.process (ch, 2, 512);
    REQUIRE (g.faults() == 2);

    std::fill (l.begin(), l.end(), 0.5f);
    std::fill (r.begin(), r.end(), 0.5f);
    g.process (ch, 2, 512);
    REQUIRE (l[0] < 0.01f);              // fading in, no jump
    REQUIRE (l[511] > l[0]);
    REQUIRE (allSafe (l));
}

TEST_CASE ("Extended mode: +12 dB output gain on a full-scale sample stays at or under 0 dBFS", "[safety]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Engine e;
        EngineHost host (e);
        host.prepare (rate, 256, Limits::extended());
        auto s = makeSine (44100, 100.0, kEngineRate, 1.0f);
        loadPad (e, { 0, 0 }, s, defaults (*s));
        e.post (cmd (Command::Type::setOutputGainDb, kNoPad, 12.0));
        e.post (cmd (Command::Type::setVolume, kNoPad, 1.0));
        e.post (cmd (Command::Type::padDown, { 0, 0 }));

        std::vector<float> l (256), r (256);
        float* o[2] = { l.data(), r.data() };
        float peak = 0.0f;
        bool safe = true;
        for (int b = 0; b < 150; ++b)
        {
            host.process (nullptr, 0, o, 2, 256);
            safe = safe && allSafe (l) && allSafe (r);
            for (auto v : l) peak = std::max (peak, std::abs (v));
        }
        REQUIRE (safe);
        REQUIRE (peak > 0.9f);   // still loud, just not over
    }
}

TEST_CASE ("NaN input through an effect resets the effects and never reaches the output", "[safety]")
{
    Engine e;
    EngineHost host (e);
    host.prepare (48000.0, 256, Limits::authentic());
    e.post (cmd (Command::Type::setExtOn, kNoPad, 1.0));
    e.post (cmd (Command::Type::setTargetExt, kNoPad, 1.0));
    Command fx = cmd (Command::Type::setEffect, kNoPad, 2.0);   // a feedback effect
    fx.index = 0;
    e.post (fx);

    std::vector<float> inL (256, 0.1f), inR (256, 0.1f), l (256), r (256);
    const float* in[2] = { inL.data(), inR.data() };
    float* o[2] = { l.data(), r.data() };
    bool safe = true;
    for (int b = 0; b < 200; ++b)
    {
        inL[50] = b == 20 ? std::numeric_limits<float>::quiet_NaN() : 0.1f;
        host.process (in, 2, o, 2, 256);
        safe = safe && allSafe (l) && allSafe (r);
    }
    REQUIRE (safe);
    REQUIRE (e.effectFaultCount() > 0);
}

TEST_CASE ("Imported audio is sanitised: NaN / Inf silenced, absurd levels capped", "[safety][io]")
{
    juce::AudioBuffer<float> b (1, 6);
    b.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
    b.setSample (0, 1, std::numeric_limits<float>::infinity());
    b.setSample (0, 2, 1.0e9f);
    b.setSample (0, 3, -1.0e9f);
    b.setSample (0, 4, 0.5f);
    b.setSample (0, 5, -2.0f);
    io::sanitise (b);
    REQUIRE (b.getSample (0, 0) == 0.0f);
    REQUIRE (b.getSample (0, 1) == 0.0f);
    REQUIRE (b.getSample (0, 2) == io::kMaxImportLevel);
    REQUIRE (b.getSample (0, 3) == -io::kMaxImportLevel);
    REQUIRE (b.getSample (0, 4) == 0.5f);
    REQUIRE (b.getSample (0, 5) == -2.0f);

    juce::AudioBuffer<float> bad (1, 1000);
    bad.clear();
    bad.setSample (0, 500, std::numeric_limits<float>::quiet_NaN());
    auto s = io::makeSample (bad, 44100.0, false);
    bool finite = true;
    for (int i = 0; i < s->length(); ++i)
        finite = finite && std::isfinite (s->channel (0)[i]);
    REQUIRE (finite);
}

TEST_CASE ("Fuzz: random pads, effects, parameters and gains never exceed 0 dBFS", "[safety][fuzz]")
{
    for (auto limits : { Limits::authentic(), Limits::extended() })
    {
        Engine e;
        EngineHost host (e);
        host.prepare (48000.0, 128, limits);
        for (int p = 0; p < 12; ++p)
        {
            auto s = p % 3 == 0 ? makeConstant (20000, p % 2 ? 1.0f : -1.0f, 2)
                                : makeSine (30000, 40.0 + 300.0 * p, kEngineRate, 1.0f);
            auto st = defaults (*s);
            st.loop = p % 4 == 1;
            st.gate = p % 5 == 2;
            loadPad (e, { 0, p }, s, st);
        }
        e.post (cmd (Command::Type::setTargetAll, kNoPad, 1.0));
        e.post (cmd (Command::Type::setVolume, kNoPad, 1.0));

        std::mt19937 rng (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::vector<float> inL (128), inR (128), l (128), r (128);
        const float* in[2] = { inL.data(), inR.data() };
        float* o[2] = { l.data(), r.data() };
        bool safe = true;
        for (int b = 0; b < 48000 * 8 / 128; ++b)   // 8 s
        {
            for (size_t i = 0; i < 128; ++i)
                inL[i] = inR[i] = (u (rng) * 2.0f - 1.0f);   // full-scale noise input
            const float roll = u (rng);
            if (roll < 0.15f)
                e.post (cmd (Command::Type::padDown, { 0, (int) (u (rng) * 11.99f) }));
            else if (roll < 0.2f)
                e.post (cmd (Command::Type::padUp, { 0, (int) (u (rng) * 11.99f) }));
            else if (roll < 0.23f)
            {
                Command c = cmd (Command::Type::setEffect, kNoPad, (double) (int) (u (rng) * (fx::kNumEffects - 0.01f)));
                c.index = 0;
                e.post (c);
            }
            else if (roll < 0.3f)
            {
                Command c = cmd (Command::Type::setEffectParams);
                c.index = (int) (u (rng) * (fx::kNumEffects - 0.01f));
                c.params = { u (rng) < 0.5f ? 1.0f : u (rng), u (rng) < 0.5f ? 1.0f : u (rng), u (rng) < 0.5f ? 1.0f : u (rng), u (rng) };
                e.post (c);
            }
            else if (roll < 0.31f)
                e.post (cmd (Command::Type::setOutputGainDb, kNoPad, u (rng) * 12.0));
            else if (roll < 0.32f)
                e.post (cmd (Command::Type::setExtOn, kNoPad, u (rng) < 0.5f ? 1.0 : 0.0));
            else if (roll < 0.33f)
                e.post (cmd (Command::Type::setEffectLimit, kNoPad, u (rng) < 0.5f ? 1.0 : 0.0));

            host.process (in, 2, o, 2, 128);
            safe = safe && allSafe (l) && allSafe (r);
        }
        REQUIRE (safe);
    }
}

TEST_CASE ("Output guard does not allocate", "[safety][realtime]")
{
    OutputGuard g;
    g.prepare (48000.0, true);
    std::vector<float> l (256, 2.0f), r (256, -2.0f);
    float* ch[2] = { l.data(), r.data() };
    resetRealtimeAllocations();
    {
        rt::ScopedRealtime guard;
        for (int i = 0; i < 100; ++i)
            g.process (ch, 2, 256);
    }
    REQUIRE (realtimeAllocations() == 0);
}
