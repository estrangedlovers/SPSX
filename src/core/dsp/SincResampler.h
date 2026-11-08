#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace spsx::dsp
{
    /** Streaming polyphase windowed-sinc resampler for arbitrary rate ratios.

        Why a custom one: the engine runs at 44.1 kHz so Lo-Fi, Bit Crash and pitch
        effects alias the same way whatever the device rate is (ARCHITECTURE.md
        section 5). That needs a converter whose alias rejection is well below the
        16-bit noise floor and whose latency is fixed and known.

        Design:
        - Kaiser-windowed sinc, beta 9 (about -90 dB stopband), cutoff at 0.95 x the
          lower Nyquist so 44.1k <-> 48k keeps everything up to ~20.9 kHz.
        - When downsampling, the kernel is widened by 1/ratio so the transition band
          stays the same width in the output domain.
        - The kernel is tabulated at `kPhases` fractional offsets; coefficients between
          phases are linearly interpolated (table error < -100 dB at 512 phases).
        - The history starts with `halfTaps` zeros so output sample k maps exactly to
          input time k * step - halfTaps. Latency = halfTaps input samples, constant.

        Real-time safe after prepare(): push/pull never allocate. */
    class SincResampler
    {
    public:
        static constexpr int kPhases = 512;

        void prepare (int numChannelsIn, double inputRate, double outputRate, int maxInputPerPush)
        {
            numChannels = numChannelsIn;
            step = inputRate / outputRate;

            const double ratio  = outputRate / inputRate;            // < 1 when downsampling
            const double widen  = std::max (1.0, 1.0 / ratio);
            halfTaps = (int) std::ceil (kBaseHalfTaps * widen);
            const double cutoff = 0.5 * std::min (1.0, ratio) * 0.95; // cycles per input sample

            const int taps = 2 * halfTaps;
            table.assign ((size_t) (kPhases + 1) * (size_t) taps, 0.0f);

            // Tap j (0..taps-1) for fractional offset f multiplies x[i - halfTaps + 1 + j],
            // whose distance from the output time (i + f) is d = j - halfTaps + 1 - f.
            for (int p = 0; p <= kPhases; ++p)
            {
                const double f = (double) p / kPhases;
                double sum = 0.0;
                float* tab = table.data() + (size_t) p * (size_t) taps;

                for (int j = 0; j < taps; ++j)
                {
                    const double d = (double) (j - halfTaps + 1) - f;
                    const double x = 2.0 * cutoff * d;
                    const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                    const double w = kaiser (d / (double) halfTaps);
                    tab[j] = (float) (2.0 * cutoff * sinc * w);
                    sum += tab[j];
                }

                // Normalise DC gain per phase so a constant input stays exactly constant.
                for (int j = 0; j < taps; ++j)
                    tab[j] = (float) (tab[j] / sum);
            }

            row.assign ((size_t) taps, 0.0f);
            capacity = maxInputPerPush + 4 * taps + 64;
            history.assign ((size_t) numChannels, std::vector<float> ((size_t) capacity, 0.0f));
            reset();
        }

        void reset()
        {
            for (auto& h : history)
                std::fill (h.begin(), h.end(), 0.0f);

            // halfTaps zeros of pre-roll, then input sample 0 sits at buffer index halfTaps.
            // Output k is centred on input time k * step exactly (no time offset); producing
            // it needs halfTaps samples of look-ahead, which is the streaming latency.
            buffered = halfTaps;
            readTime = (double) halfTaps;
        }

        /** Input samples of delay between an input event and the matching output. */
        int latencyInInputSamples() const noexcept { return halfTaps; }
        double latencyInOutputSamples() const noexcept { return halfTaps / step; }
        double getStep() const noexcept { return step; }

        void push (const float* const* in, int n)
        {
            assert (buffered + n <= capacity);
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (in[ch], in[ch] + n, history[(size_t) ch].begin() + buffered);
            buffered += n;
        }

        /** Number of outputs that can be produced from the input pushed so far. */
        int available() const noexcept
        {
            // Output at time t needs x[floor(t) + halfTaps].
            const double lastUsable = (double) (buffered - 1 - halfTaps);
            if (lastUsable < readTime)
                return 0;
            return (int) std::floor ((lastUsable - readTime) / step) + 1;
        }

        int pull (float* const* out, int n)
        {
            n = std::min (n, available());
            const int taps = 2 * halfTaps;

            for (int k = 0; k < n; ++k)
            {
                const int    i    = (int) readTime;
                const double frac = readTime - i;
                const double pf   = frac * kPhases;
                const int    p    = std::min ((int) pf, kPhases - 1);
                const float  a    = (float) (pf - p);
                const float* r0   = table.data() + (size_t) p * (size_t) taps;
                const float* r1   = r0 + taps;
                const int    base = i - halfTaps + 1;

                // Interpolate the coefficient row once, shared by all channels; the dot
                // products below then vectorise.
                float* coef = row.data();
                for (int j = 0; j < taps; ++j)
                    coef[j] = r0[j] + a * (r1[j] - r0[j]);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* x = history[(size_t) ch].data() + base;
                    float acc = 0.0f;
                    for (int j = 0; j < taps; ++j)
                        acc += x[j] * coef[j];
                    out[ch][k] = acc;
                }

                readTime += step;
            }

            discardConsumed();
            return n;
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr int    kBaseHalfTaps = 64;   // 128 taps: ~2 kHz transition at 44.1/48 kHz
        static constexpr double kBeta = 9.0;

        static double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        }

        static double kaiser (double t)   // t in [-1, 1]
        {
            if (t <= -1.0 || t >= 1.0)
                return 0.0;
            return besselI0 (kBeta * std::sqrt (1.0 - t * t)) / besselI0 (kBeta);
        }

        void discardConsumed()
        {
            // Keep everything from floor(readTime) - halfTaps + 1 onwards.
            const int keepFrom = (int) readTime - halfTaps + 1;
            if (keepFrom <= 0)
                return;

            for (auto& h : history)
                std::copy (h.begin() + keepFrom, h.begin() + buffered, h.begin());

            buffered -= keepFrom;
            readTime -= keepFrom;
        }

        int numChannels = 0;
        double step = 1.0;
        int halfTaps = kBaseHalfTaps;
        int capacity = 0;
        int buffered = 0;
        double readTime = 0.0;
        std::vector<float> table;
        std::vector<float> row;
        std::vector<std::vector<float>> history;
    };
}
