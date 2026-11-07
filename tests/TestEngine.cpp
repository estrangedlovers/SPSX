#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"

using namespace spsx;
using namespace spsx::test;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr float kUnit = 1.0f / 1048576.0f;   // makeRamp step

    struct Fixture
    {
        Engine e;
        PadId a1 { 0, 0 };

        Fixture (Limits l = Limits::authentic()) { e.prepare (kEngineRate, 64, l); }

        PadSettings load (PadId p, SampleData::Ptr s, std::function<void (PadSettings&)> edit = {})
        {
            auto st = defaults (*s);
            if (edit) edit (st);
            loadPad (e, p, s, st);
            return st;
        }

        void post (Command::Type t, PadId p = kNoPad, double v = 0.0) { e.post (cmd (t, p, v)); }
    };
}

TEST_CASE ("One-shot forward plays the region exactly once", "[engine][voice]")
{
    Fixture f;
    f.load (f.a1, makeRamp (1000));
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 1200);

    for (int i = 0; i < 1000; ++i)
        REQUIRE (out.l[(size_t) i] == (float) (i + 1) * kUnit);
    REQUIRE (peakOf (out.l, 1000) == 0.0f);
    REQUIRE (out.r[10] == out.l[10]);   // mono feeds both sides
}

TEST_CASE ("Reverse plays backwards from the end point", "[engine][voice]")
{
    Fixture f;
    f.load (f.a1, makeRamp (1000), [] (PadSettings& s) { s.reverse = true; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 1100);
    for (int i = 0; i < 1000; ++i)
        REQUIRE (out.l[(size_t) i] == (float) (1000 - i) * kUnit);
    REQUIRE (peakOf (out.l, 1000) == 0.0f);
}

TEST_CASE ("Start and end points bound playback", "[engine][voice]")
{
    Fixture f;
    f.load (f.a1, makeRamp (1000), [] (PadSettings& s) { s.start = 100; s.end = 300; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 400);
    for (int i = 0; i < 200; ++i)
        REQUIRE (out.l[(size_t) i] == (float) (101 + i) * kUnit);
    REQUIRE (peakOf (out.l, 200) == 0.0f);
}

TEST_CASE ("Loop repeats the region seamlessly", "[engine][voice]")
{
    Fixture f;
    f.load (f.a1, makeRamp (100), [] (PadSettings& s) { s.loop = true; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 1000);
    for (int i = 0; i < 1000; ++i)
        REQUIRE (out.l[(size_t) i] == (float) (i % 100 + 1) * kUnit);
}

TEST_CASE ("Reverse loop wraps from start back to end", "[engine][voice]")
{
    Fixture f;
    f.load (f.a1, makeRamp (100), [] (PadSettings& s) { s.loop = true; s.reverse = true; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 300);
    for (int i = 0; i < 300; ++i)
        REQUIRE (out.l[(size_t) i] == (float) (100 - i % 100) * kUnit);
}

TEST_CASE ("GATE: sound stops shortly after the pad is released", "[engine][gate]")
{
    Fixture f;
    f.load (f.a1, makeConstant (44100, 0.5f), [] (PadSettings& s) { s.gate = true; });
    f.post (Command::Type::padDown, f.a1);
    auto held = render (f.e, 512);
    REQUIRE (held.l[500] == 0.5f);

    f.post (Command::Type::padUp, f.a1);
    auto after = render (f.e, 512);
    REQUIRE (after.l[0] < 0.5f);                  // fading
    REQUIRE (peakOf (after.l, 100) == 0.0f);      // 2 ms fade finished
}

TEST_CASE ("Trigger mode (GATE off): release keeps playing, next press stops", "[engine][gate]")
{
    Fixture f;
    f.load (f.a1, makeConstant (44100, 0.5f), [] (PadSettings& s) { s.loop = true; });
    f.post (Command::Type::padDown, f.a1);
    f.post (Command::Type::padUp, f.a1);
    auto a = render (f.e, 1024);
    REQUIRE (a.l[1000] == 0.5f);

    f.post (Command::Type::padDown, f.a1);
    auto b = render (f.e, 512);
    REQUIRE (peakOf (b.l, 100) == 0.0f);
}

TEST_CASE ("HOLD latches a held GATE pad until the pad is pressed again", "[engine][hold]")
{
    Fixture f;
    f.load (f.a1, makeConstant (44100, 0.5f), [] (PadSettings& s) { s.gate = true; s.loop = true; });
    f.post (Command::Type::padDown, f.a1);
    f.post (Command::Type::holdDown);
    f.post (Command::Type::padUp, f.a1);
    auto a = render (f.e, 2048);
    REQUIRE (a.l[2000] == 0.5f);

    f.post (Command::Type::padDown, f.a1);
    auto b = render (f.e, 512);
    REQUIRE (peakOf (b.l, 100) == 0.0f);
}

TEST_CASE ("SUB PAD retriggers the most recently struck pad", "[engine][sub]")
{
    Fixture f;
    f.load (f.a1, makeRamp (2000));
    f.post (Command::Type::padDown, f.a1);
    f.post (Command::Type::padUp, f.a1);
    render (f.e, 500);

    f.post (Command::Type::subDown);
    auto out = render (f.e, 1000);
    // Retrigger: old voice fades while the new one restarts at the beginning.
    REQUIRE (out.l[200] == (float) (201) * kUnit);
    f.post (Command::Type::subUp);
    REQUIRE (f.e.playPosition (f.a1) >= 0);
}

TEST_CASE ("Pads without a sample are silent", "[engine]")
{
    Fixture f;
    f.post (Command::Type::padDown, { 3, 7 });
    auto out = render (f.e, 256);
    REQUIRE (peakOf (out.l) == 0.0f);
}

TEST_CASE ("LEVEL scales linearly, 127 is unity", "[engine][level]")
{
    Fixture f;
    f.load (f.a1, makeConstant (1000, 0.5f), [] (PadSettings& s) { s.level = 64; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 100);
    REQUIRE_THAT (out.l[50], WithinAbs (0.5 * 64.0 / 127.0, 1e-6));
}

TEST_CASE ("Polyphony: 12 mono channel-voices, oldest stolen", "[engine][voices]")
{
    Fixture f;
    for (int i = 0; i < 12; ++i)
        f.load ({ 0, i }, makeConstant (44100, 0.01f));
    f.load ({ 1, 0 }, makeConstant (44100, 0.01f));
    render (f.e, 64);

    for (int i = 0; i < 12; ++i)
    {
        f.post (Command::Type::padDown, { 0, i });
        render (f.e, 64);
    }
    REQUIRE (f.e.activeChannelVoices() == 12);

    f.post (Command::Type::padDown, { 1, 0 });
    render (f.e, 512);
    REQUIRE (f.e.activeChannelVoices() == 12);
    REQUIRE (f.e.playPosition ({ 0, 0 }) == -1);   // oldest was stolen
    REQUIRE (f.e.playPosition ({ 0, 1 }) >= 0);
    REQUIRE (f.e.playPosition ({ 1, 0 }) >= 0);
}

TEST_CASE ("Polyphony: stereo samples cost two channel-voices (6 stereo max)", "[engine][voices]")
{
    Fixture f;
    for (int i = 0; i < 7; ++i)
        f.load ({ 0, i }, makeConstant (44100, 0.01f, 2));
    render (f.e, 64);
    for (int i = 0; i < 7; ++i)
    {
        f.post (Command::Type::padDown, { 0, i });
        render (f.e, 64);
    }
    REQUIRE (f.e.activeChannelVoices() == 12);
    REQUIRE (f.e.playPosition ({ 0, 0 }) == -1);
    REQUIRE (f.e.playPosition ({ 0, 6 }) >= 0);
}

TEST_CASE ("Resample budget: 4 mono channel-voices", "[engine][voices]")
{
    Fixture f;
    f.post (Command::Type::setVoiceBudget, kNoPad, 4);
    for (int i = 0; i < 5; ++i)
        f.load ({ 0, i }, makeConstant (44100, 0.01f));
    render (f.e, 64);
    for (int i = 0; i < 5; ++i)
        f.post (Command::Type::padDown, { 0, i });
    render (f.e, 512);
    REQUIRE (f.e.activeChannelVoices() == 4);
}

TEST_CASE ("PAUSE freezes and resumes where it stopped", "[engine][pause]")
{
    Fixture f;
    f.load (f.a1, makeRamp (10000));
    f.post (Command::Type::padDown, f.a1);
    auto a = render (f.e, 640);
    f.post (Command::Type::pause);
    auto b = render (f.e, 640);
    REQUIRE (peakOf (b.l) == 0.0f);
    REQUIRE (f.e.isPaused());

    f.post (Command::Type::padDown, { 0, 1 });   // ignored while paused
    f.post (Command::Type::resume);
    auto c = render (f.e, 64);
    REQUIRE (c.l[0] == (float) (641) * kUnit);
}

TEST_CASE ("Stop all (CANCEL x4 / All Sound Off) silences every voice", "[engine]")
{
    Fixture f;
    for (int i = 0; i < 4; ++i)
        f.load ({ 0, i }, makeConstant (44100, 0.1f), [] (PadSettings& s) { s.loop = true; });
    for (int i = 0; i < 4; ++i)
        f.post (Command::Type::padDown, { 0, i });
    render (f.e, 256);
    f.post (Command::Type::stopAll);
    auto out = render (f.e, 512);
    REQUIRE (peakOf (out.l, 100) == 0.0f);
}

TEST_CASE ("Timed commands land on their exact sample", "[engine][timing]")
{
    Fixture f;
    f.load (f.a1, makeRamp (1000));
    render (f.e, 64);   // apply load; engine time is now 64

    auto c = cmd (Command::Type::padDown, f.a1);
    c.time = f.e.now() + 37;
    f.e.schedule (c);
    auto out = render (f.e, 128);
    REQUIRE (out.l[36] == 0.0f);
    REQUIRE (out.l[37] == kUnit);
    REQUIRE (out.l[38] == 2.0f * kUnit);
}

TEST_CASE ("Output gain feeds a hard clip in Authentic mode", "[engine][mixer]")
{
    Fixture f;
    f.load (f.a1, makeConstant (1000, 0.5f));
    f.post (Command::Type::setOutputGainDb, kNoPad, 12.0);
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 64);
    REQUIRE (out.l[10] == 1.0f);

    Fixture g (Limits::extended());
    g.load (g.a1, makeConstant (1000, 0.5f));
    g.post (Command::Type::setOutputGainDb, kNoPad, 12.0);
    g.post (Command::Type::padDown, g.a1);
    auto o2 = render (g.e, 64);
    REQUIRE (o2.l[10] > 1.9f);
}

TEST_CASE ("LO-FI requantizes and holds samples", "[engine][lofi]")
{
    Fixture f;
    f.load (f.a1, makeConstant (44100, 0.3f), [] (PadSettings& s) { s.lofi = true; });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 4096);
    // 10-bit mid-tread quantizer: 0.3 -> round(0.3 * 512) / 512
    REQUIRE_THAT (out.l[4000], WithinAbs (154.0 / 512.0, 1e-4));

    auto dry = makeRamp (4096);
    Fixture g;
    g.load (g.a1, dry, [] (PadSettings& s) { s.lofi = true; });
    g.post (Command::Type::padDown, g.a1);
    auto o = render (g.e, 2048);
    REQUIRE (o.l[1000] != (float) 1001 * kUnit);
}

TEST_CASE ("Replaced samples are released only after voices stop using them", "[engine][lifetime]")
{
    Fixture f;
    SampleData::Ptr first = makeConstant (44100, 0.2f);
    f.load (f.a1, first, [] (PadSettings& s) { s.loop = true; });
    f.post (Command::Type::padDown, f.a1);
    render (f.e, 256);
    REQUIRE (first->getReferenceCount() == 2);   // test + engine

    f.e.postSample (f.a1, makeConstant (100, 0.1f));
    render (f.e, 64);                            // swap; old voice still fading
    f.e.collectGarbage();
    render (f.e, 256);                           // fade over, retired sample flushed
    f.e.collectGarbage();
    REQUIRE (first->getReferenceCount() == 1);
}

TEST_CASE ("Engine::process does not allocate", "[engine][realtime]")
{
    Fixture f;
    for (int i = 0; i < 12; ++i)
        f.load ({ 0, i }, makeConstant (44100, 0.01f, i % 2 + 1), [i] (PadSettings& s) { s.lofi = i % 3 == 0; s.loop = true; });
    render (f.e, 64);

    resetRealtimeAllocations();
    for (int i = 0; i < 12; ++i)
        f.post (Command::Type::padDown, { 0, i });
    f.e.postSample ({ 0, 3 }, makeConstant (100, 0.1f));
    render (f.e, 44100, 61);
    f.post (Command::Type::stopAll);
    render (f.e, 1000);
    REQUIRE (realtimeAllocations() == 0);
}

TEST_CASE ("Time Modify (Authentic granular) shortens without changing pitch", "[engine][timemodify]")
{
    Fixture f;
    const int len = 88200;   // 2 s
    auto s = makeSine (len, 1000.0);
    const auto st = f.load (f.a1, s, [] (PadSettings& p)
    {
        p.timeModify = TimeModifyMode::bpm;
        p.timeModifyBpm = p.bpm * 1.25;
    });
    REQUIRE (st.bpm == 120.0);

    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 3 * 44100, 256);

    int last = 0;
    for (int i = 0; i < (int) out.l.size(); ++i)
        if (std::abs (out.l[(size_t) i]) > 1e-4f)
            last = i;
    REQUIRE (std::abs (last - (int) (len / 1.25)) < 2 * dsp::GranularStretcher::kGrain);

    // Pitch: count rising zero crossings over 0.5 s in the steady part.
    int crossings = 0;
    for (int i = 10000; i < 10000 + 22050; ++i)
        if (out.l[(size_t) i - 1] < 0.0f && out.l[(size_t) i] >= 0.0f)
            ++crossings;
    REQUIRE (std::abs (crossings - 500) <= 15);
}

TEST_CASE ("Time Modify is ignored while REVERSE is on", "[engine][timemodify]")
{
    Fixture f;
    f.load (f.a1, makeRamp (1000), [] (PadSettings& p)
    {
        p.reverse = true;
        p.timeModify = TimeModifyMode::bpm;
        p.timeModifyBpm = p.bpm * 1.25;
    });
    f.post (Command::Type::padDown, f.a1);
    auto out = render (f.e, 1100);
    REQUIRE (out.l[0] == 1000.0f * kUnit);
    REQUIRE (out.l[999] == kUnit);
}
