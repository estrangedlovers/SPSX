#pragma once

#include <algorithm>
#include <cmath>

namespace spsx
{
    /** Last stage before the device or host: nothing louder than full scale and nothing
        non-finite ever leaves the app, in either mode.

        - Non-finite samples (NaN, Inf) or absurd values mute the rest of the block and
          the output fades back in over kFadeSeconds; `faults()` counts the events so the
          engine can reset the effect that caused them.
        - Brickwall at the ceiling (0 dBFS): instant attack, smooth release. Below the
          ceiling the gain is exactly 1, so the output is bit-identical to the input (the
          Authentic hard clip and every bit-exact path are untouched). It only acts on
          overs: Extended mode (no hard clip), sample-rate converter overshoot, the
          metronome on top of a full-scale mix.
        - Fade-in after a re-prepare and after a fault, so neither pops.

        Real-time safe; channels are processed linked so the stereo image holds. */
    class OutputGuard
    {
    public:
        static constexpr float  kCeiling       = 1.0f;
        static constexpr float  kFaultLevel    = 1.0e4f;   // +80 dBFS: something is broken
        static constexpr double kReleaseSeconds = 0.08;
        static constexpr double kFadeSeconds   = 0.02;

        /** `fadeIn`: start from silence (a re-prepare while audio may be playing). */
        void prepare (double sampleRate, bool fadeIn) noexcept
        {
            rate = sampleRate > 0.0 ? sampleRate : 44100.0;
            releaseCoef = (float) (1.0 - std::exp (-1.0 / (kReleaseSeconds * rate)));
            fadeStep = (float) (1.0 / (kFadeSeconds * rate));
            gain = 1.0f;
            fade = fadeIn ? 0.0f : 1.0f;
        }

        void process (float* const* ch, int numChannels, int n) noexcept
        {
            numChannels = std::min (numChannels, 8);
            if (numChannels <= 0)
                return;

            for (int i = 0; i < n; ++i)
            {
                float peak = 0.0f;
                bool bad = false;
                for (int c = 0; c < numChannels; ++c)
                {
                    const float x = ch[c][i];
                    const float a = std::abs (x);
                    bad = bad || ! (a < kFaultLevel);   // NaN compares false
                    peak = std::max (peak, a);
                }

                if (bad)
                {
                    ++faultCount;
                    gain = 1.0f;
                    fade = 0.0f;
                    for (int c = 0; c < numChannels; ++c)
                        std::fill (ch[c] + i, ch[c] + n, 0.0f);
                    return;
                }

                // Release toward unity, then attack instantly if this sample is over.
                if (gain < 1.0f)
                {
                    // Minimum step: near unity the exponential increment drops below one
                    // float ulp and would stall short of exact transparency.
                    gain += std::max ((1.0f - gain) * releaseCoef, 1.0e-6f);
                    if (gain > 0.99999f)
                        gain = 1.0f;
                }
                if (peak * gain > kCeiling)
                {
                    gain = kCeiling / peak;
                    ++limitedSamples;
                }

                float g = gain;
                if (fade < 1.0f)
                {
                    fade = std::min (1.0f, fade + fadeStep);
                    g *= fade;
                }
                if (g < 1.0f)    // unity leaves the samples untouched (bit-exact)
                    for (int c = 0; c < numChannels; ++c)
                        ch[c][i] = std::clamp (ch[c][i] * g, -kCeiling, kCeiling);
            }
        }

        /** Non-finite or absurd blocks seen so far (audio thread). */
        int faults() const noexcept { return faultCount; }
        /** Samples the brickwall had to reduce (for tests and diagnostics). */
        long long limited() const noexcept { return limitedSamples; }
        float currentGain() const noexcept { return gain; }

    private:
        double rate = 44100.0;
        float releaseCoef = 0.001f, fadeStep = 0.001f;
        float gain = 1.0f, fade = 1.0f;
        int faultCount = 0;
        long long limitedSamples = 0;
    };
}
