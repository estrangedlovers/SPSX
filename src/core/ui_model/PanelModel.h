#pragma once

#include "Controls.h"
#include "DisplayText.h"
#include "../SamplerController.h"
#include "../CancelDetector.h"
#include "../sampler/SampleEdit.h"
#include <array>
#include <bitset>
#include <functional>
#include <optional>
#include <vector>

namespace spsx
{
    enum class Led : std::uint8_t { off, on, blink, blinkFast };

    /** Snapshot of everything the panel shows. Cheap to copy and compare, so the UI can
        repaint only when it changes. Blinking is expressed as a state; the UI animates it. */
    struct PanelView
    {
        std::array<Led, kNumControls> leds {};
        DisplayText display;
        bool busyDot = false;            // "the dot in the display will blink"
        bool sleeping = false;           // SLP: dots chase, everything else dark

        enum class Illumination : std::uint8_t { off, redBlue, blue, red };
        Illumination illumination = Illumination::redBlue;
        bool illuminationOverload = false;   // red while the digital input is too hot (Phase 4)
        bool illuminationTempo = false;      // blink with the pattern tempo (Phase 5)
        double tempo = 120.0;

        Led peak = Led::off;
        std::array<float, kNumKnobs> knobs {};

        friend bool operator== (const PanelView&, const PanelView&) = default;
    };

    /** The hardware panel's behaviour: modes, button combinations, flows and what every
        LED and the display show (SPEC sections 3 to 6, 11). It owns no audio; it drives
        the Engine (performance) and SamplerController (edits) on the message thread.

        All inputs carry `now` in seconds so the model is deterministic in tests.
        Features of later phases (effects DSP, sampling, sequencer, MIDI) keep their
        panel state here already but show "---" where an action would need the engine
        part that does not exist yet. */
    class PanelModel
    {
    public:
        struct Callbacks
        {
            /** FUNC + 3 (IMPORT) confirmed: ask the user for files, starting at this pad. */
            std::function<void (PadId)> requestImport;
            /** Free bytes where new samples are stored (REMAIN). */
            std::function<juce::int64()> freeBytes;
            /** VOLUME knob (the plugin routes it to its host parameter). */
            std::function<void (double)> setVolume;
        };

        /** Effect panel state. Phase 3 connects it to the effect engine. */
        struct EffectState
        {
            int active = -1;                         // 0..4 direct effects, 5 = MFX, -1 none
            int lastActive = -1;
            int mfxType = 1;                         // 1..24
            std::array<std::array<float, 4>, 29> params {};   // [effect][ctrl1..3, hidden balance]
            std::optional<std::array<float, 3>> preset;       // REMAIN + knobs
            bool targetAll = false;
            std::bitset<kMaxPads> targets;           // REMAIN + pads
            bool grabbing = false;                   // TAP + effect button
            int grabRestore = -1;
        };

        /** Pattern-sequencer panel settings (Phase 5 uses them). */
        struct PatternPanelState
        {
            int lengthBars = 1;
            int quantizeIndex = 6;     // see quantizeNames(); default "16"
            int shuffle = 50;
            int metronomeLevel = 100;
        };

        PanelModel (Engine& engine, SamplerController& controller);

        void setCallbacks (Callbacks c) { callbacks = std::move (c); }

        // ---- input -------------------------------------------------------------------
        void press (Control c, double now);
        void release (Control c, double now);
        /** Absolute knob position 0..1. */
        void turn (Knob k, double value, double now);
        /** Direct bank selection (keyboard Shift + bank key reaches F..J in one press). */
        void selectBank (int bank, double now);
        /** Moves a knob without acting on it (host automation of VOLUME, restored state). */
        void setKnobPosition (Knob k, double value) noexcept { knobs[(size_t) k] = juce::jlimit (0.0, 1.0, value); }
        /** Housekeeping: timeouts, MARK sessions, sleep. Call at UI rate. */
        void tick (double now);

        // ---- output ------------------------------------------------------------------
        PanelView view (double now) const;

        // ---- state (UI side panels and tests) ---------------------------------------
        PadId currentPad() const noexcept { return current; }
        int bank() const noexcept { return bankIndex; }
        bool isPatternMode() const noexcept { return patternMode; }
        bool isPaused() const noexcept { return paused; }
        bool isStereo() const noexcept { return stereo; }
        const EffectState& effects() const noexcept { return fx; }
        const PatternPanelState& pattern() const noexcept { return seq; }
        double knob (Knob k) const noexcept { return knobs[(size_t) k]; }

