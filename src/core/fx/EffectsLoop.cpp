// Looping and slicing effects.

#include "Effects.h"
#include "EffectInfo.h"
#include "DspUtil.h"

namespace spsx::fx
{
    using namespace dsp;

    namespace
    {
        /** Stereo capture buffer with a frozen loop region and fractional, wrapping reads.
            Loop edges get a short fade so the wrap never clicks. */
        class LoopBuffer
        {
        public:
            void prepare (int capacitySamples)
            {
                for (auto& b : buf) b.assign ((size_t) capacitySamples, 0.0f);
                cap = capacitySamples;
            }
            void reset()
            {
                for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
                write = 0;
            }
            void push (float l, float r) noexcept
            {
                buf[0][(size_t) write] = l;
                buf[1][(size_t) write] = r;
                write = (write + 1) % cap;
            }
            /** Freeze the last `length` samples as the loop. */
            void capture (int length) noexcept
            {
                loopLen = std::clamp (length, 16, cap - 1);
                loopStart = (write - loopLen + cap) % cap;
            }
            void setLength (int length) noexcept { loopLen = std::clamp (length, 16, cap - 1); }
            int length() const noexcept { return loopLen; }

            /** Read at a position inside the loop (wrapped), with edge fades. */
            float read (int ch, double pos) const noexcept
            {
                pos = std::fmod (pos, (double) loopLen);
                if (pos < 0.0) pos += loopLen;
                const auto i0 = (int) pos;
                const float f = (float) (pos - i0);
                const float a = buf[(size_t) ch][(size_t) ((loopStart + i0) % cap)];
                const float b = buf[(size_t) ch][(size_t) ((loopStart + (i0 + 1) % loopLen) % cap)];
                const float edge = (float) std::min (pos, (double) loopLen - pos);
                const float fade = std::min (1.0f, edge / 48.0f);
                return (a + f * (b - a)) * fade;
            }

        private:
            std::array<std::vector<float>, 2> buf;
            int cap = 1, write = 0, loopStart = 0, loopLen = 16;
        };

        // ---- DJFX LOOPER --------------------------------------------------------------
        class DjfxLooper final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                loop.prepare ((int) (2.2 * sr));
                // Turntable inertia: speed changes glide like a platter, ~120 ms.
                speed.prepare (sr, 120.0f);
                engage.prepare (sr, 3.0f);
                reset();
            }
            void reset() override
            {
                loop.reset();
                looping = false;
                pos = 0.0;
                speed.reset (djfxSpeed (params[1]));
                engage.reset (0.0f);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // LOOP SW on: the sound at that moment is looped. LENGTH (starred: Effect
                // Limit keeps loops above ~60 ms, avoiding buzzy loops) sets the loop,
                // SPEED plays it forward/backward at -1..1 with stop at 12 o'clock.
                const bool on = onOff (params[2]);
                const int len = (int) (expMap (params[0], ctx.limit ? 0.06f : 0.015f, 1.0f) * (float) ctx.sampleRate);
                if (on && ! looping)
                {
                    loop.capture (len);
                    pos = 0.0;
                }
                else if (on)
                    loop.setLength (len);
                looping = on;
                speed.set (djfxSpeed (params[1]));
                engage.set (on ? 1.0f : 0.0f);

                for (int i = 0; i < n; ++i)
                {
                    const float e = engage.next();
                    if (! looping && e < 1e-4f)
                    {
                        loop.push (l[i], r[i]);
                        continue;
                    }
                    if (! looping)
                        loop.push (l[i], r[i]);
                    const float s = speed.next();
                    // A stopped platter is silent; fade with speed near zero.
                    const float g = std::min (1.0f, std::abs (s) * 6.0f);
                    const float wl = loop.read (0, pos) * g, wr = loop.read (1, pos) * g;
                    pos += s;
                    l[i] = (1.0f - e) * l[i] + e * wl;
                    r[i] = (1.0f - e) * r[i] + e * wr;
                }
                pos = std::fmod (pos, (double) loop.length());
            }

