#pragma once

#include "../Types.h"
#include "../state/SampleData.h"
#include "../state/PadSettings.h"
#include "../dsp/LoFi.h"
#include "../dsp/GranularStretcher.h"
#include <vector>

namespace spsx
{
    /** One playing sample. Owned by VoicePool, driven by Engine on the audio thread. */
    class Voice
    {
    public:
        enum class State { idle, playing, releasing };

        /** What the voice should read from. `stretched` (Extended Time Modify) is a
            pre-rendered copy of the region at the right length; when present the voice
            plays it plainly. Otherwise `speed != 1` selects the real-time granulator. */
        struct Source
        {
            const SampleData* sample    = nullptr;
            const SampleData* stretched = nullptr;
            double speed = 1.0;                     // Time Modify source speed (1 = off)
        };

        void prepare (double engineRate, int maxBlock, const dsp::LoFi::Params& lofiParams);

        void start (PadId padIn, const Source& src, const PadSettings& s, std::int64_t now);
        /** Click-free stop: 2 ms linear fade. */
        void release() noexcept;
        /** Immediate stop, used only when every voice object is busy. */
        void kill() noexcept { state = State::idle; }

        /** Adds n samples into the dry and effect buses. Reads settings live so LEVEL,
            LOOP and region edits apply to sounding voices. `toEffect` says whether this
            pad is an effect target; the voice glides between buses over ~5 ms so
            changing targets never clicks. */
        void render (float* dryL, float* dryR, float* fxL, float* fxR, int n,
                     const PadSettings& s, const Source& src, bool toEffect) noexcept;

        State  getState() const noexcept   { return state; }
        bool   isActive() const noexcept   { return state != State::idle; }
        bool   isReleasing() const noexcept { return state == State::releasing; }
        int    channelCost() const noexcept { return channels; }
        PadId  getPad() const noexcept     { return pad; }
        std::int64_t startTime() const noexcept { return started; }
        const SampleData* sampleInUse() const noexcept { return isActive() ? current : nullptr; }
        const SampleData* stretchedInUse() const noexcept { return isActive() ? currentStretched : nullptr; }
        /** Position in source samples (for MARK and the UI). */
        SampleIndex position() const noexcept;

        // Performance flags, managed by Engine.
        bool heldByPad = false;   // pad physically down
        bool heldBySub = false;   // SUB PAD physically down
        bool latched   = false;   // HOLD applied

    private:
        enum class Mode { plain, granular, prerendered };

        int renderSource (float* const* tmp, int n, const PadSettings& s) noexcept;
        static float hermite (const float* x, SampleIndex len, double pos) noexcept;

        State state = State::idle;
        Mode mode = Mode::plain;
        PadId pad = kNoPad;
        const SampleData* current = nullptr;
        const SampleData* currentStretched = nullptr;
        int channels = 1;
        bool reverse = false;
        double pos = 0.0;
        double increment = 1.0;       // 44.1k source at engine rate
        double engineRate = kEngineRate;
        std::int64_t started = 0;

        float gain = 1.0f;            // smoothed LEVEL gain
        float send = 0.0f;            // 0 = dry bus, 1 = effect bus (smoothed)
        float sendStep = 0.005f;
        float releaseGain = 1.0f;
        float releaseStep = 0.0f;
        bool  lofiWasOn = false;

        dsp::LoFi lofi;
        dsp::GranularStretcher granular;
        std::vector<float> scratch[2];
    };
}
