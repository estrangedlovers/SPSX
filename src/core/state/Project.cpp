#include "Project.h"

namespace spsx
{
    namespace
    {
        const char* timeModifyName (TimeModifyMode m)
        {
            switch (m)
            {
                case TimeModifyMode::off:     return "off";
                case TimeModifyMode::bpm:     return "bpm";
                case TimeModifyMode::pattern: return "pattern";
            }
            return "off";
        }

        TimeModifyMode timeModifyFrom (const juce::String& s)
        {
            if (s == "bpm")     return TimeModifyMode::bpm;
            if (s == "pattern") return TimeModifyMode::pattern;
            return TimeModifyMode::off;
        }

        juce::var settingsToVar (const PadSettings& s)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("gate", s.gate);
            o->setProperty ("loop", s.loop);
            o->setProperty ("reverse", s.reverse);
            o->setProperty ("lofi", s.lofi);
            o->setProperty ("level", s.level);
            o->setProperty ("start", (juce::int64) s.start);
            o->setProperty ("end", (juce::int64) s.end);
            o->setProperty ("bpm", s.bpm);
            o->setProperty ("timeModify", timeModifyName (s.timeModify));
            o->setProperty ("timeModifyBpm", s.timeModifyBpm);
            return o;
        }

        PadSettings settingsFromVar (const juce::var& v)
        {
            PadSettings s;
            s.gate = v.getProperty ("gate", s.gate);
            s.loop = v.getProperty ("loop", s.loop);
            s.reverse = v.getProperty ("reverse", s.reverse);
            s.lofi = v.getProperty ("lofi", s.lofi);
            s.level = juce::jlimit (0, 127, (int) v.getProperty ("level", s.level));
            s.start = (juce::int64) v.getProperty ("start", 0);
            s.end = (juce::int64) v.getProperty ("end", 0);
            s.bpm = v.getProperty ("bpm", 0.0);
            s.timeModify = timeModifyFrom (v.getProperty ("timeModify", "off").toString());
            s.timeModifyBpm = v.getProperty ("timeModifyBpm", 0.0);
            return s;
        }

        juce::var boolsToVar (const std::vector<bool>& b)
        {
            juce::Array<juce::var> a;
            for (bool x : b) a.add (x);
            return a;
        }

