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
        for (int i = 0; i < numRetired; ++i)
            retired[(size_t) i]->decReferenceCount();

        Command c;
        int start1, size1, start2, size2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), start1, size1, start2, size2);
        auto drop = [this] (int s, int n)
        {
            for (int i = 0; i < n; ++i)
                if (auto* smp = uiQueue[(size_t) (s + i)].sample)
                    smp->decReferenceCount();
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

    void Engine::collectGarbage()
    {
        int start1, size1, start2, size2;
        garbageFifo.prepareToRead (garbageFifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) garbage[(size_t) (start1 + i)]->decReferenceCount();
        for (int i = 0; i < size2; ++i) garbage[(size_t) (start2 + i)]->decReferenceCount();
        garbageFifo.finishedRead (size1 + size2);
    }

    // ---- audio thread ------------------------------------------------------------------

    void Engine::schedule (const Command& c) noexcept
    {
        if (numTimed >= kTimedSize)
            return;   // queue full: drop (cannot allocate here)

        int i = numTimed;
        while (i > 0 && timed[(size_t) (i - 1)].time > c.time)
        {
            timed[(size_t) i] = timed[(size_t) (i - 1)];
            --i;
        }
        timed[(size_t) i] = c;
        ++numTimed;
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
        juce::ignoreUnused (in, numIn);   // live input arrives with sampling (Phase 4)

        // UI commands apply at the start of the block.
        {
            int start1, size1, start2, size2;
            uiFifo.prepareToRead (uiFifo.getNumReady(), start1, size1, start2, size2);
            for (int i = 0; i < size1; ++i) execute (uiQueue[(size_t) (start1 + i)]);
            for (int i = 0; i < size2; ++i) execute (uiQueue[(size_t) (start2 + i)]);
            uiFifo.finishedRead (size1 + size2);
        }

        // Timed commands split the block so they land on their exact sample.
        int done = 0;
        while (done < n)
        {
            while (numTimed > 0 && timed[0].time <= time)
            {
                execute (timed[0]);
                std::move (timed.begin() + 1, timed.begin() + numTimed, timed.begin());
                --numTimed;
            }

            int segment = n - done;
            if (numTimed > 0)
                segment = (int) std::min<std::int64_t> (segment, timed[0].time - time);

            renderSegment (out, numOut, done, segment);
            done += segment;
            time += segment;
        }

        flushRetired();
        publishState();
    }

    void Engine::renderSegment (float* const* out, int numOut, int offset, int n) noexcept
    {
        float* l = bus[0].data();
        float* r = bus[1].data();
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);

        if (! paused)
        {
            for (auto& v : voices)
            {
                if (! v.isActive())
                    continue;
                const auto& ps = pads[(size_t) v.getPad().index()];
                v.render (l, r, n, ps.settings, sourceFor (ps));
            }
        }

        // Output stage: OUTPUT GAIN is an internal processing level ahead of a fixed-point
        // style clip (manual p. 39), then the analog VOLUME pot.
        float pk = peak.load (std::memory_order_relaxed) * 0.999f;
        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * outputGain;
            float b = r[i] * outputGain;
            if (clip)
            {
                a = std::clamp (a, -1.0f, 1.0f);
                b = std::clamp (b, -1.0f, 1.0f);
            }
            pk = std::max ({ pk, std::abs (a), std::abs (b) });
            l[i] = a * volume;
            r[i] = b * volume;
        }
        peak.store (pk, std::memory_order_relaxed);

        if (numOut == 1)
        {
            for (int i = 0; i < n; ++i)
                out[0][offset + i] = 0.5f * (l[i] + r[i]);
        }
        else if (numOut >= 2)
        {
            std::copy (l, l + n, out[0] + offset);
            std::copy (r, r + n, out[1] + offset);
            for (int ch = 2; ch < numOut; ++ch)
                std::fill (out[ch] + offset, out[ch] + offset + n, 0.0f);
        }
    }

    void Engine::execute (const Command& c) noexcept
    {
        using T = Command::Type;

        if (paused && c.type != T::resume && c.type != T::setSample && c.type != T::setStretched
            && c.type != T::setSettings && c.type != T::stopAll)
        {
            // PAUSE disables everything except resuming (manual p. 12). State updates and
            // the panic stop still go through so the model never drifts.
            if (c.type != T::setPatternTempo && c.type != T::setOutputGainDb && c.type != T::setVolume
                && c.type != T::setVoiceBudget && c.type != T::setClipOutput)
                return;
        }

        switch (c.type)
        {
            case T::padDown:   if (c.pad.isValid()) padDown (c.pad, false); break;
            case T::padUp:     if (c.pad.isValid()) padUp (c.pad, false); break;
            case T::subDown:   if (lastStruck.isValid()) padDown (lastStruck, true); break;
            case T::subUp:     if (lastStruck.isValid()) padUp (lastStruck, true); break;

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

    void Engine::retire (SampleData* s) noexcept
    {
        if (s == nullptr)
            return;

        if (numRetired == kRetireSize)
        {
            // Out of room: force the oldest out, cutting any voice still reading it.
            SampleData* oldest = retired[0];
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
            SampleData* s = retired[(size_t) i];
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
    }
}
