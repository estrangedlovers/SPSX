#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"
#include "core/ui_model/PanelModel.h"

using namespace spsx;
using namespace spsx::test;

namespace
{
    constexpr int kBeat = 22050;          // samples per beat at 120 BPM, 44.1 kHz
    constexpr int kBar = 4 * kBeat;

    SeqEvent ev (int tick, int pad, int length = 24)
    {
        SeqEvent e;
        e.tick = tick;
        e.pad = (std::int16_t) pad;
        e.length = length;
        return e;
    }

    struct SeqRig
    {
        Engine e;
        explicit SeqRig (int maxBlock = 256)
        {
            e.prepare (kEngineRate, maxBlock, Limits::authentic());
            // Pads A1..A4: short constant tones, each a different level so the output tells
            // which pad sounded.
            for (int p = 0; p < 4; ++p)
            {
                auto s = makeConstant (400, 0.1f * (float) (p + 1));
                loadPad (e, { 0, p }, s, defaults (*s));
            }
            e.post (cmd (Command::Type::setPatternTempo, kNoPad, 120.0));
            render (e, 256, 256);
        }

        void post (Command::Type t, int index = 0, double value = 0.0, PadId pad = kNoPad)
        {
            Command c;
            c.type = t;
            c.index = index;
            c.value = value;
            c.pad = pad;
            e.post (c);
        }

        void setPattern (int index, int bars, std::vector<SeqEvent> events, double tempo = 120.0)
        {
            e.postPattern (index, new PatternData (bars, tempo, std::move (events)));
        }

        /** Note onsets: first sample above a small threshold after at least 50 ms of
            quiet (so oscillating sounds such as the metronome click count once). */
        static std::vector<long> onsets (const std::vector<float>& x)
        {
            std::vector<long> o;
            long lastLoud = -100000;
            for (size_t i = 0; i < x.size(); ++i)
            {
                if (std::abs (x[i]) <= 1e-3f) continue;
                if ((long) i - lastLoud > 2205)
                    o.push_back ((long) i);
                lastLoud = (long) i;
            }
            return o;
        }
    };
}

// ---- quantize ----------------------------------------------------------------------------

TEST_CASE ("Quantize grids match the manual's settings", "[sequencer][quantize]")
{
    const int len = 4 * kTicksPerBar;
    // index: S.8 S.16 4 4.3 8 8.3 16 16.3 32 oFF
    REQUIRE (Quantize::apply (100.0, 2, 50, len) == 96);     // quarter
    REQUIRE (Quantize::apply (100.0, 3, 50, len) == 128);    // quarter triplet (64)
    REQUIRE (Quantize::apply (30.0, 4, 50, len) == 48);      // 8th
    REQUIRE (Quantize::apply (40.0, 5, 50, len) == 32);      // 8th triplet
    REQUIRE (Quantize::apply (30.0, 6, 50, len) == 24);      // 16th
    REQUIRE (Quantize::apply (20.0, 7, 50, len) == 16);      // 16th triplet
    REQUIRE (Quantize::apply (17.0, 8, 50, len) == 12);      // 32nd
    REQUIRE (Quantize::apply (17.7, 9, 50, len) == 17);      // off: raw tick
    REQUIRE (Quantize::apply (1530.0, 2, 50, len) == 0);     // rounds past the end -> wraps to the top
    REQUIRE (Quantize::apply (-3.0, 9, 50, len) == len - 3);
}

TEST_CASE ("Shuffle delays every second step; 50 is straight", "[sequencer][quantize]")
{
    const int len = kTicksPerBar;
    REQUIRE (Quantize::apply (24.0, 1, 50, len) == 24);      // S.16 straight
    REQUIRE (Quantize::apply (24.0, 1, 66, len) == 32);      // 66 %: offbeat at 2/3 of the pair (24 + 7.68)
    REQUIRE (Quantize::apply (48.0, 1, 66, len) == 48);      // on-beats unchanged
    REQUIRE (Quantize::apply (48.0, 0, 75, len) == 72);      // S.8 at 75 %: 48 + 24
}

// ---- playback ----------------------------------------------------------------------------

