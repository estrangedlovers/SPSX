#include "Engine.h"
#include "../RealtimeGuard.h"
#include "../sampler/TimeModify.h"
#include <cmath>
#include <algorithm>

namespace spsx
{
    Engine::Engine()
    {
        for (auto& p : positions)
            p.store (-1);
        seq.setPatternTable (patternTable.data());
        // Queues exist before prepare() so the message thread can post state at any time.
        uiQueue.resize ((size_t) kFifoSize);
        timed.resize ((size_t) kTimedSize);
    }

    Engine::~Engine()
    {
        // Runs on the message thread after audio has stopped.
        collectGarbage();
        for (auto& ps : pads)
        {
            if (ps.sample != nullptr)    ps.sample->decReferenceCount();
            if (ps.stretched != nullptr) ps.stretched->decReferenceCount();
        }
        for (auto* p : patternTable)
            if (p != nullptr) p->decReferenceCount();
        for (int i = 0; i < numRetired; ++i)
            retired[(size_t) i]->decReferenceCount();

        int start1, size1, start2, size2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), start1, size1, start2, size2);
        auto drop = [this] (int s, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto& c = uiQueue[(size_t) (s + i)];
                if (c.sample != nullptr)
                    c.sample->decReferenceCount();
                if (c.type == Command::Type::seqSetPattern && c.ptr != nullptr)
                    static_cast<PatternData*> (c.ptr)->decReferenceCount();
            }
        };
        if (! uiQueue.empty())
        {
            drop (start1, size1);
            drop (start2, size2);
        }
        uiFifo.finishedRead (size1 + size2);
    }

    void Engine::prepare (double newRate, int newMaxBlock, const Limits& limits)
    {
        rate = newRate;
        maxBlock = newMaxBlock;
        voices.prepare (rate, maxBlock, {});
        voices.setBudget (limits.channelVoices);
        clip = limits.clipOutput;
        effects.prepare (rate, maxBlock, limits.effectSlots);
        seq.prepare (rate);
        extRamp = (float) (1.0 / (0.005 * rate));
        recPhase = RecPhase::idle;
        tap = nullptr;
        tapAttached.store (false);
        for (auto& b : bus)
            b.assign ((size_t) maxBlock, 0.0f);
        uiQueue.resize ((size_t) kFifoSize);
        timed.resize ((size_t) kTimedSize);
        numTimed = 0;
        time = 0;
    }

    // ---- message thread ----------------------------------------------------------------

    bool Engine::post (const Command& c) noexcept
    {
        int start1, size1, start2, size2;
        uiFifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 + size2 == 0)
            return false;
        uiQueue[(size_t) (size1 > 0 ? start1 : start2)] = c;
        uiFifo.finishedWrite (1);
        return true;
    }

    bool Engine::postSample (PadId pad, SampleData::Ptr s)
    {
        Command c;
        c.type = Command::Type::setSample;
        c.pad = pad;
        c.sample = s.get();
        if (c.sample != nullptr)
            c.sample->incReferenceCount();        // reference travels with the command
        if (! post (c))
        {
            if (c.sample != nullptr)
                c.sample->decReferenceCount();
            return false;
        }
        return true;
    }

    bool Engine::postStretched (PadId pad, SampleData::Ptr s, double speed)
    {
        Command c;
        c.type = Command::Type::setStretched;
        c.pad = pad;
        c.value = speed;
        c.sample = s.get();
        if (c.sample != nullptr)
            c.sample->incReferenceCount();
        if (! post (c))
        {
            if (c.sample != nullptr)
                c.sample->decReferenceCount();
            return false;
        }
        return true;
    }

    bool Engine::postSettings (PadId pad, const PadSettings& settings)
    {
        Command c;
        c.type = Command::Type::setSettings;
        c.pad = pad;
        c.settings = settings;
        return post (c);
    }

    bool Engine::postPattern (int index, PatternData::Ptr pattern)
    {
        Command c;
        c.type = Command::Type::seqSetPattern;
        c.index = index;
        auto* raw = pattern.get();
        if (raw != nullptr)
            raw->incReferenceCount();   // reference travels with the command
        c.ptr = raw;
        if (! post (c))
        {
            if (raw != nullptr)
                raw->decReferenceCount();
            return false;
        }
        return true;
    }

    void Engine::setHostTransport (bool follow, bool playing, double bpm, double ppq) noexcept
    {
        hostFollow = follow;
        hostPlaying = playing;
        hostTempo = bpm;
        hostPpq = ppq;
        seq.setHostTransport (follow, playing, ppq);
    }

    void Engine::collectGarbage()
    {
        int start1, size1, start2, size2;
        garbageFifo.prepareToRead (garbageFifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) garbage[(size_t) (start1 + i)]->decReferenceCount();
        for (int i = 0; i < size2; ++i) garbage[(size_t) (start2 + i)]->decReferenceCount();
        garbageFifo.finishedRead (size1 + size2);
    }

    // ---- audio thread ------------------------------------------------------------------

    namespace
    {
        /** Min-heap order on (time, sequence): earliest first, FIFO for equal times. */
        struct TimedLater
        {
            bool operator() (const Command& a, const Command& b) const noexcept
            {
                return a.time != b.time ? a.time > b.time : a.sequence > b.sequence;
            }
        };
    }

    void Engine::schedule (const Command& c) noexcept
    {
        // Binary heap: O(log n) per event even with thousands of pending note-offs.
        if (numTimed >= kTimedSize)
            return;   // full: drop (cannot allocate here)
        timed[(size_t) numTimed] = c;
        timed[(size_t) numTimed].sequence = nextSequence++;
        ++numTimed;
        std::push_heap (timed.begin(), timed.begin() + numTimed, TimedLater {});
    }

    void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept
    {
        // Hosts and tests may hand over more than maxBlock: split instead of overrunning.
        if (n > maxBlock)
        {
            for (int done = 0; done < n; done += maxBlock)
            {
                const int m = juce::jmin (maxBlock, n - done);
                const float* inPtrs[2] = { numIn > 0 && in != nullptr ? in[0] + done : nullptr,
                                           numIn > 1 && in != nullptr ? in[1] + done : nullptr };
                float* outPtrs[8] {};
                for (int ch = 0; ch < juce::jmin (numOut, 8); ++ch)
                    outPtrs[ch] = out[ch] + done;
                process (inPtrs, numIn, outPtrs, juce::jmin (numOut, 8), m);
            }
            return;
        }

        rt::ScopedRealtime guard;
        juce::ScopedNoDenormals noDenormals;

        // UI commands apply at the start of the block.
        {
            int start1, size1, start2, size2;
            uiFifo.prepareToRead (uiFifo.getNumReady(), start1, size1, start2, size2);
            for (int i = 0; i < size1; ++i) execute (uiQueue[(size_t) (start1 + i)]);
            for (int i = 0; i < size2; ++i) execute (uiQueue[(size_t) (start2 + i)]);
            uiFifo.finishedRead (size1 + size2);
        }

        // Pattern sequencer: schedule this block's pad hits, note-offs and clicks at their
        // exact samples (they are executed by the segment loop below).
        // Tempo: the host's when following it; otherwise a playing pattern runs at its own
        // stored tempo (p. 15). Recording uses the tempo set in standby.
        if (hostFollow && hostTempo > 0.0)
            patternTempo = hostTempo;
        else if (seq.recordState() == Sequencer::RecState::off && seq.playingIndex() >= 0)
            if (const auto* p = patternTable[(size_t) seq.playingIndex()])
                patternTempo = p->tempo;
        seqTempoFlag.store (patternTempo, std::memory_order_relaxed);
        // PAUSE freezes the sequencer too (p. 12).
        if (! paused)
        {
            seq.process (time, n, patternTempo, [this] (const Sequencer::Event& e)
            {
                Command c;
                c.time = e.time;
                if (e.kind == Sequencer::Event::Kind::click)
                {
                    c.type = Command::Type::click;
                    c.value = e.accent ? 1.0 : 0.0;
                }
                else
                {
                    c.type = e.kind == Sequencer::Event::Kind::padDown ? Command::Type::padDown : Command::Type::padUp;
                    c.pad = PadId::fromIndex (e.pad);
                    c.index = 1;   // from the sequencer: never recorded back
                }
                schedule (c);
            });
        }

        updateEffectTempo();

        // Timed commands split the block so they land on their exact sample.
        int done = 0;
        while (done < n)
        {
            // While paused, timed events wait (they run, late, on resume) so no note-off is
            // lost and no GATE voice is left hanging.
            while (! paused && numTimed > 0 && timed[0].time <= time)
            {
                std::pop_heap (timed.begin(), timed.begin() + numTimed, TimedLater {});
                --numTimed;
                execute (timed[(size_t) numTimed]);
            }

            int segment = n - done;
            if (! paused && numTimed > 0)
                segment = (int) std::min<std::int64_t> (segment, timed[0].time - time);

            const float* inSeg[2] = { numIn > 0 && in != nullptr && in[0] != nullptr ? in[0] + done : nullptr,
                                      numIn > 1 && in != nullptr && in[1] != nullptr ? in[1] + done : nullptr };
            renderSegment (inSeg, numIn, out, numOut, done, segment);
            done += segment;
            time += segment;
        }

        flushRetired();
        publishState();
    }

    bool Engine::isTarget (PadId p) const noexcept
    {
        return fxTargetAll || fxTargets.test ((size_t) p.index());
    }

    void Engine::updateEffectTempo() noexcept
    {
        // Synced effects follow "the tempo of the longest sample of the samples to which
        // the effect is being applied", or the pattern tempo (p. 44 *1). Pattern playback
        // arrives in Phase 5; until then the pattern tempo is the fallback.
        // "If a pattern is playing, the tempo of the pattern is used as the reference."
        if (seq.playingIndex() >= 0 || seq.recordState() != Sequencer::RecState::off)
        {
            fxTempo = patternTempo;
            effectTempoFlag.store (fxTempo, std::memory_order_relaxed);
            return;
        }
        SampleIndex longest = 0;
        double bpm = 0.0;
        for (int i = 0; i < kMaxPads; ++i)
        {
            const auto& ps = pads[(size_t) i];
            if (ps.sample == nullptr || ! isTarget (PadId::fromIndex (i)))
                continue;
            const auto len = ps.settings.end > ps.settings.start ? ps.settings.end - ps.settings.start : ps.sample->length();
            if (len > longest && ps.settings.bpm > 0.0)
            {
                longest = len;
                bpm = ps.settings.bpm;
            }
        }
        fxTempo = bpm > 0.0 ? bpm : patternTempo;
        effectTempoFlag.store (fxTempo, std::memory_order_relaxed);
    }

    void Engine::renderSegment (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n) noexcept
    {
        float* dl = bus[0].data(); float* dr = bus[1].data();
        float* fl = bus[2].data(); float* fr = bus[3].data();
        float* il = bus[4].data(); float* ir = bus[5].data();
        for (auto* b : { dl, dr, fl, fr, il, ir })
            std::fill (b, b + n, 0.0f);

        if (! paused)
        {
            for (auto& v : voices)
            {
                if (! v.isActive())
                    continue;
                const auto& ps = pads[(size_t) v.getPad().index()];
                v.render (dl, dr, fl, fr, n, ps.settings, sourceFor (ps), isTarget (v.getPad()));
            }
        }

        // ---- live input: LINE (stereo, INPUT GAIN) or MIC (mono, MIC LEVEL) ----------
        float inputPeak = 0.0f;
        if (in != nullptr && in[0] != nullptr && ! paused)
        {
            const float* a = in[0];
            const float* b = numIn > 1 && in[1] != nullptr ? in[1] : in[0];
            for (int i = 0; i < n; ++i)
            {
                if (micMode)
                    il[i] = ir[i] = a[i] * micGain;
                else
                {
                    il[i] = a[i] * inputGain;
                    ir[i] = b[i] * inputGain;
                }
                inputPeak = std::max ({ inputPeak, std::abs (il[i]), std::abs (ir[i]) });
            }
        }
        // PEAK lights from about -6 dBFS with a 100 ms hold (FIDELITY: threshold assumed).
        if (inputPeak >= 0.5f) peakHold = (int) (0.1 * rate);
        else peakHold = std::max (0, peakHold - n);

        // ---- EXT SOURCE (p. 24): live input played like a pad, with its own LEVEL ----
        const float extTarget = extOn ? extLevel : 0.0f;
        const float extSendTarget = fxTargetExt || fxTargetAll ? 1.0f : 0.0f;
        if (extGain > 0.0f || extTarget > 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                extGain += std::clamp (extTarget - extGain, -extRamp, extRamp);
                extSend += std::clamp (extSendTarget - extSend, -extRamp, extRamp);
                const float xl = il[i] * extGain;
                const float xr = (extStereo ? ir[i] : 0.5f * (il[i] + ir[i])) * extGain;
                const float xm = extStereo ? xl : xr;
                dl[i] += xm * (1.0f - extSend);
                dr[i] += xr * (1.0f - extSend);
                fl[i] += xm * extSend;
                fr[i] += xr * extSend;
            }
        }

        // ---- sampling monitor: input -> digital level -> effect bus (p. 19) ----------
        float digitalPeak = 0.0f;
        if (monitor)
        {
            for (int i = 0; i < n; ++i)
            {
                float a = il[i] * digitalLevel, b = ir[i] * digitalLevel;
                if (! monitorStereo)
                    a = b = 0.5f * (a + b);
                fl[i] += a;
                fr[i] += b;
                digitalPeak = std::max ({ digitalPeak, std::abs (a), std::abs (b) });
            }
        }

        // ---- effects -------------------------------------------------------------------
        effects.process (fl, fr, n, fxTempo);

        // ---- recorder ------------------------------------------------------------------
        if (tap != nullptr && recSource == 1)
        {
            // Resample: the mix through CTRL 3's level. Check level even while waiting
            // so the red illumination can be used to set it (p. 26).
            for (int i = 0; i < n; ++i)
            {
                il[i] = (dl[i] + fl[i]) * resampleLevel;
                ir[i] = (dr[i] + fr[i]) * resampleLevel;
                digitalPeak = std::max ({ digitalPeak, std::abs (il[i]), std::abs (ir[i]) });
            }
            runRecorder (n);
        }
        else if (tap != nullptr)
        {
            // Sampling records the effect bus: input after digital level and effect.
            std::copy (fl, fl + n, il);
            std::copy (fr, fr + n, ir);
            runRecorder (n);
        }
        if (inputPeak > 0.0f && recPhase == RecPhase::waitingTrigger && inputPeak >= autoThreshold)
        {
            // Triggered within this segment: start from the next one (sub-block accurate
            // to the segment, at most 32 samples late on the converted path).
            recPhase = RecPhase::recording;
        }
        if (digitalPeak >= 0.999f) overloadHold = (int) (0.3 * rate);
        else overloadHold = std::max (0, overloadHold - n);

        // ---- mix and output stage --------------------------------------------------------
        // OUTPUT GAIN is an internal processing level ahead of a fixed-point style clip
        // (manual p. 39), then the analog VOLUME pot. The count-in click is added after the
        // clip so it is never recorded or distorted.
        float pk = peak.load (std::memory_order_relaxed) * 0.999f;
        for (int i = 0; i < n; ++i)
        {
            float a = (dl[i] + fl[i]) * outputGain;
            float b = (dr[i] + fr[i]) * outputGain;
            if (clip)
            {
                a = std::clamp (a, -1.0f, 1.0f);
                b = std::clamp (b, -1.0f, 1.0f);
            }
            pk = std::max ({ pk, std::abs (a), std::abs (b) });
            const float c = click.next();
            dl[i] = (a + c) * volume;
            dr[i] = (b + c) * volume;
        }
        peak.store (pk, std::memory_order_relaxed);

        if (numOut == 1)
        {
            for (int i = 0; i < n; ++i)
                out[0][offset + i] = 0.5f * (dl[i] + dr[i]);
        }
        else if (numOut >= 2)
        {
            std::copy (dl, dl + n, out[0] + offset);
            std::copy (dr, dr + n, out[1] + offset);
            for (int ch = 2; ch < numOut; ++ch)
                std::fill (out[ch] + offset, out[ch] + offset + n, 0.0f);
        }
    }

    void Engine::runRecorder (int n) noexcept
    {
        // il/ir hold what would be recorded this segment.
        const float* l = bus[4].data();
        const float* r = bus[5].data();

        switch (recPhase)
        {
            case RecPhase::countIn:
            {
                // One bar of clicks: "-1-", "-2-", "-3-", "-4-", then "rEC" (p. 26).
                for (int i = 0; i < n; ++i)
                {
                    const auto elapsed = (std::int64_t) countInBeats * samplesPerBeat - countInLeft;
                    if (elapsed % samplesPerBeat == 0)
                    {
                        click.trigger (elapsed == 0, rate);
                        countInFlag.store ((int) (elapsed / samplesPerBeat) + 1, std::memory_order_relaxed);
                    }
                    if (--countInLeft <= 0)
                    {
                        recPhase = RecPhase::recording;
                        countInFlag.store (0, std::memory_order_relaxed);
                        // Record the rest of this segment.
                        const int rest = n - i - 1;
                        if (rest > 0)
                        {
                            tap->write (l + i + 1, r + i + 1, rest);
                            framesRecorded += rest;
                        }
                        break;
                    }
                }
                break;
            }

            case RecPhase::recording:
            {
                int m = n;
                if (framesRecorded + m > maxFrames)
                    m = (int) std::max<std::int64_t> (0, maxFrames - framesRecorded);
                if (m > 0)
                {
                    tap->write (l, r, m);
                    framesRecorded += m;
                }
                if (framesRecorded >= maxFrames || tap->overflowed.load (std::memory_order_relaxed))
                    detachTap (RecPhase::full);   // "FUL": over 2 GB (p. 20)
                break;
            }

            case RecPhase::idle:
            case RecPhase::waitingStart:
            case RecPhase::waitingTrigger:
            case RecPhase::full:
                break;
        }
    }

    void Engine::detachTap (RecPhase finalPhase) noexcept
    {
        tap = nullptr;
        recPhase = finalPhase == RecPhase::full ? RecPhase::full : RecPhase::idle;
        countInFlag.store (0, std::memory_order_relaxed);
        tapAttached.store (false, std::memory_order_release);
    }

    void Engine::execute (const Command& c) noexcept
    {
        using T = Command::Type;

        // PAUSE disables performance (manual p. 12); state updates still go through so the
        // model never drifts, and resume / stop all always work.
        const bool performance = c.type == T::padDown || c.type == T::padUp || c.type == T::subDown
                              || c.type == T::subUp || c.type == T::holdDown || c.type == T::stopPad
                              || c.type == T::recStart;
        if (paused && performance)
            return;

        switch (c.type)
        {
            case T::padDown:
                if (! c.pad.isValid()) break;
                padDown (c.pad, false);
                if (c.index != 1) seq.padDown (c.pad.index(), time);
                break;
            case T::padUp:
                if (! c.pad.isValid()) break;
                padUp (c.pad, false);
                if (c.index != 1) seq.padUp (c.pad.index(), time);
                break;
            case T::subDown:
                if (! lastStruck.isValid()) break;
                padDown (lastStruck, true);
                seq.padDown (lastStruck.index(), time);
                break;
            case T::subUp:
                if (! lastStruck.isValid()) break;
                padUp (lastStruck, true);
                seq.padUp (lastStruck.index(), time);
                break;

            case T::holdDown:
                for (auto& v : voices)
                    if (v.getState() == Voice::State::playing && (v.heldByPad || v.heldBySub))
                        v.latched = true;
                break;

            case T::stopPad:
                for (auto& v : voices)
                    if (v.isActive() && v.getPad() == c.pad)
                        v.release();
                break;

            case T::stopAll:
                for (auto& v : voices)
                    v.release();
                break;

            case T::pause:  paused = true;  pausedFlag.store (true);  break;
            case T::resume: paused = false; pausedFlag.store (false); break;

            case T::setSample:
            {
                if (! c.pad.isValid()) { retire (c.sample); break; }
                auto& ps = pads[(size_t) c.pad.index()];
                for (auto& v : voices)
                    if (v.isActive() && v.getPad() == c.pad)
                        v.release();
                retire (ps.sample);
                retire (ps.stretched);
                ps.sample = c.sample;
                ps.stretched = nullptr;
                break;
            }

            case T::setStretched:
            {
                if (! c.pad.isValid()) { retire (c.sample); break; }
                auto& ps = pads[(size_t) c.pad.index()];
                retire (ps.stretched);
                ps.stretched = c.sample;
                ps.stretchedSpeed = c.value;
                break;
            }

            case T::setSettings:
                if (c.pad.isValid())
                    pads[(size_t) c.pad.index()].settings = c.settings;
                break;

            case T::setPatternTempo: patternTempo = c.value; break;
            case T::setOutputGainDb: outputGain = (float) std::pow (10.0, c.value / 20.0); break;
            case T::setVolume:
            {
                // Audio-taper approximation of the analog VOLUME pot (FIDELITY: assumed).
                const double v = std::clamp (c.value, 0.0, 1.0);
                volume = (float) (v * v * v);
                break;
            }
            case T::setVoiceBudget: voices.setBudget ((int) c.value); break;
            case T::setClipOutput:  clip = c.value > 0.5; break;

            // ---- effects ----
            case T::setEffect:       effects.select (c.index, (int) c.value); break;
            case T::setEffectParams: effects.setParams (c.index, c.params); break;
            case T::setEffectLimit:  effects.setLimit (c.value > 0.5); break;
            case T::setTargetPad:    if (c.pad.isValid()) fxTargets.set ((size_t) c.pad.index(), c.value > 0.5); break;
            case T::setTargetAll:    fxTargetAll = c.value > 0.5; break;
            case T::setTargetExt:    fxTargetExt = c.value > 0.5; break;
            case T::clearTargets:    fxTargets.reset(); fxTargetAll = false; fxTargetExt = false; break;

            // ---- input / EXT SOURCE / sampling ----
            case T::setInputGainDb:   inputGain = (float) std::pow (10.0, c.value / 20.0); break;
            case T::setMicMode:       micMode = c.value > 0.5; break;
            case T::setMicGainDb:     micGain = (float) std::pow (10.0, c.value / 20.0); break;
            case T::setExtOn:         extOn = c.value > 0.5; break;
            case T::setExtStereo:     extStereo = c.value > 0.5; break;
            case T::setExtLevel:      extLevel = (float) (std::clamp (c.value, 0.0, 127.0) / 127.0); break;
            case T::setMonitor:       monitor = c.value > 0.5; monitorStereo = c.index != 0; break;
            case T::setDigitalLevel:  digitalLevel = (float) c.value; break;
            case T::setResampleLevel: resampleLevel = (float) c.value; break;

            case T::recArm:
            {
                if (tap != nullptr)
                    detachTap (RecPhase::idle);
                tap = static_cast<RecordTap*> (c.ptr);
                if (tap == nullptr) break;
                tapAttached.store (true, std::memory_order_release);
                recSource = c.index;
                autoThreshold = c.params[0];
                countInBeats = (int) c.params[1];
                samplesPerBeat = std::max<std::int64_t> (1, (std::int64_t) std::llround (60.0 / std::max (c.value, 1.0) * rate));
                countInLeft = (std::int64_t) countInBeats * samplesPerBeat;
                // About 2 GB of 16-bit audio per sample (p. 20).
                maxFrames = (std::int64_t) (2'000'000'000LL / (2LL * tap->numChannels()));
                framesRecorded = 0;
                if (c.params[2] > 0.5f)       recPhase = RecPhase::waitingStart;
                else if (autoThreshold > 0.0f) recPhase = RecPhase::waitingTrigger;
                else if (countInBeats > 0)     recPhase = RecPhase::countIn;
                else                           recPhase = RecPhase::recording;
                break;
            }
            case T::recStart:
                if (recPhase == RecPhase::waitingStart)
                    recPhase = RecPhase::recording;
                break;
            case T::recStop:
                if (tap != nullptr) detachTap (RecPhase::idle);
                else recPhase = RecPhase::idle;
                break;
            case T::recCancel:
                if (tap != nullptr) detachTap (RecPhase::idle);
                recPhase = RecPhase::idle;
                break;

            // ---- pattern sequencer ----
            case T::seqSetPattern:
            {
                auto* incoming = static_cast<PatternData*> (c.ptr);
                if (c.index < 0 || c.index >= kMaxPads) { retire (incoming); break; }
                auto*& slot = patternTable[(size_t) c.index];
                retire (slot);           // the sequencer reads the table each block; nothing else holds it
                slot = incoming;
                if (slot == nullptr && seq.playingIndex() == c.index)
                    seq.stop();
                break;
            }
            case T::seqPlay:       seq.play (c.index); break;
            case T::seqReserve:    seq.reserve (c.index); break;
            case T::seqStop:       seq.stop(); break;
            case T::seqArm:        seq.arm (c.index); break;
            case T::seqRecStandby:
                seq.recordStandby (c.index, (int) c.value,
                                   c.index >= 0 && c.index < kMaxPads ? patternTable[(size_t) c.index] : nullptr);
                break;
            case T::seqRecStart:   seq.recordStart(); break;
            case T::seqRehearsal:  seq.toggleRehearsal(); break;
            case T::seqRecStop:    seq.recordStop(); break;
            case T::seqRecCancel:  seq.recordCancel(); break;
            case T::seqLength:     seq.setLength ((int) c.value); break;
            case T::seqQuantize:   seq.setQuantize (c.index, (int) c.value); break;
            case T::seqErase:      seq.setEraseMode (c.value > 0.5); break;
            case T::seqErasePad:   if (c.pad.isValid()) seq.setErasePad (c.pad.index(), c.value > 0.5); break;
            case T::seqEraseAll:   seq.setEraseAll (c.value > 0.5); break;
            case T::setMetronomeLevel: metronomeLevel = (float) (std::clamp (c.value, 0.0, 127.0) / 127.0); break;
            case T::click:         click.trigger (c.value > 0.5, rate, metronomeLevel); break;
        }
    }

    void Engine::padDown (PadId p, bool viaSub) noexcept
    {
        const auto& ps = pads[(size_t) p.index()];
        if (ps.sample == nullptr)
            return;   // unlit pad: silent (manual p. 45)

        if (! viaSub)
            lastStruck = p;

        for (auto& v : voices)
        {
            if (v.getState() != Voice::State::playing || v.getPad() != p)
                continue;

            if (v.latched)                         // HOLD: press the pad again to stop (p. 12)
            {
                v.release();
                return;
            }
            if (! ps.settings.gate && ! viaSub)    // trigger mode toggles play/stop (p. 4)
            {
                v.release();
                return;
            }
            v.release();                           // retrigger: one voice per pad (SPEC U2)
        }

        startVoice (p, viaSub);
    }

    void Engine::padUp (PadId p, bool viaSub) noexcept
    {
        const auto& ps = pads[(size_t) p.index()];
        for (auto& v : voices)
        {
            if (v.getState() != Voice::State::playing || v.getPad() != p)
                continue;

            if (viaSub) v.heldBySub = false;
            else        v.heldByPad = false;

            if (ps.settings.gate && ! v.latched && ! v.heldByPad && ! v.heldBySub)
                v.release();
        }
    }

    void Engine::startVoice (PadId p, bool viaSub) noexcept
    {
        const auto& ps = pads[(size_t) p.index()];
        const int cost = juce::jlimit (1, 2, ps.sample->numChannels());
        Voice* v = voices.allocate (cost);
        if (v == nullptr)
            return;
        v->start (p, sourceFor (ps), ps.settings, time);
        v->heldByPad = ! viaSub;
        v->heldBySub = viaSub;
    }

    double Engine::timeModifySpeed (const PadSettings& s) const noexcept
    {
        return timemodify::sourceSpeed (s, patternTempo);
    }

    Voice::Source Engine::sourceFor (const PadState& ps) const noexcept
    {
        Voice::Source src;
        src.sample = ps.sample;
        src.speed = timeModifySpeed (ps.settings);
        if (ps.stretched != nullptr && std::abs (ps.stretchedSpeed - src.speed) < 1e-6 && std::abs (src.speed - 1.0) > 1e-9)
            src.stretched = ps.stretched;
        return src;
    }

    void Engine::retire (juce::ReferenceCountedObject* s) noexcept
    {
        if (s == nullptr)
            return;

        if (numRetired == kRetireSize)
        {
            // Out of room: force the oldest out, cutting any voice still reading it.
            auto* oldest = retired[0];
            for (auto& v : voices)
                if (v.sampleInUse() == oldest || v.stretchedInUse() == oldest)
                    v.kill();
            flushRetired();
        }
        if (numRetired < kRetireSize)
            retired[(size_t) numRetired++] = s;
    }

    void Engine::flushRetired() noexcept
    {
        int keep = 0;
        for (int i = 0; i < numRetired; ++i)
        {
            auto* s = retired[(size_t) i];
            bool inUse = false;
            for (auto& v : voices)
                if (v.sampleInUse() == s || v.stretchedInUse() == s)
                    inUse = true;

            if (! inUse)
            {
                int start1, size1, start2, size2;
                garbageFifo.prepareToWrite (1, start1, size1, start2, size2);
                if (size1 + size2 > 0)
                {
                    garbage[(size_t) (size1 > 0 ? start1 : start2)] = s;
                    garbageFifo.finishedWrite (1);
                    continue;
                }
            }
            retired[(size_t) keep++] = s;
        }
        numRetired = keep;
    }

    void Engine::publishState() noexcept
    {
        // Positions of the newest playing voice per pad; -1 for silent pads.
        for (size_t i = 0; i < pads.size(); ++i)
            publishedStart[i] = -1;

        for (auto& v : voices)
        {
            if (v.getState() != Voice::State::playing)
                continue;
            const auto i = (size_t) v.getPad().index();
            if (v.startTime() >= publishedStart[i])
            {
                publishedStart[i] = v.startTime();
                positions[i].store (v.position(), std::memory_order_relaxed);
            }
        }

        for (size_t i = 0; i < pads.size(); ++i)
            if (publishedStart[i] < 0 && positions[i].load (std::memory_order_relaxed) >= 0)
                positions[i].store (-1, std::memory_order_relaxed);

        voicesInUse.store (voices.usedChannelVoices(), std::memory_order_relaxed);
        peakLedFlag.store (peakHold > 0, std::memory_order_relaxed);
        overloadFlag.store (overloadHold > 0, std::memory_order_relaxed);
        recPhaseFlag.store ((int) recPhase, std::memory_order_relaxed);
    }
}