        void boolsFromVar (const juce::var& v, std::vector<bool>& out)
        {
            if (auto* a = v.getArray())
                for (int i = 0; i < juce::jmin (a->size(), (int) out.size()); ++i)
                    out[(size_t) i] = (bool) (*a)[i];
        }
    }

    Project::Project (int banks)
        : numBanks (juce::jlimit (1, kMaxBanks, banks)),
          pads ((size_t) (numBanks * kPadsPerBank)),
          patterns ((size_t) (numBanks * kPadsPerBank)),
          sampleBankProtected ((size_t) numBanks, false),
          patternBankProtected ((size_t) numBanks, false)
    {
    }

    juce::var Project::toVar() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("schema", kSchemaVersion);
        root->setProperty ("banks", numBanks);
        root->setProperty ("patternTempo", patternTempo);

        juce::Array<juce::var> padArray;
        for (int i = 0; i < (int) pads.size(); ++i)
        {
            const auto& p = pads[(size_t) i];
            if (p.file.isEmpty())
                continue;
            auto* o = new juce::DynamicObject();
            const auto id = PadId::fromIndex (i);
            o->setProperty ("bank", id.bank);
            o->setProperty ("pad", id.pad);
            o->setProperty ("file", p.file);
            o->setProperty ("settings", settingsToVar (p.settings));
            padArray.add (o);
        }
        root->setProperty ("pads", padArray);

        juce::Array<juce::var> patternArray;
        for (int i = 0; i < (int) patterns.size(); ++i)
        {
            const auto& pat = patterns[(size_t) i];
            if (pat == nullptr)
                continue;
            auto* o = new juce::DynamicObject();
            const auto id = PadId::fromIndex (i);
            o->setProperty ("bank", id.bank);
            o->setProperty ("pad", id.pad);
            o->setProperty ("bars", pat->lengthBars);
            o->setProperty ("tempo", pat->tempo);
            juce::Array<juce::var> ev;
            for (const auto& e : pat->events)
            {
                juce::Array<juce::var> item { e.tick, e.length, (int) e.pad };
                if (e.velocity != 127)
                    item.add ((int) e.velocity);   // [tick, length, pad (, velocity)]
                ev.add (item);
            }
            o->setProperty ("events", ev);
            patternArray.add (o);
        }
        root->setProperty ("patterns", patternArray);
        root->setProperty ("sampleBankProtected", boolsToVar (sampleBankProtected));
        root->setProperty ("patternBankProtected", boolsToVar (patternBankProtected));

        auto* d = new juce::DynamicObject();
        d->setProperty ("inputGainDb", device.inputGainDb);
        d->setProperty ("outputGainDb", device.outputGainDb);
        d->setProperty ("effectLimit", device.effectLimit);
        d->setProperty ("illumination", device.illumination);
        d->setProperty ("midiChannel", device.midiChannel);
        d->setProperty ("midiSync", device.midiSync);
        d->setProperty ("autoTrigLevel", device.autoTrigLevel);
        d->setProperty ("aiffFiles", device.aiffFiles);
        d->setProperty ("velocityCurve", device.velocityCurve);
        juce::Array<juce::var> map;
        for (const auto& b : device.midiMap)
            map.add (juce::Array<juce::var> { b.channel, b.cc, b.knob ? 1 : 0, b.id });
        d->setProperty ("midiMap", map);
        root->setProperty ("device", d);
        return root;
    }

    juce::Result Project::fromVar (const juce::var& v, Project& out)
    {
        if (! v.isObject())
            return juce::Result::fail ("Project data is not an object");
        const int schema = v.getProperty ("schema", 0);
        if (schema < 1 || schema > kSchemaVersion)
            return juce::Result::fail ("Unsupported project schema " + juce::String (schema));

        Project p ((int) v.getProperty ("banks", kAuthenticBanks));
        p.patternTempo = v.getProperty ("patternTempo", 120.0);

        if (auto* arr = v.getProperty ("pads", {}).getArray())
        {
            for (const auto& item : *arr)
            {
                const PadId id { (int) item.getProperty ("bank", -1), (int) item.getProperty ("pad", -1) };
                if (! id.isValid() || id.bank >= p.numBanks)
                    continue;
                auto& slot = p.slot (id);
                slot.file = item.getProperty ("file", {}).toString();
                slot.settings = settingsFromVar (item.getProperty ("settings", {}));
            }
        }

        if (auto* arr = v.getProperty ("patterns", {}).getArray())
        {
            for (const auto& item : *arr)
            {
                const PadId id { (int) item.getProperty ("bank", -1), (int) item.getProperty ("pad", -1) };
                if (! id.isValid() || id.bank >= p.numBanks)
                    continue;
                std::vector<SeqEvent> events;
                if (auto* ev = item.getProperty ("events", {}).getArray())
                {
                    for (const auto& e : *ev)
                    {
                        auto* triple = e.getArray();
                        if (triple == nullptr || triple->size() < 3) continue;
                        SeqEvent se;
                        se.tick = (int) (*triple)[0];
                        se.length = juce::jmax (0, (int) (*triple)[1]);   // 0 = one-shot (MIDI)
                        se.pad = (std::int16_t) juce::jlimit (0, kMaxPads - 1, (int) (*triple)[2]);
                        if (triple->size() > 3)
                            se.velocity = (std::uint8_t) juce::jlimit (1, 127, (int) (*triple)[3]);
                        events.push_back (se);
                        if ((int) events.size() >= kMaxPatternEvents) break;
                    }
                }
                const int bars = item.getProperty ("bars", 1);
                for (auto& e : events)
                    e.tick = juce::jlimit (0, juce::jlimit (1, 99, bars) * kTicksPerBar - 1, (int) e.tick);
                p.pattern (id) = new PatternData (bars, (double) item.getProperty ("tempo", 120.0), std::move (events));
            }
        }

        boolsFromVar (v.getProperty ("sampleBankProtected", {}), p.sampleBankProtected);
        boolsFromVar (v.getProperty ("patternBankProtected", {}), p.patternBankProtected);

        const auto d = v.getProperty ("device", {});
        p.device.inputGainDb = d.getProperty ("inputGainDb", 0);
        p.device.outputGainDb = d.getProperty ("outputGainDb", 0);
        p.device.effectLimit = d.getProperty ("effectLimit", false);
        p.device.illumination = d.getProperty ("illumination", 1);
        p.device.midiChannel = d.getProperty ("midiChannel", 1);
        p.device.midiSync = d.getProperty ("midiSync", 0);
        p.device.autoTrigLevel = d.getProperty ("autoTrigLevel", 0);
        p.device.aiffFiles = d.getProperty ("aiffFiles", false);
        p.device.velocityCurve = juce::jlimit (0, 2, (int) d.getProperty ("velocityCurve", 2));
        p.device.midiMap.clear();
        if (auto* map = d.getProperty ("midiMap", {}).getArray())
            for (const auto& item : *map)
                if (auto* a = item.getArray(); a != nullptr && a->size() >= 4)
                {
                    MidiBinding b;
                    b.channel = juce::jlimit (1, 16, (int) (*a)[0]);
                    b.cc = juce::jlimit (0, 127, (int) (*a)[1]);
                    b.knob = (int) (*a)[2] != 0;
                    b.id = (int) (*a)[3];
                    if (b.id >= 0 && b.id < 256)   // PanelModel ignores ids it does not know
                        p.device.midiMap.push_back (b);
                }

        out = std::move (p);
        return juce::Result::ok();
    }
}