TEST_CASE ("Pattern playback is sample-accurate and loops", "[sequencer][playback]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0), ev (96, 1), ev (288, 2) });
    r.post (Command::Type::seqPlay, 0);
    const auto out = render (r.e, kBar + kBeat + 100, 256);
    const auto o = SeqRig::onsets (out.l);
    REQUIRE (o.size() >= 4);
    REQUIRE (o[0] == 0);
    REQUIRE (o[1] == kBeat);
    REQUIRE (o[2] == 3 * kBeat);
    REQUIRE (o[3] == kBar);                  // looped to the top
    REQUIRE (out.l[(size_t) kBeat + 10] == 0.2f);   // pad A2 played the second hit
}

TEST_CASE ("A pattern runs at its own stored tempo", "[sequencer][playback]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0), ev (96, 0) }, 60.0);   // one beat = 1 s
    r.post (Command::Type::seqPlay, 0);
    const auto o = SeqRig::onsets (render (r.e, 50000, 256).l);
    REQUIRE (o.size() >= 2);
    REQUIRE (o[1] == 44100);
}

TEST_CASE ("Reserved pattern starts when the current one ends; SUB PAD switch is immediate", "[sequencer][playback]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0) });
    r.setPattern (1, 1, { ev (0, 1), ev (192, 1) });
    r.post (Command::Type::seqPlay, 0);
    render (r.e, kBeat, 256);
    r.post (Command::Type::seqReserve, 1);
    auto out = render (r.e, kBar, 256);   // to just past the end of bar 1
    REQUIRE (r.e.sequencer().playingIndex() == 1);
    const auto o = SeqRig::onsets (out.l);
    REQUIRE (! o.empty());
    REQUIRE (o[0] == kBar - kBeat);       // B's first hit exactly at the bar line
    REQUIRE (out.l[(size_t) o[0] + 5] == 0.2f);

    // Immediate: play pattern 0 now, mid-bar.
    r.post (Command::Type::seqPlay, 0);
    out = render (r.e, 300, 256);
    REQUIRE (r.e.sequencer().playingIndex() == 0);
    REQUIRE (out.l[10] == 0.1f);
}

TEST_CASE ("Stop ends playback", "[sequencer][playback]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0), ev (96, 0), ev (192, 0) });
    r.post (Command::Type::seqPlay, 0);
    render (r.e, 1000, 256);
    r.post (Command::Type::seqStop);
    const auto out = render (r.e, kBar, 256);
    REQUIRE (SeqRig::onsets (out.l).empty());
    REQUIRE (r.e.sequencer().playingIndex() == -1);
}

// ---- recording -------------------------------------------------------------------------

TEST_CASE ("Recording: one bar of count-in, quantized hits, commit", "[sequencer][record]")
{
    SeqRig r;
    r.post (Command::Type::seqQuantize, 6, 50.0);          // 16th
    r.post (Command::Type::seqRecStandby, 5, 1.0);         // pattern A6, 1 bar
    render (r.e, 512, 256);
    REQUIRE (r.e.sequencer().recordState() == Sequencer::RecState::standby);

    r.post (Command::Type::seqRecStart);
    render (r.e, 256, 256);
    REQUIRE (r.e.sequencer().recordState() == Sequencer::RecState::countIn);
    REQUIRE (r.e.sequencer().measure() == -4);
    render (r.e, kBar - 256 - 1024, 256);
    REQUIRE (r.e.sequencer().measure() == -1);
    render (r.e, 1024 + 256, 256);                         // into the take
    REQUIRE (r.e.sequencer().recordState() == Sequencer::RecState::recording);

    // Hit pad A3 near beat 2 (tick ~100): quantizes to 96.
    render (r.e, kBeat - (r.e.sequencer().measure() == 1 ? 0 : 0) - 512, 256);
    r.e.post (cmd (Command::Type::padDown, { 0, 2 }));
    render (r.e, kBeat / 2, 256);                          // hold half a beat (48 ticks)
    r.e.post (cmd (Command::Type::padUp, { 0, 2 }));
    render (r.e, 256, 256);
    r.post (Command::Type::seqRecStop);
    render (r.e, 256, 256);

    auto& sq = r.e.sequencer();
    REQUIRE (sq.takeResult());
    REQUIRE (sq.resultIndex() == 5);
    REQUIRE (sq.resultCount() == 1);
    const auto& e = sq.resultEvents()[0];
    REQUIRE (e.tick == 96);
    REQUIRE (e.pad == 2);
    REQUIRE (std::abs (e.length - 48) <= 2);
}

