// Delay, echo, modulation and reverb effects.

#include "Effects.h"
#include "EffectInfo.h"
#include "DspUtil.h"

namespace spsx::fx
{
    using namespace dsp;

    namespace
    {
        // ---- DELAY --------------------------------------------------------------------
        class Delay final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& d : lines) d.prepare ((int) (2.2 * sr));
                for (auto& f : lpf) f.setCutoff (9000.0f, sr);
                for (auto& f : hpf) f.setCutoff (40.0f, sr);
                fb.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& d : lines) d.reset();
                for (auto& f : lpf) f.reset();
                for (auto& f : hpf) f.reset();
                current = target = delaySamples();
                fadePos = 1.0f;
                fb.reset (feedback());
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // DELAY TIME is a note value at the reference tempo, capped at ~2 s (*1).
                // Time changes cross-fade between the old and new tap (40 ms) instead of
                // sweeping the read head, so retiming never pitch-bends the repeats.
                const float wanted = delaySamples();
                if (std::abs (wanted - target) > 0.5f)
                {
                    current = fadePos >= 1.0f ? target : current;
                    target = wanted;
                    fadePos = 0.0f;
                }
                const float fadeStep = 1.0f / (0.04f * (float) ctx.sampleRate);
                fb.set (feedback());
                bal.set (params[2]);

                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float f = fb.next();
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    float a = 1.0f;
                    if (fadePos < 1.0f)
                    {
                        a = 0.5f - 0.5f * std::cos (kPi * fadePos);
                        fadePos = std::min (1.0f, fadePos + fadeStep);
                    }
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float x = ch[c][i];
                        const float tapped = a < 1.0f ? (1.0f - a) * lines[c].read (current) + a * lines[c].read (target)
                                                      : lines[c].read (target);
                        // Gentle tone loss and soft saturation inside the loop keep long
                        // feedback musical and bounded.
                        const float loop = std::tanh (hpf[c].highpass (lpf[c].lowpass (tapped)) * f);
                        lines[c].push (x + loop);
                        ch[c][i] = dry * x + wet * tapped;
                    }
                }
            }

        private:
            float delaySamples() const
            {
                const int idx = noteIndex (params[0], ctx.tempo, 2.0);
                return (float) (noteBeats()[(size_t) idx] * 60.0 / std::max (ctx.tempo, 1.0) * ctx.sampleRate);
            }
            // FEEDBACK is starred.
            float feedback() const { return params[1] * (ctx.limit ? 0.7f : 0.98f); }

            std::array<DelayLine, 2> lines;
            std::array<OnePole, 2> lpf, hpf;
            Smoother fb, bal;
            float current = 1.0f, target = 1.0f, fadePos = 1.0f;
        };

        // ---- TAPE ECHO ----------------------------------------------------------------
        class TapeEcho final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                line.prepare ((int) (0.8 * sr));
                lpf.setCutoff (3500.0f, sr);
                hpf.setCutoff (150.0f, sr);
                time.prepare (sr, 250.0f);   // slow: moving RATE bends pitch like a tape motor
                fb.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                line.reset();
                lpf.reset();
                hpf.reset();
                wow.reset (0.0f);
                flutter.reset (0.3f);
                time.reset (delaySamples());
                fb.reset (feedback());
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                wow.setRate (0.7f, ctx.sampleRate);
                flutter.setRate (6.3f, ctx.sampleRate);
                time.set (delaySamples());
                fb.set (feedback());
                bal.set (params[2]);
                for (int i = 0; i < n; ++i)
                {
                    const float base = time.next();
                    const float mod = 1.0f + 0.0022f * Lfo::sine (wow.advance()) + 0.0006f * Lfo::sine (flutter.advance());
                    const float tapped = line.read (base * mod);
                    // INTENSITY (starred) can exceed unity: the tape saturates instead of
                    // running away, as on a real echo unit. Effect Limit keeps it below.
                    const float loop = std::tanh (1.3f * hpf.highpass (lpf.lowpass (tapped)) * fb.next()) / 1.3f;
                    const float in = 0.5f * (l[i] + r[i]);
                    line.push (in + loop);
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    l[i] = dry * l[i] + wet * tapped;
                    r[i] = dry * r[i] + wet * tapped;
                }
            }

        private:
            float delaySamples() const { return expMap (params[0], 0.60f, 0.06f) * (float) ctx.sampleRate; }
            float feedback() const { return params[1] * (ctx.limit ? 0.75f : 1.1f); }

            DelayLine line;
            OnePole lpf, hpf;
            Lfo wow, flutter;
            Smoother time, fb, bal;
        };

        // ---- CHORUS -------------------------------------------------------------------
        class Chorus final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& d : lines) d.prepare ((int) (0.05 * sr));
                depth.prepare (sr, 40.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& d : lines) d.reset();
                lfo.reset();
                depth.reset (params[0]);
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                lfo.setRate (expMap (params[1], 0.05f, 6.0f), ctx.sampleRate);
                depth.set (params[0]);
                bal.set (params[2]);
                const float sr = (float) ctx.sampleRate;
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float p = lfo.advance();
                    const float d = depth.next() * 0.008f * sr;
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        // Left and right LFOs 90 degrees apart for width.
                        const float m = 0.5f + 0.5f * Lfo::sine (p + (c == 0 ? 0.0f : 0.25f));
                        const float x = ch[c][i];
                        lines[c].push (x);
                        const float y = lines[c].read (0.010f * sr + m * d);
                        ch[c][i] = dry * x + wet * y;
                    }
                }
            }

        private:
            std::array<DelayLine, 2> lines;
            Lfo lfo;
            Smoother depth, bal;
        };

        // ---- FLANGER ------------------------------------------------------------------
        class Flanger final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& d : lines) d.prepare ((int) (0.02 * sr));
                depth.prepare (sr, 40.0f);
                fb.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& d : lines) d.reset();
                last = { 0.0f, 0.0f };
                lfo.reset();
                depth.reset (params[0]);
                fb.reset (feedback());
                bal.reset (params[3]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                lfo.setRate (expMap (params[1], 0.05f, 6.0f), ctx.sampleRate);
                depth.set (params[0]);
                fb.set (feedback());
                bal.set (params[3]);   // hidden: MFX + CTRL 1 (p. 44)
                const float sr = (float) ctx.sampleRate;
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float p = lfo.advance();
                    const float d = depth.next();
                    const float f = fb.next();
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float m = 0.5f + 0.5f * Lfo::triangle (p + (c == 0 ? 0.0f : 0.25f));
                        const float delay = (0.0006f + m * d * 0.0055f) * sr;
                        const float x = ch[c][i];
                        lines[c].push (x + f * last[(size_t) c]);
                        const float y = lines[c].read (delay);
                        last[(size_t) c] = y;
                        ch[c][i] = dry * x + wet * y;
                    }
                }
            }

        private:
            float feedback() const { return params[2] * (ctx.limit ? 0.6f : 0.95f); }
            std::array<DelayLine, 2> lines;
            std::array<float, 2> last {};
            Lfo lfo;
            Smoother depth, fb, bal;
        };

        // ---- PHASER -------------------------------------------------------------------
        class Phaser final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                manual.prepare (sr, 40.0f);
                depth.prepare (sr, 40.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& ch : stages) for (auto& s : ch) s = 0.0f;
                fbState = { 0.0f, 0.0f };
                lfo.reset();
                manual.reset (params[2]);
                depth.reset (sweepOctaves());
                bal.reset (params[3]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                lfo.setRate (expMap (params[1], 0.05f, 8.0f), ctx.sampleRate);
                manual.set (params[2]);
                depth.set (sweepOctaves());
                bal.set (params[3]);   // hidden: MFX + CTRL 1 (p. 44)
                const float sr = (float) ctx.sampleRate;
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float p = lfo.advance();
                    const float centre = expMap (manual.next(), 120.0f, 3500.0f);
                    const float oct = depth.next();
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float m = Lfo::sine (p + (c == 0 ? 0.0f : 0.2f));
                        const float hz = std::clamp (centre * std::pow (2.0f, m * oct * 0.5f), 30.0f, sr * 0.45f);
                        const float g = std::tan (kPi * hz / sr);
                        const float G = g / (1.0f + g);
                        // Eight first-order TPT all-pass stages with feedback around them.
                        float y = ch[c][i] + 0.45f * fbState[(size_t) c];
                        for (auto& s : stages[(size_t) c])
                        {
                            const float v = (y - s) * G;
                            const float lp = v + s;
                            s = lp + v;
                            y = 2.0f * lp - y;
                        }
                        fbState[(size_t) c] = y;
                        ch[c][i] = dry * ch[c][i] + wet * y;
                    }
                }
            }

        private:
            // DEPTH is starred: Effect Limit halves the sweep.
            float sweepOctaves() const { return params[0] * (ctx.limit ? 2.0f : 4.0f); }
            std::array<std::array<float, 8>, 2> stages {};
            std::array<float, 2> fbState {};
            Lfo lfo;
            Smoother manual, depth, bal;
        };

        // ---- TREMOLO/PAN --------------------------------------------------------------
        class TremoloPan final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                depth.prepare (sr, 30.0f);
                shape.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                lfo.reset();
                depth.reset (params[0]);
                shape.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // WAVEFORM: left of centre = tremolo, right = auto-pan; towards either end
                // the curve goes from sine to square (p. 43).
                lfo.setRate (expMap (params[1], 0.1f, 15.0f), ctx.sampleRate);
                depth.set (params[0]);
                shape.set (params[2]);
                for (int i = 0; i < n; ++i)
                {
                    const float p = lfo.advance();
                    const float d = depth.next();
                    const float w = shape.next();
                    const bool pan = w >= 0.5f;
                    const float hard = pan ? (w - 0.5f) * 2.0f : (0.5f - w) * 2.0f;
                    const float s = Lfo::sine (p);
                    // Morph sine -> square with a variable-slope soft sign.
                    const float k = 1.0f + hard * 30.0f;
                    const float m = std::tanh (k * s) / std::tanh (k);
                    if (! pan)
                    {
                        const float g = 1.0f - d * (0.5f - 0.5f * m);
                        l[i] *= g;
                        r[i] *= g;
                    }
                    else
                    {
                        const float angle = (0.25f + 0.25f * m * d) * kPi;   // equal-power pan
                        const float mono = 0.5f * (l[i] + r[i]);
                        const float side = 0.5f * (l[i] - r[i]) * (1.0f - d);
                        l[i] = 1.41421356f * std::cos (angle) * mono + side;
                        r[i] = 1.41421356f * std::sin (angle) * mono - side;
                    }
                }
            }

        private:
            Lfo lfo;
            Smoother depth, shape;
        };

        // ---- REVERB -------------------------------------------------------------------
        /** 8-line feedback delay network: Householder mixing, per-line damping, two lines
            gently modulated against metallic ringing, four input diffusers, pre-delay. */
        class Reverb final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                const double scale = sr / 44100.0 * 1.35;
                const int lengths[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
                for (int i = 0; i < 8; ++i)
                {
                    lineLength[(size_t) i] = (float) (lengths[i] * scale);
                    lines[(size_t) i].prepare ((int) (lineLength[(size_t) i] + 32));
                }
                const int diff[4] = { 142, 107, 379, 277 };
                for (int i = 0; i < 4; ++i)
                {
                    diffLen[(size_t) i] = (int) (diff[i] * sr / 44100.0);
                    diffusers[(size_t) i].prepare (diffLen[(size_t) i] + 4);
                }
                predelay.prepare ((int) (0.03 * sr));
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& l : lines) l.reset();
                for (auto& d : diffusers) d.reset();
                for (auto& d : damp) d.reset();
                predelay.reset();
                mod.reset();
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                const float sr = (float) ctx.sampleRate;
                const float rt60 = expMap (params[0], 0.3f, 12.0f);
                for (int i = 0; i < 8; ++i)
                {
                    gains[(size_t) i] = std::pow (10.0f, -3.0f * lineLength[(size_t) i] / (rt60 * sr));
                    damp[(size_t) i].setCutoff (expMap (params[1], 1500.0f, 14000.0f), ctx.sampleRate);
                }
                mod.setRate (0.5f, ctx.sampleRate);
                bal.set (params[2]);

                for (int i = 0; i < n; ++i)
                {
                    predelay.push (0.5f * (l[i] + r[i]));
                    float x = predelay.readInt ((int) (0.012f * sr));
                    for (int d = 0; d < 4; ++d)
                    {
                        // Schroeder all-pass diffuser.
                        const float delayed = diffusers[(size_t) d].readInt (diffLen[(size_t) d]);
                        const float v = x + 0.7f * delayed;
                        diffusers[(size_t) d].push (v);
                        x = delayed - 0.7f * v;
                    }

                    const float m = Lfo::sine (mod.advance()) * 6.0f;
                    std::array<float, 8> out {};
                    float sum = 0.0f;
                    for (int k = 0; k < 8; ++k)
                    {
                        const float len = lineLength[(size_t) k] + (k == 0 ? m : k == 1 ? -m : 0.0f);
                        out[(size_t) k] = damp[(size_t) k].lowpass (lines[(size_t) k].read (len)) * gains[(size_t) k];
                        sum += out[(size_t) k];
                    }
                    // Householder reflection: energy-preserving, dense mixing.
                    const float h = sum * (2.0f / 8.0f);
                    for (int k = 0; k < 8; ++k)
                        lines[(size_t) k].push (out[(size_t) k] - h + x);

                    const float wl = 0.35f * (out[0] - out[2] + out[4] - out[6]);
                    const float wr = 0.35f * (out[1] - out[3] + out[5] - out[7]);
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    l[i] = dry * l[i] + wet * wl;
                    r[i] = dry * r[i] + wet * wr;
                }
            }

        private:
            std::array<DelayLine, 8> lines;
            std::array<float, 8> lineLength {}, gains {};
            std::array<OnePole, 8> damp;
            std::array<DelayLine, 4> diffusers;
            std::array<int, 4> diffLen {};
            DelayLine predelay;
            Lfo mod;
            Smoother bal;
        };
    }

    std::unique_ptr<Effect> createDelay()      { return std::make_unique<Delay>(); }
    std::unique_ptr<Effect> createTapeEcho()   { return std::make_unique<TapeEcho>(); }
    std::unique_ptr<Effect> createChorus()     { return std::make_unique<Chorus>(); }
    std::unique_ptr<Effect> createFlanger()    { return std::make_unique<Flanger>(); }
    std::unique_ptr<Effect> createPhaser()     { return std::make_unique<Phaser>(); }
    std::unique_ptr<Effect> createTremoloPan() { return std::make_unique<TremoloPan>(); }
    std::unique_ptr<Effect> createReverb()     { return std::make_unique<Reverb>(); }
}
