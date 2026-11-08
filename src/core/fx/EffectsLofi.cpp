// Lo-fi, noise and utility effects. The lo-fi family keeps aliasing and quantisation
// where the hardware would have them (FIDELITY.md).

#include "Effects.h"
#include "DspUtil.h"

namespace spsx::fx
{
    using namespace dsp;

    namespace
    {
        /** Random clicks and pops: sparse impulses of random size through a short
            resonant decay, like dust on a record. */
        class Crackle
        {
        public:
            void prepare (double sr, std::uint32_t seed)
            {
                noise = Noise (seed);
                body.set (Biquad::Type::bandpass, 2200.0, 0.9, 0.0, sr);
                sampleRate = sr;
            }
            void reset() { body.reset(); impulse = 0.0f; }
            float next (float density) noexcept
            {
                // density 0..1 -> up to ~40 pops per second.
                float x = 0.0f;
                if (noise.uniform01() < density * 40.0f / (float) sampleRate)
                    x = noise.next() * (0.3f + 0.7f * noise.uniform01());
                impulse = x;
                return body.process (impulse) * 3.0f;
            }

        private:
            Noise noise;
            Biquad body;
            double sampleRate = 44100.0;
            float impulse = 0.0f;
        };

        // ---- NOISE GEN ----------------------------------------------------------------
        class NoiseGen final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (size_t c = 0; c < 2; ++c)
                {
                    band[c][0].set (Biquad::Type::highpass, 110.0, 0.707, 0.0, sr);
                    band[c][1].set (Biquad::Type::lowpass, 6500.0, 0.707, 0.0, sr);
                    hiss[c].setCutoff (1500.0f, sr);
                    crackle[c].prepare (sr, 0x1234567u + (std::uint32_t) c * 7919u);
                    white[c] = Noise (0xBADC0DEu + (std::uint32_t) c);
                }
                for (auto& s : lv) s.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                for (auto& ch : band) for (auto& b : ch) b.reset();
                for (auto& h : hiss) h.reset();
                for (auto& c : crackle) c.reset();
                humPhase = 0.0f;
                for (int k = 0; k < 3; ++k) lv[(size_t) k].reset (params[(size_t) k]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // "Applies a lo-fi effect" (band-limit + 12-bit) and adds hiss, record
                // noise and mains hum (p. 43). 60 Hz hum with harmonics.
                for (int k = 0; k < 3; ++k) lv[(size_t) k].set (params[(size_t) k]);
                const float humInc = 60.0f / (float) ctx.sampleRate;
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float wh = lv[0].next(), di = lv[1].next(), hu = lv[2].next();
                    humPhase += humInc;
                    if (humPhase >= 1.0f) humPhase -= 1.0f;
                    const float hum = Lfo::sine (humPhase) + 0.5f * Lfo::sine (2.0f * humPhase)
                                    + 0.3f * Lfo::sine (3.0f * humPhase) + 0.15f * Lfo::sine (5.0f * humPhase);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        float x = band[c][1].process (band[c][0].process (ch[c][i]));
                        x = std::round (x * 2048.0f) / 2048.0f;
                        const float h = hiss[c].highpass (white[c].next()) * wh * wh * 0.25f;
                        const float p = crackle[c].next (di) * di * 0.6f;
                        ch[c][i] = x + h + p + hum * hu * hu * 0.12f;
                    }
                }
            }

        private:
            std::array<std::array<Biquad, 2>, 2> band;
            std::array<OnePole, 2> hiss;
            std::array<Crackle, 2> crackle;
            std::array<Noise, 2> white;
            std::array<Smoother, 3> lv;
            float humPhase = 0.0f;
        };

        // ---- VINYL SIM ----------------------------------------------------------------
        class VinylSim final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (size_t c = 0; c < 2; ++c)
                {
                    lines[c].prepare ((int) (0.02 * sr));
                    crackle[c].prepare (sr, 0xC0FFEEu + (std::uint32_t) c * 104729u);
                    white[c] = Noise (0x51EEDu + (std::uint32_t) c);
                    surface[c].setCutoff (3000.0f, sr);
                }
                range.prepare (sr, 40.0f);
                wf.prepare (sr, 40.0f);
                noiseLv.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                for (auto& d : lines) d.reset();
                for (auto& ch : filt) for (auto& b : ch) b.reset();
                for (auto& c : crackle) c.reset();
                for (auto& s : surface) s.reset();
                wow.reset();
                flutter.reset (0.4f);
                range.reset (params[0]);
                wf.reset (params[2]);
                noiseLv.reset (params[1]);
                updateFilters (params[0]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // FREQUENCY RANGE narrows the playback system from full range to a worn
                // portable player; WOW/FLUTTER is 33 1/3 rpm wow plus motor flutter.
                range.set (params[0]);
                updateFilters (range.advance (n));
                wf.set (params[2]);
                noiseLv.set (params[1]);
                wow.setRate (0.555f, ctx.sampleRate);
                flutter.setRate (7.5f, ctx.sampleRate);
                const float sr = (float) ctx.sampleRate;
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float w = wf.next();
                    const float nz = noiseLv.next();
                    const float delay = (0.004f + w * (0.0025f * Lfo::sine (wow.advance()) + 0.0003f * Lfo::sine (flutter.advance()))) * sr;
                    const float mid = 0.5f * (ch[0][i] + ch[1][i]);
                    const float side = 0.5f * (ch[0][i] - ch[1][i]) * 0.7f;   // narrower stereo
                    for (size_t c = 0; c < 2; ++c)
                    {
                        lines[c].push (c == 0 ? mid + side : mid - side);
                        float x = lines[c].read (delay);
                        for (auto& b : filt[c]) x = b.process (x);
                        x = std::tanh (x * 1.1f) / 1.1f;
                        const float s = surface[c].lowpass (white[c].next()) * 0.04f;
                        ch[c][i] = x + nz * nz * (crackle[c].next (nz) * 0.5f + s);
                    }
                }
            }

        private:
            void updateFilters (float v)
            {
                const double hp = expMap (v, 25.0f, 300.0f);
                const double lp = expMap (1.0f - v, 2800.0f, 18000.0f);
                for (auto& ch : filt)
                {
                    ch[0].set (Biquad::Type::highpass, hp, 0.707, 0.0, ctx.sampleRate);
                    ch[1].set (Biquad::Type::lowpass, lp, 0.9, 0.0, ctx.sampleRate);
                }
            }

            std::array<DelayLine, 2> lines;
            std::array<std::array<Biquad, 2>, 2> filt;
            std::array<Crackle, 2> crackle;
            std::array<Noise, 2> white;
            std::array<OnePole, 2> surface;
            Lfo wow, flutter;
            Smoother range, wf, noiseLv;
        };

        // ---- RADIO TUNING -------------------------------------------------------------
        class RadioTuning final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                band[0].set (Biquad::Type::highpass, 380.0, 0.8, 0.0, sr);
                band[1].set (Biquad::Type::lowpass, 3000.0, 0.9, 0.0, sr);
                band[2].set (Biquad::Type::peak, 1500.0, 1.0, 4.0, sr);
                staticBand.set (Biquad::Type::bandpass, 2500.0, 0.6, 0.0, sr);
                crackle.prepare (sr, 0xFACEu);
                det.prepare (sr, 60.0f);
                nz.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& b : band) b.reset();
                staticBand.reset();
                crackle.reset();
                drift.reset();
                fade.reset (0.3f);
                whistlePhase = 0.0f;
                det.reset (params[0]);
                nz.reset (params[1]);
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // A mono AM receiver: narrow band, slight overdrive. DETUNE drifts the
                // tuning: heterodyne whistle, selective fading, and rising static.
                det.set (params[0]);
                nz.set (params[1]);
                bal.set (params[2]);
                drift.setRate (0.13f, ctx.sampleRate);
                fade.setRate (0.37f, ctx.sampleRate);
                for (int i = 0; i < n; ++i)
                {
                    const float d = det.next();
                    const float noiseAmt = nz.next();
                    const float dr = Lfo::sine (drift.advance());
                    const float whistleHz = 400.0f + 2600.0f * d * (0.6f + 0.4f * dr);
                    whistlePhase += whistleHz / (float) ctx.sampleRate;
                    whistlePhase -= std::floor (whistlePhase);

                    float x = 0.5f * (l[i] + r[i]);
                    for (auto& b : band) x = b.process (x);
                    x = std::tanh (x * 1.6f) / 1.6f;
                    const float fading = 1.0f - d * 0.6f * (0.5f + 0.5f * Lfo::sine (fade.advance()));
                    const float stat = staticBand.process (white.next()) * (0.15f * noiseAmt + 0.2f * d);
                    const float pop = crackle.next (noiseAmt * 0.7f) * 0.3f * noiseAmt;
                    const float wet = x * fading + stat + pop + Lfo::sine (whistlePhase) * 0.08f * d * d;

                    float dry, wg;
                    balanceGains (bal.next(), dry, wg);
                    l[i] = dry * l[i] + wg * wet;
                    r[i] = dry * r[i] + wg * wet;
                }
            }

        private:
            std::array<Biquad, 3> band;
            Biquad staticBand;
            Crackle crackle;
            Noise white { 0x7777u };
            Lfo drift, fade;
            float whistlePhase = 0.0f;
            Smoother det, nz, bal;
        };

        // ---- C.CANCELER ---------------------------------------------------------------
        class CCanceler final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                bass.set (Biquad::Type::lowpass, 180.0, 0.707, 0.0, sr);
                for (auto& h : highShelf) h.reset();
                k.prepare (sr, 30.0f);
                lo.prepare (sr, 30.0f);
                hi.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                bass.reset();
                for (auto& h : highShelf) h.reset();
                k.reset (balance());
                lo.reset (params[1]);
                hi.reset (params[2]);
                updateShelf (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // Centre cancellation: L - k*R removes what is equal in both channels;
                // L-R BALANCE moves the cancellation point for off-centre mixes. LOW BOOST
                // restores the centre's bass (kick, bass line), HIGH BOOST lifts the top.
                k.set (balance());
                lo.set (params[1]);
                hi.set (params[2]);
                updateShelf (hi.advance (n));
                for (int i = 0; i < n; ++i)
                {
                    const float kk = k.next();
                    const float low = lo.next();
                    const float mid = 0.5f * (l[i] + r[i]);
                    const float b = bass.process (mid) * low * 2.0f;
                    const float sl = l[i] - kk * r[i];
                    const float sr = r[i] - (2.0f - kk) * l[i];
                    l[i] = highShelf[0].process (0.5f * sl + b);
                    r[i] = highShelf[1].process (0.5f * sr + b);
                }
            }

        private:
            float balance() const { return 2.0f * params[0]; }   // 0..2, 1 = exact centre
            void updateShelf (float v)
            {
                for (auto& h : highShelf) h.set (Biquad::Type::highShelf, 4000.0, 0.707, v * 12.0, ctx.sampleRate);
            }
            Biquad bass;
            std::array<Biquad, 2> highShelf;
            Smoother k, lo, hi;
        };

        // ---- SUBSONIC -----------------------------------------------------------------
        class Subsonic final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                follower.set (1.0f, 60.0f, sr);
                gateAttack = std::exp (-1.0f / (0.002f * (float) sr));
                gateRelease = std::exp (-1.0f / (0.15f * (float) sr));
                pitch.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                follower.reset();
                phase = 0.0f;
                amp = 0.0f;
                open = false;
                pitch.reset (params[0]);
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // A sine tone keyed by the input level: when the input crosses THRESHOLD
                // the tone restarts from zero phase (punchy, no click) and follows the
                // input's envelope, reinforcing kicks (p. 44 *3).
                const float thrGain = dbToGain (-60.0f + 60.0f * params[1]);
                pitch.set (params[0]);
                bal.set (params[2]);
                for (int i = 0; i < n; ++i)
                {
                    const float env = follower.process (0.5f * (l[i] + r[i]));
                    const bool above = env > thrGain;
                    if (above && ! open)
                        phase = 0.0f;
                    open = above;
                    const float target = above ? 1.0f : 0.0f;
                    amp = target + (target > amp ? gateAttack : gateRelease) * (amp - target);
                    phase += expMap (pitch.next(), 25.0f, 120.0f) / (float) ctx.sampleRate;
                    phase -= std::floor (phase);
                    const float sub = std::sin (kTwoPi * phase) * amp * 0.8f;
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    l[i] = dry * l[i] + wet * sub;
                    r[i] = dry * r[i] + wet * sub;
                }
            }

        private:
            EnvelopeFollower follower;
            float gateAttack = 0.0f, gateRelease = 0.0f, phase = 0.0f, amp = 0.0f;
            bool open = false;
            Smoother pitch, bal;
        };

        // ---- RING MOD -----------------------------------------------------------------
        class RingMod final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                follower.set (5.0f, 80.0f, sr);
                freq.prepare (sr, 30.0f);
                sens.prepare (sr, 30.0f);
                bal.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                follower.reset();
                phase = 0.0f;
                freq.reset (std::log (baseHz()));
                sens.reset (sensitivity());
                bal.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // FREQUENCY sets the carrier; SENS lets the input envelope push the carrier
                // up (both starred: Effect Limit narrows them).
                freq.set (std::log (baseHz()));
                sens.set (sensitivity());
                bal.set (params[2]);
                for (int i = 0; i < n; ++i)
                {
                    const float env = std::min (1.0f, follower.process (0.5f * (l[i] + r[i])) * 4.0f);
                    const float hz = std::exp (freq.next()) * (1.0f + 3.0f * sens.next() * env);
                    phase += hz / (float) ctx.sampleRate;
                    phase -= std::floor (phase);
                    const float c = std::sin (kTwoPi * phase);
                    float dry, wet;
                    balanceGains (bal.next(), dry, wet);
                    l[i] = dry * l[i] + wet * l[i] * c;
                    r[i] = dry * r[i] + wet * r[i] * c;
                }
            }

        private:
            float baseHz() const { return expMap (params[0], 30.0f, ctx.limit ? 1000.0f : 3000.0f); }
            float sensitivity() const { return params[1] * (ctx.limit ? 0.4f : 1.0f); }
            EnvelopeFollower follower;
            float phase = 0.0f;
            Smoother freq, sens, bal;
        };
    }

    std::unique_ptr<Effect> createNoiseGen()    { return std::make_unique<NoiseGen>(); }
    std::unique_ptr<Effect> createVinylSim()    { return std::make_unique<VinylSim>(); }
    std::unique_ptr<Effect> createRadioTuning() { return std::make_unique<RadioTuning>(); }
    std::unique_ptr<Effect> createCCanceler()   { return std::make_unique<CCanceler>(); }
    std::unique_ptr<Effect> createSubsonic()    { return std::make_unique<Subsonic>(); }
    std::unique_ptr<Effect> createRingMod()     { return std::make_unique<RingMod>(); }
}