        static const std::array<const char*, 10>& quantizeNames();
        static std::vector<int> patternLengths();
        /** "0.11", "10.3", "173", "24H" (SPEC U10). */
        static DisplayText remainDisplay (double seconds);

    private:
        enum class Edit { none, startEndLevel, timeBpm, length, quantize };

        enum class Op
        {
            none, copy, exchange, import, funcSetting, del, delAll, truncate,
            protect, format, backupLoad, backupSave
        };

        enum class FuncSetting { autoTrig, illumination, effectLimit, midiChannel, midiSync, inputGain, outputGain };

        struct OpState
        {
            Op op = Op::none;
            int stage = 0;
            PadId a = kNoPad, b = kNoPad;
            std::bitset<kMaxPads> selected;
            FuncSetting setting = FuncSetting::autoTrig;
            int value = 0;          // option index for funcSetting
            bool changed = false;
            bool protectOn = true;
        };

        // input handlers
        void pressPad (int index, double now);
        void releasePad (int index);
        void pressEffect (int index, double now);
        void releaseEffect (int index);
        void pressBank (int buttonIndex, double now);
        void pressDel (double now);
        void pressRec (double now);
        void pressMark (double now);
        void pressCancel (double now);
        void pressRemain (double now);
        void pressTap (double now);
        void toggleMode (Control c, double now);
        void toggleEdit (Edit e, double now);
        void openFunc (int pad, double now);
        void opPad (PadId pad, double now);
        void endOp();
        void applyFuncSetting();
        void turnInEdit (Knob k, double v, double now);

        // helpers
        PadId padAt (int index) const noexcept { return { bankIndex, index }; }
        bool hasSample (PadId p) const;
        bool bankProtected (int bank) const;
        bool isPlaying (PadId p) const { return engine.playPosition (p) >= 0; }
        void flash (DisplayText d, double now, double seconds = 1.5);
        void flash (std::string_view s, double now, double seconds = 1.5) { flash (DisplayText::make (s), now, seconds); }
        void busy (double now) { busyUntil = now + 0.6; }
        void sendEngine (Command::Type t, PadId p = kNoPad, double v = 0.0);
        void reportStatus (SamplerController::Status s, double now);
        void beginFineAdjust();
        void finishMark (const PadSettings& s, double now);
        int funcOptionCount (FuncSetting s) const;
        DisplayText funcDisplay() const;
        DisplayText idleDisplay() const;
        static DisplayText bpmDisplay (double bpm);
        DisplayText sampleBpmDisplay() const;

        Engine& engine;
        SamplerController& controller;
        Callbacks callbacks;

        PadId current { 0, 0 };
        int bankIndex = 0;
        bool patternMode = false;
        bool stereo = true;
        bool mic = false;
        bool paused = false;
        bool sleeping = false;
        Edit edit = Edit::none;
        OpState op;
        EffectState fx;
        PatternPanelState seq;

        // held modifiers
        bool funcHeld = false, remainHeld = false, cancelHeld = false, markHeld = false;
        bool mfxHeld = false, mfxUsedAsModifier = false, djfxHeldForMfx = false, tapHeld = false;
        bool subHeld = false, holdHeld = false;
        std::bitset<12> padsDownInEngine;

        std::array<double, kNumKnobs> knobs { 0.5, 0.5, 0.5, 0.8, 0.5 };
        std::array<bool, 3> knobMovedInEdit { false, false, false };

        // START/END/LEVEL fine adjust and TIME/BPM halve/double reference
        std::optional<edit::FineAdjust> fineAdjust;
        double bpmAtEntry = 0.0;

        // MARK
        std::optional<edit::MarkSession> markSession;
        PadId markPad = kNoPad;

        // tap tempo
        std::array<double, 4> taps {};
        int numTaps = 0;

        // display
        std::optional<DisplayText> tempDisplay;
        double tempUntil = 0.0;
        double busyUntil = 0.0;
        double lastEffectBlinkUntil = 0.0;
        bool remainShowing = false;

        CancelDetector cancelDetector;
        double lastActivity = 0.0;
    };
}
