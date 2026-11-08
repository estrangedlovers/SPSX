#include "PanelModel.h"
#include "../Tempo.h"
#include "../sampler/TimeModify.h"
#include "../fx/EffectInfo.h"
#include <cmath>
#include <cstdio>

// Switches over Control handle the relevant subset and default the rest on purpose.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wswitch-enum")

namespace spsx
{
    namespace
    {
        constexpr double kSleepSeconds = 300.0;        // "approximately five minutes" (p. 40)
        constexpr double kTapResetSeconds = 2.0;

        int knobToIndex (double v, int count)
        {
            return juce::jlimit (0, count - 1, (int) std::lround (v * (count - 1)));
        }

        // ---- FUNC setting options (manual p. 25, 39, 40, 42, 47) -------------------
        const std::array<const char*, 5> kIllumination { "oFF", "r b", "b", "r", "SLP" };
        const std::array<const char*, 3> kMidiSync { "AUt", "tMp", "oFF" };
        const std::array<int, 2> kInputGain { -10, 0 };
        const std::array<int, 6> kOutputGain { -3, 0, 3, 6, 9, 12 };
    }

    PanelModel::PanelModel (Engine& e, SamplerController& c)
        : engine (e), controller (c)
    {
        for (int i = 0; i < fx::kNumEffects; ++i)
            fx.params[(size_t) i] = fx::info (i).defaults;
        syncEngine();
    }

    // ---- engine sync ------------------------------------------------------------------

    float PanelModel::levelFromKnob (double v)
    {
        // Digital level (CTRL 3 while sampling / resampling): silent at the far left,
        // 0 dB at about 80 % of travel, +12 dB at the right end, so "all the way right,
        // then back until the red goes out" (p. 19) works as described.
        if (v <= 0.0) return 0.0f;
        return (float) std::pow (10.0, (-48.0 + 60.0 * v) / 20.0);
    }

    int PanelModel::activeEffectIndex() const noexcept
    {
        if (fx.active < 0) return -1;
        return fx.active == 5 ? fx::mfxIndex (fx.mfxType) : fx.active;
    }

    void PanelModel::selectEffectParamsFromKnobs (int effect)
    {
        // The CTRL knobs are absolute pots: a newly selected effect takes their current
        // positions (SPEC U34), unless REMAIN + knobs pre-set values (p. 13). While
        // sampling or resampling CTRL 3 is the level, so the effect keeps its own value.
        auto& p = fx.params[(size_t) effect];
        if (fx.preset)
        {
            for (size_t k = 0; k < 3; ++k) p[k] = (*fx.preset)[k];
            fx.preset.reset();
            return;
        }
        p[0] = (float) knobs[0];
        p[1] = (float) knobs[1];
        if (sampling == Sampling::none && resample == Resample::none)
            p[2] = (float) knobs[2];
    }

    void PanelModel::sendEffectParams (int effect)
    {
        if (effect < 0) return;
        Command c;
        c.type = Command::Type::setEffectParams;
        c.index = effect;
        c.params = fx.params[(size_t) effect];
        engine.post (c);
    }

    void PanelModel::syncEffect()
    {
        const int effect = activeEffectIndex();
        if (effect == sentEffect)
            return;
        if (effect >= 0)
        {
            selectEffectParamsFromKnobs (effect);
            sendEffectParams (effect);
        }
        Command c;
        c.type = Command::Type::setEffect;
        c.index = 0;
        c.value = effect;
        engine.post (c);
        sentEffect = effect;
    }

    void PanelModel::syncTargets()
    {
        // "Normally, the effect will be applied only to the pad you pressed most recently"
        // (p. 13). REMAIN + pads / EXT SOURCE pick several, REMAIN + effect button picks all.
        // While sampling, the effect processes the input instead (p. 19).
        std::bitset<kMaxPads> want;
        bool all = false, ext = fxMode;
        if (sampling == Sampling::standby || sampling == Sampling::recording)
        {
            // input only
        }
        else if (fx.targetAll)
            all = true;
        else if (fx.targets.any() || fx.targetExt)
        {
            want = fx.targets;
            ext = ext || fx.targetExt;
        }
        else if (currentIsExt)
            ext = true;
        else if (current.isValid())
            want.set ((size_t) current.index());

        if (all != sentTargetAll) { sendEngine (Command::Type::setTargetAll, kNoPad, all ? 1.0 : 0.0); sentTargetAll = all; }
        if (ext != sentTargetExt) { sendEngine (Command::Type::setTargetExt, kNoPad, ext ? 1.0 : 0.0); sentTargetExt = ext; }
        const auto changed = want ^ sentTargets;
        if (changed.any())
            for (int i = 0; i < kMaxPads; ++i)
                if (changed.test ((size_t) i))
                    sendEngine (Command::Type::setTargetPad, PadId::fromIndex (i), want.test ((size_t) i) ? 1.0 : 0.0);
        sentTargets = want;
    }

    void PanelModel::syncEngine()
    {
        const auto& dev = controller.project().device;
        sendEngine (Command::Type::setEffectLimit, kNoPad, dev.effectLimit ? 1.0 : 0.0);
        sendEngine (Command::Type::setInputGainDb, kNoPad, dev.inputGainDb);
        sendEngine (Command::Type::setMicMode, kNoPad, mic ? 1.0 : 0.0);
        sendEngine (Command::Type::setMicGainDb, kNoPad, 6.0 + 40.0 * knobs[(size_t) Knob::micLevel]);
        sendEngine (Command::Type::setExtLevel, kNoPad, extLevel);
        sendEngine (Command::Type::setExtStereo, kNoPad, stereo ? 1.0 : 0.0);
        sendEngine (Command::Type::setExtOn, kNoPad, extOn ? 1.0 : 0.0);
        sentEffect = -2;
        sentTargets.reset();
        sentTargetAll = sentTargetExt = false;
        sendEngine (Command::Type::clearTargets);
        syncEffect();
        syncTargets();
        for (int e = 0; e < fx::kNumEffects; ++e)
            sendEffectParams (e);
    }

    void PanelModel::setFxProcessorMode (bool on)
    {
        fxMode = on;
        extOn = on || (extOn && ! fxMode);
        sendEngine (Command::Type::setExtOn, kNoPad, extOn ? 1.0 : 0.0);
        syncTargets();
    }

    const std::array<const char*, 10>& PanelModel::quantizeNames()
    {
        static const std::array<const char*, 10> names { "S.8", "S.16", "4", "4.3", "8", "8.3", "16", "16.3", "32", "oFF" };
        return names;
    }

    std::vector<int> PanelModel::patternLengths()
    {
        // 1..20 in single bars, then 4-bar steps; 99 reachable [SPEC U12, assumed].
        std::vector<int> v;
        for (int i = 1; i <= 20; ++i) v.push_back (i);
        for (int i = 24; i <= 96; i += 4) v.push_back (i);
        v.push_back (99);
        return v;
    }

    DisplayText PanelModel::remainDisplay (double seconds)
    {
        const auto s = (long long) std::max (0.0, seconds);
        char buf[48];
        if (s < 600)            std::snprintf (buf, sizeof buf, "%lld.%02lld", s / 60, s % 60);
        else if (s < 6000)      std::snprintf (buf, sizeof buf, "%lld.%lld", s / 60, (s % 60) / 10);
        else if (s < 60000)     std::snprintf (buf, sizeof buf, "%lld", s / 60);
        else                    std::snprintf (buf, sizeof buf, "%lldH", std::min (99LL, s / 3600));
        return DisplayText::make (buf);
    }

    // ---- helpers ----------------------------------------------------------------------

    bool PanelModel::hasSample (PadId p) const
    {
        return p.isValid() && p.bank < controller.project().numBanks && ! controller.project().slot (p).isEmpty();
    }

    bool PanelModel::hasPattern (PadId p) const
    {
        return controller.hasPattern (p);
    }

    PadId PanelModel::playingPattern() const
    {
        const int i = engine.sequencer().playingIndex();
        return i >= 0 ? PadId::fromIndex (i) : kNoPad;
    }

    void PanelModel::sendQuantize()
    {
        Command c;
        c.type = Command::Type::seqQuantize;
        c.index = seq.quantizeIndex;
        c.value = seq.shuffle;
        engine.post (c);
    }

