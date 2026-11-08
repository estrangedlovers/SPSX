#pragma once

#include "SampleData.h"
#include "PadSettings.h"
#include "PatternData.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace spsx
{
    /** One pad's document state. */
    struct PadSlot
    {
        SampleData::Ptr sample;
        juce::String file;          // file name inside the project's samples/ folder (or absolute path)
        PadSettings settings;

        bool isEmpty() const noexcept { return sample == nullptr; }
    };

    /** Device-wide settings the hardware keeps internally (FUNC menu, manual p. 39-47). */
    struct DeviceSettings
    {
        int  inputGainDb    = 0;     // -10 or 0
        int  outputGainDb   = 0;     // -3..+12 in 3 dB steps
        bool effectLimit    = false;
        int  illumination   = 1;     // 0 oFF, 1 r b (default), 2 b, 3 r, 4 SLP
        int  midiChannel    = 1;     // 1..16
        int  midiSync       = 0;     // 0 AUt (default), 1 tNp, 2 oFF
        int  autoTrigLevel  = 0;     // 0..9, 0 = off
        bool aiffFiles      = false; // file type for new samples (WAV default)

        friend bool operator== (const DeviceSettings&, const DeviceSettings&) = default;
    };

    /** Document state (ARCHITECTURE.md section 6). Owned by the message thread. */
    struct Project
    {
        explicit Project (int numBanks = kAuthenticBanks);

        int numBanks;
        std::vector<PadSlot> pads;                 // numBanks * 12
        std::vector<PatternData::Ptr> patterns;    // numBanks * 12 (nullptr = empty)
        std::vector<bool> sampleBankProtected;     // per bank (manual p. 38)
        std::vector<bool> patternBankProtected;
        double patternTempo = 120.0;
        DeviceSettings device;

        PadSlot&       slot (PadId p)       { return pads[(size_t) p.index()]; }
        const PadSlot& slot (PadId p) const { return pads[(size_t) p.index()]; }
        PatternData::Ptr& pattern (PadId p)       { return patterns[(size_t) p.index()]; }
        const PatternData::Ptr& pattern (PadId p) const { return patterns[(size_t) p.index()]; }

        /** Settings and file references only; audio is stored as files beside the JSON. */
        juce::var toVar() const;
        /** Restores settings and file references. Samples are not loaded here. */
        static juce::Result fromVar (const juce::var& v, Project& out);

        static constexpr int kSchemaVersion = 1;
    };
}
