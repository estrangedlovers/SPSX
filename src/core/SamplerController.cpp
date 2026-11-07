#include "SamplerController.h"
#include "io/AudioFileIO.h"
#include "sampler/StretchRenderer.h"
#include "sampler/TimeModify.h"

namespace spsx
{
    SamplerController::SamplerController (Engine& e, const Limits& l)
        : engine (e), lim (l), proj (l.banks), stretchGeneration ((size_t) kMaxPads, 0)
    {
    }

    SamplerController::~SamplerController()
    {
        pool.removeAllJobs (true, 10000);
    }

    void SamplerController::setLimits (const Limits& l)
    {
        lim = l;
        Command c;
        c.type = Command::Type::setVoiceBudget;
        c.value = l.channelVoices;
        engine.post (c);
        c.type = Command::Type::setClipOutput;
        c.value = l.clipOutput ? 1.0 : 0.0;
        engine.post (c);
    }

    void SamplerController::setProject (Project p)
    {
        // Clear pads the new project does not cover.
        for (int i = 0; i < (int) proj.pads.size(); ++i)
            if (! proj.pads[(size_t) i].isEmpty())
                engine.postSample (PadId::fromIndex (i), nullptr);

        proj = std::move (p);
        for (int i = 0; i < (int) proj.pads.size(); ++i)
            pushPad (PadId::fromIndex (i));
        setPatternTempo (proj.patternTempo);
        setOutputGainDb (proj.device.outputGainDb);
    }

    bool SamplerController::isProtected (PadId pad) const
    {
        return pad.bank < (int) proj.sampleBankProtected.size() && proj.sampleBankProtected[(size_t) pad.bank];
    }

    void SamplerController::pushPad (PadId pad)
    {
        const auto& slot = proj.slot (pad);
        engine.postSettings (pad, slot.settings);
        engine.postSample (pad, slot.sample);
        requestStretch (pad);
    }

    // ---- samples ------------------------------------------------------------------------

    SamplerController::Status SamplerController::setSample (PadId pad, SampleData::Ptr sample, const juce::String& fileRef)
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return Status::invalid;
        if (isProtected (pad)) return Status::protectedBank;

