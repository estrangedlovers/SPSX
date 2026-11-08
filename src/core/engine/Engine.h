#pragma once

#include "VoicePool.h"
#include "RecordTap.h"
#include "../Limits.h"
#include "../state/SampleData.h"
#include "../state/PadSettings.h"
#include "../fx/EffectHost.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <bitset>

namespace spsx
{
    /** A message to the engine. Plain data so it can travel through lock-free FIFOs.
        `time` is an absolute engine sample time; -1 means "as soon as possible". */
    struct Command
    {
        enum class Type : std::uint8_t
        {
            // performance
            padDown, padUp,
            holdDown,                 // HOLD pad (latches every pad currently held)
            subDown, subUp,           // SUB PAD
            stopPad,
            stopAll,                  // CANCEL x4, MIDI All Sound Off
            pause, resume,            // FUNC + TAP TEMPO

            // samples and settings
            setSample,                // sample pointer, ownership of one reference transfers
            setStretched,             // Extended Time Modify render (value = speed it was rendered for)
            setSettings,
            setPatternTempo,          // value = BPM
            setOutputGainDb,          // value = dB
            setVolume,                // value = 0..1 knob position
            setVoiceBudget,           // value = channel voices
            setClipOutput,            // value != 0

            // effects (Phase 3)
            setEffect,                // index = slot, value = effect 0..28 or -1
            setEffectParams,          // index = effect, params = CTRL 1..3 + hidden
            setEffectLimit,           // value != 0
            setTargetPad,             // pad, value != 0
            setTargetAll,             // value != 0
            setTargetExt,             // value != 0
            clearTargets,

            // input, EXT SOURCE, sampling (Phase 4)
            setInputGainDb,           // value = -10 or 0 (LINE)
            setMicMode,               // value != 0: input is the mic (mono, MIC LEVEL)
            setMicGainDb,             // value = dB
            setExtOn,                 // value != 0
            setExtStereo,             // value != 0
            setExtLevel,              // value = 0..127
            setMonitor,               // sampling standby: value != 0, index = stereo
            setDigitalLevel,          // value = gain (CTRL 3 while sampling)
            setResampleLevel,         // value = gain (CTRL 3 while resampling)
            recArm,                   // ptr = RecordTap*, index = source (0 input, 1 resample),
                                      // params = { auto-trigger gain or 0, count-in beats, wait-for-start }, value = tempo
            recStart,                 // starts an armed wait-for-start recording (resample)
            recStop,
            recCancel
        };

        Type type = Type::stopAll;
        PadId pad = kNoPad;
        double value = 0.0;
        int index = 0;
        SampleData* sample = nullptr;
        void* ptr = nullptr;
        std::array<float, 4> params {};
        PadSettings settings {};
        std::int64_t time = -1;
    };

    /** Recorder phases, published for the panel (display codes rdY, -1-..-4-, rEC, FUL). */
    enum class RecPhase : int { idle, waitingStart, waitingTrigger, countIn, recording, full };

    /** The audio-side engine. Runs at a fixed rate (normally 44.1 kHz).

        Signal flow per block (SPEC 7.1, 10.1, 10.5, 10.6):
          voices -> dry bus or effect bus (per pad, by effect target, glided)
          live input -> input gain / MIC LEVEL -> PEAK
                     -> EXT SOURCE (level, stereo/mono) -> dry or effect bus
                     -> sampling monitor (digital level) -> effect bus
          effect bus -> EffectHost -> + dry = mix
          recorder <- effect bus (sampling) or mix x level (resample)
          mix -> OUTPUT GAIN -> clip -> VOLUME (+ count-in click) -> out

        Thread contract:
        - process(), schedule(): audio thread only.
        - post(), collectGarbage(): message thread.
        - getters marked "any thread": lock-free reads.
        No locks or allocation after prepare(). Samples are published by pointer with
        a reference held on the audio side; retired samples go back to the message
        thread through a FIFO once no voice reads them. */
    class Engine
    {
    public:
        Engine();
        ~Engine();

        void prepare (double rate, int maxBlock, const Limits& limits);
        double getRate() const noexcept { return rate; }
        int getMaxBlock() const noexcept { return maxBlock; }

        // ---- message thread ----------------------------------------------------------
        bool post (const Command& c) noexcept;
        bool postSample (PadId pad, SampleData::Ptr s);
        bool postStretched (PadId pad, SampleData::Ptr s, double speed);
        bool postSettings (PadId pad, const PadSettings& settings);
        void collectGarbage();

