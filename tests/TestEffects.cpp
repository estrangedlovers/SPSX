#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include "AllocGuard.h"
#include "core/RealtimeGuard.h"
#include "core/fx/EffectHost.h"
#include "core/fx/EffectInfo.h"
#include "core/fx/Effects.h"
#include <cmath>
#include <random>

using namespace spsx;
using namespace spsx::fx;

namespace
{
    constexpr double kRate = 44100.0;

    /** 1 s of programme-like input: a swept sine, noise bursts, a kick-like thump and
        hard transients at full scale. */
    void makeInput (std::vector<float>& l, std::vector<float>& r)
    {
        const int n = (int) kRate;
        l.resize ((size_t) n);
        r.resize ((size_t) n);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / kRate;
            ph += 2.0 * 3.141592653589793 * (50.0 + 8000.0 * t * t) / kRate;
            float x = 0.6f * (float) std::sin (ph);
            if ((i / 4410) % 3 == 1) x += 0.4f * u (rng);
            if (i % 11025 < 400) x += 0.9f * (float) std::sin (2.0 * 3.141592653589793 * 55.0 * i / kRate);
            if (i % 9000 == 0) x = 1.0f;
            l[(size_t) i] = std::clamp (x, -1.0f, 1.0f);
            r[(size_t) i] = std::clamp (0.8f * x + 0.2f * u (rng), -1.0f, 1.0f);
        }
    }

    struct Stats { bool finite = true; float peak = 0.0f; bool denormal = false; };

    Stats run (Effect& fx, const Params& p, bool limit, std::vector<float> l, std::vector<float> r, int passes = 2)
    {
        fx.setParams (p, { kRate, 120.0, limit });
        fx.reset();
        Stats s;
        for (int pass = 0; pass < passes; ++pass)
        {
            auto bl = l, br = r;
            for (size_t i = 0; i < bl.size(); i += 32)
            {
                const int n = (int) std::min<size_t> (32, bl.size() - i);
                fx.setParams (p, { kRate, 120.0, limit });
                fx.process (bl.data() + i, br.data() + i, n);
            }
            for (size_t i = 0; i < bl.size(); ++i)
            {
                for (float v : { bl[i], br[i] })
                {
                    if (! std::isfinite (v)) s.finite = false;
                    else
                    {
                        s.peak = std::max (s.peak, std::abs (v));
                        if (v != 0.0f && std::abs (v) < 1e-30f) s.denormal = true;
                    }
                }
            }
        }
        return s;
    }
}

TEST_CASE ("Effect table: 29 effects, 17 with Effect Limit stars", "[effects][info]")
{
    int starred = 0;
    for (int e = 0; e < kNumEffects; ++e)
    {
        bool any = false;
        for (auto& p : info (e).params) any |= p.limited;
        starred += any ? 1 : 0;
        REQUIRE (create (e) != nullptr);
    }
    REQUIRE (starred == 17);
    REQUIRE (std::string (info (mfxIndex (1)).name) == "REVERB");
    REQUIRE (std::string (info (mfxIndex (24)).name) == "BPM LOOPER");
}

TEST_CASE ("Note values: DELAY can only reach values within ~2 s", "[effects][info]")
{
    // At 60 BPM a quarter is 1 s: t2 (2 s) is the longest allowed.
    REQUIRE (std::string (noteCodes()[(size_t) noteIndex (1.0f, 60.0, 2.0)]) == "t2");
    // At 200 BPM a whole note is 1.2 s: everything allowed.
    REQUIRE (std::string (noteCodes()[(size_t) noteIndex (1.0f, 200.0, 2.0)]) == "t1");
    REQUIRE (display (2, 0, 0.0f, 120.0).toString() == "t32");
    REQUIRE (display (22, 0, 1.0f, 120.0).toString() == "P16");
    REQUIRE (display (4, 1, 0.5f, 120.0).toString() == " 0.0");   // DJFX speed stop
    REQUIRE (display (4, 1, 0.0f, 120.0).toString() == "-1.0");
    REQUIRE (display (28, 1, 0.0f, 120.0).toString() == "oFF");
}

TEST_CASE ("Every effect stays finite and bounded at parameter extremes", "[effects][stability]")
{
    const int e = GENERATE (Catch::Generators::range (0, kNumEffects));
    const bool limit = GENERATE (false, true);
    INFO ("effect " << e << " " << info (e).name << " limit " << limit);

    auto fx = create (e);
    fx->prepare (kRate, 32);
    std::vector<float> l, r;
    makeInput (l, r);

    float worst = 0.0f;
    for (float a : { 0.0f, 0.5f, 1.0f })
        for (float b : { 0.0f, 0.5f, 1.0f })
            for (float c : { 0.0f, 0.5f, 1.0f })
            {
                const auto s = run (*fx, { a, b, c, 0.5f }, limit, l, r, 1);
                REQUIRE (s.finite);
                REQUIRE_FALSE (s.denormal);
                worst = std::max (worst, s.peak);
            }
    // Full-scale input: effects may add gain (EQ, drive LEVEL, feedback build-up), but
    // never run away. With Effect Limit on, the host's limiter holds output under 1.0;
    // here (without the host) each effect's own limits must keep it modest.
    REQUIRE (worst < (limit ? 8.0f : 16.0f));
}