    bool PanelModel::bankProtected (int b) const
    {
        const auto& prot = patternMode ? controller.project().patternBankProtected : controller.project().sampleBankProtected;
        return b >= 0 && b < (int) prot.size() && prot[(size_t) b];
    }

    void PanelModel::flash (DisplayText d, double now, double seconds)
    {
        tempDisplay = d;
        tempUntil = now + seconds;
    }

    void PanelModel::sendEngine (Command::Type t, PadId p, double v)
    {
        Command c;
        c.type = t;
        c.pad = p;
        c.value = v;
        engine.post (c);
    }

    void PanelModel::reportStatus (SamplerController::Status s, double now)
    {
        using S = SamplerController::Status;
        switch (s)
        {
            case S::protectedBank: flash ("Prt", now); break;
            case S::full:          flash ("FUL", now); break;
            case S::unsupported:   flash ("UnS", now); break;
            case S::emptyPad:      flash ("ENP", now); break;
            default: break;
        }
    }

    DisplayText PanelModel::bpmDisplay (double bpm)
    {
        if (bpm <= 0.0)
            return DisplayText::make ("---");
        if (bpm < 100.0)
        {
            char buf[8];
            std::snprintf (buf, sizeof buf, "%.1f", std::round (bpm * 10.0) / 10.0);
            return DisplayText::make (buf);
        }
        return DisplayText::number ((int) std::lround (bpm));
    }

    DisplayText PanelModel::sampleBpmDisplay() const
    {
        const auto* s = controller.settings (current);
        return hasSample (current) && s != nullptr ? bpmDisplay (s->bpm) : DisplayText::make ("---");
    }

    // ---- input ------------------------------------------------------------------------

    void PanelModel::press (Control c, double now)
    {
        lastActivity = now;

        if (sleeping)
        {
            // "To exit Sleep mode, press any button" (p. 40): the press only wakes the unit.
            sleeping = false;
            sendEngine (Command::Type::resume);
            return;
        }

        if (c == Control::func)   { funcHeld = true; return; }

        if (paused)
        {
            // Only FUNC + TAP TEMPO (resume) works while paused (p. 12).
            if (c == Control::tapTempo && funcHeld)
            {
                paused = false;
                sendEngine (Command::Type::resume);
            }
            return;
        }

        if (isPad (c))          { pressPad (padIndex (c), now); return; }
        if (isEffectButton (c)) { pressEffect (effectButtonIndex (c), now); return; }
        if (isBank (c))         { pressBank (bankButtonIndex (c), now); return; }

        switch (c)
        {
            case Control::del:        pressDel (now); break;
            case Control::rec:        pressRec (now); break;
            case Control::mark:       pressMark (now); break;
            case Control::cancel:     pressCancel (now); break;
            case Control::remain:     pressRemain (now); break;
            case Control::tapTempo:   pressTap (now); break;

            case Control::gate: case Control::loop: case Control::reverse: case Control::lofi:
                toggleMode (c, now);
                break;

            case Control::stereo:
                stereo = ! stereo;
                sendEngine (Command::Type::setExtStereo, kNoPad, stereo ? 1.0 : 0.0);
                if (sampling == Sampling::standby)
                {
                    Command m;
                    m.type = Command::Type::setMonitor;
                    m.value = 1.0;
                    m.index = stereo ? 1 : 0;
                    engine.post (m);
                }
                break;
            case Control::mic:
                mic = ! mic;
                sendEngine (Command::Type::setMicMode, kNoPad, mic ? 1.0 : 0.0);
                break;

            case Control::startEndLevel: toggleEdit (Edit::startEndLevel, now); break;
            case Control::timeBpm:       toggleEdit (Edit::timeBpm, now); break;
            case Control::length:        if (patternMode) toggleEdit (Edit::length, now); break;
            case Control::quantize:      if (patternMode) toggleEdit (Edit::quantize, now); break;

            case Control::patternSelect:
                patternMode = ! patternMode;
                tempDisplay.reset();
                endOp();
                if (edit == Edit::length || edit == Edit::quantize)
                    edit = Edit::none;
                break;

            case Control::hold:
                holdHeld = true;
                if (patErase)
                    sendEngine (Command::Type::seqEraseAll, kNoPad, 1.0);   // HOLD erases every pad (p. 33)
                else if (op.op == Op::none)
                    sendEngine (Command::Type::holdDown);
                break;

            case Control::subPad:
                subHeld = true;
                if (op.op == Op::none && (! patternMode || patRec == PatRec::standby || patRec == PatRec::active))
                    sendEngine (Command::Type::subDown);
                break;

            case Control::resample:  pressResample (now); break;
            case Control::extSource: pressExt (now); break;

            default: break;
        }
    }

    void PanelModel::release (Control c, double now)
    {
        juce::ignoreUnused (now);
        switch (c)
        {
            case Control::func:    funcHeld = false; return;
            case Control::remain:  remainHeld = false; remainShowing = false; return;
            case Control::cancel:  cancelHeld = false; return;
            case Control::mark:    markHeld = false; return;
            case Control::tapTempo: tapHeld = false; return;
            case Control::hold:
                holdHeld = false;
                if (patErase)
                    sendEngine (Command::Type::seqEraseAll, kNoPad, 0.0);
                return;
            case Control::extSource: releaseExt(); return;
            case Control::subPad:
                if (subHeld && (! patternMode || patRec == PatRec::standby || patRec == PatRec::active))
                    sendEngine (Command::Type::subUp);
                subHeld = false;
                return;
            default: break;
        }

        if (isPad (c))          releasePad (padIndex (c));
        if (isEffectButton (c)) releaseEffect (effectButtonIndex (c));
    }

    void PanelModel::selectBank (int b, double now)
    {
        lastActivity = now;
        if (sleeping || paused)
            return;
        bankIndex = juce::jlimit (0, controller.project().numBanks - 1, b);
        if (op.op == Op::delAll || op.op == Op::format)
            op.stage = 1;
    }

    // ---- pads -------------------------------------------------------------------------

    void PanelModel::pressPad (int index, double now)
    {
        const PadId pad = padAt (index);

        if (funcHeld && op.op == Op::none)
        {
            openFunc (index, now);
            return;
        }
        if (op.op != Op::none)
        {
            opPad (pad, now);
            return;
        }
        if (mfxHeld)
        {
            // MFX + pad = MFX 1..12; MFX + DJFX LOOPER + pad = MFX 13..24 (p. 13).
            fx.mfxType = index + 1 + (djfxHeldForMfx ? 12 : 0);
            mfxUsedAsModifier = true;
            flash (DisplayText::number (fx.mfxType), now);
            syncEffect();
            return;
        }
        if (remainHeld)
        {
            // REMAIN + pad: current pad without playing it, and toggle it as an effect target.
            current = pad;
            currentIsExt = false;
            fx.targets.flip ((size_t) pad.index());
            fx.targetAll = false;
            syncTargets();
            return;
        }

        // Sampling: choose the empty pad to record into (p. 18).
        if (sampling == Sampling::choosePad)
        {
            if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
            if (hasSample (pad)) return;   // only empty pads blink
            recordDest = pad;
            sampling = Sampling::standby;
            tempoSpecified = false;
            Command m;
            m.type = Command::Type::setMonitor;
            m.value = 1.0;
            m.index = stereo ? 1 : 0;
            engine.post (m);
            sendEngine (Command::Type::setDigitalLevel, kNoPad, levelFromKnob (digitalLevelKnob));
            syncTargets();
            return;
        }
        if (sampling != Sampling::none)
            return;

        // Resample: choose the destination, then a pad press starts playback and recording
        // together (p. 26).
        if (resample == Resample::choosePad || resample == Resample::padChosen)
        {
            if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
            if (hasSample (pad)) return;   // "You can't resample to a pad to which a sample is already assigned."
            recordDest = pad;
            resample = Resample::padChosen;
            return;
        }
        if (patternMode && (patRec == PatRec::none || patRec == PatRec::choosePad))
        {
            pressPatternPad (pad, now);
            return;
        }
        if (patErase)
        {
            // ErS: hold a pad to erase its notes as the pattern passes (p. 33).
            padsErasing.set ((size_t) index);
            sendEngine (Command::Type::seqErasePad, pad, 1.0);
            return;
        }

        if (markHeld && ! markSession && hasSample (pad))
        {
            // Hold MARK + pad: adjust only the end point (p. 28).
            markSession.emplace (edit::MarkSession::Mode::endOnly, *controller.settings (pad),
                                 controller.project().slot (pad).sample->length(), kEngineRate);
            markPad = pad;
        }

        current = pad;
        currentIsExt = false;
        if (edit == Edit::startEndLevel)
            beginFineAdjust();
        if (edit == Edit::timeBpm && hasSample (pad))
            bpmAtEntry = controller.settings (pad)->bpm;

        // Targets first, so the new voice starts on the right bus.
        syncTargets();
        sendEngine (Command::Type::padDown, pad);
        padsDownInEngine.set ((size_t) index);

        if (resample == Resample::armed && hasSample (pad))
        {
            controller.startArmedRecording();   // same engine block as the pad hit
            resample = Resample::recording;
        }
    }

