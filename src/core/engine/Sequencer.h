#pragma once

#include "../state/PatternData.h"
#include <array>
#include <atomic>
#include <bitset>
#include <vector>

namespace spsx
{
    /** The pattern sequencer (SPEC section 9). Audio thread only, except the atomics.

        Playback: each block, events whose tick falls inside the block are emitted with
        their exact sample offset (pad down, and pad up after the recorded length), so the
        engine's timed queue triggers them sample-accurately. At the end of the pattern it
        loops, or switches to the reserved pattern (p. 15).

        Recording (p. 32-33): standby runs the metronome; REC gives one bar of count-in,
        then loop recording with overdub. Pad hits are quantized as they are recorded
        (the hardware quantizes on input), shuffle delays every second step for S.8/S.16.
        Events recorded in the current pass are not played back until the next pass, so a
        hit never sounds twice. Rehearsal plays without recording; erase removes the held
        pads' events as the playhead passes them. All storage is preallocated. */
    class Sequencer
    {
    public:
        enum class RecState : int { off, standby, countIn, recording, rehearsal };

        /** What the sequencer asks the engine to do at an absolute sample time. */
        struct Event
        {
            enum class Kind { padDown, padUp, click } kind;
            std::int64_t time;
            int pad;          // PadId index
            bool accent;      // click: first beat of the bar
            float velocity = 1.0f;
        };

        void prepare (double sampleRate);

        // ---- patterns (pointer table owned by the engine) ---------------------------
        void setPatternTable (PatternData* const* table) noexcept { patterns = table; }

        // ---- transport --------------------------------------------------------------
        void play (int index) noexcept;          // start now from the top
        void reserve (int index) noexcept;       // start when the current pattern ends
        void stop() noexcept;
        void arm (int index) noexcept;           // wait for host / MIDI start (HOLD + pad, p. 47)

        // ---- recording -------------------------------------------------------------
        void recordStandby (int index, int lengthBars, const PatternData* existing) noexcept;
        void recordStart() noexcept;             // count-in, then recording
        void toggleRehearsal() noexcept;
        void recordStop() noexcept;              // commit: result() becomes valid
        void recordCancel() noexcept;
        void setLength (int bars) noexcept;
        void setQuantize (int index, int shuffle) noexcept { quantIndex = index; shufflePercent = shuffle; }
        void setEraseMode (bool on) noexcept { erasing = on; if (! on) { erasePads.reset(); eraseAll = false; } }
        void setErasePad (int padIndex, bool held) noexcept { if (padIndex >= 0 && padIndex < kMaxPads) erasePads.set ((size_t) padIndex, held); }
        void setEraseAll (bool held) noexcept { eraseAll = held; }

        /** Pad pressed / released by the performer (not by the sequencer). */
        /** `oneShot`: a MIDI Note On with no Note Off to come (SPEC U6); recorded with
            length 0, which plays back without a release. */
        void padDown (int padIndex, std::int64_t time, float velocity = 1.0f, bool oneShot = false) noexcept;
        void padUp (int padIndex, std::int64_t time) noexcept;

        // ---- host / external sync -----------------------------------------------------
        void setHostTransport (bool follow, bool playing, double ppq) noexcept;

        /** Generate this block's events. `emit (const Event&)` schedules them. */
        template <typename Emit>
        void process (std::int64_t blockStart, int n, double tempo, Emit&& emit) noexcept;

        // ---- state for the panel (any thread) -----------------------------------------
        int playingIndex() const noexcept  { return playingFlag.load (std::memory_order_relaxed); }
        int reservedIndex() const noexcept { return reservedFlag.load (std::memory_order_relaxed); }
        int armedIndex() const noexcept    { return armedFlag.load (std::memory_order_relaxed); }
        RecState recordState() const noexcept { return (RecState) recStateFlag.load (std::memory_order_relaxed); }
        int recordingIndex() const noexcept { return recIndexFlag.load (std::memory_order_relaxed); }
        /** Bar (1-based) and beat (1-4) at the play head; count-in beats left as -4..-1. */
        int measure() const noexcept { return measureFlag.load (std::memory_order_relaxed); }
        int beat() const noexcept    { return beatFlag.load (std::memory_order_relaxed); }
        bool recordingFull() const noexcept { return fullFlag.load (std::memory_order_relaxed); }

