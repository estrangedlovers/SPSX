#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "core/sampler/TimeModify.h"
#include "core/Tempo.h"

using namespace spsx;
using namespace spsx::edit;

TEST_CASE ("BPM derivation picks a power-of-two beat count in 80..160", "[edit][bpm]")
{
    REQUIRE (deriveBpm (88200, kEngineRate) == 120.0);       // 2 s = 4 beats at 120
    REQUIRE (deriveBpm (11025, kEngineRate) == 120.0);       // 0.25 s = half a beat at 120
    REQUIRE (deriveBpm (44100 * 3, kEngineRate) == 80.0);    // 3 s: 4 beats at 80
    REQUIRE (deriveBpm (0, kEngineRate) == 0.0);
}

TEST_CASE ("BPM halve/double stay inside 40..200", "[edit][bpm]")
{
    REQUIRE (halveBpm (120.0) == 60.0);
    REQUIRE (doubleBpm (90.0) == 180.0);
    REQUIRE (halveBpm (70.0) == 70.0);      // 35 would be out of range
    REQUIRE (doubleBpm (120.0) == 120.0);   // 240 out of range
}

TEST_CASE ("Fine adjust: +-130 ms per operation, centre cancels, 100 ms minimum", "[edit][fine]")
{
    const SampleIndex len = 44100 * 2;
    PadSettings base;
    setRegion (base, 10000, 60000, len);
    FineAdjust fa (base, len, kEngineRate);

    REQUIRE (fa.apply (0.0, 0.0) == base);

    auto right = fa.apply (1.0, 0.0);
    REQUIRE (right.start == 10000 + 5733);   // 0.130 s * 44100
    REQUIRE (right.end == 60000);
    REQUIRE (right.bpm == deriveBpm (60000 - 15733, kEngineRate));

    auto left = fa.apply (-1.0, 0.0);
    REQUIRE (left.start == 10000 - 5733);

    // Cannot pass the sample's beginning.
    PadSettings nearStart;
    setRegion (nearStart, 1000, 60000, len);
    REQUIRE (FineAdjust (nearStart, len, kEngineRate).apply (-1.0, 0.0).start == 0);

    // Minimum region of ~100 ms is enforced.
    PadSettings tight;
    setRegion (tight, 10000, 16000, len);
    auto t = FineAdjust (tight, len, kEngineRate).apply (1.0, 0.0);
    REQUIRE (t.end - t.start == 4410);
}

TEST_CASE ("MARK: start then end", "[edit][mark]")
{
    const SampleIndex len = 100000;
    PadSettings base;
    setRegion (base, 0, len, len);
    MarkSession m (MarkSession::Mode::startAndEnd, base, len, kEngineRate);
    REQUIRE_FALSE (m.markPressed (12000).has_value());
    auto r = m.markPressed (60000);
    REQUIRE (r.has_value());
    REQUIRE (r->start == 12000);
    REQUIRE (r->end == 60000);
    REQUIRE (r->isMarked (len));
}

TEST_CASE ("MARK: end only (hold MARK + pad)", "[edit][mark]")
{
    const SampleIndex len = 100000;
    PadSettings base;
    setRegion (base, 5000, len, len);
    MarkSession m (MarkSession::Mode::endOnly, base, len, kEngineRate);
    auto r = m.markPressed (70000);
    REQUIRE (r->start == 5000);
    REQUIRE (r->end == 70000);
}

TEST_CASE ("MARK: start only completes when the pad stops", "[edit][mark]")
{
    const SampleIndex len = 100000;
    PadSettings base;
    setRegion (base, 0, len, len);
    MarkSession m (MarkSession::Mode::startOnly, base, len, kEngineRate);
    REQUIRE_FALSE (m.markPressed (30000).has_value());
    auto r = m.padStopped();
    REQUIRE (r->start == 30000);
    REQUIRE (r->end == len);
}

