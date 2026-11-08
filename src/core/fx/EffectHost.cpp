#include "EffectHost.h"
#include "EffectInfo.h"
#include "Effects.h"
#include <algorithm>
#include <cmath>

namespace spsx::fx
{
    namespace
    {
        constexpr int kChunk = 32;

        /** Soft knee from 0.8 to a ceiling of 1.0 (Effect Limit safety stage). */
        inline float softLimit (float x) noexcept
        {
            const float a = std::abs (x);
            if (a <= 0.8f) return x;
            return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
        }
    }

    void EffectHost::prepare (double sr, int, int slots)
    {
        sampleRate = sr;
        numSlots = std::clamp (slots, 1, kMaxSlots);
        effects.clear();
        effects.resize ((size_t) numSlots);
        // A host may prepare again (rate change, re-activation): keep the knob values and
        // the selected effects so the sound does not change under the user.
        if (params.size() != (size_t) kNumEffects)
        {
            params.assign ((size_t) kNumEffects, Params {});
            for (int e = 0; e < kNumEffects; ++e)
                params[(size_t) e] = info (e).defaults;
        }

        for (auto& slot : effects)
        {
            slot.reserve ((size_t) kNumEffects);
            for (int e = 0; e < kNumEffects; ++e)
            {
                auto fx = create (e);
                fx->setParams (params[(size_t) e], { sr, 120.0, false });
                fx->prepare (sr, kChunk);
                slot.push_back (std::move (fx));
            }
        }
        for (auto& t : tmp)
            t.assign ((size_t) kChunk, 0.0f);
        fadeStep = (float) (1.0 / (0.025 * sr));
        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto& st = slotState[(size_t) i];
            const int keep = i < numSlots ? st.current : -1;
            st = {};
            st.current = keep;
        }
    }

    void EffectHost::select (int slot, int effect) noexcept
    {
        if (slot < 0 || slot >= numSlots)
            return;
        auto& s = slotState[(size_t) slot];
        if (effect == s.current)
            return;
        if (effect >= 0)
        {
            auto& fx = *effects[(size_t) slot][(size_t) effect];
            fx.setParams (params[(size_t) effect], { sampleRate, 120.0, limit });
            fx.reset();
        }
        // A switch during a running fade starts from where the listener is now.
        s.previous = s.fade < 0.5f ? s.previous : s.current;
        s.current = effect;
        s.fade = 0.0f;
    }

    void EffectHost::setParams (int effect, const Params& p) noexcept
    {
        if (effect >= 0 && effect < kNumEffects)
            params[(size_t) effect] = p;
    }

    void EffectHost::runEffect (int slot, int effect, float* l, float* r, int n, double tempo) noexcept
    {
        if (effect < 0)
            return;   // bypass: the buffer already holds the input
        auto& fx = *effects[(size_t) slot][(size_t) effect];
        fx.setParams (params[(size_t) effect], { sampleRate, tempo, limit });
        fx.process (l, r, n);
    }

    void EffectHost::processChunk (int slot, float* l, float* r, int n, double tempo) noexcept
    {
        auto& s = slotState[(size_t) slot];

        if (s.fade >= 1.0f)
        {
            runEffect (slot, s.current, l, r, n, tempo);
            return;
        }

        // Cross-fade: run both on copies of the input.
        float* al = tmp[0].data(); float* ar = tmp[1].data();
        float* bl = tmp[2].data(); float* br = tmp[3].data();
        std::copy (l, l + n, al); std::copy (r, r + n, ar);
        std::copy (l, l + n, bl); std::copy (r, r + n, br);
        runEffect (slot, s.previous, al, ar, n, tempo);
        runEffect (slot, s.current, bl, br, n, tempo);

        for (int i = 0; i < n; ++i)
        {
            const float t = s.fade;
            const float gNew = std::sin (t * 1.5707963f), gOld = std::cos (t * 1.5707963f);
            l[i] = gOld * al[i] + gNew * bl[i];
            r[i] = gOld * ar[i] + gNew * br[i];
            s.fade = std::min (1.0f, s.fade + fadeStep);
        }
        if (s.fade >= 1.0f)
            s.previous = -1;
    }

    void EffectHost::process (float* l, float* r, int n, double tempo) noexcept
    {
        for (int done = 0; done < n; done += kChunk)
        {
            const int m = std::min (kChunk, n - done);
            for (int slot = 0; slot < numSlots; ++slot)
                processChunk (slot, l + done, r + done, m, tempo);
        }

        if (limit)
            for (int i = 0; i < n; ++i)
            {
                l[i] = softLimit (l[i]);
                r[i] = softLimit (r[i]);
            }
    }
}
