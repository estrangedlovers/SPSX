#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "core/SamplerController.h"
#include "core/io/AudioFileIO.h"

using namespace spsx;
using Status = SamplerController::Status;

namespace
{
    juce::File writeTestWav (const juce::String& name, int length, double hz = 440.0)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spsx-tests");
        dir.createDirectory();
        auto f = dir.getChildFile (name);
        auto s = io::makeSample (test::makeSine (length, hz)->buffer(), kEngineRate, true);
        REQUIRE (io::writeFile (f, *s, io::FileType::wav).wasOk());
        return f;
    }
}

TEST_CASE ("Controller: load assigns defaults (LEVEL 127, derived BPM)", "[controller]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    SamplerController c (e, Limits::authentic());

    REQUIRE (c.loadFile ({ 0, 0 }, writeTestWav ("a.wav", 88200)) == Status::ok);
    const auto* s = c.settings ({ 0, 0 });
    REQUIRE (s->level == 127);
    REQUIRE (s->bpm == 120.0);
    REQUIRE (s->end == 88200);
    REQUIRE (c.project().slot ({ 0, 0 }).sample->isQuantized16());

    e.post (test::cmd (Command::Type::padDown, { 0, 0 }));
    auto out = test::render (e, 2000);
    REQUIRE (test::peakOf (out.l) > 0.4f);
}

TEST_CASE ("Controller: async load completes on tick()", "[controller]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    SamplerController c (e, Limits::authentic());
    Status result = Status::invalid;
    c.loadFileAsync ({ 2, 5 }, writeTestWav ("b.wav", 4410), [&result] (Status s) { result = s; });
    c.waitForJobs();
    REQUIRE (result == Status::ok);
    REQUIRE_FALSE (c.project().slot ({ 2, 5 }).isEmpty());
}

TEST_CASE ("Controller: copy, exchange, delete", "[controller]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    SamplerController c (e, Limits::authentic());
    c.setSample ({ 0, 0 }, test::makeRamp (1000), "ramp");
    c.setSample ({ 0, 1 }, test::makeConstant (500, 0.1f), "const");

    REQUIRE (c.copySample ({ 0, 0 }, { 5, 11 }) == Status::ok);   // to another bank
    REQUIRE (c.project().slot ({ 5, 11 }).sample == c.project().slot ({ 0, 0 }).sample);

    REQUIRE (c.exchangeSamples ({ 0, 0 }, { 0, 1 }) == Status::ok);
    REQUIRE (c.project().slot ({ 0, 0 }).sample->length() == 500);
    REQUIRE (c.project().slot ({ 0, 1 }).sample->length() == 1000);

    REQUIRE (c.deleteSample ({ 0, 1 }) == Status::ok);
    REQUIRE (c.project().slot ({ 0, 1 }).isEmpty());
    REQUIRE (c.deleteSample ({ 0, 1 }) == Status::emptyPad);
}

TEST_CASE ("Controller: protected banks refuse edits (Prt)", "[controller]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    SamplerController c (e, Limits::authentic());
    c.setSample ({ 1, 0 }, test::makeRamp (1000), "ramp");
    c.project().sampleBankProtected[1] = true;

    REQUIRE (c.deleteSample ({ 1, 0 }) == Status::protectedBank);
    REQUIRE (c.setSample ({ 1, 1 }, test::makeRamp (10), "x") == Status::protectedBank);
    REQUIRE (c.copySample ({ 0, 0 }, { 1, 2 }) == Status::protectedBank);
    REQUIRE (c.editSettings ({ 1, 0 }, [] (PadSettings& s) { s.loop = true; }) == Status::protectedBank);
}

TEST_CASE ("Controller: truncate requires MARK and rewrites the sample", "[controller]")
{
    Engine e;
    e.prepare (kEngineRate, 64, Limits::authentic());
    SamplerController c (e, Limits::authentic());
    c.setSample ({ 0, 0 }, test::makeRamp (20000), "ramp");

    REQUIRE (c.truncate ({ 0, 0 }) == Status::invalid);   // MARK not lit
    c.editSettings ({ 0, 0 }, [] (PadSettings& s) { edit::setRegion (s, 5000, 15000, 20000); });
    REQUIRE (c.truncate ({ 0, 0 }) == Status::ok);
    const auto& slot = c.project().slot ({ 0, 0 });
    REQUIRE (slot.sample->length() == 10000);
    REQUIRE (slot.sample->channel (0)[0] == test::makeRamp (20000)->channel (0)[5000]);
    REQUIRE_FALSE (slot.settings.isMarked (slot.sample->length()));
}

TEST_CASE ("Controller (Extended): Time Modify renders in the background and plays", "[controller][timemodify]")
{
    Engine e;
    e.prepare (kEngineRate, 256, Limits::extended());
    SamplerController c (e, Limits::extended());
    c.setSample ({ 0, 0 }, test::makeSine (88200, 1000.0), "sine");
    c.editSettings ({ 0, 0 }, [] (PadSettings& s)
    {
        s.timeModify = TimeModifyMode::bpm;
        s.timeModifyBpm = 150.0;   // 120 -> 150: 1.25x speed
    });
    c.waitForJobs();
    test::render (e, 256);   // apply the stretched buffer

    e.post (test::cmd (Command::Type::padDown, { 0, 0 }));
    auto out = test::render (e, 3 * 44100, 256);
    int last = 0;
    for (int i = 0; i < (int) out.l.size(); ++i)
        if (std::abs (out.l[(size_t) i]) > 1e-3f)
            last = i;
    REQUIRE (std::abs (last - 70560) < 2000);
}

TEST_CASE ("Project JSON round trip", "[state]")
{
    Project p;
    p.patternTempo = 93.0;
    p.device.outputGainDb = 6;
    p.device.midiChannel = 10;
    p.device.illumination = 4;
    p.sampleBankProtected[3] = true;
    auto& slot = p.slot ({ 4, 7 });
    slot.file = "samples/E0000008.WAV";
    slot.settings.gate = true;
    slot.settings.loop = true;
    slot.settings.level = 90;
    slot.settings.start = 123;
    slot.settings.end = 45678;
    slot.settings.bpm = 97.5;
    slot.settings.timeModify = TimeModifyMode::pattern;

    const auto json = juce::JSON::toString (p.toVar());
    Project q;
    REQUIRE (Project::fromVar (juce::JSON::parse (json), q).wasOk());
    REQUIRE (q.patternTempo == 93.0);
    REQUIRE (q.device == p.device);
    REQUIRE (q.sampleBankProtected[3]);
    REQUIRE (q.slot ({ 4, 7 }).file == slot.file);
    REQUIRE (q.slot ({ 4, 7 }).settings == slot.settings);
    REQUIRE (q.slot ({ 0, 0 }).file.isEmpty());

    Project bad;
    REQUIRE (Project::fromVar (juce::JSON::parse ("{\"schema\": 99}"), bad).failed());
}
