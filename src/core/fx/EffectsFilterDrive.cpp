// Filter, EQ, drive and dynamics effects. Nonlinear stages run 4x oversampled
// (JUCE polyphase IIR half-band) so drive does not fold harmonics back as aliasing.
// BIT CRASH is the deliberate exception: its aliasing is the effect (FIDELITY.md).

#include "Effects.h"
#include "DspUtil.h"
#include <juce_dsp/juce_dsp.h>

namespace spsx::fx
{
    using namespace dsp;

    namespace
    {
        /** Runs a per-sample function at 4x the rate on a stereo block. */
        class Oversampled
        {
        public:
            void prepare (int maxBlock)
            {
                os.initProcessing ((size_t) maxBlock);
                os.reset();
            }
            void reset() { os.reset(); }

            /** fn (channel, sample, t) where t runs 0..1 across the block (for ramps). */
            template <typename Fn>
            void process (float* l, float* r, int n, Fn&& fn)
            {
                float* ch[2] = { l, r };
                juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
                auto up = os.processSamplesUp (block);
                const int m = (int) up.getNumSamples();
                const float inv = 1.0f / (float) m;
                for (size_t c = 0; c < 2; ++c)
                {
                    float* x = up.getChannelPointer ((size_t) c);
                    for (int i = 0; i < m; ++i)
                        x[i] = fn ((int) c, x[i], (float) i * inv);
                }
                os.processSamplesDown (block);
            }

        private:
            juce::dsp::Oversampling<float> os { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
        };

        // ---- FILTER+DRIVE -------------------------------------------------------------
        class FilterDrive final : public Effect
        {
        public:
            void prepare (double sr, int maxBlock) override
            {
                ctx.sampleRate = sr;
                os.prepare (maxBlock);
                logCut.prepare (sr, 25.0f);
                q.prepare (sr, 30.0f);
                drive.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                os.reset();
                for (auto& f : svf) f.reset();
                logCut.reset (std::log (cutoffHz()));
                q.reset (resonanceQ());
                drive.reset (driveGain());
                lastDrive = drive.value();
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // DRIVE: tanh saturation before the filter, gain-compensated so turning it
                // up adds grit rather than only loudness.
                drive.set (driveGain());
                const float g0 = lastDrive, g1 = drive.advance (n);
                lastDrive = g1;
                os.process (l, r, n, [g0, g1] (int, float x, float t)
                {
                    const float g = g0 + (g1 - g0) * t;
                    return std::tanh (g * x) / (0.6f + 0.4f * std::sqrt (g));
                });

                logCut.set (std::log (cutoffHz()));
                q.set (resonanceQ());
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float hz = std::exp (logCut.next());
                    const float qq = q.next();
                    for (size_t c = 0; c < 2; ++c)
                    {
                        svf[c].setCoefficients (hz, qq, ctx.sampleRate);
                        ch[c][i] = svf[c].process (ch[c][i]).lp;
                    }
                }
            }

        private:
            float cutoffHz() const { return expMap (params[0], 40.0f, 18000.0f); }
            // RESONANCE is starred: Effect Limit keeps it well below self-oscillation.
            float resonanceQ() const { return 0.5f * std::pow (ctx.limit ? 8.0f : 36.0f, params[1]); }
            float driveGain() const { return dbToGain (params[2] * (ctx.limit ? 18.0f : 36.0f)); }

            Oversampled os;
            std::array<Svf, 2> svf;
            Smoother logCut, q, drive;
            float lastDrive = 1.0f;
        };