TEST_CASE ("Recording: a new hit is not replayed in the same pass, but is in the next", "[sequencer][record]")
{
    SeqRig r;
    r.post (Command::Type::setMetronomeLevel, 0, 0.0);     // only pad hits in the output
    r.post (Command::Type::seqQuantize, 2, 50.0);          // quarter notes
    r.post (Command::Type::seqRecStandby, 0, 1.0);
    r.post (Command::Type::seqRecStart);
    render (r.e, kBar + 256, 256);                          // count-in done
    // Hit at ~tick 80: quantizes forward to beat 2 (96), still ahead in this pass.
    render (r.e, (int) (80.0 / 96.0 * kBeat) - 256, 256);
    r.e.post (cmd (Command::Type::padDown, { 0, 3 }));
    r.e.post (cmd (Command::Type::padUp, { 0, 3 }));
    const auto pass1 = render (r.e, kBar - (int) (80.0 / 96.0 * kBeat), 256);
    // Only the live hit sounds in pass 1 (its onset is at the start of this render).
    REQUIRE (SeqRig::onsets (pass1.l).size() == 1);
    const auto pass2 = render (r.e, kBar, 256);
    REQUIRE (SeqRig::onsets (pass2.l).size() == 1);           // replayed once in pass 2
}

TEST_CASE ("Overdub keeps the existing take; rehearsal records nothing", "[sequencer][record]")
{
    SeqRig r;
    r.setPattern (0, 2, { ev (0, 0), ev (384, 1) });
    r.post (Command::Type::seqQuantize, 2, 50.0);
    r.post (Command::Type::seqRecStandby, 0, 1.0);          // asks 1 bar, pattern has 2: stays 2
    r.post (Command::Type::seqRecStart);
    render (r.e, kBar + kBeat + 256, 256);
    r.e.post (cmd (Command::Type::padDown, { 0, 2 }));
    r.e.post (cmd (Command::Type::padUp, { 0, 2 }));
    render (r.e, 512, 256);
    r.post (Command::Type::seqRehearsal);
    render (r.e, 512, 256);
    REQUIRE (r.e.sequencer().recordState() == Sequencer::RecState::rehearsal);
    r.e.post (cmd (Command::Type::padDown, { 0, 3 }));     // rehearsal: not recorded
    r.e.post (cmd (Command::Type::padUp, { 0, 3 }));
    render (r.e, 512, 256);
    r.post (Command::Type::seqRecStop);
    render (r.e, 256, 256);
    auto& sq = r.e.sequencer();
    REQUIRE (sq.takeResult());
    REQUIRE (sq.resultLengthBars() == 2);
    REQUIRE (sq.resultCount() == 3);
}

TEST_CASE ("Erase removes the held pad's notes as the pattern passes", "[sequencer][record]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0), ev (96, 1), ev (192, 0), ev (288, 1) });
    r.post (Command::Type::seqRecStandby, 0, 1.0);
    r.post (Command::Type::seqRecStart);
    render (r.e, kBar + 256, 256);                          // count-in done, at the top
    r.post (Command::Type::seqErase, 0, 1.0);
    r.e.post (cmd (Command::Type::seqErasePad, { 0, 1 }, 1.0));
    render (r.e, kBar, 256);                                // one full pass with A2 held
    r.post (Command::Type::seqRecStop);
    render (r.e, 256, 256);
    auto& sq = r.e.sequencer();
    REQUIRE (sq.takeResult());
    REQUIRE (sq.resultCount() == 2);
    for (int i = 0; i < 2; ++i)
        REQUIRE (sq.resultEvents()[(size_t) i].pad == 0);
}

TEST_CASE ("A pattern with notes cannot be made shorter", "[sequencer][record]")
{
    SeqRig r;
    r.setPattern (0, 4, { ev (0, 0) });
    r.post (Command::Type::seqRecStandby, 0, 4.0);
    r.post (Command::Type::seqLength, 0, 2.0);
    r.post (Command::Type::seqRecStop);
    render (r.e, 256, 256);
    REQUIRE (r.e.sequencer().takeResult());
    REQUIRE (r.e.sequencer().resultLengthBars() == 4);
}

