#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace spsx
{
    namespace
    {
        constexpr std::uint16_t kExistingPass = 0xFFFF;   // events from before this take
    }

    void Sequencer::prepare (double sampleRate)
    {
        rate = sampleRate;
        recBuffer.assign ((size_t) kMaxPatternEvents, SeqEvent {});
        recCount = 0;
        openNote.fill (0);
        ticksPerSample = 120.0 * kPpq / (60.0 * rate);
        publish();
    }

    int Sequencer::lengthTicks() const noexcept
    {
        if (rec != RecState::off)
            return recLengthBars * kTicksPerBar;
        if (const auto* p = current())
            return p->lengthTicks();
        return 0;
    }

    // ---- transport ------------------------------------------------------------------------

    void Sequencer::play (int index) noexcept
    {
        if (rec != RecState::off || patterns == nullptr || index < 0 || index >= kMaxPads || patterns[index] == nullptr)
            return;
        playing = index;
        reserved = -1;
        armed = -1;
        pos = 0.0;
        publish();
    }

    void Sequencer::reserve (int index) noexcept
    {
        if (patterns == nullptr || index < 0 || index >= kMaxPads || patterns[index] == nullptr)
            return;
        if (playing < 0) { play (index); return; }
        reserved = index;
        publish();
    }

    void Sequencer::stop() noexcept
    {
        playing = reserved = -1;
        pos = 0.0;
        publish();
    }

    void Sequencer::arm (int index) noexcept
    {
        armed = index;
        publish();
    }

    void Sequencer::setHostTransport (bool follow, bool isPlaying, double ppq) noexcept
    {
        hostFollow = follow;
        hostPlaying = isPlaying;
        hostPpq = ppq;
    }

    // ---- recording ------------------------------------------------------------------------

    void Sequencer::recordStandby (int index, int lengthBars, const PatternData* existing) noexcept
    {
        stop();
        rec = RecState::standby;
        recIndex = index;
        recCount = 0;
        recLengthBars = std::clamp (lengthBars, 1, 99);
        if (existing != nullptr)
        {
            // Overdub: the existing take plays back and new hits are added (p. 33).
            recLengthBars = std::max (recLengthBars, existing->lengthBars);
            for (const auto& e : existing->events)
            {
                if (recCount >= kMaxPatternEvents) break;
                auto copy = e;
                copy.pass = kExistingPass;
                recBuffer[(size_t) recCount++] = copy;
            }
        }
        pass = 0;
        countInPos = 0.0;
        absTick = 0.0;
        full = false;
        erasing = eraseAll = false;
        erasePads.reset();
        openNote.fill (0);
        resultFlag.store (false);
        publish();
    }

    void Sequencer::recordStart() noexcept
    {
        if (rec != RecState::standby) return;
        rec = RecState::countIn;
        countInPos = 0.0;
        publish();
    }

    void Sequencer::toggleRehearsal() noexcept
    {
        if (rec == RecState::recording)      rec = RecState::rehearsal;
        else if (rec == RecState::rehearsal) rec = RecState::recording;
        publish();
    }

    void Sequencer::recordStop() noexcept
    {
        if (rec == RecState::off) return;
        // Notes still held get the length up to now.
        for (int p = 0; p < kMaxPads; ++p)
            if (openNote[(size_t) p] != 0)
                padUp (p, posTime);
        rec = RecState::off;
        resultSize = recCount;
        resultPattern = recIndex;
        recIndex = -1;
        resultFlag.store (true);
        publish();
    }

    void Sequencer::recordCancel() noexcept
    {
        rec = RecState::off;
        recIndex = -1;
        recCount = 0;
        publish();
    }

    void Sequencer::setLength (int bars) noexcept
    {
        bars = std::clamp (bars, 1, 99);
        // "If the pattern already contains data, you can't make it shorter than its
        // current length" (p. 32).
        if (recCount > 0 && bars < recLengthBars)
            return;
        recLengthBars = bars;
        if (pos >= lengthTicks())
            pos = 0.0;
        publish();
    }

    void Sequencer::insertRecorded (const SeqEvent& e) noexcept
    {
        if (recCount >= kMaxPatternEvents)
        {
            full = true;
            return;
        }
        auto first = recBuffer.begin();
        auto last = recBuffer.begin() + recCount;
        auto at = std::upper_bound (first, last, e.tick, [] (std::int32_t t, const SeqEvent& x) { return t < x.tick; });
        std::move_backward (at, last, last + 1);
        *at = e;
        ++recCount;
    }

    void Sequencer::padDown (int padIndex, std::int64_t time, float velocity, bool oneShot) noexcept
    {
        if (rec != RecState::recording || erasing || padIndex < 0 || padIndex >= kMaxPads)
            return;
        const int len = lengthTicks();
        // The play head is at `pos` at engine time `posTime`: commands at the start of a
        // block and timed commands inside it both map exactly.
        const double raw = pos + (double) (time - posTime) * ticksPerSample;
        SeqEvent e;
        e.tick = Quantize::apply (raw, quantIndex, shufflePercent, len);
        e.length = oneShot ? 0 : 1;
        e.velocity = (std::uint8_t) std::clamp ((int) std::lround (velocity * 127.0f), 1, 127);
        e.pad = (std::int16_t) padIndex;
        e.pass = pass;
        e.id = nextId++;
        insertRecorded (e);
        if (oneShot)
        {
            openNote[(size_t) padIndex] = 0;
            return;
        }
        openNote[(size_t) padIndex] = e.id;
        openNoteTick[(size_t) padIndex] = absTick + (double) (time - posTime) * ticksPerSample;
    }

    void Sequencer::padUp (int padIndex, std::int64_t time) noexcept
    {
        if (padIndex < 0 || padIndex >= kMaxPads) return;
        const auto id = openNote[(size_t) padIndex];
        if (id == 0) return;
        openNote[(size_t) padIndex] = 0;
        const double nowAbs = absTick + (double) (time - posTime) * ticksPerSample;
        const auto length = (std::int32_t) std::clamp (std::llround (nowAbs - openNoteTick[(size_t) padIndex]), 1LL, (long long) std::max (1, lengthTicks()));
        for (int i = 0; i < recCount; ++i)
            if (recBuffer[(size_t) i].id == id)
            {
                recBuffer[(size_t) i].length = length;
                break;
            }
    }

    void Sequencer::publish() noexcept
    {
        playingFlag.store (playing, std::memory_order_relaxed);
        reservedFlag.store (reserved, std::memory_order_relaxed);
        armedFlag.store (armed, std::memory_order_relaxed);
        recStateFlag.store ((int) rec, std::memory_order_relaxed);
        recIndexFlag.store (recIndex, std::memory_order_relaxed);
        fullFlag.store (full, std::memory_order_relaxed);
        if (rec == RecState::countIn)
        {
            measureFlag.store (-(4 - (int) (countInPos / kPpq)), std::memory_order_relaxed);
            beatFlag.store (0, std::memory_order_relaxed);
        }
        else
        {
            measureFlag.store ((int) (pos / kTicksPerBar) + 1, std::memory_order_relaxed);
            beatFlag.store ((int) (std::fmod (pos, (double) kTicksPerBar) / kPpq) + 1, std::memory_order_relaxed);
        }
    }
}
