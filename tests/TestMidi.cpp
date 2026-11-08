#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"
#include "core/RealtimeGuard.h"
#include "core/midi/MidiInput.h"

using namespace spsx;
using namespace spsx::test;
using Catch::Matchers::WithinAbs;
using Kind = MidiInput::Event::Kind;

namespace
{
    constexpr double kRate = 48000.0;

    MidiSettings authentic (int channel = 1, int sync = MidiSettings::autoSync)
    {
        MidiSettings s;
        s.baseChannel = channel;
        s.sync = sync;
        return s;
    }

    MidiSettings extended (int channel = 1)
    {
        auto s = authentic (channel);
        s.honourNoteOff = true;
        s.velocity = true;
        s.minBpm = 20.0;
        s.maxBpm = 300.0;
        return s;
    }

    struct Rig
    {
        MidiInput in;
        std::vector<MidiInput::Event> events;

        explicit Rig (MidiSettings s)
        {
            in.prepare (kRate);
            in.setSettings (s);
        }

        void block (std::vector<std::pair<int, juce::MidiMessage>> msgs, int n = 512)
        {
            auto sink = [this] (const MidiInput::Event& e) { events.push_back (e); };
            in.beginBlock (n, sink);
            for (auto& [off, m] : msgs)
                in.handle (m, off, sink);
            in.endBlock();
        }

        /** Sends n clock ticks at `bpm`, one block per tick (block length = interval). */
        void clocks (int n, double bpm)
        {
            const int interval = (int) std::lround (60.0 * kRate / (24.0 * bpm));
            for (int i = 0; i < n; ++i)
                block ({ { 0, juce::MidiMessage::midiClock() } }, interval);
        }
    };
}

TEST_CASE ("MIDI note map: banks A..E on Base Ch, F..J on Base Ch + 1, note 46 = EXT SOURCE", "[midi]")
{
    Rig r (authentic (3));
    r.block ({ { 10, juce::MidiMessage::noteOn (3, 47, (juce::uint8) 100) },
               { 11, juce::MidiMessage::noteOn (3, 106, (juce::uint8) 100) },
               { 12, juce::MidiMessage::noteOn (4, 47, (juce::uint8) 100) },
               { 13, juce::MidiMessage::noteOn (4, 70, (juce::uint8) 100) },
               { 14, juce::MidiMessage::noteOn (5, 47, (juce::uint8) 100) },     // other channel
               { 15, juce::MidiMessage::noteOn (3, 45, (juce::uint8) 100) },     // below the map
               { 16, juce::MidiMessage::noteOn (3, 107, (juce::uint8) 100) },    // above the map
               { 17, juce::MidiMessage::noteOn (3, 46, (juce::uint8) 100) } });

    REQUIRE (r.events.size() == 6);
    REQUIRE (r.events[0].kind == Kind::padDown);
    REQUIRE (r.events[0].pad == PadId { 0, 0 });    // A1
    REQUIRE (r.events[0].offset == 10);
    REQUIRE (r.events[1].pad == PadId { 4, 11 });   // E12
    REQUIRE (r.events[2].pad == PadId { 5, 0 });    // F1
    REQUIRE (r.events[3].pad == PadId { 6, 11 });   // G12
    REQUIRE (r.events[4].kind == Kind::extDown);
    REQUIRE (r.events[5].kind == Kind::extUp);      // Authentic: press and release at once
}

TEST_CASE ("MIDI: Pad Base Ch 16 puts banks F..J on channel 1 (SPEC U17)", "[midi]")
{
    Rig r (authentic (16));
    r.block ({ { 0, juce::MidiMessage::noteOn (1, 47, (juce::uint8) 100) },
               { 0, juce::MidiMessage::noteOn (16, 47, (juce::uint8) 100) } });
    REQUIRE (r.events.size() == 2);
    REQUIRE (r.events[0].pad == PadId { 5, 0 });
    REQUIRE (r.events[1].pad == PadId { 0, 0 });
}