TEST_CASE ("Metronome clicks every beat during standby", "[sequencer][metronome]")
{
    SeqRig r;
    r.post (Command::Type::setMetronomeLevel, 0, 127.0);
    r.post (Command::Type::seqRecStandby, 0, 1.0);
    const auto out = render (r.e, kBar, 256);
    const auto o = SeqRig::onsets (out.l);
    REQUIRE (o.size() >= 4);
    REQUIRE (o[1] - o[0] == kBeat);

    SeqRig q;
    q.post (Command::Type::setMetronomeLevel, 0, 0.0);
    q.post (Command::Type::seqRecStandby, 0, 1.0);
    REQUIRE (peakOf (render (q.e, kBar, 256).l) < 1e-6f);
}

// ---- host sync ----------------------------------------------------------------------------

TEST_CASE ("Host transport: armed pattern starts with the host at its position, stops with it", "[sequencer][host]")
{
    SeqRig r;
    r.setPattern (0, 1, { ev (0, 0), ev (192, 1) });
    r.post (Command::Type::seqArm, 0);
    r.e.setHostTransport (true, false, 120.0, 0.0);
    render (r.e, 512, 256);
    REQUIRE (r.e.sequencer().playingIndex() == -1);

    r.e.setHostTransport (true, true, 120.0, 1.0);          // host at beat 2
    render (r.e, 256, 256);
    REQUIRE (r.e.sequencer().playingIndex() == 0);
    REQUIRE (r.e.sequencer().beat() == 2);

    r.e.setHostTransport (true, false, 120.0, 2.0);
    render (r.e, 256, 256);
    REQUIRE (r.e.sequencer().playingIndex() == -1);

    // With the host stopped, a pattern still plays by hand.
    r.post (Command::Type::seqPlay, 0);
    render (r.e, 512, 256);
    REQUIRE (r.e.sequencer().playingIndex() == 0);
}

TEST_CASE ("Sequencer playback and recording do not allocate", "[sequencer][realtime]")
{
    SeqRig r;
    std::vector<SeqEvent> many;
    for (int t = 0; t < kTicksPerBar; t += 6)
        many.push_back (ev (t, t % 4, 3));
    r.setPattern (0, 1, many);
    r.setPattern (1, 1, many);
    render (r.e, 256, 256);

    resetRealtimeAllocations();
    r.post (Command::Type::seqPlay, 0);
    render (r.e, kBar, 256);
    r.post (Command::Type::seqReserve, 1);
    render (r.e, kBar, 256);
    r.post (Command::Type::seqRecStandby, 0, 1.0);
    r.post (Command::Type::seqRecStart);
    for (int i = 0; i < 50; ++i)
    {
        r.e.post (cmd (Command::Type::padDown, { 0, i % 4 }));
        render (r.e, 2048, 256);
        r.e.post (cmd (Command::Type::padUp, { 0, i % 4 }));
    }
    r.post (Command::Type::seqRecStop);
    render (r.e, 512, 256);
    REQUIRE (realtimeAllocations() == 0);
}

// ---- project + panel ------------------------------------------------------------------------

TEST_CASE ("Patterns survive the project JSON round trip", "[sequencer][state]")
{
    Project p;
    p.pattern ({ 2, 3 }) = new PatternData (8, 93.5, { ev (0, 0, 10), ev (700, 14, 48) });
    Project q;
    REQUIRE (Project::fromVar (juce::JSON::parse (juce::JSON::toString (p.toVar())), q).wasOk());
    const auto& pat = q.pattern ({ 2, 3 });
    REQUIRE (pat != nullptr);
    REQUIRE (pat->lengthBars == 8);
    REQUIRE (pat->tempo == 93.5);
    REQUIRE (pat->events == p.pattern ({ 2, 3 })->events);
    REQUIRE (q.pattern ({ 0, 0 }) == nullptr);
}

namespace
{
    struct PatternPanel
    {
        Engine engine;
        SamplerController controller { engine, Limits::authentic() };
        PanelModel model { engine, controller };
        double now = 100.0;

