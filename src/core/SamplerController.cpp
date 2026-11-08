#include "SamplerController.h"
#include "io/AudioFileIO.h"
#include "sampler/StretchRenderer.h"
#include "sampler/TimeModify.h"

namespace spsx
{
    /** Drains a RecordTap to disk on its own thread. 16-bit output is written as
        integers (bit-exact, no dither); 32-bit output as float. */
    class SamplerController::RecordingSession final : private juce::Thread
    {
    public:
        RecordingSession (const juce::File& f, int channels, bool aiff, int bits)
            : juce::Thread ("SPSX recorder"), file (f), tap (channels, (int) (kEngineRate * 10.0)), bitDepth (bits)
        {
            file.getParentDirectory().createDirectory();
            file.deleteFile();
            std::unique_ptr<juce::AudioFormat> format;
            if (aiff) format = std::make_unique<juce::AiffAudioFormat>();
            else      format = std::make_unique<juce::WavAudioFormat>();
            std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
            if (os != nullptr)
                writer = format->createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (kEngineRate)
                                                          .withNumChannels (channels)
                                                          .withBitsPerSample (bits)
                                                          .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral));
            if (writer != nullptr)
                startThread (juce::Thread::Priority::high);
        }

        ~RecordingSession() override { finish(); }

        bool isOk() const noexcept { return writer != nullptr; }
        RecordTap& getTap() noexcept { return tap; }
        const juce::File& getFile() const noexcept { return file; }

        /** Stops after writing everything already in the FIFO, then closes the file. */
        void finish()
        {
            done.store (true);
            stopThread (10000);
            writer.reset();
        }

    private:
        void run() override
        {
            constexpr int chunk = 4096;
            std::vector<float> l ((size_t) chunk), r ((size_t) chunk);
            std::vector<int> il ((size_t) chunk), ir ((size_t) chunk);
            while (true)
            {
                const int n = tap.read (l.data(), r.data(), chunk);
                if (n > 0)
                    write (l.data(), r.data(), il.data(), ir.data(), n);
                else if (done.load())
                    break;
                else
                    wait (5);
            }
        }

        void write (const float* l, const float* r, int* il, int* ir, int n)
        {
            if (bitDepth == 32)
            {
                const float* ch[2] = { l, r };
                writer->writeFromFloatArrays (ch, tap.numChannels(), n);
                return;
            }
            const float scale = bitDepth >= 24 ? 8388608.0f : 32768.0f;
            const int shift = bitDepth >= 24 ? 8 : 16;
            const int maxCode = (int) scale - 1;
            auto convert = [&] (const float* x, int* out)
            {
                for (int i = 0; i < n; ++i)
                    out[i] = (int) ((unsigned int) juce::jlimit (-(int) scale, maxCode, (int) std::lrint (x[i] * scale)) << shift);
            };
            convert (l, il);
            convert (r, ir);
            const int* ch[3] = { il, ir, nullptr };
            writer->write (ch, n);
        }

        juce::File file;
        RecordTap tap;
        int bitDepth;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        std::atomic<bool> done { false };
    };

    SamplerController::SamplerController (Engine& e, const Limits& l)
        : engine (e), lim (l), proj (l.banks), stretchGeneration ((size_t) kMaxPads, 0)
    {
    }

    SamplerController::~SamplerController()
    {
        if (session != nullptr && engine.isTapAttached())
        {
            // The audio thread may still write into the tap; only possible when the
            // engine is being torn down with us, after audio stopped.
            jassertfalse;
        }
        session.reset();
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

        for (int i = 0; i < (int) proj.patterns.size(); ++i)
            if (proj.patterns[(size_t) i] != nullptr)
                engine.postPattern (i, nullptr);

        proj = std::move (p);
        for (int i = 0; i < (int) proj.pads.size(); ++i)
            pushPad (PadId::fromIndex (i));
        for (int i = 0; i < (int) proj.patterns.size(); ++i)
            if (proj.patterns[(size_t) i] != nullptr)
                pushPattern (PadId::fromIndex (i));
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

    // ---- patterns -----------------------------------------------------------------------

    bool SamplerController::isPatternProtected (PadId pad) const
    {
        return pad.bank < (int) proj.patternBankProtected.size() && proj.patternBankProtected[(size_t) pad.bank];
    }

    bool SamplerController::hasPattern (PadId pad) const
    {
        return pad.isValid() && pad.bank < proj.numBanks && proj.pattern (pad) != nullptr;
    }

    void SamplerController::pushPattern (PadId pad)
    {
        engine.postPattern (pad.index(), proj.pattern (pad));
    }

    SamplerController::Status SamplerController::setPattern (PadId pad, PatternData::Ptr pattern)
    {
        if (! pad.isValid() || pad.bank >= proj.numBanks) return Status::invalid;
        if (isPatternProtected (pad)) return Status::protectedBank;
        proj.pattern (pad) = pattern;
        pushPattern (pad);
        return Status::ok;
    }

    SamplerController::Status SamplerController::deletePattern (PadId pad)
    {
        if (! hasPattern (pad)) return Status::emptyPad;
        return setPattern (pad, nullptr);
    }

    SamplerController::Status SamplerController::copyPattern (PadId from, PadId to)
    {
        if (! hasPattern (from) || ! to.isValid() || to.bank >= proj.numBanks) return Status::emptyPad;
        return setPattern (to, proj.pattern (from));   // immutable: sharing is a copy
    }

    SamplerController::Status SamplerController::exchangePatterns (PadId a, PadId b)
    {
        if (! a.isValid() || ! b.isValid() || a.bank >= proj.numBanks || b.bank >= proj.numBanks) return Status::invalid;
        if (isPatternProtected (a) || isPatternProtected (b)) return Status::protectedBank;
        std::swap (proj.pattern (a), proj.pattern (b));
        pushPattern (a);
        pushPattern (b);
        return Status::ok;
    }

    SamplerController::Status SamplerController::commitRecordedPattern()
    {
        const auto& seq = engine.sequencer();
        const int index = seq.resultIndex();
        if (index < 0 || index >= kMaxPads) return Status::invalid;
        const auto pad = PadId::fromIndex (index);
        const int n = seq.resultCount();
        if (n <= 0)
            return proj.pattern (pad) != nullptr ? setPattern (pad, nullptr) : Status::emptyPad;   // everything erased
        std::vector<SeqEvent> events (seq.resultEvents().begin(), seq.resultEvents().begin() + n);
        for (auto& e : events) { e.pass = 0; e.id = 0; }
        return setPattern (pad, new PatternData (seq.resultLengthBars(), proj.patternTempo, std::move (events)));
    }

    void SamplerController::setPatternTempoOf (PadId pad, double bpm)
    {
        if (! hasPattern (pad) || isPatternProtected (pad)) return;
        const auto& old = *proj.pattern (pad);
        if (juce::exactlyEqual (old.tempo, bpm)) return;
        setPattern (pad, new PatternData (old.lengthBars, bpm, old.events));
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

    // ---- recording ----------------------------------------------------------------------

    SamplerController::Status SamplerController::beginRecording (const RecordOptions& o)
    {
        if (session != nullptr || loadingRecording) return Status::invalid;
        if (! o.destination.isValid() || o.destination.bank >= proj.numBanks) return Status::invalid;
        if (isProtected (o.destination)) return Status::protectedBank;
        if (! proj.slot (o.destination).isEmpty()) return Status::occupied;

        recordingsDir.createDirectory();
        if (recordingsDir.getBytesFreeOnVolume() < 10 * 1024 * 1024)
            return Status::full;

        const bool aiff = proj.device.aiffFiles;
        const auto name = juce::String::formatted ("SPSX_%c%02d_", (char) ('A' + o.destination.bank), o.destination.pad + 1)
                          + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + (aiff ? ".aif" : ".wav");
        const auto file = recordingsDir.getNonexistentChildFile (name.upToLastOccurrenceOf (".", false, false),
                                                                 aiff ? ".aif" : ".wav", false);
        // Authentic: 16-bit like the hardware. Extended: 32-bit float.
        auto s = std::make_unique<RecordingSession> (file, o.stereo ? 2 : 1, aiff, lim.requantize16 ? 16 : (aiff ? 24 : 32));
        if (! s->isOk())
            return Status::invalid;

        Command c;
        c.type = Command::Type::recArm;
        c.ptr = &s->getTap();
        c.index = o.resample ? 1 : 0;
        c.value = o.tempo;
        c.params = { o.autoTriggerGain, (float) o.countInBeats, o.waitForStart ? 1.0f : 0.0f, 0.0f };
        if (! engine.post (c))
            return Status::invalid;

        session = std::move (s);
        recordOptions = o;
        sessionAttachedOnce = false;
        stopRequested = cancelRequested = false;
        return Status::ok;
    }

    void SamplerController::startArmedRecording()
    {
        Command c;
        c.type = Command::Type::recStart;
        engine.post (c);
    }

    void SamplerController::stopRecording()
    {
        if (session == nullptr) return;
        stopRequested = true;
        Command c;
        c.type = Command::Type::recStop;
        engine.post (c);
    }

    void SamplerController::cancelRecording()
    {
        if (session == nullptr) return;
        cancelRequested = true;
        Command c;
        c.type = Command::Type::recCancel;
        engine.post (c);
    }

    void SamplerController::finishRecording (bool keep)
    {
        session->finish();
        const auto file = session->getFile();
        const auto frames = session->getTap().framesWritten.load();
        session.reset();

        if (! keep || frames <= 0)
        {
            file.deleteFile();
            if (onRecordingFinished) onRecordingFinished (keep ? Status::emptyPad : Status::ok);
            return;
        }

        loadingRecording = true;
        const auto o = recordOptions;
        loadFileAsync (o.destination, file, [this, o] (Status st)
        {
            loadingRecording = false;
            if (st == Status::ok && o.snapToTempo && o.tempo > 0.0)
            {
                // "the end point will be automatically set to a beat of the BPM value you
                // specified" (p. 26).
                editSettings (o.destination, [&] (PadSettings& s)
                {
                    const double beat = 60.0 / o.tempo * kEngineRate;
                    const auto len = proj.slot (o.destination).sample->length();
                    const double beats = std::max (1.0, std::round ((double) len / beat));
                    s.end = std::min<SampleIndex> (len, (SampleIndex) std::llround (beats * beat));
                    s.bpm = o.tempo;
                });
            }
            if (onRecordingFinished) onRecordingFinished (st);
        });
    }

    void SamplerController::tick()
    {
        if (session != nullptr)
        {
            const bool attached = engine.isTapAttached();
            sessionAttachedOnce = sessionAttachedOnce || attached;
            const bool engineReleased = sessionAttachedOnce && ! attached;
            if (engineReleased)
                finishRecording (! cancelRequested);
            else if (! sessionAttachedOnce && (stopRequested || cancelRequested))
                finishRecording (false);   // stopped before the engine picked it up
        }

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