        /** After recordStop(): the recorded pattern, valid until the next recordStandby().
            Message thread, once resultReady() returns true (exchange resets it). */
        bool takeResult() noexcept { return resultFlag.exchange (false); }
        const std::vector<SeqEvent>& resultEvents() const noexcept { return recBuffer; }
        int resultCount() const noexcept { return resultSize; }
        int resultLengthBars() const noexcept { return recLengthBars; }
        int resultIndex() const noexcept { return resultPattern; }

    private:
        const PatternData* current() const noexcept { return playing >= 0 && patterns != nullptr ? patterns[playing] : nullptr; }
        int lengthTicks() const noexcept;
        void publish() noexcept;
        void insertRecorded (const SeqEvent& e) noexcept;

        template <typename Emit>
        void emitRange (double from, double to, std::int64_t blockStart, double tickToSample, double tps, Emit& emit) noexcept;

        PatternData* const* patterns = nullptr;
        double rate = 44100.0;

        int playing = -1, reserved = -1, armed = -1;
        double pos = 0.0;                 // play head in ticks
        std::int64_t blockStartTime = 0;
        std::int64_t posTime = 0;         // engine time that `pos` corresponds to
        double ticksPerSample = 0.0;

        // recording
        RecState rec = RecState::off;
        int recIndex = -1, recLengthBars = 1;
        double countInPos = 0.0;
        std::vector<SeqEvent> recBuffer;
        int recCount = 0;
        std::uint16_t pass = 0;
        std::uint32_t nextId = 1;
        int quantIndex = 6, shufflePercent = 50;
        std::array<std::uint32_t, kMaxPads> openNote {};
        std::array<double, kMaxPads> openNoteTick {};
        double absTick = 0.0;             // ticks since recording started (for note lengths)
        bool erasing = false, eraseAll = false;
        std::bitset<kMaxPads> erasePads;
        bool full = false;

        // host sync
        bool hostFollow = false, hostPlaying = false, lastHostPlaying = false;
        double hostPpq = 0.0;

        // published
        std::atomic<int> playingFlag { -1 }, reservedFlag { -1 }, armedFlag { -1 }, recStateFlag { 0 }, recIndexFlag { -1 };
        std::atomic<int> measureFlag { 1 }, beatFlag { 1 };
        std::atomic<bool> resultFlag { false }, fullFlag { false };
        int resultSize = 0, resultPattern = -1;
    };

    // ---- template implementation ------------------------------------------------------

