// Pitch and voice effects. All low latency (< 25 ms) so they stay playable.

#include "Effects.h"
#include "DspUtil.h"

namespace spsx::fx
{
    using namespace dsp;

    namespace
    {
        // ---- VOICE TRANS --------------------------------------------------------------
        /** Formant shifter that keeps the pitch: a pitch-synchronous overlap-add.
            - A YIN-style tracker (on a 4x decimated signal, every 256 samples) estimates
              the period T. Unvoiced input falls back to T = 5 ms.
            - Every T output samples a grain starts, read from the input around a point
              a fixed latency behind the write head. Because successive grains start one
              period apart, voiced content overlaps coherently: pitch is preserved.
            - Each grain reads the input at FORMANT ratio f, so its spectral envelope
              (the formants) is scaled by f while the grain spacing keeps the pitch.
            Latency is about 15 ms. */
        class VoiceTrans final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                latency = (int) (0.0145 * sr);
                maxPeriod = (int) (0.0136 * sr);          // ~73 Hz
                fallbackPeriod = (float) (0.005 * sr);
                input.assign (kRing, 0.0f);
                formant.prepare (sr, 40.0f);
                eff.prepare (sr, 20.0f);
                dir.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                std::fill (input.begin(), input.end(), 0.0f);
                write = 0;
                for (auto& g : grains) g.active = false;
                untilGrain = 0;
                untilAnalysis = 0;
                period = fallbackPeriod;
                formant.reset (formantRatio());
                eff.reset (params[1]);
                dir.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                formant.set (formantRatio());
                eff.set (params[1]);
                dir.set (params[2]);
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.5f * (l[i] + r[i]);
                    input[(size_t) (write & kMask)] = x;
                    ++write;

                    if (--untilAnalysis <= 0)
                    {
                        analyse();
                        untilAnalysis = 256;
                    }

                    const float f = formant.next();
                    if (--untilGrain <= 0)
                    {
                        spawnGrain (f);
                        untilGrain = std::max (8, (int) std::lround (period));
                    }

                    float y = 0.0f;
                    for (auto& g : grains)
                    {
                        if (! g.active) continue;
                        const float t = g.pos / g.length;                       // 0..1 across the grain
                        const float w = 0.5f - 0.5f * std::cos (kTwoPi * t);
                        y += w * readInput (g.start + g.pos * g.ratio);
                        g.pos += 1.0f;
                        if (g.pos >= g.length) g.active = false;
                    }
                    // Overlap of Hann grains of length 2T/f at spacing T sums to 1/f.
                    y *= std::min (f, 1.6f);

