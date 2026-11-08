#pragma once

#include "Effect.h"
#include <memory>
#include <vector>

namespace spsx::fx
{
    /** Runs the selected effect(s) on the effect bus (SPEC 7.1).
        - All 29 effects are created and prepared up front for each slot, so switching
          never allocates on the audio thread.
        - Switching (including on/off) cross-fades old and new output over 25 ms with
          equal-power curves: "Effects switch seamlessly" (p. 2). The newly selected
          effect is reset first, so it never replays a stale tail.
        - Processing runs in chunks of at most 32 samples so block-rate parameters are
          refreshed often enough to avoid zipper noise.
        - With Effect Limit on, a soft limiter caps the effect output just under 0 dBFS
          on top of the per-parameter limits inside each effect.
        Audio thread only, except prepare(). */
    class EffectHost
    {
    public:
        static constexpr int kMaxSlots = 2;

        void prepare (double sampleRate, int maxBlock, int slots);

        /** -1 = off. */
        void select (int slot, int effect) noexcept;
        int selected (int slot) const noexcept { return slotState[(size_t) slot].current; }
        void setParams (int effect, const Params& p) noexcept;
        const Params& getParams (int effect) const noexcept { return params[(size_t) effect]; }
        void setLimit (bool on) noexcept { limit = on; }

        void process (float* left, float* right, int numSamples, double tempo) noexcept;

    private:
        struct SlotState
        {
            int current = -1, previous = -1;
            float fade = 1.0f;   // 0 -> 1 while cross-fading previous -> current
        };

        void processChunk (int slot, float* l, float* r, int n, double tempo) noexcept;
        void runEffect (int slot, int effect, float* l, float* r, int n, double tempo) noexcept;

        double sampleRate = 44100.0;
        int numSlots = 1;
        bool limit = false;
        std::vector<std::vector<std::unique_ptr<Effect>>> effects;   // [slot][effect]
        std::vector<Params> params;
        std::array<SlotState, kMaxSlots> slotState;
        std::vector<float> tmp[4];
        float fadeStep = 0.001f;
    };
}
