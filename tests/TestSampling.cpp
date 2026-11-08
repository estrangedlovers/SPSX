#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestUtil.h"
#include "AllocGuard.h"
#include "core/ui_model/PanelModel.h"
#include "core/io/AudioFileIO.h"
#include <cmath>

using namespace spsx;
using namespace spsx::test;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr float kPi = 3.14159265f;

    /** Engine with a live input signal generator. */
    struct Rig
    {
        Engine e;
        std::vector<float> inL, inR, outL, outR;
        double phase = 0.0;
        float inputAmp = 0.5f, inputHz = 440.0f;

        explicit Rig (Limits l = Limits::authentic())
        {
            e.prepare (kEngineRate, 256, l);
            for (auto* v : { &inL, &inR, &outL, &outR }) v->assign (256, 0.0f);
        }

        void post (Command::Type t, double v = 0.0, int index = 0, PadId p = kNoPad)
        {
            Command c;
            c.type = t;
            c.value = v;
            c.index = index;
            c.pad = p;
            e.post (c);
        }

        /** Renders n blocks of 256; returns all output (left). */
        std::vector<float> run (int blocks)
        {
            std::vector<float> all;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    inL[(size_t) i] = inputAmp * (float) std::sin (phase);
                    inR[(size_t) i] = -inL[(size_t) i];
                    phase += 2.0 * 3.141592653589793 * inputHz / kEngineRate;
                }
                const float* in[2] = { inL.data(), inR.data() };
                float* out[2] = { outL.data(), outR.data() };
                e.process (in, 2, out, 2, 256);
                all.insert (all.end(), outL.begin(), outL.end());
            }
            return all;
        }
    };

    float rms (const std::vector<float>& x, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < x.size(); ++i) s += x[i] * (double) x[i];
        return (float) std::sqrt (s / (double) std::max<size_t> (1, x.size() - from));
    }
}

// ---- engine: input, EXT SOURCE, routing ------------------------------------------------

TEST_CASE ("Live input is silent unless EXT SOURCE or sampling monitor is on", "[input]")
{
    Rig r;
    REQUIRE (rms (r.run (20)) == 0.0f);

    r.post (Command::Type::setExtOn, 1.0);
    const auto out = r.run (20);
    REQUIRE_THAT (rms (out, 2048), WithinAbs (0.5 / std::sqrt (2.0), 0.01));   // level 127 = unity
}

TEST_CASE ("EXT SOURCE level, mono mode and INPUT GAIN", "[input]")
{
    Rig r;
    r.post (Command::Type::setExtOn, 1.0);
    r.post (Command::Type::setExtLevel, 64.0);
    REQUIRE_THAT (rms (r.run (20), 2048), WithinAbs (0.3536 * 64.0 / 127.0, 0.01));

    r.post (Command::Type::setExtLevel, 127.0);
    r.post (Command::Type::setExtStereo, 0.0);   // L and R are opposite: mono sums to silence
    REQUIRE (rms (r.run (20), 2048) < 0.001f);

    r.post (Command::Type::setExtStereo, 1.0);
    r.post (Command::Type::setInputGainDb, -10.0);
    REQUIRE_THAT (rms (r.run (20), 2048), WithinAbs (0.3536 * 0.3162, 0.01));
}

TEST_CASE ("MIC mode takes the first input channel with MIC LEVEL gain", "[input]")
{
    Rig r;
    r.inputAmp = 0.05f;
    r.post (Command::Type::setExtOn, 1.0);
    r.post (Command::Type::setMicMode, 1.0);
    r.post (Command::Type::setMicGainDb, 20.0);
    REQUIRE_THAT (rms (r.run (20), 2048), WithinAbs (0.05 * 10.0 / std::sqrt (2.0), 0.01));
}

TEST_CASE ("PEAK lights for a hot input, not a quiet one", "[input]")
{
    Rig r;
    r.inputAmp = 0.1f;
    r.run (4);
    REQUIRE_FALSE (r.e.inputPeakLed());
    r.inputAmp = 0.9f;
    r.run (4);
    REQUIRE (r.e.inputPeakLed());
}

TEST_CASE ("Effects apply only to target pads; others stay dry", "[effects][routing]")
{
    // FILTER+DRIVE with the cutoff fully closed removes a 5 kHz tone.
    Engine e;
    e.prepare (kEngineRate, 256, Limits::authentic());
    auto tone = makeSine (44100, 5000.0, kEngineRate, 0.5f);
    loadPad (e, { 0, 0 }, tone, defaults (*tone));
    loadPad (e, { 0, 1 }, tone, defaults (*tone));

    Command c;
    c.type = Command::Type::setEffectParams;
    c.index = 0;
    c.params = { 0.0f, 0.0f, 0.0f, 0.5f };
    e.post (c);
    c = {};
    c.type = Command::Type::setEffect;
    c.value = 0;
    e.post (c);
    e.post (cmd (Command::Type::setTargetPad, { 0, 0 }, 1.0));

    e.post (cmd (Command::Type::padDown, { 0, 0 }));
    auto targeted = render (e, 8192, 256);
    e.post (cmd (Command::Type::stopAll));
    render (e, 2048, 256);
    e.post (cmd (Command::Type::padDown, { 0, 1 }));
    auto dry = render (e, 8192, 256);

    REQUIRE (rms (targeted.l, 4096) < 0.01f);
    REQUIRE (rms (dry.l, 4096) > 0.3f);
}

