#pragma once

#include "engine/Engine.h"
#include "state/Project.h"
#include "sampler/SampleEdit.h"
#include "engine/RecordTap.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <mutex>

namespace spsx
{
    /** Message-thread facade over the document (Project) and the audio Engine.
        Every edit goes through here so the Project and the engine's pad table stay in
        step. Disk loads and Time Modify renders run on a worker pool; their results
        are applied in tick(), which the owner calls from a message-thread timer. */
    class SamplerController
    {
    public:
        enum class Status { ok, protectedBank, emptyPad, occupied, unsupported, full, invalid };

        SamplerController (Engine& engine, const Limits& limits);
        ~SamplerController();

        const Limits& limits() const noexcept { return lim; }
        void setLimits (const Limits& l);

        Project& project() noexcept { return proj; }
        const Project& project() const noexcept { return proj; }
        /** Replace the whole document and push it to the engine. */
        void setProject (Project p);

        // ---- samples ------------------------------------------------------------------
        Status loadFile (PadId pad, const juce::File& file);                  // synchronous
        void   loadFileAsync (PadId pad, const juce::File& file, std::function<void (Status)> done = {});
        Status setSample (PadId pad, SampleData::Ptr sample, const juce::String& fileRef);
        Status deleteSample (PadId pad);
        Status copySample (PadId from, PadId to);
        Status exchangeSamples (PadId a, PadId b);
        /** Destructive in Authentic mode (manual p. 29). Requires MARK lit. */
        Status truncate (PadId pad);

        // ---- settings -----------------------------------------------------------------
        Status editSettings (PadId pad, const std::function<void (PadSettings&)>& edit);
        const PadSettings* settings (PadId pad) const;

        void setPatternTempo (double bpm);

        // ---- patterns (Phase 5) ----------------------------------------------------------
        bool hasPattern (PadId pad) const;
        Status setPattern (PadId pad, PatternData::Ptr pattern);
        Status deletePattern (PadId pad);
        Status copyPattern (PadId from, PadId to);
        Status exchangePatterns (PadId a, PadId b);
        /** Stores the sequencer's finished take (after Engine::sequencer().takeResult()). */
        Status commitRecordedPattern();
        /** Sets the tempo of a stored pattern (TIME/BPM in pattern mode). */
        void setPatternTempoOf (PadId pad, double bpm);
        void setOutputGainDb (int db);
        void setVolume (double knob01);

        // ---- recording (sampling and resampling, Phase 4) -------------------------------
        struct RecordOptions
        {
            PadId destination = kNoPad;
            bool stereo = true;
            bool resample = false;              // false: input through the effect, true: the mix
            float autoTriggerGain = 0.0f;       // 0 = off, else start when input reaches it
            int countInBeats = 0;               // 4 = one bar of clicks before recording
            double tempo = 120.0;
            bool waitForStart = false;          // resample: armed until a pad starts it
            bool snapToTempo = false;           // a BPM was given in standby: end on a beat
        };

        /** Creates the file and writer thread and arms the engine. */
        Status beginRecording (const RecordOptions& options);
        /** Starts a wait-for-start recording (resample) in the same block as a pad hit. */
        void startArmedRecording();
        void stopRecording();
        void cancelRecording();
        /** True from beginRecording until the new sample has been written and loaded. */
        bool isRecording() const noexcept { return session != nullptr || loadingRecording; }
        PadId recordingDestination() const noexcept { return recordOptions.destination; }
        /** Called on the message thread when a recording landed on its pad (or failed). */
        std::function<void (Status)> onRecordingFinished;

        void setRecordingsDirectory (const juce::File& dir) { recordingsDir = dir; }
        juce::File getRecordingsDirectory() const { return recordingsDir; }

        /** Message-thread housekeeping: frees retired audio, applies finished jobs. */
        void tick();
        /** Blocks until background jobs finish, then applies them (tests, shutdown). */
        void waitForJobs();

    private:
        class RecordingSession;
        void finishRecording (bool keep);

        bool isProtected (PadId pad) const;
        bool isPatternProtected (PadId pad) const;
        void pushPattern (PadId pad);
        void pushPad (PadId pad);
        void requestStretch (PadId pad);

        struct Finished
        {
            enum class Kind { load, stretch } kind;
            PadId pad;
            SampleData::Ptr sample;
            juce::String fileRef;
            Status status = Status::ok;
            double speed = 1.0;
            std::uint64_t generation = 0;
            std::function<void (Status)> done;
        };

        Engine& engine;
        Limits lim;
        Project proj;
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("SPSX worker").withNumberOfThreads (2) };
        std::mutex finishedLock;
        std::vector<Finished> finished;
        std::vector<std::uint64_t> stretchGeneration;   // per pad, discards stale renders

        std::unique_ptr<RecordingSession> session;
        RecordOptions recordOptions;
        bool sessionAttachedOnce = false, stopRequested = false, cancelRequested = false, loadingRecording = false;
        juce::File recordingsDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                       .getChildFile ("SPSX").getChildFile ("Recordings");
    };
}