TEST_CASE ("MIDI Authentic: velocity and Note Off ignored, Note On is a one-shot hit", "[midi]")
{
    Rig r (authentic());
    r.block ({ { 0, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 10) },
               { 5, juce::MidiMessage::noteOff (1, 50) },
               { 6, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 0) } });
    REQUIRE (r.events.size() == 1);
    REQUIRE (r.events[0].oneShot);
    REQUIRE (r.events[0].velocity == 1.0f);
}

TEST_CASE ("MIDI Extended: velocity scales level, Note Off and velocity 0 release", "[midi]")
{
    Rig r (extended());
    r.block ({ { 0, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 64) },
               { 5, juce::MidiMessage::noteOff (1, 50) },
               { 6, juce::MidiMessage::noteOn (1, 51, (juce::uint8) 127) },
               { 7, juce::MidiMessage::noteOn (1, 51, (juce::uint8) 0) },
               { 8, juce::MidiMessage::noteOn (1, 46, (juce::uint8) 99) } });
    REQUIRE (r.events.size() == 5);
    REQUIRE_FALSE (r.events[0].oneShot);
    const float v64 = 64.0f / 127.0f;
    REQUIRE_THAT (r.events[0].velocity, WithinAbs (v64 * v64, 1e-6));
    REQUIRE (r.events[1].kind == Kind::padUp);
    REQUIRE (r.events[2].velocity == 1.0f);
    REQUIRE (r.events[3].kind == Kind::padUp);
    REQUIRE (r.events[4].kind == Kind::extDown);    // held until its Note Off
}

TEST_CASE ("MIDI: All Sound Off on any channel stops everything", "[midi]")
{
    Rig r (authentic (1));
    r.block ({ { 20, juce::MidiMessage::allSoundOff (9) } });
    REQUIRE (r.events.size() == 1);
    REQUIRE (r.events[0].kind == Kind::stopAll);
    REQUIRE (r.events[0].offset == 20);
}

TEST_CASE ("MIDI: Active Sensing timeout silences everything once", "[midi]")
{
    Rig r (authentic());
    r.block ({ { 0, juce::MidiMessage (0xfe) } });
    for (int i = 0; i < 20; ++i)
        r.block ({}, 512);   // 213 ms
    REQUIRE (r.events.empty());
    for (int i = 0; i < 20; ++i)
        r.block ({}, 512);
    REQUIRE (r.events.size() == 1);
    REQUIRE (r.events[0].kind == Kind::stopAll);
}

TEST_CASE ("MIDI clock: tempo follows in AUt and tNp, not in oFF", "[midi][sync]")
{
    for (int sync : { 0, 1, 2 })
    {
        Rig r (authentic (1, sync));
        r.clocks (30, 120.0);
        const auto tr = r.in.transportAt (0);
        if (sync == MidiSettings::off)
        {
            REQUIRE_FALSE (tr.follow);
        }
        else
        {
            REQUIRE (tr.follow);
            REQUIRE_THAT (tr.bpm, WithinAbs (120.0, 0.01));
        }
    }

    // Authentic tempo range is 40..200 (p. 46); Extended wider.
    Rig fast (authentic());
    fast.clocks (30, 250.0);
    REQUIRE_THAT (fast.in.transportAt (0).bpm, WithinAbs (200.0, 1e-9));
    Rig fastExt (extended());
    fastExt.clocks (30, 250.0);
    REQUIRE_THAT (fastExt.in.transportAt (0).bpm, WithinAbs (250.0, 0.1));

    // Clock stops: sync lets go after half a second.
    for (int i = 0; i < 60; ++i)
        fastExt.block ({}, 512);
    REQUIRE_FALSE (fastExt.in.transportAt (0).follow);
}