TEST_CASE ("Synced effects follow the longest targeted sample's BPM", "[effects][routing]")
{
    Engine e;
    e.prepare (kEngineRate, 256, Limits::authentic());
    auto shortS = makeConstant (44100, 0.1f);       // 1 s -> 120 BPM
    auto longS = makeConstant (44100 * 3, 0.1f);    // 3 s -> 80 BPM
    loadPad (e, { 0, 0 }, shortS, defaults (*shortS));
    loadPad (e, { 0, 1 }, longS, defaults (*longS));
    e.post (cmd (Command::Type::setPatternTempo, kNoPad, 100.0));
    render (e, 256);
    REQUIRE (e.effectTempo() == 100.0);   // nothing targeted: pattern tempo

    e.post (cmd (Command::Type::setTargetPad, { 0, 0 }, 1.0));
    e.post (cmd (Command::Type::setTargetPad, { 0, 1 }, 1.0));
    render (e, 256);
    REQUIRE (e.effectTempo() == 80.0);
}

// ---- engine: recording -----------------------------------------------------------------

TEST_CASE ("Sampling records the monitored input bit for bit", "[sampling]")
{
    Rig r;
    RecordTap tap (2, 200000);
    Command m;
    m.type = Command::Type::setMonitor;
    m.value = 1.0;
    m.index = 1;
    r.e.post (m);

    Command arm;
    arm.type = Command::Type::recArm;
    arm.ptr = &tap;
    arm.value = 120.0;
    r.e.post (arm);
    const auto out = r.run (10);
    REQUIRE (r.e.recordingPhase() == RecPhase::recording);
    REQUIRE (r.e.isTapAttached());

    r.post (Command::Type::recStop);
    r.run (1);
    REQUIRE_FALSE (r.e.isTapAttached());
    REQUIRE (tap.framesWritten.load() == 2560);

    std::vector<float> l (2560), rr (2560);
    REQUIRE (tap.read (l.data(), rr.data(), 2560) == 2560);
    for (int i = 0; i < 2560; ++i)
        REQUIRE (l[(size_t) i] == out[(size_t) i]);   // monitored = recorded (no effect, unity level)
}

TEST_CASE ("Count-in: one bar of clicks, then recording", "[sampling]")
{
    Rig r;
    RecordTap tap (1, 200000);
    Command arm;
    arm.type = Command::Type::recArm;
    arm.ptr = &tap;
    arm.value = 120.0;              // beat = 22050 samples
    arm.params = { 0.0f, 4.0f, 0.0f, 0.0f };
    r.e.post (arm);
    r.run (1);
    REQUIRE (r.e.recordingPhase() == RecPhase::countIn);
    REQUIRE (r.e.countInBeat() == 1);
    r.run (100);                    // 25600 samples: second beat
    REQUIRE (r.e.countInBeat() == 2);
    r.run (250);                    // past 88200
    REQUIRE (r.e.recordingPhase() == RecPhase::recording);
    REQUIRE (tap.framesWritten.load() == 351 * 256 - 88200);
}

TEST_CASE ("Auto sampling waits for the input to reach the trigger level", "[sampling]")
{
    Rig r;
    RecordTap tap (1, 200000);
    r.inputAmp = 0.01f;
    Command arm;
    arm.type = Command::Type::recArm;
    arm.ptr = &tap;
    arm.value = 120.0;
    arm.params = { 0.25f, 0.0f, 0.0f, 0.0f };
    r.e.post (arm);
    r.run (20);
    REQUIRE (r.e.recordingPhase() == RecPhase::waitingTrigger);
    REQUIRE (tap.framesWritten.load() == 0);
    r.inputAmp = 0.6f;
    r.run (5);
    REQUIRE (r.e.recordingPhase() == RecPhase::recording);
    REQUIRE (tap.framesWritten.load() > 0);
}

