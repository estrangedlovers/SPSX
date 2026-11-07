#pragma once

#include "VoicePool.h"
#include "../Limits.h"
#include "../state/SampleData.h"
#include "../state/PadSettings.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace spsx
{
    /** A message to the engine. Plain data so it can travel through lock-free FIFOs.
        `time` is an absolute engine sample time; -1 means "as soon as possible". */
    struct Command
    {
        enum class Type : std::uint8_t
        {
            padDown, padUp,
            holdDown,                 // HOLD pad pressed (latches every pad currently held)
            subDown, subUp,           // SUB PAD
            stopPad,
            stopAll,                  // CANCEL x4, MIDI All Sound Off
            pause, resume,            // FUNC + TAP TEMPO
            setSample,                // sample pointer, ownership of one reference transfers
            setStretched,             // Extended Time Modify render for `pad` (value = speed it was rendered for)
            setSettings,
            setPatternTempo,          // value = BPM
            setOutputGainDb,          // value = dB
            setVolume,                // value = 0..1 knob position
            setVoiceBudget,           // value = channel voices
            setClipOutput             // value != 0
        };

        Type type = Type::stopAll;
        PadId pad = kNoPad;
        double value = 0.0;
        SampleData* sample = nullptr;
        PadSettings settings {};
        std::int64_t time = -1;
    };

    /** The audio-side engine. Runs at a fixed rate (normally 44.1 kHz).

        Thread contract:
        - process(), schedule(): audio thread only.
        - post(), collectGarbage(), the getters marked "any thread": message thread.
        No locks or allocation after prepare(). Samples are published by pointer with
        a reference held on the audio side; retired samples go back to the message
        thread through a FIFO once no voice reads them, and are released there. */
    class Engine
    {
    public:
        Engine();
        ~Engine();

        void prepare (double rate, int maxBlock, const Limits& limits);
        double getRate() const noexcept { return rate; }
        int getMaxBlock() const noexcept { return maxBlock; }

        // ---- message thread ----------------------------------------------------------
        /** Returns false if the FIFO is full (the command is dropped; the caller keeps
            any sample reference it tried to pass). */
        bool post (const Command& c) noexcept;
        /** Convenience: takes its own reference to `s` (nullptr clears the pad). */
        bool postSample (PadId pad, SampleData::Ptr s);
        bool postStretched (PadId pad, SampleData::Ptr s, double speed);
        bool postSettings (PadId pad, const PadSettings& settings);
        /** Releases samples the audio thread has finished with. Call regularly. */
        void collectGarbage();

        // ---- any thread (lock-free reads) -------------------------------------------
        /** Source-sample play position of the newest voice on that pad, or -1. */
        SampleIndex playPosition (PadId pad) const noexcept { return positions[(size_t) pad.index()].load (std::memory_order_relaxed); }
        int activeChannelVoices() const noexcept { return voicesInUse.load (std::memory_order_relaxed); }
        float outputPeak() const noexcept { return peak.load (std::memory_order_relaxed); }
        bool isPaused() const noexcept { return pausedFlag.load (std::memory_order_relaxed); }

        // ---- audio thread ------------------------------------------------------------
        /** Queue a timed command from the audio thread (MIDI, sequencer). */
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

        void execute (const Command& c) noexcept;
        void renderSegment (float* const* out, int numOut, int offset, int n) noexcept;
        void padDown (PadId p, bool viaSub) noexcept;
        void padUp (PadId p, bool viaSub) noexcept;
        void startVoice (PadId p, bool viaSub) noexcept;
        Voice::Source sourceFor (const PadState& ps) const noexcept;
        double timeModifySpeed (const PadSettings& s) const noexcept;
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

        std::vector<float> bus[2];

        // UI -> audio
        static constexpr int kFifoSize = 1024;
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
        std::atomic<bool> pausedFlag { false };

        JUCE_DECLARE_NON_COPYABLE (Engine)
    };
}