TEST_CASE ("MIDI Start / Stop / Continue / Song Position drive the transport in AUt only", "[midi][sync]")
{
    Rig r (authentic());
    r.clocks (24, 120.0);                                // tempo first, transport stopped
    REQUIRE_FALSE (r.in.transportAt (0).playing);

    r.block ({ { 0, juce::MidiMessage::midiStart() } }, 1);
    REQUIRE (r.in.transportAt (0).playing);
    REQUIRE (r.in.transportAt (0).ppq == 0.0);
    // Clock interval is 1000 samples. Each block ends one interval after its clock, so
    // the position read at the next block's start is one clock further on.
    r.clocks (25, 120.0);                                // first clock = position 0
    REQUIRE_THAT (r.in.transportAt (0).ppq, WithinAbs (25.0 / 24.0, 1e-3));

    r.block ({ { 0, juce::MidiMessage::midiStop() } }, 1);
    REQUIRE_FALSE (r.in.transportAt (0).playing);
    r.block ({ { 0, juce::MidiMessage::songPositionPointer (8) } }, 1);   // 8 sixteenths
    REQUIRE_THAT (r.in.transportAt (0).ppq, WithinAbs (2.0, 1e-9));
    r.block ({ { 0, juce::MidiMessage::midiContinue() } }, 1);
    r.clocks (12, 120.0);                                // positions 2.0 .. 2.0 + 11/24

    // Within a clock interval the position is interpolated.
    auto sink = [] (const MidiInput::Event&) {};
    r.in.beginBlock (1000, sink);
    r.in.handle (juce::MidiMessage::midiClock(), 0, sink);   // 2.5
    REQUIRE_THAT (r.in.transportAt (0).ppq, WithinAbs (2.5, 1e-3));
    REQUIRE_THAT (r.in.transportAt (500).ppq, WithinAbs (2.5 + 0.5 / 24.0, 1e-3));
    r.in.endBlock();

    // tNp ignores Start.
    Rig t (authentic (1, MidiSettings::tempoOnly));
    t.clocks (24, 120.0);
    t.block ({ { 0, juce::MidiMessage::midiStart() } }, 1);
    REQUIRE_FALSE (t.in.transportAt (0).playing);
}

TEST_CASE ("MIDI handle() reports transport messages so the block can be split", "[midi][sync]")
{
    MidiInput in;
    in.prepare (kRate);
    in.setSettings (authentic());
    auto sink = [] (const MidiInput::Event&) {};
    in.beginBlock (256, sink);
    REQUIRE (in.handle (juce::MidiMessage::midiStart(), 100, sink));
    REQUIRE_FALSE (in.handle (juce::MidiMessage::midiClock(), 101, sink));
    REQUIRE_FALSE (in.handle (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 1), 102, sink));
    in.endBlock();

    in.setSettings (authentic (1, MidiSettings::off));
    in.beginBlock (256, sink);
    REQUIRE_FALSE (in.handle (juce::MidiMessage::midiStart(), 0, sink));
    in.endBlock();
}

TEST_CASE ("MIDI parsing does not allocate", "[midi][realtime]")
{
    MidiInput in;
    in.prepare (kRate);
    in.setSettings (extended());
    const auto on = juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100);
    const auto clk = juce::MidiMessage::midiClock();
    const auto start = juce::MidiMessage::midiStart();
    int count = 0;
    auto sink = [&count] (const MidiInput::Event&) { ++count; };
    resetRealtimeAllocations();
    {
        rt::ScopedRealtime guard;
        for (int b = 0; b < 100; ++b)
        {
            in.beginBlock (480, sink);
            in.handle (b == 10 ? start : clk, 0, sink);
            in.handle (on, 3, sink);
            (void) in.transportAt (100);
            in.endBlock();
        }
    }
    REQUIRE (count == 100);
    REQUIRE (realtimeAllocations() == 0);
}

// ---- engine side -------------------------------------------------------------------

TEST_CASE ("MIDI one-shot: a GATE sample plays out without a Note Off", "[midi][engine]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    auto s = makeConstant (1000, 0.25f);
    auto st = defaults (*s);
    st.gate = true;
    loadPad (e, { 0, 0 }, s, st);
    render (e, 64);

    auto c = cmd (Command::Type::padDown, { 0, 0 });
    c.index = 2;   // MIDI one-shot
    e.post (c);
    auto out = render (e, 1200);
    REQUIRE (out.l[0] == 0.25f);
    REQUIRE (out.l[999] == 0.25f);
    REQUIRE (peakOf (out.l, 1000) == 0.0f);
}