TEST_CASE ("MARK: BPM entered between presses snaps the end to a beat", "[edit][mark]")
{
    const SampleIndex len = 44100 * 10;
    PadSettings base;
    setRegion (base, 0, len, len);
    MarkSession m (MarkSession::Mode::startAndEnd, base, len, kEngineRate);
    m.markPressed (1000);
    m.setSnapBpm (120.0);                          // beat = 22050 samples
    auto r = m.markPressed (1000 + 4 * 22050 + 3000);
    REQUIRE (r->end == 1000 + 4 * 22050);
    REQUIRE (r->bpm == 120.0);
}

TEST_CASE ("Truncate keeps only the region (destructive)", "[edit][truncate]")
{
    auto s = test::makeRamp (1000);
    PadSettings st;
    setRegion (st, 200, 700, 1000);
    auto t = truncate (*s, st);
    REQUIRE (t->length() == 500);
    REQUIRE (t->channel (0)[0] == s->channel (0)[200]);
    REQUIRE (t->channel (0)[499] == s->channel (0)[699]);
    REQUIRE (st.start == 0);
    REQUIRE (st.end == 500);
    REQUIRE_FALSE (st.isMarked (t->length()));
}

TEST_CASE ("Time Modify knob: 0 = oFF, 127 = Ptn, range 0.5x..1.3x within 40..200", "[edit][timemodify]")
{
    PadSettings s;
    s.bpm = 100.0;
    timemodify::applyKnob (s, 0);
    REQUIRE (s.timeModify == TimeModifyMode::off);
    timemodify::applyKnob (s, 127);
    REQUIRE (s.timeModify == TimeModifyMode::pattern);
    timemodify::applyKnob (s, 1);
    REQUIRE (s.timeModifyBpm == 50.0);
    timemodify::applyKnob (s, 126);
    REQUIRE (s.timeModifyBpm == 130.0);

    s.bpm = 180.0;   // 1.3x would be 234: capped at 200
    timemodify::applyKnob (s, 126);
    REQUIRE (s.timeModifyBpm == 200.0);
    s.bpm = 60.0;    // 0.5x would be 30: floored at 40
    timemodify::applyKnob (s, 1);
    REQUIRE (s.timeModifyBpm == 40.0);
}

TEST_CASE ("Time Modify Ptn follows the pattern tempo, clamped to the allowed range", "[edit][timemodify]")
{
    PadSettings s;
    s.bpm = 100.0;
    s.timeModify = TimeModifyMode::pattern;
    REQUIRE (timemodify::sourceSpeed (s, 120.0) == 1.2);
    REQUIRE (timemodify::sourceSpeed (s, 190.0) == 1.3);
    REQUIRE (timemodify::sourceSpeed (s, 40.0) == 0.5);
    s.reverse = true;
    REQUIRE (timemodify::sourceSpeed (s, 120.0) == 1.0);
}

TEST_CASE ("Tempo table matches the manual", "[tempo]")
{
    const auto& t = tempo::table();
    REQUIRE (t.size() == 127);
    REQUIRE (t.front() == 40);
    REQUIRE (t.back() == 200);
    auto has = [&t] (int v) { return std::find (t.begin(), t.end(), v) != t.end(); };
    REQUIRE (has (42));
    REQUIRE_FALSE (has (41));
    REQUIRE (has (60));
    REQUIRE (has (61));
    REQUIRE (has (159));
    REQUIRE (has (160));
    REQUIRE_FALSE (has (161));
    REQUIRE (has (180));
    REQUIRE_FALSE (has (181));
    REQUIRE (has (183));
    REQUIRE (has (190));
    REQUIRE_FALSE (has (199));
    REQUIRE (tempo::snap (181.0) == 180);
    REQUIRE (tempo::snap (199.0) == 200);
    REQUIRE (tempo::snap (197.0) == 196);
    REQUIRE (tempo::fromIndex (tempo::indexOf (183)) == 183);
}
