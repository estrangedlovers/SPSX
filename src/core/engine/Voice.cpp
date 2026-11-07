#include "Voice.h"
#include <cmath>
#include <algorithm>

namespace spsx
{
    namespace
    {
        constexpr double kReleaseSeconds = 0.002;   // stop/steal fade (FIDELITY: hardware may click)
    }

    void Voice::prepare (double rate, int maxBlock, const dsp::LoFi::Params& lofiParams)
    {
        engineRate = rate;
        for (auto& s : scratch)
            s.assign ((size_t) maxBlock, 0.0f);
        lofi.prepare (rate, lofiParams);
        state = State::idle;
    }

    void Voice::start (PadId padIn, const Source& src, const PadSettings& s, std::int64_t now)
    {
        pad = padIn;
        current = src.sample;
        currentStretched = src.stretched;
        channels = juce::jlimit (1, 2, current->numChannels());
        reverse = s.reverse;
        increment = kEngineRate / engineRate;
        started = now;
        gain = (float) s.level / 127.0f;
        releaseGain = 1.0f;
        releaseStep = 0.0f;
        heldByPad = heldBySub = latched = false;
        lofi.reset();
        lofiWasOn = s.lofi;

        const SampleIndex len = current->length();
        const SampleIndex st  = juce::jlimit<SampleIndex> (0, len, s.start);
        const SampleIndex en  = s.end > st ? juce::jmin (s.end, len) : len;

        if (currentStretched != nullptr)
        {
            mode = Mode::prerendered;
            pos = 0.0;
        }
        else if (std::abs (src.speed - 1.0) > 1e-9 && ! reverse)   // Time Modify is disabled with REVERSE (p. 30)
        {
            mode = Mode::granular;
            granular.start ((double) st, src.speed);
            pos = (double) st;
        }
        else
        {
            mode = Mode::plain;
            pos = reverse ? (double) (en - 1) : (double) st;
        }

        state = State::playing;
    }

    void Voice::release() noexcept
    {
        if (state != State::playing)
            return;
        state = State::releasing;
        releaseStep = (float) (1.0 / std::max (1.0, kReleaseSeconds * engineRate));
    }

    SampleIndex Voice::position() const noexcept
    {
        if (mode == Mode::granular)
            return (SampleIndex) granular.position();
        return (SampleIndex) pos;
    }

    float Voice::hermite (const float* x, SampleIndex len, double p) noexcept
    {
        const auto i = (SampleIndex) std::floor (p);
        const float f = (float) (p - (double) i);
        auto at = [x, len] (SampleIndex k) { return x[std::clamp<SampleIndex> (k, 0, len - 1)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int Voice::renderSource (float* const* tmp, int n, const PadSettings& s) noexcept
    {
        const SampleData* src = (mode == Mode::prerendered) ? currentStretched : current;
        const SampleIndex len = src->length();
        const float* in[2] = { src->channel (0), src->channel (1) };

        SampleIndex st, en;
        if (mode == Mode::prerendered)
        {
            st = 0;
            en = len;
        }
        else
        {
            st = juce::jlimit<SampleIndex> (0, len - 1, s.start);
            en = s.end > st ? juce::jmin (s.end, len) : len;
        }

        if (mode == Mode::granular)
            return granular.process (in, channels, st, en, s.loop, tmp, channels, n);

        const double regionLen = (double) (en - st);
        const bool integral = std::abs (increment - 1.0) < 1e-12;

        for (int i = 0; i < n; ++i)
        {
            if (! reverse)
            {
                if (pos < (double) st)
                    pos = (double) st;          // region moved past the play head
                if (pos >= (double) en)
                {
                    if (! s.loop)
                        return i;
                    pos = (double) st + std::fmod (pos - (double) st, regionLen);
                }
            }
            else
            {
                if (pos >= (double) en)
                    pos = (double) (en - 1);
                if (pos < (double) st)
                {
                    if (! s.loop)
                        return i;
                    pos += regionLen * std::ceil (((double) st - pos) / regionLen);
                }
            }

            if (integral)
            {
                const auto k = (SampleIndex) pos;
                for (int c = 0; c < channels; ++c)
                    tmp[c][i] = in[c][k];
            }
            else
            {
                for (int c = 0; c < channels; ++c)
                    tmp[c][i] = hermite (in[c], len, pos);
            }

            pos += reverse ? -increment : increment;
        }
        return n;
    }

    void Voice::render (float* busL, float* busR, int n, const PadSettings& s, const Source&) noexcept
    {
        if (state == State::idle)
            return;

        float* tmp[2] = { scratch[0].data(), scratch[1].data() };
        const int produced = renderSource (tmp, n, s);

        if (s.lofi)
        {
            if (! lofiWasOn)
                lofi.reset();
            lofi.process (tmp, channels, produced);
        }
        lofiWasOn = s.lofi;

        // LEVEL: linear 0..127 (FIDELITY: mapping unverified). Ramp across the block.
        const float target = (float) s.level / 127.0f;
        const float gStep = produced > 0 ? (target - gain) / (float) produced : 0.0f;

        for (int i = 0; i < produced; ++i)
        {
            gain += gStep;
            float g = gain;

            if (state == State::releasing)
            {
                releaseGain -= releaseStep;
                if (releaseGain <= 0.0f)
                {
                    state = State::idle;
                    return;
                }
                g *= releaseGain;
            }

            const float l = tmp[0][i] * g;
            const float r = (channels > 1 ? tmp[1][i] : tmp[0][i]) * g;   // mono feeds both sides equally
            busL[i] += l;
            busR[i] += r;
        }
        gain = target;

        if (produced < n)
            state = State::idle;   // natural end of a one-shot
    }
}