TEST_CASE ("Silence in gives silence out (except noise generators)", "[effects][silence]")
{
    const int e = GENERATE (Catch::Generators::range (0, kNumEffects));
    INFO ("effect " << e << " " << info (e).name);
    auto fx = create (e);
    fx->prepare (kRate, 32);
    std::vector<float> l ((size_t) kRate, 0.0f), r ((size_t) kRate, 0.0f);
    const auto s = run (*fx, info (e).defaults, false, l, r, 1);
    REQUIRE (s.finite);
    if (info (e).generatesNoise)
        REQUIRE (s.peak > 1e-4f);
    else
        REQUIRE (s.peak < 1e-6f);
}

TEST_CASE ("Effect host: Effect Limit keeps the output below full scale", "[effects][host]")
{
    EffectHost host;
    host.prepare (kRate, 512, 1);
    host.setLimit (true);
    std::vector<float> l, r;
    makeInput (l, r);
    for (int e : { 0, 2, 6, 11, 13, 16, 17, 25 })
    {
        host.select (0, e);
        host.setParams (e, { 1.0f, 1.0f, 1.0f, 1.0f });
        auto bl = l, br = r;
        for (auto& x : bl) x *= 2.0f;   // hot input
        host.process (bl.data(), br.data(), (int) bl.size(), 120.0);
        float pk = 0.0f;
        for (size_t i = 0; i < bl.size(); ++i)
            pk = std::max ({ pk, std::abs (bl[i]), std::abs (br[i]) });
        INFO ("effect " << e);
        REQUIRE (pk <= 1.0f);
    }
}

TEST_CASE ("Effect host: switching effects never clicks", "[effects][host]")
{
    // A steady sine through: off -> DELAY -> ISOLATOR -> off. The largest sample-to-
    // sample step must stay close to the sine's own maximum step.
    EffectHost host;
    host.prepare (kRate, 512, 1);
    const int n = 44100;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / kRate);
    const float sineStep = 0.5f * 2.0f * 3.14159f * 220.0f / (float) kRate;

    float maxStep = 0.0f, prev = 0.0f;
    for (int block = 0; block < n / 256; ++block)
    {
        if (block == 20) host.select (0, 3);   // ISOLATOR
        if (block == 60) host.select (0, 17);  // EQUALIZER
        if (block == 100) host.select (0, -1); // off
        float* bl = l.data() + block * 256;
        float* br = r.data() + block * 256;
        host.process (bl, br, 256, 120.0);
        for (int i = 0; i < 256; ++i)
        {
            maxStep = std::max (maxStep, std::abs (bl[i] - prev));
            prev = bl[i];
        }
    }
    REQUIRE (maxStep < sineStep * 2.5f);
}

TEST_CASE ("Effect host processing does not allocate", "[effects][realtime]")
{
    EffectHost host;
    host.prepare (kRate, 512, 2);
    std::vector<float> l (4096, 0.1f), r (4096, -0.1f);
    test::resetRealtimeAllocations();
    {
        rt::ScopedRealtime guard;
        for (int e = -1; e < kNumEffects; ++e)
        {
            host.select (0, e);
            host.setParams (std::max (e, 0), { 0.7f, 0.7f, 1.0f, 0.5f });
            host.process (l.data(), r.data(), 4096, 128.0);
        }
    }
    REQUIRE (test::realtimeAllocations() == 0);
}

TEST_CASE ("DELAY repeats land on the synced note value", "[effects][sync]")
{
    auto fx = create (2);
    fx->prepare (kRate, 32);
    // t4 at 120 BPM = 0.5 s. Knob for t4: index 7 of the values reachable within 2 s.
    const int reachable = 13;   // at 120 BPM every value up to t1 (2 s) fits
    const float knob = 7.0f / (float) (reachable - 1);
    REQUIRE (std::string (noteCodes()[(size_t) noteIndex (knob, 120.0, 2.0)]) == "t4");
    std::vector<float> l (44100, 0.0f), r (44100, 0.0f);
    l[0] = r[0] = 1.0f;
    fx->setParams ({ knob, 0.0f, 1.0f, 0.5f }, { kRate, 120.0, false });
    fx->reset();
    for (size_t i = 0; i < l.size(); i += 32)
    {
        fx->setParams ({ knob, 0.0f, 1.0f, 0.5f }, { kRate, 120.0, false });
        fx->process (l.data() + i, r.data() + i, (int) std::min<size_t> (32, l.size() - i));
    }
    size_t peakAt = 1;
    for (size_t i = 1; i < l.size(); ++i)
        if (std::abs (l[i]) > std::abs (l[peakAt])) peakAt = i;
    REQUIRE (std::abs ((long) peakAt - 22050) <= 2);
}