TEST_CASE ("MIDI velocity scales the voice level exactly", "[midi][engine]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::extended());
    auto s = makeConstant (1000, 0.5f);
    loadPad (e, { 0, 0 }, s, defaults (*s));
    render (e, 64);
    auto c = cmd (Command::Type::padDown, { 0, 0 });
    c.velocity = 0.25f;
    e.post (c);
    auto out = render (e, 256);
    REQUIRE_THAT (out.l[0], WithinAbs (0.125f, 1e-7));   // no ramp from full level
    REQUIRE_THAT (out.l[200], WithinAbs (0.125f, 1e-7));
}

TEST_CASE ("MIDI one-shot hits record with length 0 and play back without a release", "[midi][sequencer]")
{
    Engine e;
    e.prepare (kEngineRate, 256, Limits::authentic());
    auto s = makeConstant (30000, 0.2f);
    auto st = defaults (*s);
    st.gate = true;
    loadPad (e, { 0, 0 }, s, st);
    e.post (cmd (Command::Type::setPatternTempo, kNoPad, 120.0));
    render (e, 256, 256);

    auto post = [&] (Command::Type t, int index = 0, double value = 0.0)
    {
        Command c;
        c.type = t;
        c.index = index;
        c.value = value;
        e.post (c);
    };
    post (Command::Type::seqQuantize, 9, 50.0);   // off
    post (Command::Type::seqRecStandby, 5, 1.0);
    render (e, 512, 256);
    post (Command::Type::seqRecStart);
    render (e, 4 * 22050 + 1024, 256);             // count-in
    REQUIRE (e.sequencer().recordState() == Sequencer::RecState::recording);

    auto hit = cmd (Command::Type::padDown, { 0, 0 });
    hit.index = 2;
    e.post (hit);
    render (e, 2048, 256);
    post (Command::Type::seqRecStop);
    render (e, 256, 256);

    auto& sq = e.sequencer();
    REQUIRE (sq.takeResult());
    REQUIRE (sq.resultCount() == 1);
    REQUIRE (sq.resultEvents()[0].length == 0);
    REQUIRE (sq.resultEvents()[0].velocity == 127);
}

// ---- Extended: learn, curves, export, persistence ---------------------------------

#include "core/ui_model/PanelModel.h"
#include "core/midi/MidiFileExport.h"

TEST_CASE ("MIDI learn binds a CC to a knob and a button; rebinding replaces", "[midi][learn]")
{
    Engine engine;
    engine.prepare (kEngineRate, 512, Limits::extended());
    SamplerController controller { engine, Limits::extended() };
    PanelModel model { engine, controller };
    double now = 100.0;

    model.armMidiLearn (PanelModel::LearnTarget { true, (int) Knob::ctrl2 });
    model.handleMidiControl (2, 21, 0, now);                 // binds, does not turn
    REQUIRE_FALSE (model.midiLearnTarget().has_value());
    REQUIRE (controller.project().device.midiMap.size() == 1);

    model.handleMidiControl (2, 21, 127, now);
    REQUIRE (model.knob (Knob::ctrl2) == 1.0);
    model.handleMidiControl (2, 21, 0, now);
    REQUIRE (model.knob (Knob::ctrl2) == 0.0);
    model.handleMidiControl (3, 21, 127, now);               // other channel: not bound
    REQUIRE (model.knob (Knob::ctrl2) == 0.0);

    // Same CC to a button: replaces the knob binding.
    model.armMidiLearn (PanelModel::LearnTarget { false, (int) Control::hold });
    model.handleMidiControl (2, 21, 0, now);
    REQUIRE (controller.project().device.midiMap.size() == 1);
    REQUIRE_FALSE (controller.project().device.midiMap[0].knob);
    model.handleMidiControl (2, 21, 127, now);
    REQUIRE (model.knob (Knob::ctrl2) == 0.0);

    model.clearMidiMap();
    REQUIRE (controller.project().device.midiMap.empty());
}

