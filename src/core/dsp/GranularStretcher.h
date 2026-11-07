#pragma once

#include "../Types.h"
#include <cmath>
#include <array>

namespace spsx::dsp
{
    /** Real-time pitch-preserving time stretch used for Authentic-mode Time Modify.

        The manual warns that Time Modify "may sound quavery or noisy" (p. 30), which
        is the signature of a plain overlap-add granulator, so that is what this is:
        Hann grains of ~40 ms at 50% overlap, each grain reading the source at the
        original speed (pitch preserved) while grain start points advance by
        hop * speed. No transient or phase alignment on purpose. Confidence Low until
        the Fidelity lab compares it with hardware recordings.

        The first grain uses a flat leading half so the sample's attack is not faded in.
        Allocation-free; reads straight from the sample's channel pointers. */
    class GranularStretcher
    {
    public:
        static constexpr int kGrain = 1764;            // 40 ms at 44.1 kHz
        static constexpr int kHop   = kGrain / 2;

        void start (double sourcePosition, double speedIn) noexcept
        {
            analysisPos = sourcePosition;
            speed = speedIn;
            for (auto& g : grains)
                g.active = false;
            untilNextGrain = 0;
            first = true;
            stopSpawning = false;
            finished = false;
        }

        void setSpeed (double s) noexcept { speed = s; }
        bool isFinished() const noexcept  { return finished; }
        double position() const noexcept  { return analysisPos; }

        /** Renders n samples into out[0..numOutCh). Region is [start, end).
            Returns the number of samples rendered before finishing (n if still running). */
        int process (const float* const* src, int srcChannels, SampleIndex start, SampleIndex end,
                     bool loop, float* const* out, int numOutCh, int n) noexcept
        {
            const double regionLen = (double) (end - start);
            if (regionLen <= 0.0)
            {
                finished = true;
                return 0;
            }

            for (int i = 0; i < n; ++i)
            {
                if (untilNextGrain == 0)
                {
                    if (analysisPos >= (double) end)
                    {
                        if (loop)
                            analysisPos = (double) start + std::fmod (analysisPos - (double) start, regionLen);
                        else
                            stopSpawning = true;
                    }

                    if (! stopSpawning)
                    {
                        auto& g = grains[(size_t) nextSlot];
                        nextSlot ^= 1;
                        g.active = true;
                        g.srcStart = analysisPos;
                        g.phase = 0;
                        g.flatHead = first;
                        first = false;
                        analysisPos += kHop * speed;
                    }
                    untilNextGrain = kHop;
                }
                --untilNextGrain;

                float acc[2] { 0.0f, 0.0f };
                bool any = false;

                for (auto& g : grains)
                {
                    if (! g.active)
                        continue;
                    any = true;

                    double p = g.srcStart + g.phase;
                    if (p >= (double) end)
                        p = loop ? (double) start + std::fmod (p - (double) start, regionLen) : -1.0;

                    if (p >= 0.0)
                    {
                        const float w = (g.flatHead && g.phase < kHop) ? 1.0f : window (g.phase);
                        const auto idx = (SampleIndex) p;
                        for (int c = 0; c < numOutCh; ++c)
                            acc[c] += w * src[c < srcChannels ? c : srcChannels - 1][idx];
                    }

                    if (++g.phase >= kGrain)
                        g.active = false;
                }

                if (! any && stopSpawning)
                {
                    finished = true;
                    return i;
                }

                for (int c = 0; c < numOutCh; ++c)
                    out[c][i] = acc[c];
            }
            return n;
        }

    private:
        static float window (int phase) noexcept
        {
            // Periodic Hann: two grains at 50% overlap sum to exactly 1.
            static const auto table = []
            {
                std::array<float, kGrain> t {};
                for (int i = 0; i < kGrain; ++i)
                    t[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / kGrain));
                return t;
            }();
            return table[(size_t) phase];
        }

        struct Grain
        {
            bool active = false;
            bool flatHead = false;
            double srcStart = 0.0;
            int phase = 0;
        };

        std::array<Grain, 2> grains {};
        int nextSlot = 0;
        int untilNextGrain = 0;
        double analysisPos = 0.0;
        double speed = 1.0;
        bool first = true;
        bool stopSpawning = false;
        bool finished = false;
    };
}
