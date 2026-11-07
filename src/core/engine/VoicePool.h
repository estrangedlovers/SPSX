#pragma once

#include "Voice.h"
#include "../Limits.h"
#include <array>

namespace spsx
{
    /** Channel-voice budget and stealing policy (SPEC U4, ARCHITECTURE section 9).

        The manual gives the budget (12 mono / 6 stereo; 4 / 2 while resampling) but
        not the stealing rule. Chosen rule:
          1. A pad that is already sounding reuses its own slot (Engine releases it
             before allocating, so the budget frees up first).
          2. Otherwise steal the oldest sounding voice(s) until the new voice fits.
          3. Stolen voices fade over 2 ms; fading voices do not count against budget.
        Voice objects outnumber the budget so fades never block a new note. */
    class VoicePool
    {
    public:
        void prepare (double engineRate, int maxBlock, const dsp::LoFi::Params& lofiParams)
        {
            for (auto& v : voices)
                v.prepare (engineRate, maxBlock, lofiParams);
        }

        void setBudget (int channelVoices) noexcept { budget = channelVoices; }
        int  getBudget() const noexcept { return budget; }

        int usedChannelVoices() const noexcept
        {
            int used = 0;
            for (auto& v : voices)
                if (v.getState() == Voice::State::playing)
                    used += v.channelCost();
            return used;
        }

        /** Returns a voice object ready to start, stealing as needed, or nullptr if the
            voice can never fit (stereo voice with a budget of 1). */
        Voice* allocate (int cost) noexcept
        {
            if (cost > budget)
                return nullptr;

            int used = usedChannelVoices();
            while (used + cost > budget)
            {
                Voice* oldest = nullptr;
                for (auto& v : voices)
                    if (v.getState() == Voice::State::playing && (oldest == nullptr || v.startTime() < oldest->startTime()))
                        oldest = &v;
                if (oldest == nullptr)
                    break;
                oldest->release();
                used -= oldest->channelCost();
            }

            for (auto& v : voices)
                if (! v.isActive())
                    return &v;

            // Every object is busy fading: cut the oldest fade.
            Voice* victim = nullptr;
            for (auto& v : voices)
                if (v.isReleasing() && (victim == nullptr || v.startTime() < victim->startTime()))
                    victim = &v;
            if (victim != nullptr)
                victim->kill();
            return victim;
        }

        auto begin() noexcept { return voices.begin(); }
        auto end() noexcept   { return voices.end(); }

    private:
        std::array<Voice, kMaxVoiceObjects> voices;
        int budget = 12;
    };
}
