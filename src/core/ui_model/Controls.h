#pragma once

#include <cstdint>

namespace spsx
{
    /** Every button and pad on the top panel (SPEC section 1.1). */
    enum class Control : std::uint8_t
    {
        filterDrive, voiceTrans, delay, isolator, djfxLooper, mfx,      // effect buttons
        patternSelect, length, quantize,                                // PATTERN SEQUENCER
        del, rec, resample, cancel, remain, mark,
        startEndLevel, timeBpm, tapTempo,                               // SAMPLE EDIT
        lofi, stereo, gate, loop, reverse,                              // SAMPLE MODE
        bankAF, bankBG, bankCH, bankDI, bankEJ, func,                   // BANK
        pad1, pad2, pad3, pad4, pad5, pad6, pad7, pad8, pad9, pad10, pad11, pad12,
        hold, extSource, subPad,
        mic,
        count
    };

    constexpr int kNumControls = (int) Control::count;

    enum class Knob : std::uint8_t { ctrl1, ctrl2, ctrl3, volume, micLevel, count };
    constexpr int kNumKnobs = (int) Knob::count;

    constexpr bool isPad (Control c) noexcept     { return c >= Control::pad1 && c <= Control::pad12; }
    constexpr int  padIndex (Control c) noexcept  { return (int) c - (int) Control::pad1; }
    constexpr Control padControl (int i) noexcept { return (Control) ((int) Control::pad1 + i); }

    constexpr bool isBank (Control c) noexcept    { return c >= Control::bankAF && c <= Control::bankEJ; }
    constexpr int  bankButtonIndex (Control c) noexcept { return (int) c - (int) Control::bankAF; }

    /** Index into the effect table: 0 FILTER+DRIVE .. 4 DJFX LOOPER, 5 MFX. */
    constexpr bool isEffectButton (Control c) noexcept { return c <= Control::mfx; }
    constexpr int  effectButtonIndex (Control c) noexcept { return (int) c; }

    const char* controlName (Control c) noexcept;
    const char* knobName (Knob k) noexcept;
}