        auto& slot = proj.slot (pad);
        slot.sample = sample;
        slot.file = fileRef;
        slot.settings = sample != nullptr ? edit::defaultsFor (*sample) : PadSettings {};
        pushPad (pad);
        return Status::ok;
    }

    SamplerController::Status SamplerController::loadFile (PadId pad, const juce::File& file)
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return Status::invalid;
        if (isProtected (pad)) return Status::protectedBank;

        auto r = io::loadFile (file, lim.requantize16);
        if (r.sample == nullptr)
            return r.code == "FUL" ? Status::full : Status::unsupported;
        return setSample (pad, r.sample, file.getFullPathName());
    }

    void SamplerController::loadFileAsync (PadId pad, const juce::File& file, std::function<void (Status)> done)
    {
        const bool q = lim.requantize16;
        pool.addJob ([this, pad, file, q, done]
        {
            auto r = io::loadFile (file, q);
            Finished f { Finished::Kind::load, pad, r.sample, file.getFullPathName(),
                         r.sample != nullptr ? Status::ok : (r.code == "FUL" ? Status::full : Status::unsupported),
                         1.0, 0, done };
            std::lock_guard<std::mutex> lock (finishedLock);
            finished.push_back (std::move (f));
        });
    }

    SamplerController::Status SamplerController::deleteSample (PadId pad)
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return Status::invalid;
        if (isProtected (pad)) return Status::protectedBank;
        if (proj.slot (pad).isEmpty()) return Status::emptyPad;
        return setSample (pad, nullptr, {});
    }

    SamplerController::Status SamplerController::copySample (PadId from, PadId to)
    {
        if (! from.isValid() || ! to.isValid()) return Status::invalid;
        if (isProtected (to)) return Status::protectedBank;
        const auto src = proj.slot (from);
        if (src.isEmpty()) return Status::emptyPad;

        auto& dst = proj.slot (to);
        dst = src;   // SampleData is immutable, so sharing it is a copy
        pushPad (to);
        return Status::ok;
    }

    SamplerController::Status SamplerController::exchangeSamples (PadId a, PadId b)
    {
        if (! a.isValid() || ! b.isValid()) return Status::invalid;
        if (isProtected (a) || isProtected (b)) return Status::protectedBank;
        std::swap (proj.slot (a), proj.slot (b));
        pushPad (a);
        pushPad (b);
        return Status::ok;
    }

    SamplerController::Status SamplerController::truncate (PadId pad)
    {
        if (! pad.isValid()) return Status::invalid;
        if (isProtected (pad)) return Status::protectedBank;
        auto& slot = proj.slot (pad);
        if (slot.isEmpty()) return Status::emptyPad;
        if (! slot.settings.isMarked (slot.sample->length())) return Status::invalid;   // MARK must be lit

        // Extended mode makes truncate undoable (Phase 7); the operation itself is the same.
        auto s = slot.settings;
        auto newSample = edit::truncate (*slot.sample, s);
        slot.sample = newSample;
        slot.settings = s;
        pushPad (pad);
        return Status::ok;
    }

    // ---- settings -----------------------------------------------------------------------

    SamplerController::Status SamplerController::editSettings (PadId pad, const std::function<void (PadSettings&)>& edit)
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return Status::invalid;
        if (isProtected (pad)) return Status::protectedBank;
        auto& slot = proj.slot (pad);
        if (slot.isEmpty()) return Status::emptyPad;

        const auto before = slot.settings;
        edit (slot.settings);
        if (slot.settings == before)
            return Status::ok;

        engine.postSettings (pad, slot.settings);

        const bool regionOrTimeChanged = before.start != slot.settings.start || before.end != slot.settings.end
                                      || before.timeModify != slot.settings.timeModify
                                      || ! juce::exactlyEqual (before.timeModifyBpm, slot.settings.timeModifyBpm)
                                      || ! juce::exactlyEqual (before.bpm, slot.settings.bpm) || before.reverse != slot.settings.reverse;
        if (regionOrTimeChanged)
            requestStretch (pad);
        return Status::ok;
    }

    const PadSettings* SamplerController::settings (PadId pad) const
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return nullptr;
        return &proj.slot (pad).settings;
    }

    void SamplerController::setPatternTempo (double bpm)
    {
        proj.patternTempo = bpm;
        Command c;
        c.type = Command::Type::setPatternTempo;
        c.value = bpm;
        engine.post (c);

        // Pads following the pattern tempo need a fresh render in Extended mode.
        for (int i = 0; i < (int) proj.pads.size(); ++i)
            if (! proj.pads[(size_t) i].isEmpty() && proj.pads[(size_t) i].settings.timeModify == TimeModifyMode::pattern)
                requestStretch (PadId::fromIndex (i));
    }

    void SamplerController::setOutputGainDb (int db)
    {
        proj.device.outputGainDb = db;
        Command c;
        c.type = Command::Type::setOutputGainDb;
        c.value = db;
        engine.post (c);
    }

    void SamplerController::setVolume (double knob01)
    {
        Command c;
        c.type = Command::Type::setVolume;
        c.value = knob01;
        engine.post (c);
    }

    void SamplerController::requestStretch (PadId pad)
    {
        auto& gen = stretchGeneration[(size_t) pad.index()];
        ++gen;   // invalidates any render in flight

        if (! lim.highQualityStretch)
            return;   // Authentic: real-time granular in the voice
        const auto& slot = proj.slot (pad);
        if (slot.isEmpty())
            return;

        const double speed = timemodify::sourceSpeed (slot.settings, proj.patternTempo);
        if (std::abs (speed - 1.0) < 1e-9)
            return;

        const auto sample = slot.sample;
        const auto st = slot.settings.start, en = slot.settings.end > 0 ? slot.settings.end : sample->length();
        const auto myGen = gen;

        pool.addJob ([this, pad, sample, st, en, speed, myGen]
        {
            auto rendered = renderStretched (*sample, st, en, speed);
            Finished f { Finished::Kind::stretch, pad, rendered, {}, Status::ok, speed, myGen, {} };
            std::lock_guard<std::mutex> lock (finishedLock);
            finished.push_back (std::move (f));
        });
    }

    void SamplerController::tick()
    {
        engine.collectGarbage();

        std::vector<Finished> ready;
        {
            std::lock_guard<std::mutex> lock (finishedLock);
            ready.swap (finished);
        }

        for (auto& f : ready)
        {
            if (f.kind == Finished::Kind::load)
            {
                auto status = f.status == Status::ok ? setSample (f.pad, f.sample, f.fileRef) : f.status;
                if (f.done)
                    f.done (status);
            }
            else if (f.generation == stretchGeneration[(size_t) f.pad.index()])
            {
                engine.postStretched (f.pad, f.sample, f.speed);
            }
        }
    }

    void SamplerController::waitForJobs()
    {
        while (pool.getNumJobs() > 0)
            juce::Thread::sleep (2);
        tick();
    }
}