        // ---- any thread (lock-free reads) -------------------------------------------
        SampleIndex playPosition (PadId pad) const noexcept { return positions[(size_t) pad.index()].load (std::memory_order_relaxed); }
        int activeChannelVoices() const noexcept { return voicesInUse.load (std::memory_order_relaxed); }
        float outputPeak() const noexcept { return peak.load (std::memory_order_relaxed); }
        bool isPaused() const noexcept { return pausedFlag.load (std::memory_order_relaxed); }
        /** PEAK indicator: analog input level is high (lit with a short hold). */
        bool inputPeakLed() const noexcept { return peakLedFlag.load (std::memory_order_relaxed); }
        /** Display illumination red: the digital sampling/resampling level is clipping. */
        bool digitalOverload() const noexcept { return overloadFlag.load (std::memory_order_relaxed); }
        RecPhase recordingPhase() const noexcept { return (RecPhase) recPhaseFlag.load (std::memory_order_relaxed); }
        int countInBeat() const noexcept { return countInFlag.load (std::memory_order_relaxed); }
        /** True while the engine holds a RecordTap pointer (the owner must keep it alive). */
        bool isTapAttached() const noexcept { return tapAttached.load (std::memory_order_acquire); }
        /** Tempo synced effects follow (longest targeted sample, else pattern). */
        double effectTempo() const noexcept { return effectTempoFlag.load (std::memory_order_relaxed); }

        // ---- audio thread ------------------------------------------------------------
        void schedule (const Command& c) noexcept;
        void process (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept;
        std::int64_t now() const noexcept { return time; }

    private:
        struct PadState
        {
            SampleData* sample = nullptr;      // engine holds one reference
            SampleData* stretched = nullptr;
            double stretchedSpeed = 1.0;
            PadSettings settings {};
        };

        /** Count-in and metronome click: short decaying sine, accented first beat. */
        struct Click
        {
            float phase = 0.0f, inc = 0.0f, amp = 0.0f, decay = 0.999f;
            void trigger (bool accent, double sr) noexcept
            {
                phase = 0.0f;
                inc = (float) ((accent ? 1760.0 : 1320.0) / sr);
                amp = accent ? 0.5f : 0.35f;
                decay = (float) std::exp (-1.0 / (0.025 * sr));
            }
            float next() noexcept
            {
                if (amp < 1e-4f) return 0.0f;
                const float y = amp * std::sin (6.2831853f * phase);
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
                amp *= decay;
                return y;
            }
        };

        void execute (const Command& c) noexcept;
        void renderSegment (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n) noexcept;
        void runRecorder (int n) noexcept;
        void padDown (PadId p, bool viaSub) noexcept;
        void padUp (PadId p, bool viaSub) noexcept;
        void startVoice (PadId p, bool viaSub) noexcept;
        bool isTarget (PadId p) const noexcept;
        double timeModifySpeed (const PadSettings& s) const noexcept;
        void updateEffectTempo() noexcept;
        void detachTap (RecPhase finalPhase) noexcept;
        Voice::Source sourceFor (const PadState& ps) const noexcept;
        void retire (SampleData* s) noexcept;
        void flushRetired() noexcept;
        void publishState() noexcept;

        double rate = kEngineRate;
        int maxBlock = 512;
        std::int64_t time = 0;

        VoicePool voices;
        std::array<PadState, kMaxPads> pads {};
        PadId lastStruck = kNoPad;
        bool paused = false;
        double patternTempo = 120.0;
        float outputGain = 1.0f;
        float volume = 1.0f;
        bool clip = true;

        // effects
        fx::EffectHost effects;
        std::bitset<kMaxPads> fxTargets;
        bool fxTargetAll = false, fxTargetExt = false;
        double fxTempo = 120.0;

        // live input and EXT SOURCE
        float inputGain = 1.0f, micGain = 1.0f;
        bool micMode = false;
        bool extOn = false, extStereo = true;
        float extLevel = 1.0f, extGain = 0.0f, extSend = 0.0f, extRamp = 0.001f;
        bool monitor = false, monitorStereo = true;
        float digitalLevel = 1.0f, resampleLevel = 1.0f;
        int peakHold = 0, overloadHold = 0;

        // recorder
        RecordTap* tap = nullptr;
        RecPhase recPhase = RecPhase::idle;
        int recSource = 0;            // 0 input, 1 resample
        float autoThreshold = 0.0f;
        std::int64_t countInLeft = 0, samplesPerBeat = 0, maxFrames = 0, framesRecorded = 0;
        int countInBeats = 0;
        Click click;

        std::vector<float> bus[6];    // dry L/R, fx L/R, live L/R

        // UI -> audio
        static constexpr int kFifoSize = 2048;
        juce::AbstractFifo uiFifo { kFifoSize };
        std::vector<Command> uiQueue;

        // audio-thread timed events (kept sorted by time)
        static constexpr int kTimedSize = 1024;
        std::vector<Command> timed;
        int numTimed = 0;

        // retired samples waiting until no voice uses them, then audio -> message
        static constexpr int kRetireSize = 256;
        std::array<SampleData*, kRetireSize> retired {};
        int numRetired = 0;
        juce::AbstractFifo garbageFifo { kRetireSize };
        std::array<SampleData*, kRetireSize> garbage {};

        std::array<std::atomic<SampleIndex>, kMaxPads> positions;
        std::array<std::int64_t, kMaxPads> publishedStart {};
        std::atomic<int> voicesInUse { 0 };
        std::atomic<float> peak { 0.0f };
        std::atomic<bool> pausedFlag { false }, peakLedFlag { false }, overloadFlag { false }, tapAttached { false };
        std::atomic<int> recPhaseFlag { 0 }, countInFlag { 0 };
        std::atomic<double> effectTempoFlag { 120.0 };

        JUCE_DECLARE_NON_COPYABLE (Engine)
    };
}
