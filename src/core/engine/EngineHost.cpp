#include "EngineHost.h"
#include <cmath>
#include <algorithm>

namespace spsx
{
    void EngineHost::prepare (double deviceRateIn, int maxDeviceBlockIn, const Limits& limits)
    {
        deviceRate = deviceRateIn;
        maxDeviceBlock = juce::jmax (1, maxDeviceBlockIn);
        deviceTime = 0;
        underflows = 0;
        guard.prepare (deviceRateIn, preparedOnce);
        preparedOnce = true;
        converting = limits.internal44k && std::abs (deviceRate - kEngineRate) > 1e-6;

        if (! converting)
        {
            ratio = 1.0;
            schedulingDelay = 0;
            latency = inputLatency = 0;
            engine.prepare (deviceRate, maxDeviceBlock, limits);
            return;
        }

        ratio = kEngineRate / deviceRate;
        engine.prepare (kEngineRate, kChunk, limits);

        const int maxEngineBlock = (int) std::ceil (maxDeviceBlock * ratio) + 2;

        outRs.prepare (2, kEngineRate, deviceRate, maxEngineBlock + 4 * kChunk + 256);
        inRs.prepare (2, deviceRate, kEngineRate, maxDeviceBlock);

        const int ho = outRs.latencyInInputSamples();                 // engine samples
        const int hiEngine = (int) std::ceil (inRs.latencyInInputSamples() * ratio);
        schedulingDelay = ho + kChunk + 2;
        latency = (int) std::ceil (schedulingDelay / ratio);

        // Input pre-roll so the engine (which runs ahead by up to Ho + kChunk) never
        // reads input that has not arrived yet.
        const int preroll = ho + hiEngine + 2 * kChunk + 8;
        inputLatency = (int) std::ceil ((preroll + hiEngine) / ratio);

        fifoCap = preroll + 2 * maxEngineBlock + 4 * kChunk + 64;
        for (auto& f : inFifo)
            f.assign ((size_t) fifoCap, 0.0f);
        fifoRead = 0;
        fifoCount = preroll;

        const int scratchSize = juce::jmax (maxDeviceBlock, maxEngineBlock) + 64;
        for (auto* v : { scratchIn, scratchOut })
            for (int ch = 0; ch < 2; ++ch)
                v[ch].assign ((size_t) scratchSize, 0.0f);
        for (auto* v : { chunkIn, chunkOut })
            for (int ch = 0; ch < 2; ++ch)
                v[ch].assign ((size_t) kChunk, 0.0f);
    }

    std::int64_t EngineHost::engineTimeForOffset (int offset) const noexcept
    {
        if (! converting)
            return deviceTime + offset;
        return (std::int64_t) std::llround ((double) (deviceTime + offset) * ratio) + schedulingDelay;
    }

    void EngineHost::scheduleAt (Command c, int offset) noexcept
    {
        c.time = engineTimeForOffset (offset);
        engine.schedule (c);
    }

    void EngineHost::process (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept
    {
        // Hosts may exceed the announced block size; split rather than fail.
        int done = 0;
        while (done < n)
        {
            const int m = juce::jmin (maxDeviceBlock, n - done);
            const float* inPtrs[2] = { numIn > 0 ? in[0] + done : nullptr, numIn > 1 ? in[1] + done : nullptr };
            float* outPtrs[8] {};
            for (int ch = 0; ch < juce::jmin (numOut, 8); ++ch)
                outPtrs[ch] = out[ch] + done;
            processChunk (inPtrs, numIn, outPtrs, juce::jmin (numOut, 8), m);
            done += m;
        }

        // Nothing louder than full scale and nothing non-finite reaches the host.
        guard.process (out, numOut, n);
        if (guard.faults() != guardFaults)
        {
            guardFaults = guard.faults();
            engine.resetEffects();
        }
    }

    void EngineHost::processChunk (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept
    {
        if (! converting)
        {
            engine.process (in, numIn, out, numOut, n);
            deviceTime += n;
            return;
        }

        // --- input: device rate -> engine rate, into the FIFO ---------------------------
        {
            const float* src[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                if (numIn > 0 && in[juce::jmin (ch, numIn - 1)] != nullptr)
                    src[ch] = in[juce::jmin (ch, numIn - 1)];
                else
                {
                    std::fill (scratchIn[ch].begin(), scratchIn[ch].begin() + n, 0.0f);
                    src[ch] = scratchIn[ch].data();
                }
            }
            inRs.push (src, n);

            float* dst[2] = { scratchOut[0].data(), scratchOut[1].data() };
            const int got = inRs.pull (dst, juce::jmin (inRs.available(), (int) scratchOut[0].size()));
            for (int i = 0; i < got && fifoCount < fifoCap; ++i)
            {
                const int w = (fifoRead + fifoCount) % fifoCap;
                inFifo[0][(size_t) w] = dst[0][i];
                inFifo[1][(size_t) w] = dst[1][i];
                ++fifoCount;
            }
        }

        // --- render engine chunks until the output resampler can deliver n -------------
        while (outRs.available() < n)
        {
            for (int i = 0; i < kChunk; ++i)
            {
                if (fifoCount > 0)
                {
                    chunkIn[0][(size_t) i] = inFifo[0][(size_t) fifoRead];
                    chunkIn[1][(size_t) i] = inFifo[1][(size_t) fifoRead];
                    fifoRead = (fifoRead + 1) % fifoCap;
                    --fifoCount;
                }
                else
                {
                    ++underflows;   // pre-roll too small
                    chunkIn[0][(size_t) i] = chunkIn[1][(size_t) i] = 0.0f;
                }
            }

            const float* ci[2] = { chunkIn[0].data(), chunkIn[1].data() };
            float* co[2] = { chunkOut[0].data(), chunkOut[1].data() };
            engine.process (ci, 2, co, 2, kChunk);
            const float* co2[2] = { co[0], co[1] };
            outRs.push (co2, kChunk);
        }

        // --- output: engine rate -> device rate ------------------------------------------
        float* dst[2] = { scratchOut[0].data(), scratchOut[1].data() };
        outRs.pull (dst, n);

        if (numOut == 1)
        {
            for (int i = 0; i < n; ++i)
                out[0][i] = 0.5f * (dst[0][i] + dst[1][i]);
        }
        else
        {
            std::copy (dst[0], dst[0] + n, out[0]);
            std::copy (dst[1], dst[1] + n, out[1]);
            for (int ch = 2; ch < numOut; ++ch)
                std::fill (out[ch], out[ch] + n, 0.0f);
        }
        deviceTime += n;
    }
}