                    const float wet = y * eff.next();
                    const float d = dir.next();
                    l[i] = d * l[i] + wet;
                    r[i] = d * r[i] + wet;
                }
            }

        private:
            static constexpr int kRing = 8192, kMask = kRing - 1;

            struct Grain
            {
                bool active = false;
                double start = 0.0;     // input index at grain start
                float pos = 0.0f, length = 1.0f, ratio = 1.0f;
            };

            float formantRatio() const { return std::pow (2.0f, (params[0] - 0.5f) * 2.0f); }   // 0.5..2

            float readInput (double index) const noexcept
            {
                const auto i0 = (long long) std::floor (index);
                const float fr = (float) (index - (double) i0);
                const float a = input[(size_t) (i0 & kMask)], b = input[(size_t) ((i0 + 1) & kMask)];
                return a + fr * (b - a);
            }

            void spawnGrain (float f) noexcept
            {
                for (auto& g : grains)
                {
                    if (g.active) continue;
                    // Input span of 2T centred `latency` behind the write head.
                    g.active = true;
                    g.ratio = f;
                    g.length = std::max (8.0f, 2.0f * period / f);
                    g.start = (double) write - latency - period;
                    g.pos = 0.0f;
                    return;
                }
            }

            void analyse() noexcept
            {
                // YIN cumulative-mean-normalised difference on a 4x decimated window.
                constexpr int D = 4, W = 256;
                const int maxLag = std::min (maxPeriod / D, 180);
                float dec[W + 200];
                const long long base = write - (W + maxLag) * D;
                for (int k = 0; k < W + maxLag; ++k)
                {
                    float s = 0.0f;
                    for (int j = 0; j < D; ++j) s += input[(size_t) ((base + k * D + j) & kMask)];
                    dec[k] = s * 0.25f;
                }

                float runningSum = 0.0f;
                int best = -1;
                float prevNorm = 1.0f;
                for (int tau = 1; tau <= maxLag; ++tau)
                {
                    float diff = 0.0f;
                    for (int k = 0; k < W; ++k)
                    {
                        const float dd = dec[k] - dec[k + tau];
                        diff += dd * dd;
                    }
                    runningSum += diff;
                    const float norm = runningSum > 0.0f ? diff * (float) tau / runningSum : 1.0f;
                    if (tau > 3 && norm < 0.15f && norm < prevNorm)
                        best = tau;
                    else if (best > 0)
                        break;   // first dip below threshold found: take its minimum
                    prevNorm = norm;
                }

                const float target = best > 0 ? (float) (best * D) : fallbackPeriod;
                period += 0.5f * (target - period);   // smooth jumps between frames
            }

            std::vector<float> input;
            long long write = 0;
            std::array<Grain, 8> grains;
            int untilGrain = 0, untilAnalysis = 0, latency = 640, maxPeriod = 600;
            float period = 220.0f, fallbackPeriod = 220.0f;
            Smoother formant, eff, dir;
        };

        // ---- OCTAVE -------------------------------------------------------------------
        class Octave final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                down1.prepare (sr, 50.0f);
                down2.prepare (sr, 70.0f);
                down1.setRatio (0.5f);
                down2.setRatio (0.25f);
                lp1.setCutoff (2200.0f, sr);
                lp2.setCutoff (900.0f, sr);
                for (auto& s : lv) s.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                down1.reset();
                down2.reset();
                lp1.reset();
                lp2.reset();
                for (int k = 0; k < 3; ++k) lv[(size_t) k].reset (params[(size_t) k]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                for (int k = 0; k < 3; ++k) lv[(size_t) k].set (params[(size_t) k]);
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.5f * (l[i] + r[i]);
                    const float o1 = lp1.lowpass (down1.process (x));
                    const float o2 = lp2.lowpass (down2.process (x));
                    const float g2 = lv[0].next(), g1 = lv[1].next(), gd = lv[2].next();
                    const float sub = 1.2f * (g2 * o2 + g1 * o1);
                    l[i] = gd * l[i] + sub;
                    r[i] = gd * r[i] + sub;
                }
            }

        private:
            PitchShifter down1, down2;
            OnePole lp1, lp2;
            std::array<Smoother, 3> lv;
        };

        // ---- CHROMATIC PS -------------------------------------------------------------
        class ChromaticPS final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& ch : shifters) for (auto& s : ch) s.prepare (sr, 45.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& ch : shifters) for (auto& s : ch) s.reset();
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // Two voices, each +-1 octave in semitone steps.
                const float r1 = std::pow (2.0f, (float) semitonesFor (params[0]) / 12.0f);
                const float r2 = std::pow (2.0f, (float) semitonesFor (params[1]) / 12.0f);
                for (auto& ch : shifters) { ch[0].setRatio (r1); ch[1].setRatio (r2); }
                bal.set (params[2]);
                float* io[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float x = io[c][i];
                        const float y = 0.5f * (shifters[(size_t) c][0].process (x) + shifters[(size_t) c][1].process (x));
                        io[c][i] = dry * x + wet * y;
                    }
                }
            }

        private:
            static int semitonesFor (float v) { return std::clamp ((int) std::lround (v * 24.0f), 0, 24) - 12; }
            std::array<std::array<PitchShifter, 2>, 2> shifters;
            Smoother bal;
        };

        // ---- PITCH --------------------------------------------------------------------
        class Pitch final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& s : shifters) s.prepare (sr, 45.0f);
                ratio.prepare (sr, 40.0f);
                fb.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& s : shifters) s.reset();
                last = { 0.0f, 0.0f };
                ratio.reset (ratioFor());
                fb.reset (feedback());
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // Continuous +-1 octave. FEEDBACK (starred) re-shifts the output, so
                // repeats climb or fall in pitch.
                ratio.set (ratioFor());
                fb.set (feedback());
                bal.set (params[2]);
                float* io[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float rr = ratio.next();
                    const float f = fb.next();
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        shifters[(size_t) c].setRatio (rr);
                        const float x = io[c][i];
                        const float y = shifters[(size_t) c].process (x + f * last[(size_t) c]);
                        last[(size_t) c] = std::tanh (y);
                        io[c][i] = dry * x + wet * y;
                    }
                }
            }

        private:
            float ratioFor() const { return std::pow (2.0f, (params[0] - 0.5f) * 2.0f); }
            float feedback() const { return params[1] * (ctx.limit ? 0.5f : 0.85f); }
            std::array<PitchShifter, 2> shifters;
            std::array<float, 2> last {};
            Smoother ratio, fb, bal;
        };
    }

    std::unique_ptr<Effect> createVoiceTrans()  { return std::make_unique<VoiceTrans>(); }
    std::unique_ptr<Effect> createOctave()      { return std::make_unique<Octave>(); }
    std::unique_ptr<Effect> createChromaticPS() { return std::make_unique<ChromaticPS>(); }
    std::unique_ptr<Effect> createPitch()       { return std::make_unique<Pitch>(); }
}
