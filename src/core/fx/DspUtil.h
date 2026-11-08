#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/** Small, allocation-free DSP building blocks shared by the effects. All of them are
    prepared (and may allocate) in prepare(); processing never allocates. */
namespace spsx::fx::dsp
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    /** Zero filter states that have decayed below audibility before they turn denormal.
        The engine also sets flush-to-zero, but effects must be safe on their own (a host
        or test may call them without it). */
    inline float flushTiny (float x) noexcept { return std::abs (x) < 1e-20f ? 0.0f : x; }

    /** Exponential map 0..1 -> lo..hi (for frequencies, times). */
    inline float expMap (float v, float lo, float hi) noexcept { return lo * std::pow (hi / lo, std::clamp (v, 0.0f, 1.0f)); }
    inline float linMap (float v, float lo, float hi) noexcept { return lo + (hi - lo) * std::clamp (v, 0.0f, 1.0f); }

    /** BALANCE convention for every effect: both at full level at 12 o'clock, fully dry
        at the left end, fully wet at the right end. */
    inline void balanceGains (float b, float& dry, float& wet) noexcept
    {
        b = std::clamp (b, 0.0f, 1.0f);
        dry = std::min (1.0f, 2.0f * (1.0f - b));
        wet = std::min (1.0f, 2.0f * b);
    }

    /** One-pole parameter smoother (about `ms` to settle). */
    class Smoother
    {
    public:
        void prepare (double sampleRate, float ms) noexcept
        {
            coeff = (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
        }
        void reset (float v) noexcept { current = target = v; }
        void set (float v) noexcept { target = v; }
        float next() noexcept { current = target + coeff * (current - target); return current; }
        /** Advance n samples at once (block-rate parameters). */
        float advance (int n) noexcept { current = target + std::pow (coeff, (float) n) * (current - target); return current; }
        float value() const noexcept { return current; }
        float targetValue() const noexcept { return target; }

    private:
        float coeff = 0.0f, current = 0.0f, target = 0.0f;
    };

    /** xorshift white noise in [-1, 1). Deterministic per seed. */
    class Noise
    {
    public:
        explicit Noise (std::uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}
        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
        }
        float uniform01() noexcept { return 0.5f * (next() + 1.0f); }

    private:
        std::uint32_t state;
    };

    /** Topology-preserving-transform one-pole (low-pass and high-pass outputs). */
    class OnePole
    {
    public:
        void setCutoff (float hz, double sampleRate) noexcept
        {
            const float g = std::tan (kPi * std::min (hz, (float) sampleRate * 0.49f) / (float) sampleRate);
            G = g / (1.0f + g);
        }
        void reset() noexcept { s = 0.0f; }
        float lowpass (float x) noexcept
        {
            const float v = (x - s) * G;
            const float y = v + s;
            s = flushTiny (y + v);
            return y;
        }
        float highpass (float x) noexcept { return x - lowpass (x); }

    private:
        float G = 0.0f, s = 0.0f;
    };

    /** Zero-delay-feedback state-variable filter (Simper/Cytomic), stable under fast
        modulation and at high resonance. One instance per channel. */
    class Svf
    {
    public:
        struct Out { float lp, bp, hp; };

        void setCoefficients (float hz, float q, double sampleRate) noexcept
        {
            hz = std::clamp (hz, 5.0f, (float) sampleRate * 0.49f);
            g = std::tan (kPi * hz / (float) sampleRate);
            k = 1.0f / std::max (q, 0.05f);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        Out process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flushTiny (2.0f * v1 - ic1);
            ic2 = flushTiny (2.0f * v2 - ic2);
            return { v2, v1, x - k * v1 - v2 };
        }
        /** Band-pass normalised to 0 dB at the centre. */
        float bandpassNorm (float x) noexcept { return process (x).bp * k; }

    private:
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    };

    /** RBJ-cookbook biquad, transposed direct form II. */
    class Biquad
    {
    public:
        enum class Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };

        void set (Type type, double hz, double q, double gainDb, double sampleRate) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * 3.141592653589793 * std::clamp (hz, 5.0, sampleRate * 0.49) / sampleRate;
            const double cw = std::cos (w), sw = std::sin (w);
            const double alpha = sw / (2.0 * std::max (q, 0.05));
            double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

            switch (type)
            {
                case Type::lowpass:  b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::highpass: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::bandpass: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::peak:
                    b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                    a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                    break;
                case Type::lowShelf:
                {
                    const double s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                    a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
                    break;
                }
                case Type::highShelf:
                {
                    const double s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                    a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }
            c0 = (float) (b0 / a0); c1 = (float) (b1 / a0); c2 = (float) (b2 / a0);
            d1 = (float) (a1 / a0); d2 = (float) (a2 / a0);
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = c0 * x + z1;
            z1 = flushTiny (c1 * x - d1 * y + z2);
            z2 = flushTiny (c2 * x - d2 * y);
            return y;
        }

    private:
        float c0 = 1, c1 = 0, c2 = 0, d1 = 0, d2 = 0, z1 = 0, z2 = 0;
    };

    /** DC blocker (first-order high-pass around 10 Hz). */
    class DcBlocker
    {
    public:
        void prepare (double sampleRate) noexcept { r = (float) std::exp (-2.0 * 3.141592653589793 * 10.0 / sampleRate); }
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = flushTiny (x - x1 + r * y1);
            x1 = x;
            y1 = y;
            return y;
        }

    private:
        float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
    };

    /** Power-of-two circular delay line with Hermite fractional reads. */
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 4)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }
        void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }
        int capacity() const noexcept { return mask - 3; }

        void push (float x) noexcept
        {
            buffer[(size_t) write] = x;
            write = (write + 1) & mask;
        }
        /** Sample written `delay` samples ago (delay >= 1 reads the previous push). */
        float read (float delay) const noexcept
        {
            delay = std::clamp (delay, 1.0f, (float) capacity());
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
        float readInt (int delay) const noexcept { return at (write - std::clamp (delay, 1, capacity())); }

    private:
        float at (int i) const noexcept { return buffer[(size_t) (i & mask)]; }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    /** Phase-accumulator LFO. */
    class Lfo
    {
    public:
        void setRate (float hz, double sampleRate) noexcept { inc = hz / (float) sampleRate; }
        void reset (float phase0 = 0.0f) noexcept { phase = phase0; }
        float advance() noexcept
        {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            return phase;
        }
        float phaseValue() const noexcept { return phase; }
        static float sine (float p) noexcept { return std::sin (kTwoPi * p); }
        static float triangle (float p) noexcept { return 4.0f * std::abs (p - std::floor (p + 0.5f)) - 1.0f; }

    private:
        float phase = 0.0f, inc = 0.0f;
    };

    /** Peak envelope follower with separate attack and release. */
    class EnvelopeFollower
    {
    public:
        void set (float attackMs, float releaseMs, double sampleRate) noexcept
        {
            att = (float) std::exp (-1.0 / (0.001 * std::max (attackMs, 0.01f) * sampleRate));
            rel = (float) std::exp (-1.0 / (0.001 * std::max (releaseMs, 0.01f) * sampleRate));
        }
        void reset() noexcept { env = 0.0f; }
        float process (float x) noexcept
        {
            const float a = std::abs (x);
            env = flushTiny (a + (a > env ? att : rel) * (env - a));
            return env;
        }
        float value() const noexcept { return env; }

    private:
        float att = 0.0f, rel = 0.0f, env = 0.0f;
    };

    /** Low-latency delay-line pitch shifter: two taps sweep through a short window in
        opposite phase and cross-fade with a raised-cosine, so each tap's discontinuity
        happens while it is silent. Latency is about half the window. */
    class PitchShifter
    {
    public:
        void prepare (double sampleRate, float windowMs = 40.0f)
        {
            window = (float) (0.001 * windowMs * sampleRate);
            line.prepare ((int) window + 8);
            reset();
        }
        void reset() noexcept { line.reset(); phase = 0.0f; }
        void setRatio (float r) noexcept { ratio = r; }

        float process (float x) noexcept
        {
            line.push (x);
            // The delay changes by (1 - ratio) per sample: shrinking delay = higher pitch.
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);
            const float p2 = phase + 0.5f - (phase + 0.5f >= 1.0f ? 1.0f : 0.0f);
            const float d1 = 2.0f + phase * window, d2 = 2.0f + p2 * window;
            const float w1 = 0.5f - 0.5f * std::cos (kTwoPi * phase);
            const float w2 = 0.5f - 0.5f * std::cos (kTwoPi * p2);
            return w1 * line.read (d1) + w2 * line.read (d2);
        }

    private:
        DelayLine line;
        float window = 1764.0f, phase = 0.0f, ratio = 1.0f;
    };

    /** Linkwitz-Riley 4th-order crossover low/high pair from two cascaded Butterworth
        sections (Q = 0.7071). */
    class LR4
    {
    public:
        void set (double hz, double sampleRate) noexcept
        {
            for (auto& b : lp) b.set (Biquad::Type::lowpass, hz, 0.70710678, 0.0, sampleRate);
            for (auto& b : hp) b.set (Biquad::Type::highpass, hz, 0.70710678, 0.0, sampleRate);
        }
        void reset() noexcept { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
        float low (float x) noexcept  { return lp[1].process (lp[0].process (x)); }
        float high (float x) noexcept { return hp[1].process (hp[0].process (x)); }

    private:
        std::array<Biquad, 2> lp, hp;
    };
}
