// Headless renderer: loads samples onto pads, plays a script, writes a WAV.
//
//   render_cli --pad A1=kick.wav --pad B12=loop.aif --script hits.txt --out out.wav
//              [--seconds 4] [--rate 48000] [--block 256] [--extended]
//
// Script lines (times in milliseconds, '#' starts a comment):
//   0     down A1          pad down       250  up A1         pad up
//   500   hold                            600  sub down / sub up
//   700   set A1 gate=1 loop=1 reverse=0 lofi=1 level=100 start=0 end=44100
//   800   tempo 128        900  stopall   950  pause / resume
//   1000  outgain 6        (dB)
//
// Rendering goes through EngineHost, so a --rate other than 44100 exercises the
// 44.1 kHz internal-rate conversion exactly as the plugin does.

#include "core/engine/Engine.h"
#include "core/engine/EngineHost.h"
#include "core/SamplerController.h"
#include "core/io/AudioFileIO.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>

using namespace spsx;

namespace
{
    std::optional<PadId> parsePad (const juce::String& s)
    {
        if (s.length() < 2) return std::nullopt;
        const int bank = juce::CharacterFunctions::toUpperCase (s[0]) - 'A';
        const int pad = s.substring (1).getIntValue() - 1;
        PadId id { bank, pad };
        if (! id.isValid()) return std::nullopt;
        return id;
    }

    struct Event
    {
        double ms;
        juce::StringArray tokens;
    };

    int fail (const juce::String& msg)
    {
        std::cerr << "render_cli: " << msg << std::endl;
        return 1;
    }
}

int main (int argc, char** argv)
{

    juce::StringArray padArgs;
    juce::File scriptFile, outFile;
    double seconds = 4.0, rate = kEngineRate;
    int block = 256;
    bool extended = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--pad")            padArgs.add (next());
        else if (a == "--script")    scriptFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--out")       outFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--seconds")   seconds = next().getDoubleValue();
        else if (a == "--rate")      rate = next().getDoubleValue();
        else if (a == "--block")     block = next().getIntValue();
        else if (a == "--extended")  extended = true;
        else return fail ("unknown argument " + a);
    }
    if (outFile == juce::File())
        return fail ("--out is required");

    const auto limits = extended ? Limits::extended() : Limits::authentic();
    Engine engine;
    EngineHost host (engine);
    host.prepare (rate, block, limits);
    SamplerController controller (engine, limits);

    for (auto& p : padArgs)
    {
        const auto id = parsePad (p.upToFirstOccurrenceOf ("=", false, false));
        if (! id) return fail ("bad pad " + p);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (p.fromFirstOccurrenceOf ("=", false, false));
        if (controller.loadFile (*id, file) != SamplerController::Status::ok)
            return fail ("UnS: cannot load " + file.getFullPathName());
    }

    std::vector<Event> events;
    if (scriptFile.existsAsFile())
    {
        juce::StringArray lines;
        scriptFile.readLines (lines);
        for (auto line : lines)
        {
            line = line.upToFirstOccurrenceOf ("#", false, false).trim();
            if (line.isEmpty()) continue;
            auto t = juce::StringArray::fromTokens (line, " \t", "");
            t.removeEmptyStrings();
            if (t.size() < 2) return fail ("bad script line: " + line);
            Event e { t[0].getDoubleValue(), {} };
            t.remove (0);
            e.tokens = t;
            events.push_back (e);
        }
        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.ms < b.ms; });
    }

    // Settings edits go through the controller (message side) before the block that
    // contains them; performance events are scheduled sample-accurately.
    const auto total = (juce::int64) std::llround (seconds * rate);
    juce::AudioBuffer<float> out (2, (int) total);
    out.clear();
    size_t next = 0;

    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) juce::jmin<juce::int64> (block, total - pos);

        while (next < events.size() && events[next].ms * rate / 1000.0 < (double) (pos + n))
        {
            const auto& ev = events[next++];
            const int offset = (int) juce::jlimit<juce::int64> (0, n - 1, (juce::int64) std::llround (ev.ms * rate / 1000.0) - pos);
            const auto& t = ev.tokens;
            Command c;
            auto sched = [&] (Command::Type type, PadId pad = kNoPad, double v = 0.0)
            {
                c.type = type; c.pad = pad; c.value = v;
                host.scheduleAt (c, offset);
            };

            if (t[0] == "down" || t[0] == "up")
            {
                auto id = parsePad (t[1]);
                if (! id) return fail ("bad pad " + t[1]);
                sched (t[0] == "down" ? Command::Type::padDown : Command::Type::padUp, *id);
            }
            else if (t[0] == "sub")      sched (t[1] == "down" ? Command::Type::subDown : Command::Type::subUp);
            else if (t[0] == "hold")     sched (Command::Type::holdDown);
            else if (t[0] == "stopall")  sched (Command::Type::stopAll);
            else if (t[0] == "pause")    sched (Command::Type::pause);
            else if (t[0] == "resume")   sched (Command::Type::resume);
            else if (t[0] == "tempo")    controller.setPatternTempo (t[1].getDoubleValue());
            else if (t[0] == "outgain")  controller.setOutputGainDb (t[1].getIntValue());
            else if (t[0] == "set")
            {
                auto id = parsePad (t[1]);
                if (! id) return fail ("bad pad " + t[1]);
                controller.editSettings (*id, [&t] (PadSettings& s)
                {
                    for (int k = 2; k < t.size(); ++k)
                    {
                        const auto key = t[k].upToFirstOccurrenceOf ("=", false, false);
                        const auto val = t[k].fromFirstOccurrenceOf ("=", false, false);
                        if (key == "gate") s.gate = val.getIntValue() != 0;
                        else if (key == "loop") s.loop = val.getIntValue() != 0;
                        else if (key == "reverse") s.reverse = val.getIntValue() != 0;
                        else if (key == "lofi") s.lofi = val.getIntValue() != 0;
                        else if (key == "level") s.level = juce::jlimit (0, 127, val.getIntValue());
                        else if (key == "start") s.start = val.getLargeIntValue();
                        else if (key == "end") s.end = val.getLargeIntValue();
                        else if (key == "tm") { s.timeModify = TimeModifyMode::bpm; s.timeModifyBpm = val.getDoubleValue(); }
                        else if (key == "tmptn") s.timeModify = TimeModifyMode::pattern;
                    }
                });
                controller.waitForJobs();
            }
            else return fail ("unknown command " + t[0]);
        }

        float* o[2] = { out.getWritePointer (0, (int) pos), out.getWritePointer (1, (int) pos) };
        host.process (nullptr, 0, o, 2, n);
        controller.tick();
    }

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream().release());
    if (os == nullptr) return fail ("cannot write " + outFile.getFullPathName());
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
        return fail ("write failed");

    std::cout << "Wrote " << outFile.getFullPathName() << " (" << seconds << " s at " << rate
              << " Hz, latency " << host.latencySamples() << " samples)" << std::endl;
    return 0;
}