TEST_CASE ("Resample: armed, then the pad hit starts playback and recording together", "[resample]")
{
    Engine e;
    e.prepare (kEngineRate, 256, Limits::authentic());
    auto ramp = makeRamp (5000);
    loadPad (e, { 0, 0 }, ramp, defaults (*ramp));
    RecordTap tap (1, 100000);

    Command arm;
    arm.type = Command::Type::recArm;
    arm.ptr = &tap;
    arm.index = 1;                      // resample
    arm.value = 120.0;
    arm.params = { 0.0f, 0.0f, 1.0f, 0.0f };
    e.post (arm);
    render (e, 1024, 256);
    REQUIRE (e.recordingPhase() == RecPhase::waitingStart);
    REQUIRE (tap.framesWritten.load() == 0);

    e.post (cmd (Command::Type::padDown, { 0, 0 }));
    e.post (cmd (Command::Type::recStart));
    render (e, 1024, 256);
    std::vector<float> l (1024), r (1024);
    REQUIRE (tap.read (l.data(), r.data(), 1024) == 1024);
    // The ramp is mono: recorded mono = (L + R) / 2 of the mix -> the ramp itself.
    for (int i = 0; i < 1024; ++i)
        REQUIRE (l[(size_t) i] == ramp->channel (0)[i]);
}

TEST_CASE ("Digital level overload lights the illumination red", "[sampling]")
{
    Rig r;
    Command m;
    m.type = Command::Type::setMonitor;
    m.value = 1.0;
    m.index = 1;
    r.e.post (m);
    r.post (Command::Type::setDigitalLevel, 1.0);
    r.run (4);
    REQUIRE_FALSE (r.e.digitalOverload());
    r.post (Command::Type::setDigitalLevel, 4.0);
    r.run (4);
    REQUIRE (r.e.digitalOverload());
}

TEST_CASE ("Engine with effects, input and recording does not allocate", "[realtime]")
{
    Rig r;
    auto s = makeSine (44100, 220.0);
    for (int i = 0; i < 12; ++i)
        loadPad (r.e, { 0, i }, s, defaults (*s));
    RecordTap tap (2, 400000);
    r.run (2);

    resetRealtimeAllocations();
    for (int e = 0; e < 29; ++e)
    {
        Command c;
        c.type = Command::Type::setEffect;
        c.value = e;
        r.e.post (c);
        r.e.post (cmd (Command::Type::setTargetAll, kNoPad, 1.0));
        r.e.post (cmd (Command::Type::padDown, { 0, e % 12 }));
        if (e == 3)
        {
            Command arm;
            arm.type = Command::Type::recArm;
            arm.ptr = &tap;
            arm.index = 1;
            arm.value = 120.0;
            r.e.post (arm);
        }
        r.post (Command::Type::setExtOn, e % 2);
        r.run (8);
    }
    r.post (Command::Type::recStop);
    r.run (2);
    REQUIRE (realtimeAllocations() == 0);
}

// ---- controller + panel ---------------------------------------------------------------

namespace
{
    struct PanelRig
    {
        Rig rig;
        SamplerController controller { rig.e, Limits::authentic() };
        PanelModel model { rig.e, controller };
        double now = 500.0;

        PanelRig()
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spsx-rec-tests");
            dir.deleteRecursively();
            controller.setRecordingsDirectory (dir);
            run (1);
        }

        void run (int blocks)
        {
            rig.run (blocks);
            controller.tick();
            model.tick (now);
        }
        void tap (Control c) { model.press (c, now); run (1); model.release (c, now); run (1); }
        std::string display() { return model.view (now).display.toString(); }
        Led led (Control c) { return model.view (now).leds[(size_t) c]; }

        void waitForRecording()
        {
            for (int i = 0; i < 400 && controller.isRecording(); ++i)
            {
                run (1);
                juce::Thread::sleep (5);
            }
        }
    };
}

TEST_CASE ("Panel sampling: REC, choose pad, standby, REC, REC lands on the pad", "[sampling][panel]")
{
    PanelRig p;
    p.model.selectBank (9, p.now);   // bank J, like the manual's example (p. 18)
    p.tap (Control::rec);
    REQUIRE (p.led (Control::rec) == Led::on);
    REQUIRE (p.led (Control::pad1) == Led::blink);   // empty pads blink

    p.tap (Control::pad1);
    REQUIRE (p.model.isSampling());
    REQUIRE (p.led (Control::rec) == Led::blink);      // standby
    REQUIRE (p.led (Control::pad1) == Led::on);

    p.tap (Control::rec);                               // start
    REQUIRE (p.display() == "rEC");
    p.run (172);                                        // ~1 s of input
    p.tap (Control::rec);                               // stop
    p.waitForRecording();

    const auto& slot = p.controller.project().slot ({ 9, 0 });
    REQUIRE_FALSE (slot.isEmpty());
    REQUIRE (slot.sample->numChannels() == 2);
    REQUIRE (std::abs (slot.sample->length() - 172 * 256 - 512) < 1024);
    REQUIRE (slot.sample->isQuantized16());
    REQUIRE (slot.settings.level == 127);
}