    void PanelModel::releasePad (int index)
    {
        if (padsErasing.test ((size_t) index))
        {
            padsErasing.reset ((size_t) index);
            sendEngine (Command::Type::seqErasePad, padAt (index), 0.0);
            return;
        }
        if (padsDownInEngine.test ((size_t) index))
        {
            sendEngine (Command::Type::padUp, padAt (index));
            padsDownInEngine.reset ((size_t) index);
        }
    }

    // ---- effects (panel state only until Phase 3) -------------------------------------

    void PanelModel::pressEffect (int index, double now)
    {
        juce::ignoreUnused (now);
        if (op.op != Op::none)
            return;

        if (tapHeld && ! mfxHeld)
        {
            // EFFECT GRAB: on only while the effect button is held (p. 13).
            fx.grabbing = true;
            fx.grabRestore = fx.active;
            fx.active = index;
            syncEffect();
            return;
        }
        if (index == 5)
        {
            // MFX doubles as a modifier: it toggles on release unless MFX + pad / DJFX /
            // CTRL 3 was used while it was held (SPEC U31).
            mfxHeld = true;
            mfxUsedAsModifier = false;
            return;
        }
        if (index == 4 && mfxHeld)
        {
            djfxHeldForMfx = true;
            mfxUsedAsModifier = true;
            return;
        }
        if (remainHeld)
        {
            // REMAIN + effect button: apply to all samples (p. 13).
            fx.targetAll = true;
            fx.targets.reset();
            fx.active = index;
            syncTargets();
            syncEffect();
            return;
        }

        if (fx.active == index)
        {
            fx.lastActive = index;
            fx.active = -1;
        }
        else
            fx.active = index;
        syncEffect();
    }

    void PanelModel::releaseEffect (int index)
    {
        if (fx.grabbing && fx.active == index)
        {
            fx.lastActive = index;
            fx.active = fx.grabRestore;
            fx.grabbing = false;
            syncEffect();
            if (index == 5)
                mfxHeld = false;
            return;
        }

        if (index == 5)
        {
            if (mfxHeld && ! mfxUsedAsModifier && op.op == Op::none && ! paused)
            {
                if (fx.active == 5) { fx.lastActive = 5; fx.active = -1; }
                else fx.active = 5;
                syncEffect();
            }
            mfxHeld = false;
            djfxHeldForMfx = false;
        }
        if (index == 4)
            djfxHeldForMfx = false;
    }

    // ---- banks ------------------------------------------------------------------------

    void PanelModel::pressBank (int i, double now)
    {
        // Press once: A..E (lit). Press again: F..J (blinking). (p. 11)
        const int banks = controller.project().numBanks;
        int next = i;
        if (bankIndex == i && i + 5 < banks) next = i + 5;
        else if (bankIndex == i + 5)         next = i;
        selectBank (next, now);
    }

    // ---- mode buttons -----------------------------------------------------------------

    void PanelModel::toggleMode (Control c, double now)
    {
        if (op.op != Op::none)
            return;

        if (currentIsExt)
        {
            // With EXT SOURCE current, GATE makes it momentary (p. 24).
            if (c == Control::gate)
            {
                extGate = ! extGate;
                if (extGate && extOn && ! extHeld && ! fxMode)
                {
                    extOn = false;
                    sendEngine (Command::Type::setExtOn, kNoPad, 0.0);
                }
            }
            return;
        }
        if (! hasSample (current))
            return;

        const auto status = controller.editSettings (current, [c] (PadSettings& s)
        {
            switch (c)
            {
                case Control::gate:    s.gate = ! s.gate; break;
                case Control::loop:    s.loop = ! s.loop; break;
                case Control::reverse: s.reverse = ! s.reverse; break;
                case Control::lofi:    s.lofi = ! s.lofi; break;
                default: break;
            }
        });
        reportStatus (status, now);
    }

    void PanelModel::toggleEdit (Edit e, double now)
    {
        if (op.op != Op::none)
            return;
        edit = (edit == e) ? Edit::none : e;
        knobMovedInEdit = { false, false, false };
        tempDisplay.reset();   // a mode change shows the new mode's value at once

        if (edit == Edit::startEndLevel)
            beginFineAdjust();
        else
            fineAdjust.reset();

        if (edit == Edit::timeBpm)
            bpmAtEntry = hasSample (current) ? controller.settings (current)->bpm : 0.0;

        juce::ignoreUnused (now);
    }

    void PanelModel::beginFineAdjust()
    {
        fineAdjust.reset();
        knobMovedInEdit = { false, false, false };
        if (hasSample (current))
            fineAdjust.emplace (*controller.settings (current), controller.project().slot (current).sample->length(), kEngineRate);
    }

    // ---- DEL / REC / MARK / CANCEL / REMAIN / TAP -------------------------------------

    // ---- EXT SOURCE (p. 24) -------------------------------------------------------------

    void PanelModel::pressExt (double now)
    {
        juce::ignoreUnused (now);
        if (op.op != Op::none || patternMode)
            return;

        if (remainHeld)
        {
            // REMAIN + EXT SOURCE: make it the current pad / an effect target.
            currentIsExt = true;
            fx.targetExt = ! fx.targetExt;
            fx.targetAll = false;
            syncTargets();
            return;
        }

        currentIsExt = true;
        extHeld = true;
        if (! fxMode)
        {
            extOn = extGate ? true : ! extOn;
            sendEngine (Command::Type::setExtOn, kNoPad, extOn ? 1.0 : 0.0);
        }
        syncTargets();
    }

    void PanelModel::releaseExt()
    {
        extHeld = false;
        if (extGate && extOn && ! fxMode)
        {
            extOn = false;
            sendEngine (Command::Type::setExtOn, kNoPad, 0.0);
        }
    }

    // ---- sampling and resampling (p. 18, 25, 26) ---------------------------------------

    void PanelModel::startRecording (bool resampling, double now)
    {
        SamplerController::RecordOptions o;
        o.destination = recordDest;
        o.stereo = stereo;
        o.resample = resampling;
        o.tempo = controller.project().patternTempo;
        if (! resampling)
        {
            // Auto sampling: start when the input exceeds the AUTO TRIG LEVEL (p. 25).
            // Levels 1..9 map to -54..-6 dBFS in 6 dB steps (SPEC U35).
            const int lvl = controller.project().device.autoTrigLevel;
            o.autoTriggerGain = lvl > 0 ? (float) std::pow (10.0, (-54.0 + 6.0 * (lvl - 1)) / 20.0) : 0.0f;
            // START/END/LEVEL lit + REC = one bar of count-in (p. 26).
            o.countInBeats = edit == Edit::startEndLevel ? 4 : 0;
            o.snapToTempo = tempoSpecified || o.countInBeats > 0;
        }
        o.waitForStart = resampling;

        const auto status = controller.beginRecording (o);
        if (status != SamplerController::Status::ok)
        {
            reportStatus (status, now);
            if (resampling) exitResample (false);
            else            exitSampling (false);
            return;
        }
        if (resampling) resample = Resample::armed;
        else            sampling = Sampling::recording;
    }