TEST_CASE ("Velocity curves: fixed, linear, soft", "[midi]")
{
    REQUIRE (MidiInput::velocityGain (0.5f, MidiSettings::fixed) == 1.0f);
    REQUIRE (MidiInput::velocityGain (0.5f, MidiSettings::linear) == 0.5f);
    REQUIRE (MidiInput::velocityGain (0.5f, MidiSettings::soft) == 0.25f);

    // Authentic (velocity off) ignores the curve.
    auto s = authentic();
    s.velocityCurve = MidiSettings::linear;
    Rig r (s);
    r.block ({ { 0, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 20) } });
    REQUIRE (r.events[0].velocity == 1.0f);

    // CCs reach the panel only with MIDI learn on (Extended).
    Rig a (authentic());
    a.block ({ { 0, juce::MidiMessage::controllerEvent (1, 21, 64) } });
    REQUIRE (a.events.empty());
    auto e = extended();
    e.controlChange = true;
    Rig x (e);
    x.block ({ { 0, juce::MidiMessage::controllerEvent (4, 21, 64) } });
    REQUIRE (x.events.size() == 1);
    REQUIRE (x.events[0].kind == Kind::controlChange);
    REQUIRE (x.events[0].channel == 4);
    REQUIRE (x.events[0].value == 64);
}

TEST_CASE ("Pattern export: the SMF plays the same pads back through MIDI IN", "[midi][smf]")
{
    std::vector<SeqEvent> ev (3);
    ev[0].tick = 0;   ev[0].pad = (std::int16_t) PadId { 0, 0 }.index(); ev[0].length = 48;
    ev[1].tick = 96;  ev[1].pad = (std::int16_t) PadId { 6, 3 }.index(); ev[1].length = 0; ev[1].velocity = 90;
    ev[2].tick = 380; ev[2].pad = (std::int16_t) PadId { 4, 11 }.index(); ev[2].length = 24;
    PatternData p (1, 93.0, ev);

    auto file = midi::patternToMidiFile (p, 2);
    REQUIRE (file.getTimeFormat() == kPpq);
    const auto* track = file.getTrack (0);
    REQUIRE (track != nullptr);

    std::vector<juce::MidiMessage> notes;
    double tempo = 0.0, last = 0.0;
    for (const auto* h : *track)
    {
        const auto& m = h->message;
        if (m.isTempoMetaEvent()) tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
        if (m.isNoteOn()) notes.push_back (m);
        last = std::max (last, m.getTimeStamp());
    }
    REQUIRE_THAT (tempo, WithinAbs (93.0, 0.01));
    REQUIRE (notes.size() == 3);
    REQUIRE (last <= p.lengthTicks());

    // Feeding the notes back through MIDI IN hits the same pads.
    Rig r (authentic (2));
    for (auto& m : notes)
        r.block ({ { 0, m } });
    REQUIRE (r.events.size() == 3);
    REQUIRE (r.events[0].pad == PadId { 0, 0 });
    REQUIRE (r.events[1].pad == PadId { 6, 3 });
    REQUIRE (r.events[2].pad == PadId { 4, 11 });
    REQUIRE (notes[1].getVelocity() == 90);
    REQUIRE (notes[1].getTimeStamp() == 96.0);
}

TEST_CASE ("MIDI settings, mappings and pattern velocities survive the project JSON", "[midi][state]")
{
    Project p;
    p.device.midiChannel = 7;
    p.device.midiSync = 1;
    p.device.velocityCurve = 1;
    p.device.midiMap = { { 3, 74, true, (int) Knob::ctrl1 }, { 10, 64, false, (int) Control::hold } };
    std::vector<SeqEvent> ev (2);
    ev[0].tick = 10; ev[0].length = 0;  ev[0].velocity = 33;
    ev[1].tick = 20; ev[1].length = 12;
    p.pattern ({ 1, 2 }) = new PatternData (2, 100.0, ev);

    Project q;
    REQUIRE (Project::fromVar (juce::JSON::parse (juce::JSON::toString (p.toVar())), q).wasOk());
    REQUIRE (q.device == p.device);
    REQUIRE (q.pattern ({ 1, 2 })->events == p.pattern ({ 1, 2 })->events);
}