        // ---- ISOLATOR -----------------------------------------------------------------
        class Isolator final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& x : lowX)  x.set (300.0, sr);
                for (auto& x : highX) x.set (3000.0, sr);
                for (auto& g : gains) g.prepare (sr, 15.0f);
                reset();
            }
            void reset() override
            {
                for (auto& x : lowX) x.reset();
                for (auto& x : highX) x.reset();
                for (int b = 0; b < 3; ++b) gains[(size_t) b].reset (bandGain (params[(size_t) b]));
            }

            void process (float* l, float* r, int n) noexcept override
            {
                for (int b = 0; b < 3; ++b) gains[(size_t) b].set (bandGain (params[(size_t) b]));
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float gl = gains[0].next(), gm = gains[1].next(), gh = gains[2].next();
                    for (size_t c = 0; c < 2; ++c)
                    {
                        // low + mid + high == input exactly when all gains are 1.
                        const float x = ch[c][i];
                        const float low = lowX[c].low (x);
                        const float rest = x - low;
                        const float high = highX[c].high (rest);
                        const float mid = rest - high;
                        ch[c][i] = gl * low + gm * mid + gh * high;
                    }
                }
            }

        private:
            /** Left of 12 o'clock fades the band out completely; right boosts it. All three
                are starred: Effect Limit caps the boost at +3 dB instead of +6 dB. */
            float bandGain (float v) const
            {
                if (v < 0.5f) { const float t = v / 0.5f; return t * t; }
                const float maxBoost = ctx.limit ? 1.4125f : 1.9953f;
                return 1.0f + (v - 0.5f) * 2.0f * (maxBoost - 1.0f);
            }

            std::array<LR4, 2> lowX, highX;
            std::array<Smoother, 3> gains;
        };

        // ---- EQUALIZER ----------------------------------------------------------------
        class Equalizer final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& g : gainDb) g.prepare (sr, 20.0f);
                reset();
            }
            void reset() override
            {
                for (auto& ch : bands) for (auto& b : ch) b.reset();
                for (int b = 0; b < 3; ++b) gainDb[(size_t) b].reset (bandDb (params[(size_t) b]));
                update();
            }

            void process (float* l, float* r, int n) noexcept override
            {
                for (int b = 0; b < 3; ++b) gainDb[(size_t) b].set (bandDb (params[(size_t) b]));
                for (auto& g : gainDb) g.advance (n);
                update();
                float* ch[2] = { l, r };
                for (size_t c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        float x = ch[c][i];
                        for (auto& b : bands[(size_t) c]) x = b.process (x);
                        ch[c][i] = x;
                    }
            }

        private:
            // Starred: Effect Limit reduces the range from +-15 dB to +-6 dB.
            float bandDb (float v) const { return (v - 0.5f) * 2.0f * (ctx.limit ? 6.0f : 15.0f); }
            void update()
            {
                for (auto& ch : bands)
                {
                    ch[0].set (Biquad::Type::lowShelf, 200.0, 0.707, gainDb[0].value(), ctx.sampleRate);
                    ch[1].set (Biquad::Type::peak, 1200.0, 0.8, gainDb[1].value(), ctx.sampleRate);
                    ch[2].set (Biquad::Type::highShelf, 5000.0, 0.707, gainDb[2].value(), ctx.sampleRate);
                }
            }
            std::array<std::array<Biquad, 3>, 2> bands;
            std::array<Smoother, 3> gainDb;
        };

        // ---- WAH ----------------------------------------------------------------------
        class Wah final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                manual.prepare (sr, 30.0f);
                q.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                for (auto& f : svf) f.reset();
                lfo.reset();
                manual.reset (params[2]);
                q.reset (peakQ());
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // RATE at the far left stops the sweep: MANUAL then works as a fixed wah.
                const float rateHz = params[1] < 0.02f ? 0.0f : expMap (params[1], 0.1f, 8.0f);
                lfo.setRate (rateHz, ctx.sampleRate);
                manual.set (params[2]);
                q.set (peakQ());
                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const float sweep = rateHz > 0.0f ? 0.5f + 0.5f * Lfo::triangle (lfo.advance()) : 0.0f;
                    const float pos = std::clamp (manual.next() + sweep * 0.55f, 0.0f, 1.0f);
                    const float hz = expMap (pos, 280.0f, 2800.0f);
                    const float qq = q.next();
                    for (size_t c = 0; c < 2; ++c)
                    {
                        svf[c].setCoefficients (hz, qq, ctx.sampleRate);
                        ch[c][i] = svf[c].bandpassNorm (ch[c][i]) * 1.2f;
                    }
                }
            }

        private:
            float peakQ() const { return expMap (params[0], 1.0f, 12.0f); }
            std::array<Svf, 2> svf;
            Lfo lfo;
            Smoother manual, q;
        };

        // ---- DISTORTION / OVERDRIVE / FUZZ --------------------------------------------
        class Drive final : public Effect
        {
        public:
            enum class Kind { distortion, overdrive, fuzz };
            explicit Drive (Kind k) : kind (k) {}

            void prepare (double sr, int maxBlock) override
            {
                ctx.sampleRate = sr;
                os.prepare (maxBlock);
                gain.prepare (sr, 30.0f);
                tone.prepare (sr, 30.0f);
                level.prepare (sr, 20.0f);
                for (auto& d : dc) d.prepare (sr);
                for (auto& h : pre) h.setCutoff (kind == Kind::fuzz ? 80.0f : 40.0f, sr);
                reset();
            }
            void reset() override
            {
                os.reset();
                for (auto& d : dc) d.reset();
                for (auto& h : pre) h.reset();
                for (auto& t : post) for (auto& p : t) p.reset();
                gain.reset (driveGain());
                tone.reset (toneHz());
                level.reset (outLevel());
                lastGain = gain.value();
            }

            void process (float* l, float* r, int n) noexcept override
            {
                float* ch[2] = { l, r };
                for (size_t c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                        ch[c][i] = pre[c].highpass (ch[c][i]);

                gain.set (driveGain());
                const float g0 = lastGain, g1 = gain.advance (n);
                lastGain = g1;
                const Kind k = kind;
                os.process (l, r, n, [g0, g1, k] (int, float x, float t)
                {
                    const float v = x * (g0 + (g1 - g0) * t);
                    switch (k)
                    {
                        case Kind::overdrive:
                            // Asymmetric soft clip: even harmonics, gentle onset.
                            return (std::tanh (v + 0.25f) - std::tanh (0.25f)) * 1.15f;
                        case Kind::distortion:
                        {
                            // Hard knee (order-6 soft clip): strong odd harmonics, tight.
                            const float a = std::abs (v);
                            return v / std::pow (1.0f + a * a * a * a * a * a, 1.0f / 6.0f);
                        }
                        case Kind::fuzz:
                        {
                            // Partial rectification (octave-up fizz) into a hard clip. The
                            // rectifier's DC is removed by the output DC blocker.
                            const float w = v + 0.6f * std::abs (v);
                            return std::clamp (w, -1.0f, 1.0f);
                        }
                    }
                    return v;
                });

                tone.set (toneHz());
                level.set (outLevel());
                const float hz = tone.advance (n);
                for (auto& pc : post) for (auto& p : pc) p.setCutoff (hz, ctx.sampleRate);
                for (int i = 0; i < n; ++i)
                {
                    const float lv = level.next();
                    for (size_t c = 0; c < 2; ++c)
                    {
                        float y = post[c][1].lowpass (post[c][0].lowpass (ch[c][i]));
                        ch[c][i] = dc[c].process (y) * lv;
                    }
                }
            }

        private:
            // All three parameters are starred. Effect Limit lowers the drive ceiling,
            // keeps TONE out of the harshest top end and caps LEVEL at unity.
            float driveGain() const
            {
                switch (kind)
                {
                    case Kind::overdrive:  return dbToGain (params[0] * (ctx.limit ? 18.0f : 30.0f));
                    case Kind::distortion: return dbToGain (6.0f + params[0] * (ctx.limit ? 24.0f : 42.0f));
                    case Kind::fuzz:       return dbToGain (18.0f + params[0] * (ctx.limit ? 22.0f : 40.0f));
                }
                return 1.0f;
            }
            float toneHz() const { return expMap (params[1], 700.0f, ctx.limit ? 7000.0f : 14000.0f); }
            float outLevel() const { return dbToGain (std::min (-30.0f + 36.0f * params[2], ctx.limit ? 0.0f : 6.0f)); }

            Kind kind;
            Oversampled os;
            Smoother gain, tone, level;
            float lastGain = 1.0f;
            std::array<OnePole, 2> pre;
            std::array<std::array<OnePole, 2>, 2> post;
            std::array<DcBlocker, 2> dc;
        };

        // ---- COMP ---------------------------------------------------------------------
        class Comp final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                makeup.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                envDb = -120.0f;
                makeup.reset (levelGain());
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // SUSTAIN lowers the threshold and raises the ratio together, like a
                // pedal-style sustainer. CTRL 2 is ATTACK, or RELEASE with Effect Limit on
                // (p. 43). Stereo-linked peak detection in the log domain.
                const float thr = -6.0f - 42.0f * params[0];
                const float ratio = 2.0f + 10.0f * params[0];
                const float knee = 6.0f;
                const float attackMs = ctx.limit ? 3.0f : expMap (params[1], 0.1f, 80.0f);
                const float releaseMs = ctx.limit ? expMap (params[1], 20.0f, 1000.0f) : 150.0f;
                const float aC = std::exp (-1.0f / (0.001f * attackMs * (float) ctx.sampleRate));
                const float rC = std::exp (-1.0f / (0.001f * releaseMs * (float) ctx.sampleRate));
                // Automatic partial make-up (about a third of the static reduction at full
                // scale) so SUSTAIN does not just make it quieter, without letting
                // transients the detector misses come out tens of dB hot.
                const float autoMakeup = -0.3f * (thr * (1.0f - 1.0f / ratio));
                makeup.set (levelGain());

                for (int i = 0; i < n; ++i)
                {
                    const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
                    const float inDb = 20.0f * std::log10 (peak + 1e-9f);
                    envDb = inDb + (inDb > envDb ? aC : rC) * (envDb - inDb);

                    const float over = envDb - thr;
                    float grDb = 0.0f;
                    if (over > knee * 0.5f)       grDb = over * (1.0f / ratio - 1.0f);
                    else if (over > -knee * 0.5f) { const float o = over + knee * 0.5f; grDb = (1.0f / ratio - 1.0f) * o * o / (2.0f * knee); }

                    const float g = dbToGain (grDb + autoMakeup) * makeup.next();
                    l[i] *= g;
                    r[i] *= g;
                }
            }

        private:
            // LEVEL is starred: Effect Limit caps it at 0 dB.
            float levelGain() const { return dbToGain (std::min (-12.0f + 18.0f * params[2], ctx.limit ? 0.0f : 6.0f)); }
            float envDb = -120.0f;
            Smoother makeup;
        };

        // ---- BIT CRASH ----------------------------------------------------------------
        class BitCrash final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& d : dc) d.prepare (sr);
                cutoff.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                phase = 1.0f;
                held = { 0.0f, 0.0f };
                for (auto& d : dc) d.reset();
                for (auto& f : lp) for (auto& p : f) p.reset();
                cutoff.reset (filterHz());
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // Sample-and-hold decimation with no anti-alias filter (the aliasing is the
                // point), quantisation without dither, then the FILTER knob closes a
                // post low-pass. BIT is starred: Effect Limit stops at 4 bits.
                const float targetRate = expMap (params[0], 400.0f, (float) ctx.sampleRate);
                const float inc = targetRate / (float) ctx.sampleRate;
                const float bits = linMap (params[1], ctx.limit ? 4.0f : 1.0f, 16.0f);
                const float steps = std::pow (2.0f, bits - 1.0f);
                cutoff.set (filterHz());
                const float hz = cutoff.advance (n);
                for (auto& f : lp) for (auto& p : f) p.setCutoff (hz, ctx.sampleRate);

                float* ch[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    phase += inc;
                    const bool sampleNow = phase >= 1.0f;
                    if (sampleNow) phase -= std::floor (phase);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        if (sampleNow)
                        {
                            const float x = std::clamp (ch[c][i], -1.0f, 1.0f);
                            held[(size_t) c] = bits >= 15.99f ? x : std::round (x * steps) / steps;
                        }
                        float y = lp[c][1].lowpass (lp[c][0].lowpass (held[(size_t) c]));
                        ch[c][i] = dc[c].process (y);
                    }
                }
            }

        private:
            float filterHz() const { return expMap (1.0f - params[2], 300.0f, 20000.0f); }
            float phase = 1.0f;
            std::array<float, 2> held {};
            std::array<std::array<OnePole, 2>, 2> lp;
            std::array<DcBlocker, 2> dc;
            Smoother cutoff;
        };
    }

    std::unique_ptr<Effect> createFilterDrive() { return std::make_unique<FilterDrive>(); }
    std::unique_ptr<Effect> createIsolator()    { return std::make_unique<Isolator>(); }
    std::unique_ptr<Effect> createEqualizer()   { return std::make_unique<Equalizer>(); }
    std::unique_ptr<Effect> createWah()         { return std::make_unique<Wah>(); }
    std::unique_ptr<Effect> createDistortion()  { return std::make_unique<Drive> (Drive::Kind::distortion); }
    std::unique_ptr<Effect> createOverdrive()   { return std::make_unique<Drive> (Drive::Kind::overdrive); }
    std::unique_ptr<Effect> createFuzz()        { return std::make_unique<Drive> (Drive::Kind::fuzz); }
    std::unique_ptr<Effect> createComp()        { return std::make_unique<Comp>(); }
    std::unique_ptr<Effect> createBitCrash()    { return std::make_unique<BitCrash>(); }
}