    void PanelModel::exitSampling (bool cancelRecording)
    {
        if (cancelRecording && sampling == Sampling::recording)
            controller.cancelRecording();
        sampling = Sampling::none;
        Command m;
        m.type = Command::Type::setMonitor;
        m.value = 0.0;
        engine.post (m);
        syncTargets();
    }

    void PanelModel::pressResample (double now)
    {
        if (op.op != Op::none || patternMode || sampling != Sampling::none)
            return;
        if (resample != Resample::none)
        {
            exitResample (true);
            return;
        }
        // While resampling the polyphony drops to 4 mono / 2 stereo (p. 26).
        resample = Resample::active;
        sendEngine (Command::Type::setVoiceBudget, kNoPad, controller.limits().resampleChannelVoices);
        Command c;
        c.type = Command::Type::setResampleLevel;
        c.value = levelFromKnob (resampleLevelKnob);
        engine.post (c);
        flash ("LEU", now);
    }

    void PanelModel::exitResample (bool cancelRecording)
    {
        if (cancelRecording && (resample == Resample::armed || resample == Resample::recording))
            controller.cancelRecording();
        resample = Resample::none;
        sendEngine (Command::Type::setVoiceBudget, kNoPad, controller.limits().channelVoices);
    }

    // ---- pattern sequencer (p. 14-15, 32-34) -----------------------------------------------

    void PanelModel::pressPatternPad (PadId pad, double now)
    {
        if (patRec == PatRec::choosePad)
        {
            // Choose the pattern to record: an empty pad, or an existing one to overdub.
            if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
            patRecPad = pad;
            patRec = PatRec::standby;
            patErase = false;
            const auto existing = controller.project().pattern (pad);
            if (existing != nullptr)
                seq.lengthBars = existing->lengthBars;
            sendQuantize();
            sendEngine (Command::Type::setMetronomeLevel, kNoPad, seq.metronomeLevel);
            Command c;
            c.type = Command::Type::seqRecStandby;
            c.index = pad.index();
            c.value = seq.lengthBars;
            engine.post (c);
            return;
        }

        if (! hasPattern (pad))
            return;
        lastPattern = pad;
        const auto playing = playingPattern();
        const auto tempo = controller.project().pattern (pad)->tempo;

        if (holdHeld)
        {
            // HOLD + pad: wait for the external start (host transport / MIDI, p. 47).
            Command c;
            c.type = Command::Type::seqArm;
            c.index = engine.sequencer().armedIndex() == pad.index() ? -1 : pad.index();
            engine.post (c);
            return;
        }
        Command c;
        c.index = pad.index();
        if (subHeld || ! playing.isValid())
        {
            // SUB PAD + pad switches at once (p. 15); otherwise start when stopped.
            c.type = Command::Type::seqPlay;
            controller.setPatternTempo (tempo);
        }
        else if (playing == pad)
            c.type = Command::Type::seqStop;   // pressing the playing pad stops it
        else
            c.type = Command::Type::seqReserve;   // the next pattern starts at the end of this one
        engine.post (c);
    }

    void PanelModel::pressPatternRec (double now)
    {
        switch (patRec)
        {
            case PatRec::none:
                patRec = PatRec::choosePad;
                return;
            case PatRec::choosePad:
                patRec = PatRec::none;
                return;
            case PatRec::standby:
                sendEngine (Command::Type::seqRecStart);   // one bar of count-in, then record
                patRec = PatRec::active;
                return;
            case PatRec::active:
                sendEngine (Command::Type::seqRehearsal);  // rEH: play without recording
                return;
        }
        juce::ignoreUnused (now);
    }

    void PanelModel::stopPatternRecording (bool commit)
    {
        if (patRec == PatRec::standby || patRec == PatRec::active)
            sendEngine (commit ? Command::Type::seqRecStop : Command::Type::seqRecCancel);
        if (patErase)
            sendEngine (Command::Type::seqErase, kNoPad, 0.0);
        patRec = PatRec::none;
        patErase = false;
        padsErasing.reset();
    }

    void PanelModel::pressDel (double now)
    {
        if (cancelHeld && op.op == Op::none)
        {
            // CANCEL + DEL: delete all samples (or patterns) of a bank (p. 30, 34).
            op = {};
            op.op = Op::delAll;
            return;
        }

        switch (op.op)
        {
            case Op::none:
                if (patRec == PatRec::active)
                {
                    // DEL while recording: erase mode (ErS, p. 33). DEL again leaves it.
                    patErase = ! patErase;
                    sendEngine (Command::Type::seqErase, kNoPad, patErase ? 1.0 : 0.0);
                    return;
                }
                if (patRec != PatRec::none)
                    return;
                if (patternMode)
                {
                    bool any = false;
                    for (int p = 0; p < kPadsPerBank; ++p)
                        any = any || hasPattern ({ bankIndex, p });
                    if (! any) { flash ("ENP", now); return; }   // no patterns in this bank (p. 34)
                }
                op = {};
                op.op = Op::del;
                return;

            case Op::del:
            {
                bool any = false;
                for (int i = 0; i < kMaxPads; ++i)
                {
                    if (! op.selected.test ((size_t) i))
                        continue;
                    any = true;
                    reportStatus (patternMode ? controller.deletePattern (PadId::fromIndex (i))
                                              : controller.deleteSample (PadId::fromIndex (i)), now);
                }
                if (any)
                    busy (now);
                endOp();
                return;
            }

            case Op::truncate:
                reportStatus (controller.truncate (current), now);
                busy (now);
                endOp();
                return;

            case Op::delAll:
            case Op::format:
            {
                if (op.stage == 0)
                    return;   // choose a bank first
                const int first = op.op == Op::format ? 0 : bankIndex;
                const int last  = op.op == Op::format ? controller.project().numBanks - 1 : bankIndex;
                bool blocked = false;
                for (int b = first; b <= last; ++b)
                {
                    if (op.op == Op::delAll && bankProtected (b)) { blocked = true; continue; }
                    for (int p = 0; p < kPadsPerBank; ++p)
                    {
                        // Format clears samples and patterns; delete all clears the mode's data.
                        const bool samples = op.op == Op::format || ! patternMode;
                        const bool patterns = op.op == Op::format || patternMode;
                        if (samples && hasSample ({ b, p }) && ! controller.project().sampleBankProtected[(size_t) b])
                            controller.deleteSample ({ b, p });
                        if (patterns && hasPattern ({ b, p }) && ! controller.project().patternBankProtected[(size_t) b])
                            controller.deletePattern ({ b, p });
                    }
                }
                if (blocked && op.op == Op::delAll)
                    flash ("Prt", now);
                busy (now);
                endOp();
                return;
            }

            case Op::backupLoad:
                flash ("ENP", now);   // backups arrive with projects (Phase 7)
                endOp();
                return;

            default:
                return;
        }
    }