        private:
            LoopBuffer loop;
            bool looping = false;
            double pos = 0.0;
            Smoother speed, engage;
        };

        // ---- BPM LOOPER ---------------------------------------------------------------
        class BpmLooper final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                loop.prepare ((int) (8.0 * sr));   // a whole note at 40 BPM is 6 s
                engage.prepare (sr, 3.0f);
                reset();
            }
            void reset() override
            {
                loop.reset();
                looping = false;
                autoActive = false;
                pos = 0.0;
                clock = 0.0;
                engage.reset (0.0f);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // LENGTH is a note value at the reference tempo (starred: Effect Limit stops
                // at a 16th). LOOP SW captures now. TIMING N re-captures and starts the loop
                // automatically every N eighth notes (SPEC U8).
                const int minIndex = ctx.limit ? 1 : 0;
                const int idx = std::max (minIndex, noteIndex (params[0], ctx.tempo));
                const double beat = 60.0 / std::max (ctx.tempo, 1.0) * ctx.sampleRate;
                const int len = (int) (noteBeats()[(size_t) idx] * beat);
                const int timing = timing8 (params[1]);
                const bool sw = onOff (params[2]);
                const double period = timing > 0 ? timing * 0.5 * beat : 0.0;

                for (int i = 0; i < n; ++i)
                {
                    bool capture = false;
                    if (sw && ! looping) capture = true;
                    if (! sw && timing > 0)
                    {
                        clock += 1.0;
                        if (clock >= period)
                        {
                            clock -= period;
                            capture = true;
                            autoActive = true;
                        }
                    }
                    if (! sw && timing == 0)
                        autoActive = false;

                    const bool active = sw || autoActive;
                    if (capture)
                    {
                        loop.capture (len);
                        pos = 0.0;
                    }
                    looping = active;
                    if (! active)
                        loop.push (l[i], r[i]);

                    engage.set (active ? 1.0f : 0.0f);
                    const float e = engage.next();
                    if (e > 1e-4f)
                    {
                        const float wl = loop.read (0, pos), wr = loop.read (1, pos);
                        pos += 1.0;
                        l[i] = (1.0f - e) * l[i] + e * wl;
                        r[i] = (1.0f - e) * r[i] + e * wr;
                    }
                }
                if (looping)
                    loop.setLength (len);
            }

        private:
            LoopBuffer loop;
            bool looping = false, autoActive = false;
            double pos = 0.0, clock = 0.0;
            Smoother engage;
        };

        // ---- SLICER+FLG ---------------------------------------------------------------
        class SlicerFlanger final : public Effect
        {
        public:
            void prepare (double sr, int) override
            {
                ctx.sampleRate = sr;
                for (auto& d : lines) d.prepare ((int) (0.02 * sr));
                gateRise = 1.0f / (0.0015f * (float) sr);
                fb.prepare (sr, 30.0f);
                reset();
            }
            void reset() override
            {
                for (auto& d : lines) d.reset();
                last = { 0.0f, 0.0f };
                stepPhase = 0.0;
                gate = 0.0f;
                lastStep = -1;
                lfo.reset();
                fb.reset (params[2]);
            }

            void process (float* l, float* r, int n) noexcept override
            {
                // One cycle of the TIMING PTN takes RATE (a note value, 32nd to one bar) at
                // the reference tempo (*2). Each pattern is 32 steps; an onset after a
                // sounding step gets a 1 ms dip so repeated slices articulate.
                const auto& pattern = slicerPatterns()[(size_t) slicerPattern (params[0])];
                const double cycleBeats = noteBeats()[(size_t) noteIndex (params[1], ctx.tempo)];
                const double samplesPerStep = cycleBeats / 32.0 * 60.0 / std::max (ctx.tempo, 1.0) * ctx.sampleRate;
                fb.set (params[2]);
                lfo.setRate (0.35f, ctx.sampleRate);
                const float sr = (float) ctx.sampleRate;
                const float dipLen = 0.001f * sr;

                float* io[2] = { l, r };
                for (int i = 0; i < n; ++i)
                {
                    const int step = (int) stepPhase % 32;
                    const double within = (stepPhase - std::floor (stepPhase)) * samplesPerStep;
                    const char cell = pattern[step];
                    bool open = cell != '.';
                    if (cell == 'X' && within < dipLen && step != lastStep && pattern[(step + 31) % 32] != '.')
                        open = false;
                    stepPhase += 1.0 / samplesPerStep;
                    if (stepPhase >= 32.0) stepPhase -= 32.0;
                    lastStep = within >= dipLen ? step : lastStep;

                    const float target = open ? 1.0f : 0.0f;
                    gate += std::clamp (target - gate, -gateRise, gateRise);

                    // FEEDBACK (starred) is the flanger depth: mix and regeneration rise together.
                    const float depth = fb.next();
                    const float mix = std::min (1.0f, depth * 3.0f) * 0.5f;
                    const float regen = depth * (ctx.limit ? 0.6f : 0.9f);
                    const float m = 0.5f + 0.5f * Lfo::triangle (lfo.advance());
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float x = io[c][i] * gate;
                        lines[c].push (x + regen * last[(size_t) c]);
                        const float y = lines[c].read ((0.0008f + 0.003f * (c == 0 ? m : 1.0f - m)) * sr);
                        // Flush the decaying feedback tail before it turns denormal.
                        last[(size_t) c] = std::abs (y) < 1e-15f ? 0.0f : y;
                        io[c][i] = (1.0f - mix) * x + mix * y;
                    }
                }
            }

        private:
            std::array<DelayLine, 2> lines;
            std::array<float, 2> last {};
            double stepPhase = 0.0;
            float gate = 0.0f, gateRise = 0.01f;
            int lastStep = -1;
            Lfo lfo;
            Smoother fb;
        };
    }

    std::unique_ptr<Effect> createDjfxLooper()    { return std::make_unique<DjfxLooper>(); }
    std::unique_ptr<Effect> createBpmLooper()     { return std::make_unique<BpmLooper>(); }
    std::unique_ptr<Effect> createSlicerFlanger() { return std::make_unique<SlicerFlanger>(); }
}
