#pragma once

#include <array>

namespace spsx::fx
{
    /** Number of effects: 0..4 direct buttons, 5..28 = MFX 1..24 (SPEC 7.3, 7.4). */
    constexpr int kNumEffects = 29;
    constexpr int mfxIndex (int mfxNumber) noexcept { return 4 + mfxNumber; }   // MFX 1 -> 5

    /** Knob values 0..1: CTRL 1..3 plus the hidden 4th (FLANGER/PHASER balance, p. 44). */
    using Params = std::array<float, 4>;

    struct Context
    {
        double sampleRate = 44100.0;
        double tempo = 120.0;     // reference tempo for synced parameters (SPEC 7.5 *1, *2, *4)
        bool limit = false;       // Effect Limit mode (p. 42)
    };

    /** One effect. Stereo, in place. prepare() may allocate; nothing else does.
        Parameters arrive once per block via setParams(); effects smooth them internally
        so knob moves never zipper. */
    class Effect
    {
    public:
        virtual ~Effect() = default;

        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void reset() = 0;
        virtual void process (float* left, float* right, int numSamples) noexcept = 0;

        void setParams (const Params& p, const Context& c) noexcept
        {
            params = p;
            ctx = c;
        }

    protected:
        Params params { 0.5f, 0.5f, 0.5f, 0.5f };
        Context ctx;
    };
}