        PatternPanel()
        {
            engine.prepare (kEngineRate, 256, Limits::authentic());
            model.syncEngine();
            for (int p = 0; p < 4; ++p)
                controller.setSample ({ 0, p }, makeConstant (400, 0.1f * (float) (p + 1)), "s");
            run (1);
        }
        void run (int samples)
        {
            render (engine, samples, 256);
            controller.tick();
            model.tick (now);
        }
        void tap (Control c) { model.press (c, now); run (256); model.release (c, now); run (256); }
        std::string display() { return model.view (now).display.toString(); }
        Led led (Control c) { return model.view (now).leds[(size_t) c]; }
    };
}

TEST_CASE ("Panel: record a pattern, play it, reserve, stop, delete", "[sequencer][panel]")
{
    PatternPanel p;
    p.tap (Control::patternSelect);
    REQUIRE (p.display() == "Ptn");

    // REC -> choose pad 1 -> standby -> REC -> count-in -> record -> CANCEL commits.
    p.tap (Control::rec);
    REQUIRE (p.led (Control::pad1) == Led::blink);          // empty pattern pads blink
    p.tap (Control::pad1);
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::rec);
    REQUIRE (p.display() == " -4");
    p.run (kBar);
    REQUIRE (p.display() == " 1.1");
    p.tap (Control::pad2);                                  // plays sample A2 and records it
    p.run (kBeat);
    p.tap (Control::cancel);
    p.run (512);
    REQUIRE (p.controller.hasPattern ({ 0, 0 }));
    REQUIRE (p.controller.project().pattern ({ 0, 0 })->events.size() == 1);
    REQUIRE (p.led (Control::pad1) == Led::blink);          // has a pattern

    // Play it, then reserve another, then stop.
    p.controller.setPattern ({ 0, 1 }, new PatternData (1, 120.0, { ev (0, 3) }));
    p.run (256);
    p.tap (Control::pad1);
    REQUIRE (p.engine.sequencer().playingIndex() == 0);
    REQUIRE (p.led (Control::pad1) == Led::on);
    REQUIRE (p.model.view (p.now).illuminationTempo);
    p.tap (Control::pad2);
    REQUIRE (p.engine.sequencer().reservedIndex() == 1);
    REQUIRE (p.led (Control::pad2) == Led::blinkFast);
    p.tap (Control::cancel);
    REQUIRE (p.engine.sequencer().playingIndex() == -1);

    // SUB PAD + pad switches immediately.
    p.tap (Control::pad1);
    p.model.press (Control::subPad, p.now);
    p.tap (Control::pad2);
    p.model.release (Control::subPad, p.now);
    REQUIRE (p.engine.sequencer().playingIndex() == 1);
    p.tap (Control::pad2);                                  // pressing the playing pad stops it

    // DEL: select pattern pads, DEL deletes.
    p.tap (Control::del);
    REQUIRE (p.display() == "dEL");
    p.tap (Control::pad2);
    p.tap (Control::del);
    REQUIRE_FALSE (p.controller.hasPattern ({ 0, 1 }));
    REQUIRE (p.controller.hasPattern ({ 0, 0 }));
}

TEST_CASE ("Panel: ErS and rEH while recording; FUNC + 1 copies a pattern", "[sequencer][panel]")
{
    PatternPanel p;
    p.controller.setPattern ({ 0, 0 }, new PatternData (1, 120.0, { ev (0, 0), ev (192, 1) }));
    p.run (256);
    p.tap (Control::patternSelect);
    p.tap (Control::rec);
    p.tap (Control::pad1);                                  // overdub the existing pattern
    p.tap (Control::rec);
    p.run (kBar);
    p.tap (Control::rec);
    REQUIRE (p.display() == "rEH");
    p.tap (Control::rec);
    p.tap (Control::del);
    REQUIRE (p.display() == "ErS");
    p.model.press (Control::pad2, p.now);                   // erase A2's notes for a full pass
    p.run (kBar);
    p.model.release (Control::pad2, p.now);
    p.tap (Control::del);
    p.tap (Control::cancel);
    p.run (512);
    REQUIRE (p.controller.project().pattern ({ 0, 0 })->events.size() == 1);

    p.model.press (Control::func, p.now);
    p.tap (Control::pad1);
    p.model.release (Control::func, p.now);
    REQUIRE (p.display() == "CPY");
    p.tap (Control::pad1);
    p.tap (Control::pad6);
    p.tap (Control::rec);
    REQUIRE (p.controller.hasPattern ({ 0, 5 }));
}