    void PanelModel::pressRec (double now)
    {
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                if (op.stage == 2)
                {
                    const auto s = patternMode ? (op.op == Op::copy ? controller.copyPattern (op.a, op.b)
                                                                    : controller.exchangePatterns (op.a, op.b))
                                               : (op.op == Op::copy ? controller.copySample (op.a, op.b)
                                                                    : controller.exchangeSamples (op.a, op.b));
                    reportStatus (s, now);
                    busy (now);
                    endOp();
                }
                return;

            case Op::import:
                if (op.stage == 1)
                {
                    if (callbacks.requestImport)
                        callbacks.requestImport (op.a);
                    endOp();
                }
                return;

            case Op::funcSetting:
                applyFuncSetting();
                endOp();
                return;

            case Op::protect:
            {
                auto& prot = patternMode ? controller.project().patternBankProtected : controller.project().sampleBankProtected;
                if (bankIndex < (int) prot.size())
                    prot[(size_t) bankIndex] = op.protectOn;
                endOp();
                return;
            }

            case Op::none:
                if (patternMode || patRec != PatRec::none)
                {
                    pressPatternRec (now);
                    return;
                }
                switch (resample)
                {
                    case Resample::active:    resample = Resample::choosePad; return;
                    case Resample::choosePad: return;
                    case Resample::padChosen: startRecording (true, now); return;
                    case Resample::armed:     return;   // waiting for the source pad
                    case Resample::recording:
                        controller.stopRecording();
                        exitResample (false);
                        busy (now);
                        return;
                    case Resample::none: break;
                }
                switch (sampling)
                {
                    case Sampling::none:      sampling = Sampling::choosePad; return;
                    case Sampling::choosePad: exitSampling (false); return;
                    case Sampling::standby:   startRecording (false, now); return;
                    case Sampling::recording:
                        controller.stopRecording();
                        exitSampling (false);
                        busy (now);
                        return;
                }
                return;

            default:
                return;
        }
    }

    void PanelModel::pressMark (double now)
    {
        markHeld = true;

        // DEL then MARK on a marked pad: truncate (p. 29).
        if (op.op == Op::del && op.selected.none())
        {
            if (hasSample (current) && controller.settings (current)->isMarked (controller.project().slot (current).sample->length()))
            {
                op.op = Op::truncate;
                return;
            }
            flash ("---", now, 1.0);
            return;
        }
        if (op.op != Op::none || patternMode)
            return;

        if (markSession)
        {
            if (auto result = markSession->markPressed (engine.playPosition (markPad)))
                finishMark (*result, now);
            return;
        }

        if (! hasSample (current) || ! isPlaying (current))
            return;

        const auto& slot = controller.project().slot (current);
        if (slot.settings.isMarked (slot.sample->length()))
        {
            // Pressing the lit MARK during playback clears the region (p. 27).
            controller.editSettings (current, [len = slot.sample->length()] (PadSettings& s) { edit::setRegion (s, 0, len, len); });
            return;
        }

        markSession.emplace (edit::MarkSession::Mode::startAndEnd, slot.settings, slot.sample->length(), kEngineRate);
        markPad = current;
        markSession->markPressed (engine.playPosition (current));
    }

    void PanelModel::finishMark (const PadSettings& s, double now)
    {
        const auto pad = markPad;
        reportStatus (controller.editSettings (pad, [&s] (PadSettings& target)
        {
            target.start = s.start;
            target.end = s.end;
            target.bpm = s.bpm;
        }), now);
        markSession.reset();
        markPad = kNoPad;
    }

    void PanelModel::pressCancel (double now)
    {
        cancelHeld = true;

        if (funcHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::protect;
            op.protectOn = false;
            return;
        }

        if (cancelDetector.press (now))
        {
            sendEngine (Command::Type::stopAll);
            flash ("---", now, 0.5);
        }

        const bool hadOp = op.op != Op::none;
        if (hadOp)
            endOp();
        if (patRec != PatRec::none)
            stopPatternRecording (patRec == PatRec::active);   // CANCEL ends recording and keeps it (p. 33)
        else if (! hadOp && sampling == Sampling::none && resample == Resample::none && playingPattern().isValid())
            sendEngine (Command::Type::seqStop);                // CANCEL stops pattern playback (p. 15)
        if (sampling != Sampling::none)
            exitSampling (true);
        if (resample != Resample::none)
            exitResample (true);
        if (markSession)
        {
            markSession.reset();
            markPad = kNoPad;
        }
    }

    void PanelModel::pressRemain (double now)
    {
        if (funcHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::protect;
            op.protectOn = true;
            return;
        }
        if (cancelHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::format;
            return;
        }

        remainHeld = true;
        remainShowing = true;
        juce::ignoreUnused (now);
    }

    void PanelModel::pressTap (double now)
    {
        tapHeld = true;

        if (funcHeld)
        {
            paused = true;
            sendEngine (Command::Type::pause);
            return;
        }

        // With every effect off, TAP reminds you of the last effect (p. 13).
        if (fx.active < 0 && fx.lastActive >= 0)
            lastEffectBlinkUntil = now + 2.0;

        if (numTaps > 0 && now - taps[(size_t) ((numTaps - 1) % 4)] > kTapResetSeconds)
            numTaps = 0;
        taps[(size_t) (numTaps % 4)] = now;
        ++numTaps;
        if (numTaps < 2)
            return;

        const int n = std::min (numTaps, 4);
        const double first = taps[(size_t) ((numTaps - n) % 4)];
        const double bpmRaw = 60.0 * (n - 1) / (now - first);
        const double bpm = controller.limits().steppedTempo ? (double) tempo::snap (bpmRaw)
                                                            : std::clamp (std::round (bpmRaw * 10.0) / 10.0, 40.0, 200.0);

        if (markSession)
            markSession->setSnapBpm (bpm);
        else if (edit == Edit::timeBpm && ! patternMode && hasSample (current))
            controller.editSettings (current, [bpm] (PadSettings& s) { s.bpm = bpm; });
        else
            controller.setPatternTempo (bpm);

        if (sampling == Sampling::standby)
            tempoSpecified = true;
        flash (bpmDisplay (bpm), now);
    }

    // ---- FUNC menus -------------------------------------------------------------------

    int PanelModel::funcOptionCount (FuncSetting s) const
    {
        switch (s)
        {
            case FuncSetting::autoTrig:     return 10;
            case FuncSetting::illumination: return (int) kIllumination.size();
            case FuncSetting::effectLimit:  return 2;
            case FuncSetting::midiChannel:  return 16;
            case FuncSetting::midiSync:     return (int) kMidiSync.size();
            case FuncSetting::inputGain:    return (int) kInputGain.size();
            case FuncSetting::outputGain:   return (int) kOutputGain.size();
        }
        return 1;
    }

    void PanelModel::openFunc (int pad, double now)
    {
        const auto& dev = controller.project().device;
        op = {};

        switch (pad)
        {
            case 0: op.op = Op::copy; break;
            case 1: op.op = Op::exchange; break;
            case 2: op.op = patternMode ? Op::none : Op::import; break;
            case 3: op.op = Op::funcSetting; op.setting = FuncSetting::autoTrig;     op.value = dev.autoTrigLevel; break;
            case 4: op.op = Op::backupLoad; break;
            case 5: op.op = Op::backupSave; break;
            case 6: op.op = Op::funcSetting; op.setting = FuncSetting::illumination; op.value = dev.illumination; break;
            case 7: op.op = Op::funcSetting; op.setting = FuncSetting::effectLimit;  op.value = dev.effectLimit ? 1 : 0; break;
            case 8: op.op = Op::funcSetting; op.setting = FuncSetting::midiChannel;  op.value = dev.midiChannel - 1; break;
            case 9: op.op = Op::funcSetting; op.setting = FuncSetting::midiSync;     op.value = dev.midiSync; break;
            case 10:
                op.op = Op::funcSetting; op.setting = FuncSetting::inputGain;
                op.value = dev.inputGainDb <= -10 ? 0 : 1;
                break;
            case 11:
            {
                op.op = Op::funcSetting; op.setting = FuncSetting::outputGain;
                const auto it = std::find (kOutputGain.begin(), kOutputGain.end(), dev.outputGainDb);
                op.value = it == kOutputGain.end() ? 1 : (int) (it - kOutputGain.begin());
                break;
            }
            default: break;
        }

        if (op.op == Op::none)
            flash ("ENP", now);   // pattern copy/exchange arrive in Phase 5
        if (op.op == Op::import && bankProtected (bankIndex))
            flash ("Prt", now);
    }

    void PanelModel::opPad (PadId pad, double now)
    {
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                if (op.stage == 0)
                {
                    if (op.op == Op::copy && ! (patternMode ? hasPattern (pad) : hasSample (pad)))
                        return;   // only pads with data are copy sources
                    op.a = pad;
                    op.stage = 1;
                }
                else if (op.stage >= 1)
                {
                    if (pad == op.a)
                        return;
                    op.b = pad;
                    op.stage = 2;   // REC blinks
                }
                return;

            case Op::import:
                if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
                op.a = pad;
                op.stage = 1;
                return;

            case Op::del:
                if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
                if (patternMode)
                {
                    // Several patterns can be selected only within one bank (p. 34).
                    for (int i = 0; i < kMaxPads; ++i)
                        if (op.selected.test ((size_t) i) && PadId::fromIndex (i).bank != pad.bank)
                            op.selected.reset ((size_t) i);
                    if (hasPattern (pad))
                        op.selected.flip ((size_t) pad.index());
                    return;
                }
                if (hasSample (pad))
                    op.selected.flip ((size_t) pad.index());
                return;

            default:
                return;
        }
    }

    void PanelModel::applyFuncSetting()
    {
        auto& dev = controller.project().device;
        const int v = op.value;
        switch (op.setting)
        {
            case FuncSetting::autoTrig:     dev.autoTrigLevel = v; break;
            case FuncSetting::illumination: dev.illumination = v; break;
            case FuncSetting::effectLimit:
                dev.effectLimit = v == 1;
                sendEngine (Command::Type::setEffectLimit, kNoPad, dev.effectLimit ? 1.0 : 0.0);
                break;
            case FuncSetting::midiChannel:  dev.midiChannel = v + 1; break;
            case FuncSetting::midiSync:     dev.midiSync = v; break;
            case FuncSetting::inputGain:
                dev.inputGainDb = kInputGain[(size_t) v];
                sendEngine (Command::Type::setInputGainDb, kNoPad, dev.inputGainDb);
                break;
            case FuncSetting::outputGain:   controller.setOutputGainDb (kOutputGain[(size_t) v]); break;
        }
    }

    DisplayText PanelModel::funcDisplay() const
    {
        const int v = op.value;
        switch (op.setting)
        {
            case FuncSetting::autoTrig:     return DisplayText::make ("-" + std::to_string (v) + "-");
            case FuncSetting::illumination: return DisplayText::make (kIllumination[(size_t) v]);
            case FuncSetting::effectLimit:  return DisplayText::make (v == 1 ? "L.on" : "L.oF");
            case FuncSetting::midiChannel:  return DisplayText::number (v + 1);
            case FuncSetting::midiSync:     return DisplayText::make (kMidiSync[(size_t) v]);
            case FuncSetting::inputGain:    return DisplayText::number (kInputGain[(size_t) v]);
            case FuncSetting::outputGain:   return DisplayText::number (kOutputGain[(size_t) v]);
        }
        return {};
    }

    void PanelModel::endOp()
    {
        op = {};
    }

    // ---- knobs ------------------------------------------------------------------------

    void PanelModel::turn (Knob k, double v, double now)
    {
        v = juce::jlimit (0.0, 1.0, v);
        knobs[(size_t) k] = v;
        lastActivity = now;

        if (k == Knob::volume)
        {
            if (callbacks.setVolume) callbacks.setVolume (v);
            else controller.setVolume (v);
            return;
        }
        if (k == Knob::micLevel)
        {
            // MIC LEVEL cannot be turned fully down (p. 45): +6 dB to +46 dB.
            sendEngine (Command::Type::setMicGainDb, kNoPad, 6.0 + 40.0 * v);
            return;
        }
        if (sleeping || paused)
            return;

        if (op.op == Op::funcSetting)
        {
            if (k == Knob::ctrl3)
            {
                op.value = knobToIndex (v, funcOptionCount (op.setting));
                op.changed = true;
            }
            return;
        }
        if (op.op != Op::none)
            return;

        const int value127 = (int) std::lround (v * 127.0);

        // While sampling, CTRL 3 is the digital input level (p. 19); while resampling it is
        // the resample level (p. 26). CTRL 1 / 2 still control the effect.
        if (k == Knob::ctrl3 && (sampling == Sampling::standby || sampling == Sampling::recording))
        {
            digitalLevelKnob = v;
            sendEngine (Command::Type::setDigitalLevel, kNoPad, levelFromKnob (v));
            flash (DisplayText::number (value127), now);
            return;
        }
        if (k == Knob::ctrl3 && resample != Resample::none)
        {
            resampleLevelKnob = v;
            Command c;
            c.type = Command::Type::setResampleLevel;
            c.value = levelFromKnob (v);
            engine.post (c);
            flash (DisplayText::number (value127), now);
            return;
        }

        if (edit != Edit::none)
        {
            turnInEdit (k, v, now);
            return;
        }

        const auto knobIdx = (size_t) k;   // ctrl1..3

        if (mfxHeld)
        {
            mfxUsedAsModifier = true;
            if (k == Knob::ctrl3)
            {
                fx.mfxType = 1 + knobToIndex (v, 24);
                flash (DisplayText::number (fx.mfxType), now);
                syncEffect();
            }
            else if (k == Knob::ctrl1 && (fx.mfxType == 4 || fx.mfxType == 5))
            {
                // FLANGER / PHASER hidden balance (p. 44).
                const int e = fx::mfxIndex (fx.mfxType);
                fx.params[(size_t) e][3] = (float) v;
                sendEffectParams (e);
                flash (DisplayText::number (value127), now);
            }
            return;
        }

        if (remainHeld)
        {
            if (! fx.preset) fx.preset = std::array<float, 3> { 0.5f, 0.5f, 0.5f };
            (*fx.preset)[knobIdx] = (float) v;
            flash (DisplayText::number (value127), now);
            return;
        }

        const int effect = activeEffectIndex();
        if (effect >= 0)
        {
            fx.params[(size_t) effect][knobIdx] = (float) v;
            sendEffectParams (effect);
            flash (fx::display (effect, (int) knobIdx, (float) v, engine.effectTempo()), now);
        }
    }

    void PanelModel::turnInEdit (Knob k, double v, double now)
    {
        const int idx = (int) k;
        if (idx < 3)
            knobMovedInEdit[(size_t) idx] = true;

        switch (edit)
        {
            case Edit::startEndLevel:
            {
                if (patternMode)
                {
                    if (k == Knob::ctrl3)
                    {
                        seq.metronomeLevel = (int) std::lround (v * 127.0);
                        sendEngine (Command::Type::setMetronomeLevel, kNoPad, seq.metronomeLevel);
                        flash (DisplayText::number (seq.metronomeLevel), now);
                    }
                    return;
                }
                if (currentIsExt)
                {
                    // EXT SOURCE LEVEL (p. 24).
                    if (k == Knob::ctrl3)
                    {
                        extLevel = (int) std::lround (v * 127.0);
                        sendEngine (Command::Type::setExtLevel, kNoPad, extLevel);
                        flash (DisplayText::number (extLevel), now);
                    }
                    return;
                }
                if (! hasSample (current))
                    return;

                if (k == Knob::ctrl3)
                {
                    const int level = (int) std::lround (v * 127.0);
                    reportStatus (controller.editSettings (current, [level] (PadSettings& s) { s.level = level; }), now);
                    flash (DisplayText::number (level), now);
                    return;
                }

                if (! fineAdjust)
                    return;
                // Offsets are relative to the knob centre, and only once a knob has moved
                // in this mode (a knob left off-centre does not jump the point on entry).
                const double off1 = knobMovedInEdit[0] ? (knobs[0] - 0.5) * 2.0 : 0.0;
                const double off2 = knobMovedInEdit[1] ? (knobs[1] - 0.5) * 2.0 : 0.0;
                const auto adjusted = fineAdjust->apply (std::abs (off1) < 0.01 ? 0.0 : off1,
                                                         std::abs (off2) < 0.01 ? 0.0 : off2);
                reportStatus (controller.editSettings (current, [&adjusted] (PadSettings& s)
                {
                    s.start = adjusted.start;
                    s.end = adjusted.end;
                    s.bpm = adjusted.bpm;
                }), now);
                // Offset shown in tens of milliseconds, -13..13 [SPEC U30, assumed].
                const double off = k == Knob::ctrl1 ? off1 : off2;
                flash (DisplayText::number ((int) std::lround (off * 13.0)), now);
                return;
            }

            case Edit::timeBpm:
                if (k == Knob::ctrl2)
                {
                    if (patternMode || ! hasSample (current) || sampling == Sampling::standby)
                    {
                        const double bpm = controller.limits().steppedTempo
                                               ? (double) tempo::fromIndex (knobToIndex (v, 127))
                                               : std::round ((40.0 + v * 160.0) * 10.0) / 10.0;
                        controller.setPatternTempo (bpm);
                        // The playing (or last chosen) pattern keeps its tempo (p. 15).
                        if (patRec == PatRec::none)
                        {
                            const auto target = playingPattern().isValid() ? playingPattern() : lastPattern;
                            if (target.isValid())
                                controller.setPatternTempoOf (target, bpm);
                        }
                        if (sampling == Sampling::standby)
                            tempoSpecified = true;
                        flash (bpmDisplay (bpm), now);
                    }
                    else
                    {
                        // Left halves, right doubles the BPM shown on entry (p. 30).
                        double bpm = bpmAtEntry;
                        if (v < 1.0 / 3.0)      bpm = edit::halveBpm (bpmAtEntry);
                        else if (v > 2.0 / 3.0) bpm = edit::doubleBpm (bpmAtEntry);
                        reportStatus (controller.editSettings (current, [bpm] (PadSettings& s) { s.bpm = bpm; }), now);
                        flash (bpmDisplay (bpm), now);
                    }
                }
                else if (k == Knob::ctrl1 && ! patternMode && hasSample (current))
                {
                    const int knobValue = (int) std::lround (v * 127.0);
                    PadSettings after;
                    reportStatus (controller.editSettings (current, [knobValue, &after] (PadSettings& s)
                    {
                        timemodify::applyKnob (s, knobValue);
                        after = s;
                    }), now);
                    if (after.timeModify == TimeModifyMode::off)          flash ("oFF", now);
                    else if (after.timeModify == TimeModifyMode::pattern) flash ("Ptn", now);
                    else                                                  flash (bpmDisplay (after.timeModifyBpm), now);
                }
                return;

            case Edit::length:
                if (k == Knob::ctrl3)
                {
                    const auto lengths = patternLengths();
                    int bars = lengths[(size_t) knobToIndex (v, (int) lengths.size())];
                    // A pattern that already has notes cannot get shorter (p. 32).
                    if (patRec != PatRec::none && patRecPad.isValid() && controller.project().pattern (patRecPad) != nullptr)
                        bars = std::max (bars, controller.project().pattern (patRecPad)->lengthBars);
                    seq.lengthBars = bars;
                    if (patRec == PatRec::standby || patRec == PatRec::active)
                        sendEngine (Command::Type::seqLength, kNoPad, bars);
                    flash (DisplayText::number (seq.lengthBars), now);
                }
                return;

            case Edit::quantize:
                if (k == Knob::ctrl3)
                {
                    seq.quantizeIndex = knobToIndex (v, 10);
                    sendQuantize();   // takes effect immediately, also while recording (p. 33)
                    flash (quantizeNames()[(size_t) seq.quantizeIndex], now);
                }
                else if (k == Knob::ctrl2 && seq.quantizeIndex <= 1)
                {
                    seq.shuffle = 50 + (int) std::lround (v * 25.0);   // range assumed (SPEC U13)
                    sendQuantize();
                    flash (DisplayText::number (seq.shuffle), now);
                }
                return;

            case Edit::none:
                return;
        }
    }

    // ---- time -------------------------------------------------------------------------

    void PanelModel::tick (double now)
    {
        if (tempDisplay && now >= tempUntil)
            tempDisplay.reset();

        // Start-only MARK completes when the pad stops (p. 28).
        if (markSession && markPad.isValid() && ! isPlaying (markPad))
        {
            if (auto result = markSession->padStopped())
                finishMark (*result, now);
            else
            {
                markSession.reset();
                markPad = kNoPad;
            }
        }

        // Recording stopped by the engine: "FUL" (2 GB per sample, p. 20) or a disk problem.
        if ((sampling == Sampling::recording || resample == Resample::recording)
            && engine.recordingPhase() == RecPhase::full)
        {
            flash ("FUL", now, 2.0);
            controller.stopRecording();
            if (sampling == Sampling::recording) exitSampling (false);
            else                                 exitResample (false);
        }
        if (sampling == Sampling::recording && ! controller.isRecording())
            exitSampling (false);
        if (controller.isRecording() && sampling == Sampling::none && resample == Resample::none)
            busy (now);   // writing / loading the new sample: the dot blinks

        // A finished pattern take: store it (the display dot blinks while it is written).
        if (engine.sequencer().takeResult())
        {
            reportStatus (controller.commitRecordedPattern(), now);
            busy (now);
        }
        if (engine.sequencer().recordingFull())
            flash ("FUL", now, 1.0);

        // SLP: sleep after about five minutes without operation (p. 40).
        if (! sleeping && controller.project().device.illumination == 4 && now - lastActivity > kSleepSeconds)
        {
            sleeping = true;
            sendEngine (Command::Type::pause);   // "No sound will be produced while in Sleep mode"
        }
    }

    // ---- view -------------------------------------------------------------------------

    DisplayText PanelModel::idleDisplay() const
    {
        switch (op.op)
        {
            case Op::copy:        return DisplayText::make ("CPY");
            case Op::exchange:    return DisplayText::make ("CHG");
            case Op::import:      return DisplayText::make ("iNP");
            case Op::funcSetting: return funcDisplay();
            case Op::del:         return DisplayText::make ("dEL");
            case Op::delAll:      return DisplayText::make ("dAL");
            case Op::truncate:    return DisplayText::make ("trC");
            case Op::protect:     return DisplayText::make (op.protectOn ? "P.on" : "P.oF");
            case Op::format:      return DisplayText::make ("FNt");
            case Op::backupLoad:  return DisplayText::make ("Lod");
            case Op::backupSave:  return DisplayText::make ("SAU");
            case Op::none:        break;
        }

        // Sampling / resampling codes (p. 25, 26).
        if (sampling == Sampling::recording)
        {
            switch (engine.recordingPhase())
            {
                case RecPhase::waitingTrigger: return DisplayText::make ("rdY");
                case RecPhase::countIn:        return DisplayText::make ("-" + std::to_string (std::max (1, engine.countInBeat())) + "-");
                case RecPhase::full:           return DisplayText::make ("FUL");
                default:                       return DisplayText::make ("rEC");
            }
        }
        if (sampling == Sampling::standby)
            return bpmDisplay (controller.project().patternTempo);
        if (resample == Resample::recording)
            return DisplayText::make ("rEC");
        if (resample != Resample::none)
            return DisplayText::make ("LEU");

        switch (edit)
        {
            case Edit::startEndLevel:
                if (currentIsExt) return DisplayText::number (extLevel);
                if (patternMode) return DisplayText::number (seq.metronomeLevel);
                return hasSample (current) ? DisplayText::number (controller.settings (current)->level) : DisplayText::make ("---");
            case Edit::timeBpm:
                return (patternMode || ! hasSample (current)) ? bpmDisplay (controller.project().patternTempo) : sampleBpmDisplay();
            case Edit::length:   return DisplayText::number (seq.lengthBars);
            case Edit::quantize: return DisplayText::make (quantizeNames()[(size_t) seq.quantizeIndex]);
            case Edit::none:     break;
        }

        if (patRec == PatRec::active)
        {
            const auto& sq = engine.sequencer();
            if (patErase) return DisplayText::make ("ErS");
            switch (sq.recordState())
            {
                case Sequencer::RecState::countIn:   return DisplayText::make (std::to_string (sq.measure()));   // -4 .. -1
                case Sequencer::RecState::rehearsal: return DisplayText::make ("rEH");
                case Sequencer::RecState::recording:
                {
                    // Measure and beat (p. 33): "12.3"
                    const auto txt = std::to_string (std::min (99, sq.measure())) + "." + std::to_string (sq.beat());
                    return DisplayText::make (txt.size() < 4 ? " " + txt : txt);
                }
                default: break;
            }
        }
        if (patternMode)
            return DisplayText::make ("Ptn");
        if (currentIsExt)
            return DisplayText::number (extLevel);
        // Idle: the current sample's BPM [SPEC U28, assumed].
        return sampleBpmDisplay();
    }

    PanelView PanelModel::view (double now) const
    {
        PanelView v;
        v.knobs = { (float) knobs[0], (float) knobs[1], (float) knobs[2], (float) knobs[3], (float) knobs[4] };
        v.tempo = controller.project().patternTempo;
        v.illumination = (PanelView::Illumination) juce::jlimit (0, 3, controller.project().device.illumination == 4
                                                                      ? 0 : controller.project().device.illumination);

        if (sleeping)
        {
            v.sleeping = true;
            return v;
        }

        auto led = [&v] (Control c, Led l) { v.leds[(size_t) c] = l; };

        // Display
        if (remainShowing && op.op == Op::none)
        {
            const double bytes = callbacks.freeBytes ? (double) callbacks.freeBytes() : 0.0;
            v.display = remainDisplay (bytes / (kEngineRate * 2.0 * 2.0));   // shown for stereo (p. 25)
        }
        else if (tempDisplay && now < tempUntil)
            v.display = *tempDisplay;
        else
            v.display = idleDisplay();
        v.busyDot = now < busyUntil;

        // Effects
        for (int i = 0; i < 6; ++i)
            if (fx.active == i)
                led ((Control) i, Led::on);
        if (fx.active < 0 && fx.lastActive >= 0 && now < lastEffectBlinkUntil)
            led ((Control) fx.lastActive, Led::blink);

        // Pattern and edit buttons
        led (Control::patternSelect, patternMode ? Led::on : Led::off);
        led (Control::length, edit == Edit::length ? Led::on : Led::off);
        led (Control::quantize, edit == Edit::quantize ? Led::on : Led::off);
        led (Control::startEndLevel, edit == Edit::startEndLevel ? Led::on : Led::off);
        led (Control::timeBpm, edit == Edit::timeBpm ? Led::on : Led::off);
        led (Control::stereo, stereo ? Led::on : Led::off);
        led (Control::mic, mic ? Led::on : Led::off);
        led (Control::func, funcHeld ? Led::on : Led::off);

        // Current pad's SAMPLE MODE and MARK (EXT SOURCE shows its own GATE)
        if (currentIsExt)
            led (Control::gate, extGate ? Led::on : Led::off);
        else if (hasSample (current) && ! patternMode)
        {
            const auto& slot = controller.project().slot (current);
            led (Control::gate, slot.settings.gate ? Led::on : Led::off);
            led (Control::loop, slot.settings.loop ? Led::on : Led::off);
            led (Control::reverse, slot.settings.reverse ? Led::on : Led::off);
            led (Control::lofi, slot.settings.lofi ? Led::on : Led::off);
            led (Control::mark, slot.settings.isMarked (slot.sample->length()) ? Led::on : Led::off);
        }
        if (markSession)
            led (Control::mark, Led::blink);

        // Banks: lit = A..E, blinking = F..J; blinking all while a bank must be chosen.
        const bool chooseBank = ((op.op == Op::delAll || op.op == Op::format) && op.stage == 0)
                                || resample == Resample::choosePad;
        for (int i = 0; i < 5; ++i)
        {
            Led l = Led::off;
            if (chooseBank)              l = Led::blink;
            else if (bankIndex == i)     l = Led::on;
            else if (bankIndex == i + 5) l = Led::blink;
            led ((Control) ((int) Control::bankAF + i), l);
        }

        // DEL / REC per operation
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                led (Control::rec, op.stage == 2 ? Led::blink : (op.op == Op::exchange ? Led::on : Led::off));
                if (op.op == Op::exchange) led (Control::del, Led::on);
                break;
            case Op::import:      led (Control::rec, op.stage == 1 ? Led::blink : Led::on); break;
            case Op::funcSetting: led (Control::rec, op.changed ? Led::blink : Led::off); break;
            case Op::protect:     led (Control::rec, Led::blink); break;
            case Op::del:         led (Control::del, op.selected.any() ? Led::blink : Led::on); break;
            case Op::truncate:    led (Control::del, Led::blink); break;
            case Op::delAll:      led (Control::del, op.stage == 1 ? Led::blink : Led::on); break;
            case Op::format:      led (Control::del, op.stage == 1 ? Led::blink : Led::off); break;
            case Op::backupLoad:
            case Op::backupSave:  led (Control::rec, Led::off); break;
            case Op::none:        break;
        }

        // Input, recording and EXT SOURCE
        v.peak = engine.inputPeakLed() ? Led::on : Led::off;
        v.illuminationOverload = engine.digitalOverload()
                                 && (sampling == Sampling::standby || sampling == Sampling::recording || resample != Resample::none);
        led (Control::extSource, extOn ? Led::on : Led::off);
        led (Control::resample, resample != Resample::none ? Led::on : Led::off);
        if (op.op == Op::none)
        {
            switch (sampling)
            {
                case Sampling::choosePad: led (Control::rec, Led::on); break;
                case Sampling::standby:   led (Control::rec, Led::blink); break;
                case Sampling::recording: led (Control::rec, engine.recordingPhase() == RecPhase::recording ? Led::on : Led::blink); break;
                case Sampling::none:      break;
            }
            switch (resample)
            {
                case Resample::choosePad:
                case Resample::padChosen: led (Control::rec, Led::blink); break;
                case Resample::armed:
                case Resample::recording: led (Control::rec, Led::on); break;
                case Resample::active:
                case Resample::none:      break;
            }
        }

        // Pattern sequencer
        {
            const auto& sq = engine.sequencer();
            const bool running = sq.playingIndex() >= 0 || patRec == PatRec::active || patRec == PatRec::standby;
            v.illuminationTempo = running;     // the ring blinks with the pattern's tempo (p. 40)
            v.tempo = engine.sequencerTempo();
            if (op.op == Op::none)
            {
                switch (patRec)
                {
                    case PatRec::choosePad: led (Control::rec, Led::on); break;
                    case PatRec::standby:   led (Control::rec, Led::blink); break;
                    case PatRec::active:
                        led (Control::rec, sq.recordState() == Sequencer::RecState::recording ? Led::on : Led::blink);
                        if (patErase) led (Control::del, Led::on);
                        break;
                    case PatRec::none: break;
                }
            }
        }

        // Pads
        const bool choosingRecordPad = sampling == Sampling::choosePad || resample == Resample::choosePad
                                       || resample == Resample::padChosen;
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            const PadId p = padAt (i);
            Led l = Led::off;

            if (choosingRecordPad && op.op == Op::none)
                l = p == recordDest && resample == Resample::padChosen ? Led::on
                    : (! hasSample (p) && ! bankProtected (p.bank) ? Led::blink : Led::off);
            else if ((sampling == Sampling::standby || sampling == Sampling::recording) && op.op == Op::none)
                l = p == recordDest ? Led::on : Led::off;
            else if (remainHeld && op.op == Op::none)
                l = (fx.targetAll || fx.targets.test ((size_t) p.index())) ? Led::on : Led::off;
            else if (op.op == Op::copy || op.op == Op::exchange)
            {
                const bool hasData = patternMode ? hasPattern (p) : hasSample (p);
                if (p == op.a || p == op.b)            l = Led::on;
                else if (op.stage == 0 && (op.op == Op::exchange || hasData)) l = Led::blink;
                else if (op.stage == 1)                l = Led::blink;
            }
            else if (op.op == Op::import)
                l = (op.stage == 1 && p == op.a) ? Led::on : (op.stage == 0 ? Led::blink : Led::off);
            else if (op.op == Op::del)
                l = op.selected.test ((size_t) p.index()) ? Led::on
                    : ((patternMode ? hasPattern (p) : hasSample (p)) ? Led::blink : Led::off);
            else if (op.op != Op::none)
                l = Led::off;
            else if (patRec == PatRec::choosePad)
                // New pattern: empty pads blink; pads with a pattern can be overdubbed.
                l = bankProtected (p.bank) ? Led::off : (hasPattern (p) ? Led::on : Led::blink);
            else if (patRec == PatRec::standby)
                l = p == patRecPad ? Led::on : Led::off;
            else if (patRec == PatRec::active)
                l = padsErasing.test ((size_t) i) ? Led::blinkFast : (isPlaying (p) ? Led::blink : (hasSample (p) ? Led::on : Led::off));
            else if (patternMode)
            {
                // Pads with a pattern blink; the playing one is lit; reserved / armed blink fast.
                const auto& sq = engine.sequencer();
                if (sq.playingIndex() == p.index())                                   l = Led::on;
                else if (sq.reservedIndex() == p.index() || sq.armedIndex() == p.index()) l = Led::blinkFast;
                else if (hasPattern (p))                                              l = Led::blink;
            }
            else if (isPlaying (p))
                l = Led::blink;   // SPEC U1
            else if (hasSample (p))
                l = Led::on;

            led (padControl (i), l);
        }

        led (Control::hold, holdHeld ? Led::on : Led::off);
        led (Control::subPad, subHeld ? Led::on : Led::off);
        return v;
    }
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
