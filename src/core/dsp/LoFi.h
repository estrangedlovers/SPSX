#pragma once

#include <cmath>
#include <algorithm>

namespace spsx::dsp
{
    /** The pad LO-FI mode (manual p. 12: "Switches the sample's playback quality").
        The manual says nothing about how. Working model (FIDELITY.md, confidence Low):
        zero-order-hold decimation with NO anti-alias filter (the aliasing is the
        point), requantization without dither, then a gentle one-pole low-pass that
        stands in for the hardware's output smoothing. All values live in Params so the
        Fidelity lab (Phase 10) can tune them without code changes. */
    class LoFi
    {
    public:
        struct Params
        {
            int   holdFactor = 4;       // 44.1k / 4 = 11.025 kHz effective rate
            int   bits       = 10;      // quantization depth
            float lowpassHz  = 9000.0f; // post smoothing
        };

        void prepare (double sampleRate, const Params& p) noexcept
        {
            params = p;
            const double x = std::exp (-2.0 * kPi * p.lowpassHz / sampleRate);
            lpCoeff = (float) x;
            steps   = (float) (1 << (p.bits - 1));
            reset();
        }

        void reset() noexcept
        {
            counter = 0;
            held[0] = held[1] = 0.0f;
            lp[0] = lp[1] = 0.0f;
        }

        /** In-place on up to two channels. Channels share the hold clock, like a
            single converter would. */
        void process (float* const* ch, int numChannels, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const bool sampleNow = (counter == 0);
                counter = (counter + 1) % params.holdFactor;

                for (int c = 0; c < numChannels; ++c)
                {
                    if (sampleNow)
                        held[c] = std::round (ch[c][i] * steps) / steps;   // truncation-free mid-tread quantizer

                    lp[c] = held[c] + lpCoeff * (lp[c] - held[c]);
                    ch[c][i] = lp[c];
                }
            }
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        Params params;
        float lpCoeff = 0.0f, steps = 512.0f;
        int counter = 0;
        float held[2] {}, lp[2] {};
    };
}
