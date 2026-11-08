// CPU benchmark. Build in Release and run: spsx_bench [seconds]
// Reports each effect alone, then the worst case the spec asks for: 12 voices playing,
// the effect active on all of them, a dense pattern running, through EngineHost at
// 44.1 kHz (bypass) and 48 kHz (sample-rate conversion).

#include "core/engine/Engine.h"
#include "core/engine/EngineHost.h"
#include "core/fx/EffectInfo.h"
#include "core/fx/Effects.h"
#include "core/sampler/SampleEdit.h"
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace spsx;

namespace
{
    double nowSec()
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    SampleData::Ptr tone (int len, double hz, int channels)
    {
        juce::AudioBuffer<float> b (channels, len);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < len; ++i)
                b.setSample (ch, i, 0.2f * (float) std::sin (2.0 * 3.14159265 * hz * (i + ch * 17) / kEngineRate));
        return new SampleData (std::move (b), true);
    }

    /** Percent of real time used rendering `seconds` of audio. */
    double runEngine (double deviceRate, int block, int effect, double seconds)
    {
        Engine e;
        EngineHost host (e);
        host.prepare (deviceRate, block, Limits::authentic());
        for (int p = 0; p < 12; ++p)
        {
            auto s = tone (44100 * 4, 110.0 * (p + 1), 1);   // 12 mono voices = the full budget
            auto st = edit::defaultsFor (*s);
            st.loop = true;
            e.postSettings ({ 0, p }, st);
            e.postSample ({ 0, p }, s);
        }
        // Dense pattern: a 16th-note hit on every pad pair, 1 bar, looping.
        std::vector<SeqEvent> ev;
        for (int t = 0; t < kTicksPerBar; t += 24)
        {
            SeqEvent x;
            x.tick = t;
            x.pad = (std::int16_t) (12 + (t / 24) % 12);    // bank B so the 12 looping voices keep sounding
            x.length = 12;
            ev.push_back (x);
        }
        for (int p = 0; p < 12; ++p)
        {
            auto s = tone (2000, 880.0, 1);
            e.postSettings ({ 1, p }, edit::defaultsFor (*s));
            e.postSample ({ 1, p }, s);
        }
        e.postPattern (0, new PatternData (1, 140.0, ev));

        Command c;
        c.type = Command::Type::setTargetAll;
        c.value = 1.0;
        e.post (c);
        c = {};
        c.type = Command::Type::setEffect;
        c.value = effect;
        e.post (c);
        c = {};
        c.type = Command::Type::setEffectParams;
        c.index = std::max (effect, 0);
        c.params = { 0.7f, 0.7f, 0.8f, 0.5f };
        e.post (c);
        c = {};
        c.type = Command::Type::seqPlay;
        c.index = 0;
        e.post (c);

        std::vector<float> l ((size_t) block), r ((size_t) block), il ((size_t) block, 0.01f), ir ((size_t) block, 0.01f);
        float* out[2] = { l.data(), r.data() };
        const float* in[2] = { il.data(), ir.data() };
        // Start all 12 looping voices.
        for (int p = 0; p < 12; ++p)
        {
            Command d;
            d.type = Command::Type::padDown;
            d.pad = { 0, p };
            e.post (d);
        }
        host.process (in, 2, out, 2, block);

        const int blocks = (int) (seconds * deviceRate / block);
        const double t0 = nowSec();
        for (int b = 0; b < blocks; ++b)
            host.process (in, 2, out, 2, block);
        const double t1 = nowSec();
        return 100.0 * (t1 - t0) / ((double) blocks * block / deviceRate);
    }

    double runEffect (int effect, double seconds)
    {
        auto fx = fx::create (effect);
        fx->prepare (44100.0, 32);
        fx->setParams ({ 0.7f, 0.7f, 0.8f, 0.5f }, { 44100.0, 120.0, false });
        std::vector<float> l (32), r (32);
        const int blocks = (int) (seconds * 44100.0 / 32);
        double ph = 0.0;
        const double t0 = nowSec();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 32; ++i) { l[(size_t) i] = 0.3f * (float) std::sin (ph); r[(size_t) i] = l[(size_t) i]; ph += 0.05; }
            fx->setParams ({ 0.7f, 0.7f, 0.8f, 0.5f }, { 44100.0, 120.0, false });
            fx->process (l.data(), r.data(), 32);
        }
        return 100.0 * (nowSec() - t0) / seconds;
    }
}

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 5.0;
    std::printf ("Effect alone (%% of one core, real time at 44.1 kHz):\n");
    for (int e = 0; e < fx::kNumEffects; ++e)
        std::printf ("  %2d %-14s %6.2f %%\n", e, fx::info (e).name, runEffect (e, seconds));

    std::printf ("\nWorst case: 12 voices + effect on all + dense pattern (%% of one core):\n");
    std::printf ("  %-14s %10s %10s\n", "effect", "44.1k/256", "48k/128");
    double worst44 = 0.0, worst48 = 0.0;
    for (int e = -1; e < fx::kNumEffects; ++e)
    {
        const double a = runEngine (44100.0, 256, e, seconds);
        const double b = runEngine (48000.0, 128, e, seconds);
        worst44 = std::max (worst44, a);
        worst48 = std::max (worst48, b);
        std::printf ("  %-14s %9.2f%% %9.2f%%\n", e < 0 ? "(no effect)" : fx::info (e).name, a, b);
    }
    std::printf ("\nWorst: %.2f %% at 44.1 kHz, %.2f %% at 48 kHz (with sample-rate conversion)\n", worst44, worst48);
    return 0;
}
