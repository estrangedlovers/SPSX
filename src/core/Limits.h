#pragma once

#include "Types.h"

namespace spsx
{
    /** Every difference between Authentic and Extended mode is a value in here.
        Engine and model code read these values; there are no "if authentic"
        branches in DSP code (ARCHITECTURE.md section 3). */
    struct Limits
    {
        /** Polyphony counted in mono channel-voices: a stereo voice costs 2.
            Manual p. 11: "12 monaural samples or 6 stereo samples". */
        int  channelVoices         = 12;
        /** Manual p. 26: "up to four monaural samples or two stereo samples" while resampling. */
        int  resampleChannelVoices = 4;
        int  effectSlots           = 1;
        int  banks                 = kAuthenticBanks;

        /** Store and record samples on the 16-bit grid without dither (manual p. 49). */
        bool requantize16          = true;
        /** Truncate rewrites the sample and cannot be undone (manual p. 29). */
        bool destructiveTruncate   = true;
        /** Pattern tempo restricted to the hardware's value table (manual p. 15). */
        bool steppedTempo          = true;
        /** Hard clip at 0 dBFS after output gain (fixed-point output stage). */
        bool clipOutput            = true;
        /** Run DSP at 44.1 kHz with SRC to the device rate. */
        bool internal44k           = true;
        /** Time Modify: real-time granular (hardware-like, "quavery") vs high quality render. */
        bool highQualityStretch    = false;
        bool undo                  = false;
        bool honourNoteOff         = false;
        bool velocity              = false;

        static Limits authentic() noexcept { return {}; }

        static Limits extended() noexcept
        {
            Limits l;
            l.channelVoices         = 32;
            l.resampleChannelVoices = 32;
            l.effectSlots           = 2;
            l.requantize16          = false;
            l.destructiveTruncate   = false;
            l.steppedTempo          = false;
            l.clipOutput            = false;
            l.highQualityStretch    = true;
            l.undo                  = true;
            l.honourNoteOff         = true;
            l.velocity              = true;
            return l;
        }
    };

    /** Physical voice objects. Larger than any budget so stolen voices can fade out
        without counting against polyphony. */
    constexpr int kMaxVoiceObjects = 48;
}