    template <typename Emit>
    void Sequencer::emitRange (double from, double to, std::int64_t blockStart, double tickToSample, double tps, Emit& emit) noexcept
    {
        // Events with tick in [from, to). Sample offset is measured from the block start,
        // where the play head was at `pos` (captured by the caller as tickToSample origin).
        auto fire = [&] (const SeqEvent& e)
        {
            const double off = ((double) e.tick - tickToSample) / tps;
            const auto t = blockStart + (std::int64_t) std::floor (std::max (0.0, off));
            emit (Event { Event::Kind::padDown, t, e.pad, false, (float) e.velocity / 127.0f });
            if (e.length <= 0)
                return;   // one-shot (MIDI without Note Off): no release
            const auto len = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) std::max (1, e.length) / tps));
            emit (Event { Event::Kind::padUp, t + len, e.pad, false });
        };

        const bool recordingPass = rec == RecState::recording || rec == RecState::rehearsal;
        if (recordingPass)
        {
            for (int i = 0; i < recCount; )
            {
                const auto& e = recBuffer[(size_t) i];
                if ((double) e.tick >= to) break;
                if ((double) e.tick >= from)
                {
                    if (erasing && (eraseAll || erasePads.test ((size_t) e.pad)))
                    {
                        // Erase: drop it from the take (p. 33).
                        std::move (recBuffer.begin() + i + 1, recBuffer.begin() + recCount, recBuffer.begin() + i);
                        --recCount;
                        continue;
                    }
                    if (e.pass != pass)
                        fire (e);
                }
                ++i;
            }
            return;
        }

        if (const auto* p = current())
        {
            const auto& ev = p->events;
            auto it = std::lower_bound (ev.begin(), ev.end(), from, [] (const SeqEvent& e, double v) { return (double) e.tick < v; });
            for (; it != ev.end() && (double) it->tick < to; ++it)
                fire (*it);
        }
    }

    template <typename Emit>
    void Sequencer::process (std::int64_t blockStart, int n, double tempo, Emit&& emit) noexcept
    {
        blockStartTime = blockStart;
        const double tps = std::max (1e-9, tempo * kPpq / (60.0 * rate));   // ticks per sample
        ticksPerSample = tps;

        // Host transport (like MIDI start/stop, p. 47): the armed pattern starts when the
        // host starts, playback stops when the host stops, and while both run the pattern
        // stays phase-locked to the host's bar position. With the host stopped, pads still
        // play patterns by hand.
        if (hostFollow)
        {
            const bool started = hostPlaying && ! lastHostPlaying;
            const bool stopped = ! hostPlaying && lastHostPlaying;
            lastHostPlaying = hostPlaying;
            if (started && armed >= 0 && rec == RecState::off)
            {
                playing = armed;
                armed = -1;
                const int len = lengthTicks();
                pos = len > 0 ? std::fmod (hostPpq * kPpq, (double) len) : 0.0;
            }
            else if (stopped && playing >= 0 && rec == RecState::off)
                stop();
            else if (hostPlaying && playing >= 0 && rec == RecState::off)
            {
                const int len = lengthTicks();
                const double target = len > 0 ? std::fmod (hostPpq * kPpq, (double) len) : 0.0;
                if (std::abs (target - pos) > 2.0 && std::abs (target - pos) < len - 2.0)
                    pos = target;
            }
        }

        double remaining = n * tps;
        double sampleCursor = 0.0;   // samples consumed within the block

        // Metronome: standby, count-in, recording and rehearsal (p. 32).
        auto clicks = [&] (double fromTick, double toTick, double originTick, double originSample)
        {
            const double firstBeat = std::ceil (fromTick / kPpq) * kPpq;
            for (double b = firstBeat; b < toTick; b += kPpq)
            {
                const auto t = blockStart + (std::int64_t) (originSample + (b - originTick) / tps);
                emit (Event { Event::Kind::click, t, 0, std::fmod (b, (double) kTicksPerBar) < 0.5 });
            }
        };

        if (rec == RecState::standby)
        {
            clicks (countInPos, countInPos + remaining, countInPos, 0.0);
            countInPos += remaining;
            posTime = blockStart + n;
            publish();
            return;
        }

        if (rec == RecState::countIn)
        {
            const double left = kTicksPerBar - countInPos;
            const double use = std::min (left, remaining);
            clicks (countInPos, countInPos + use, countInPos, 0.0);
            countInPos += use;
            remaining -= use;
            sampleCursor += use / tps;
            if (countInPos < kTicksPerBar - 1e-6)
            {
                posTime = blockStart + n;
                publish();
                return;
            }
            rec = RecState::recording;   // count-in done: record from the top
            pos = 0.0;
            absTick = 0.0;
        }

        const bool recordingPass = rec == RecState::recording || rec == RecState::rehearsal;
        if (! recordingPass && playing < 0)
        {
            posTime = blockStart + n;
            publish();
            return;
        }

        int guard = 0;
        while (remaining > 1e-9 && guard++ < 64)
        {
            const int len = lengthTicks();
            if (len <= 0) break;
            const double end = std::min ((double) len, pos + remaining);
            const double originTick = pos - sampleCursor * tps;   // tick at block sample 0 on this lap
            if (recordingPass)
                clicks (pos, end, originTick, 0.0);
            emitRange (pos, end, blockStart, originTick, tps, emit);
            const double used = end - pos;
            remaining -= used;
            sampleCursor += used / tps;
            absTick += used;
            pos = end;

            if (pos >= len - 1e-9)
            {
                pos = 0.0;
                if (recordingPass)
                    ++pass;
                else if (reserved >= 0)
                {
                    playing = reserved;   // reserved pattern starts when the current one ends
                    reserved = -1;
                }
            }
        }
        posTime = blockStart + n;
        publish();
    }
}