TEST_CASE ("Panel sampling with auto trigger shows rdY", "[sampling][panel]")
{
    PanelRig p;
    p.controller.project().device.autoTrigLevel = 9;   // -6 dBFS
    p.rig.inputAmp = 0.05f;
    p.tap (Control::rec);
    p.tap (Control::pad1);
    p.tap (Control::rec);
    REQUIRE (p.display() == "rdY");
    p.tap (Control::cancel);
    REQUIRE_FALSE (p.model.isSampling());
    p.waitForRecording();
    REQUIRE (p.controller.project().slot ({ 0, 0 }).isEmpty());
}

TEST_CASE ("Panel sampling with START/END/LEVEL lit counts in and snaps the end to a beat", "[sampling][panel]")
{
    PanelRig p;
    p.controller.setPatternTempo (120.0);
    p.tap (Control::rec);
    p.tap (Control::pad2);
    p.tap (Control::startEndLevel);
    p.tap (Control::rec);
    REQUIRE (p.display() == "-1-");
    p.run (90);
    REQUIRE (p.display() == "-2-");
    p.run (270 + 190);                                  // count-in done, ~1.1 s recorded
    REQUIRE (p.display() == "rEC");
    p.tap (Control::rec);
    p.waitForRecording();
    const auto& slot = p.controller.project().slot ({ 0, 1 });
    REQUIRE_FALSE (slot.isEmpty());
    REQUIRE (slot.settings.bpm == 120.0);
    REQUIRE (slot.settings.end % 22050 == 0);           // a whole number of beats
}

TEST_CASE ("Panel resample: RESAMPLE, REC, empty pad, REC, source pad, REC", "[resample][panel]")
{
    PanelRig p;
    p.controller.setSample ({ 0, 0 }, makeSine (44100, 330.0), "src");
    p.run (1);
    p.tap (Control::pad1);
    p.tap (Control::loop);   // keep it sounding
    p.tap (Control::pad1);   // trigger mode: stop it again
    p.run (4);

    p.tap (Control::resample);
    REQUIRE (p.display() == "LEU");
    REQUIRE (p.led (Control::resample) == Led::on);
    REQUIRE (p.rig.e.activeChannelVoices() <= 4);

    p.tap (Control::rec);
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::pad1);                               // occupied: not a destination
    p.tap (Control::pad5);
    p.tap (Control::rec);                                // armed
    REQUIRE (p.led (Control::rec) == Led::on);
    REQUIRE (p.rig.e.recordingPhase() == RecPhase::waitingStart);

    p.tap (Control::pad1);                               // starts playback + recording
    REQUIRE (p.rig.e.recordingPhase() == RecPhase::recording);
    p.run (100);
    p.tap (Control::rec);
    p.waitForRecording();
    REQUIRE_FALSE (p.model.isResampling());
    REQUIRE_FALSE (p.controller.project().slot ({ 0, 4 }).isEmpty());
}

TEST_CASE ("Panel EXT SOURCE: toggles, or is momentary with GATE", "[input][panel]")
{
    PanelRig p;
    p.tap (Control::extSource);
    REQUIRE (p.model.isExtOn());
    REQUIRE (p.model.isExtCurrent());
    REQUIRE (p.led (Control::extSource) == Led::on);
    p.tap (Control::extSource);
    REQUIRE_FALSE (p.model.isExtOn());

    p.tap (Control::gate);                               // EXT is current: GATE = momentary
    p.model.press (Control::extSource, p.now);
    p.run (1);
    REQUIRE (p.model.isExtOn());
    p.model.release (Control::extSource, p.now);
    p.run (1);
    REQUIRE_FALSE (p.model.isExtOn());
}

TEST_CASE ("Panel effects reach the engine: DELAY on a pad produces repeats", "[effects][panel]")
{
    PanelRig p;
    juce::AudioBuffer<float> click (1, 2000);
    click.clear();
    click.setSample (0, 0, 0.9f);
    p.controller.setSample ({ 0, 0 }, new SampleData (std::move (click), false), "click");
    p.run (1);
    p.tap (Control::pad1);   // current pad = A1 (the effect target)
    p.run (40);

    p.tap (Control::delay);
    p.model.turn (Knob::ctrl1, 7.0f / 12.0f, p.now);   // t4 at 120 BPM: 0.5 s
    p.model.turn (Knob::ctrl2, 0.5, p.now);
    p.model.turn (Knob::ctrl3, 0.5, p.now);
    REQUIRE (p.display() == " 64");                      // CTRL 3 BALANCE value
    p.run (2);
    p.tap (Control::pad1);
    const auto out = p.rig.run (200);                    // ~1.16 s
    // Find energy around 0.5 s after the hit.
    float echo = 0.0f;
    for (size_t i = 21000; i < 24000 && i < out.size(); ++i)
        echo = std::max (echo, std::abs (out[i]));
    REQUIRE (echo > 0.1f);
}
