#pragma once

#include "Engine.h"
#include "../dsp/SincResampler.h"

namespace spsx
{
    /** Runs the Engine at 44.1 kHz inside a host or device running at any rate
        (ARCHITECTURE.md section 5), or directly at the device rate when the internal
        rate equals it or Extended "native rate" is on.

        Timing model (SRC path):
        - Output sample d (absolute device time) is engine time d * r, r = 44100 / fs.
          The output resampler needs `Ho` engine samples of look-ahead, so the engine
          renders ahead of the output by up to Ho + kChunk samples.
        - Events from the host block at device offset o are scheduled at engine time
          (D + o) * r + S, S = Ho + kChunk + 2. They are therefore always in the future
          for the engine and every event has the same constant latency S / r device
          samples. That latency is what latencySamples() reports to the host.
        - The input path is delayed by a zero pre-roll so the engine never waits for
          input that has not arrived. Round-trip input latency is reported separately
          for the sampler (Phase 4).
        Real-time safe after prepare(). */
    class EngineHost
    {
    public:
        static constexpr int kChunk = 32;   // engine render granularity on the SRC path

        explicit EngineHost (Engine& e) : engine (e) {}

        void prepare (double deviceRateIn, int maxDeviceBlockIn, const Limits& limits);

        /** Absolute engine time for an event at `offset` within the current device block. */
        std::int64_t engineTimeForOffset (int offset) const noexcept;
        /** Audio thread: schedule a command at a device-block offset. */
        void scheduleAt (Command c, int offset) noexcept;

        void process (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept;

        int latencySamples() const noexcept { return latency; }
        int inputLatencySamples() const noexcept { return inputLatency; }
        bool isConverting() const noexcept { return converting; }
        /** Times the engine had to read input that had not arrived (should stay 0). */
        int underflowCount() const noexcept { return underflows; }
        double engineRate() const noexcept { return engine.getRate(); }

    private:
        void processChunk (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept;

        Engine& engine;
        bool converting = false;
        double deviceRate = kEngineRate;
        double ratio = 1.0;              // engine samples per device sample
        int maxDeviceBlock = 512;
        int schedulingDelay = 0;         // S, engine samples
        int latency = 0;
        int inputLatency = 0;
        std::int64_t deviceTime = 0;
        int underflows = 0;

        dsp::SincResampler inRs, outRs;
        std::vector<float> inFifo[2];
        int fifoRead = 0, fifoCount = 0, fifoCap = 0;
        std::vector<float> scratchIn[2], scratchOut[2], chunkIn[2], chunkOut[2];
    };
}
